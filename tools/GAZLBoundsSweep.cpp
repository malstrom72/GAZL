/*
	GAZL is released under the BSD 2-Clause License.

	Copyright 2010-2025, Magnus Lidström

	Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
	following conditions are met:

	1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
	disclaimer.

	2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
	disclaimer in the documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
	WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
	Deterministic boundary sweep of every memory-accessing instruction form: PEEK, POKE, GETL, SETL and COPY, in each
	operand form the assembler accepts, at every edge of the memory arena. Each probe is a tiny generated program
	whose access lands just before, on, or just after an edge (start of memory, end of globals, end of writable
	memory, end of memory, and the 32-bit wrap), and every outcome is checked against an oracle written here, not
	derived from the VM:

	  - memory: an address `a` reads [0, memorySize - 1) and writes [0, rwMemorySize), counted from `MEMORY_OFFSET`,
	    with the address arithmetic `base + index` wrapping at 32 bits. The last word is the assembler's zero guard,
	    which stops a native scanning for a string's terminator; no checked read reaches it, so a native can tell a
	    string that ran into it from one that ended in real memory. GETL / SETL index a local array; index `i` is in
	    range when 0 <= i and the slot lies below the end of the data stack.
	  - an access out of range stops the program with BAD_PEEK / BAD_POKE (ACCESS_VIOLATION for COPY) and changes
	    nothing; one in range reads or writes exactly the cells it names.
	  - a form whose address is all constants (`PEEK var &address`, `POKE &address ...`) has no run-time check, so
	    the assembler must refuse every such form that is out of range. Refusing one that is in range is reported
	    too, except where the assembler's own rule is stricter than memory: a write through a constant label, an
	    offset outside the label's section, or a COPY count larger than writable memory.

	The whole memory, plus canary zones on both sides of it, is compared after every run. The only cells exempt are
	the probe function's own frame at the bottom of the data stack, which the program writes itself.

	The memory a native reaches through `accessConstMemory` and `accessMemory` is swept too, by the same rule.
	So are two refusals a host relies on: a frame past INT_MAX at assembly, and a truncated or corrupt blob in
	`thawMemory`.

	Built with GAZL_JIT, every probe also runs through the JIT, checked against the same oracle (not against the
	interpreter), unless the host forbids executable memory. Exits non-zero on any failure.
*/

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include "../src/GAZL.h"
#ifdef GAZL_JIT
#include "../src/GAZLJit.h"
#endif

using namespace GAZL;

namespace {

const UInt MEMORY_SIZE = 256;					// words, the whole arena: globals, data stack and constants
const UInt READABLE_SIZE = MEMORY_SIZE - 1;		// all but the zero guard in the last word
const UInt CANARY_SIZE = 64;						// canary words on each side of the arena
const UInt FRAME_ZONE = 32;						// words at the bottom of the data stack the probe's own frame may use
const int CODE_SIZE = 256;
const int FUNCTION_COUNT = 16;
const int CALL_STACK_SIZE = 16;

enum Op { PEEK, POKE, GETL, SETL, COPY };

const char* const OP_NAMES[] = { "PEEK", "POKE", "GETL", "SETL", "COPY" };

/*
	Where the assembler put things. Constants grow down from the top of memory and an immediate operand can add one,
	so the layout belongs to each program, and a probe is generated against the layout its own program ends up with.
*/
struct Layout {
	UInt globalsSize;
	UInt constsSize;
	UInt inAddress;					// `in`, three words the host fills before the run
	UInt outAddress;				// `out`, where the program stores what it read
	UInt gAddress;					// `g`, eight writable words
	UInt cAddress;					// `c`, eight constants
	bool operator==(const Layout& o) const {
		return globalsSize == o.globalsSize && constsSize == o.constsSize && inAddress == o.inAddress
				&& outAddress == o.outAddress && gAddress == o.gAddress && cAddress == o.cAddress;
	}
	bool operator!=(const Layout& o) const { return !(*this == o); }
	UInt rwSize() const { return MEMORY_SIZE - constsSize; }
};

/*
	One probe: a program, the inputs the host stores in `in`, and what the oracle needs to know about the access.
	`allConst` marks a form with no run-time check, which the assembler must refuse when it is out of range.
*/
struct Probe {
	std::string form;				// e.g. "POKE ptr int var"
	std::string what;				// the probe, for the failure report
	std::string source;
	Int inputs[3];
	Op op;
	bool allConst;
	bool refusalOk;					// the assembler may refuse it: a write through a constant label (`&address(w)`), an
									// offset outside the label's section, or a COPY count larger than writable memory
	bool fromEnd;					// GETL / SETL: `index` is relative to the first slot past the data stack
	UInt address;					// PEEK / POKE: the effective pointer; COPY: the destination pointer
	UInt source2;					// COPY: the source pointer
	UInt count;						// COPY: the word count
	Int index;						// GETL / SETL: the index
	Int value;						// POKE / SETL: the value written
};

std::string number(long long v) {
	char buffer[32];
	std::snprintf(buffer, sizeof buffer, "%lld", v);
	return buffer;
}

std::string hex(UInt v) {
	char buffer[16];
	std::snprintf(buffer, sizeof buffer, "0x%08X", v);
	return buffer;
}

const Int POKE_VALUE = 0x5A5A5A5A;
const Int CONST_BASE = 1000;					// `c` holds CONST_BASE + 0..7, so a read of it is recognisable

/*
	The source around a probe. `in:0..2` are copied to the locals `pa` (pointer), `ix` (index) and `vl` (value); the
	probe instruction comes next; whatever it read is stored at `out:0` and the address of the local array `ar` at
	`out:1`.
*/
std::string program(const std::string& body) {
	std::string s;
	s += "in: GLOB *3\n";
	s += "out: GLOB *2\n";
	s += "g: GLOB *8\n";
	s += "c: CNST *8\n";
	for (int i = 0; i < 8; ++i) s += " DATi #" + number(CONST_BASE + i) + "\n";
	s += "main: FUNC\n";
	s += " pa: LOCp\n";
	s += " ix: LOCi\n";
	s += " vl: LOCi\n";
	s += " rs: LOCi\n";
	s += " ad: LOCp\n";
	s += " ar: LOCA *4\n";
	s += " PEEK pa &in:0\n";
	s += " PEEK ix &in:1\n";
	s += " PEEK vl &in:2\n";
	s += " MOVi rs #0\n";
	s += " ADRL ad ar *4\n";
	s += " POKE &out:1 ad\n";
	s += body;
	s += " POKE &out:0 rs\n";
	s += " RETU\n";
	return s;
}

/*
	Interesting targets, as offsets from the start of memory: each side of every edge, plus pointers far outside it
	(null, the sign boundary, the top of the address space). `layout` decides where the edges are.
*/
std::vector<UInt> targets(const Layout& layout) {
	const UInt edges[] = { 0, layout.globalsSize, layout.rwSize(), READABLE_SIZE, MEMORY_SIZE };	// reads end at the guard
	std::vector<UInt> t;
	for (size_t e = 0; e < sizeof (edges) / sizeof (*edges); ++e) {
		for (int d = -1; d <= 1; ++d) t.push_back(MEMORY_OFFSET + edges[e] + (UInt)d);
	}
	t.push_back(0);
	t.push_back(MEMORY_OFFSET - (UInt)CANARY_SIZE);
	t.push_back(0x7FFFFFFFu);
	t.push_back(0x80000000u);
	t.push_back(0xFFFFFFFFu);
	return t;
}

/*
	Every probe, generated against `layout`. The order is fixed, so probe `n` of one layout corresponds to probe `n`
	of another, which is what lets a probe be regenerated until its layout is stable.
*/
std::vector<Probe> generate(const Layout& L) {
	std::vector<Probe> probes;
	const std::vector<UInt> T = targets(L);
	const Int EXTREMES[] = { 0, 1, -1, 0x7FFFFFFF, (Int)0x80000000u };
	const size_t EXTREME_COUNT = sizeof (EXTREMES) / sizeof (*EXTREMES);

	// Pointer bases for the `ptr` forms: the start of memory, the last writable word, the end, and outside it.
	std::vector<UInt> bases;
	bases.push_back(MEMORY_OFFSET);
	bases.push_back(MEMORY_OFFSET + L.rwSize() - 1);
	bases.push_back(MEMORY_OFFSET + MEMORY_SIZE);
	bases.push_back(0);
	bases.push_back(0x80000000u);
	bases.push_back(0xFFFFFFFFu);

	// Constant bases for the `&address` forms: label, offset and the pointer they name.
	struct Label { const char* name; UInt offset; UInt pointer; };
	const Label LABELS[] = {
		{ "in", 0, L.inAddress }, { "g", 7, L.gAddress + 7 }, { "c", 0, L.cAddress }, { "c", 7, L.cAddress + 7 }
	};
	const size_t LABEL_COUNT = sizeof (LABELS) / sizeof (*LABELS);

	// An index for every (base, target) pair so the access lands on the target, plus the extremes.
	struct IndexSet {
		static std::vector<Int> make(UInt base, const std::vector<UInt>& T, const Int* extremes, size_t n) {
			std::vector<Int> v;
			for (size_t i = 0; i < T.size(); ++i) v.push_back((Int)(T[i] - base));
			for (size_t i = 0; i < n; ++i) v.push_back(extremes[i]);
			return v;
		}
	};

	const char* const PEEK_POKE_FORMS[][2] = {
		// form, body (pa = pointer var, ix = index var, vl = value var)
		{ "PEEK var ptr int", " PEEK rs pa ix\n" },
		{ "PEEK var ptr", " PEEK rs pa\n" },
		{ "POKE ptr int var", " POKE pa ix vl\n" },
		{ "POKE ptr int #const", " POKE pa ix #1515870810\n" },
		{ "POKE ptr var", " POKE pa vl\n" },
		{ "POKE ptr #const", " POKE pa #1515870810\n" }
	};
	for (size_t f = 0; f < sizeof (PEEK_POKE_FORMS) / sizeof (*PEEK_POKE_FORMS); ++f) {
		const std::string form = PEEK_POKE_FORMS[f][0];
		const bool indexed = (form.find(" int") != std::string::npos);
		const Op op = (form[1] == 'E' ? PEEK : POKE);
		for (size_t b = 0; b < bases.size(); ++b) {
			const std::vector<Int> indexes = indexed ? IndexSet::make(bases[b], T, EXTREMES, EXTREME_COUNT)
					: std::vector<Int>(1, 0);
			if (!indexed && b > 0) break;
			for (size_t i = 0; i < (indexed ? indexes.size() : T.size()); ++i) {
				Probe p = Probe();
				p.form = form;
				p.op = op;
				p.source = program(PEEK_POKE_FORMS[f][1]);
				p.inputs[0] = (Int)(indexed ? bases[b] : T[i]);
				p.inputs[1] = indexed ? indexes[i] : 0;
				p.inputs[2] = POKE_VALUE;
				p.address = (UInt)p.inputs[0] + (UInt)p.inputs[1];
				p.value = POKE_VALUE;
				p.what = "ptr=" + hex((UInt)p.inputs[0]) + " index=" + number(p.inputs[1]);
				probes.push_back(p);
			}
		}
	}

	// `ptr #int`: the index is an immediate, so each one is its own program.
	const char* const IMMEDIATE_INDEX_FORMS[][2] = {
		{ "PEEK var ptr #int", " PEEK rs pa #%s\n" },
		{ "POKE ptr #int var", " POKE pa #%s vl\n" },
		{ "POKE ptr #int #const", " POKE pa #%s #1515870810\n" }
	};
	for (size_t f = 0; f < sizeof (IMMEDIATE_INDEX_FORMS) / sizeof (*IMMEDIATE_INDEX_FORMS); ++f) {
		const std::string form = IMMEDIATE_INDEX_FORMS[f][0];
		for (size_t x = 0; x < EXTREME_COUNT; ++x) {
			for (size_t i = 0; i < T.size(); ++i) {
				char body[96];
				std::snprintf(body, sizeof body, IMMEDIATE_INDEX_FORMS[f][1], number(EXTREMES[x]).c_str());
				Probe p = Probe();
				p.form = form;
				p.op = (form[1] == 'E' ? PEEK : POKE);
				p.source = program(body);
				p.inputs[0] = (Int)(T[i] - (UInt)EXTREMES[x]);
				p.inputs[1] = 0;
				p.inputs[2] = POKE_VALUE;
				p.address = T[i];
				p.value = POKE_VALUE;
				p.what = "ptr=" + hex((UInt)p.inputs[0]) + " index=#" + number(EXTREMES[x]);
				probes.push_back(p);
			}
		}
	}

	// `&address int`: a constant base, a run-time index.
	const char* const LABEL_INDEX_FORMS[][2] = {
		{ "PEEK var &address int", " PEEK rs &%s:%u ix\n" },
		{ "POKE &address int var", " POKE &%s:%u ix vl\n" },
		{ "POKE &address int #const", " POKE &%s:%u ix #1515870810\n" }
	};
	for (size_t f = 0; f < sizeof (LABEL_INDEX_FORMS) / sizeof (*LABEL_INDEX_FORMS); ++f) {
		const std::string form = LABEL_INDEX_FORMS[f][0];
		const Op op = (form[1] == 'E' ? PEEK : POKE);
		for (size_t l = 0; l < LABEL_COUNT; ++l) {
			const std::vector<Int> indexes = IndexSet::make(LABELS[l].pointer, T, EXTREMES, EXTREME_COUNT);
			for (size_t i = 0; i < indexes.size(); ++i) {
				char body[96];
				std::snprintf(body, sizeof body, LABEL_INDEX_FORMS[f][1], LABELS[l].name, LABELS[l].offset);
				Probe p = Probe();
				p.form = form;
				p.op = op;
				p.source = program(body);
				p.inputs[0] = 0;
				p.inputs[1] = indexes[i];
				p.inputs[2] = POKE_VALUE;
				p.address = LABELS[l].pointer + (UInt)indexes[i];
				p.refusalOk = (op == POKE && LABELS[l].pointer >= L.cAddress);		// `&address(w)` must be writable
				p.value = POKE_VALUE;
				p.what = std::string("&") + LABELS[l].name + ":" + number(LABELS[l].offset)
						+ " index=" + number(indexes[i]);
				probes.push_back(p);
			}
		}
	}

	// `&address` alone: no run-time check at all, so the assembler is the only check. Offsets run to one past each
	// section, and past the end of memory.
	const char* const LABEL_ONLY_FORMS[][2] = {
		{ "PEEK var &address", " PEEK rs &%s:%u\n" },
		{ "POKE &address var", " POKE &%s:%u vl\n" },
		{ "POKE &address #const", " POKE &%s:%u #1515870810\n" }
	};
	struct Section { const char* name; UInt pointer; UInt size; };
	const Section SECTIONS[] = { { "in", L.inAddress, 3 }, { "out", L.outAddress, 2 }, { "g", L.gAddress, 8 }
			, { "c", L.cAddress, 8 } };
	for (size_t f = 0; f < sizeof (LABEL_ONLY_FORMS) / sizeof (*LABEL_ONLY_FORMS); ++f) {
		const std::string form = LABEL_ONLY_FORMS[f][0];
		for (size_t s = 0; s < sizeof (SECTIONS) / sizeof (*SECTIONS); ++s) {
			const UInt offsets[] = { 0, SECTIONS[s].size - 1, SECTIONS[s].size, SECTIONS[s].size + 1
					, MEMORY_OFFSET + MEMORY_SIZE - SECTIONS[s].pointer };
			for (size_t o = 0; o < sizeof (offsets) / sizeof (*offsets); ++o) {
				char body[96];
				std::snprintf(body, sizeof body, LABEL_ONLY_FORMS[f][1], SECTIONS[s].name, offsets[o]);
				Probe p = Probe();
				p.form = form;
				p.op = (form[1] == 'E' ? PEEK : POKE);
				p.allConst = true;
				p.refusalOk = (offsets[o] >= SECTIONS[s].size || (p.op == POKE && SECTIONS[s].pointer >= L.cAddress));
				p.source = program(body);
				p.inputs[2] = POKE_VALUE;
				p.address = SECTIONS[s].pointer + offsets[o];
				p.value = POKE_VALUE;
				p.what = std::string("&") + SECTIONS[s].name + ":" + number(offsets[o]);
				probes.push_back(p);
			}
		}
	}

	// GETL / SETL on the local array `ar`. Where `ar` lands is known only after a run, so the probe asks for an index
	// relative to the end of the data stack (`fromEnd`) and the driver turns it into a number; the oracle is then
	// checked against the address the run reports.
	const char* const LOCAL_FORMS[][2] = {
		{ "GETL var var int", " GETL rs ar ix\n" },
		{ "SETL var int var", " SETL ar ix vl\n" },
		{ "SETL var int #const", " SETL ar ix #1515870810\n" }
	};
	for (size_t f = 0; f < sizeof (LOCAL_FORMS) / sizeof (*LOCAL_FORMS); ++f) {
		const std::string form = LOCAL_FORMS[f][0];
		const Int SMALL[] = { -1, 0, 1, 3, 4 };
		for (size_t i = 0; i < 5 + 3 + 2; ++i) {
			Probe p = Probe();
			p.form = form;
			p.op = (form[0] == 'G' ? GETL : SETL);
			p.source = program(LOCAL_FORMS[f][1]);
			p.inputs[2] = POKE_VALUE;
			p.value = POKE_VALUE;
			if (i < 5) {
				p.index = SMALL[i];
			} else if (i < 8) {
				p.fromEnd = true;
				p.index = (Int)i - 6;					// -1, 0, +1 around the first slot past the end
			} else {
				p.index = (i == 8 ? 0x7FFFFFFF : (Int)0x80000000u);
			}
			p.inputs[1] = p.index;
			probes.push_back(p);
		}
	}

	// COPY: four operand forms, a constant count, and destinations and sources on each side of the edges.
	const UInt rw = L.rwSize();
	const UInt COUNTS[] = { 0, 1, 4, rw, rw + 1, MEMORY_SIZE, MEMORY_SIZE + 1, 0x7FFFFFFFu, 0x80000000u };
	for (size_t n = 0; n < sizeof (COUNTS) / sizeof (*COUNTS); ++n) {
		const UInt count = COUNTS[n];
		const UInt small = (count <= MEMORY_SIZE ? count : 0);
		// Pointer pairs (dst, src) for the `ptr ptr` form.
		std::vector<std::pair<UInt, UInt> > pairs;
		const UInt dsts[] = { MEMORY_OFFSET, MEMORY_OFFSET + rw - small, MEMORY_OFFSET + rw - small + 1
				, MEMORY_OFFSET - 1, 0xFFFFFFFFu };
		const UInt srcs[] = { MEMORY_OFFSET + READABLE_SIZE - small, MEMORY_OFFSET + READABLE_SIZE - small + 1
				, MEMORY_OFFSET, MEMORY_OFFSET - 1, 0x80000000u };
		for (size_t d = 0; d < 5; ++d) for (size_t s = 0; s < 5; ++s) pairs.push_back(std::make_pair(dsts[d], srcs[s]));
		for (size_t k = 0; k < pairs.size(); ++k) {
			Probe p = Probe();
			p.form = "COPY ptr ptr *size";
			p.op = COPY;
			p.refusalOk = (count > rw);
			// `ad` holds the address of `ar` until the probe; load the source into it from `in:1` first.
			p.source = program(" PEEK ad &in:1\n COPY pa ad *" + number(count) + "\n");
			p.inputs[0] = (Int)pairs[k].first;
			p.inputs[1] = (Int)pairs[k].second;
			p.address = pairs[k].first;
			p.source2 = pairs[k].second;
			p.count = count;
			p.what = "dst=" + hex(p.address) + " src=" + hex(p.source2) + " *" + number(count);
			probes.push_back(p);
		}
		// Constant forms: the label `g` or `c` as destination, `c` or `g` as source, the other side a pointer.
		struct Pair { const char* dst; UInt dstPointer; const char* src; UInt srcPointer; };
		const Pair LABEL_PAIRS[] = { { "g", L.gAddress, "c", L.cAddress }, { "c", L.cAddress, "g", L.gAddress } };
		for (size_t k = 0; k < 2; ++k) {
			const Pair& lp = LABEL_PAIRS[k];
			Probe p = Probe();
			p.op = COPY;
			p.count = count;
			p.form = "COPY &address &address *size";					// checked at run time like the others
			p.refusalOk = (count > rw || lp.dstPointer >= L.cAddress);
			p.source = program(std::string(" COPY &") + lp.dst + " &" + lp.src + " *" + number(count) + "\n");
			p.address = lp.dstPointer;
			p.source2 = lp.srcPointer;
			p.what = std::string("&") + lp.dst + " &" + lp.src + " *" + number(count);
			probes.push_back(p);
			for (size_t s = 0; s < 5; ++s) {
				Probe q = p;
				q.form = "COPY &address ptr *size";
				q.source = program(std::string(" COPY &") + lp.dst + " pa *" + number(count) + "\n");
				q.inputs[0] = (Int)srcs[s];
				q.source2 = srcs[s];
				q.what = std::string("&") + lp.dst + " src=" + hex(srcs[s]) + " *" + number(count);
				probes.push_back(q);
				Probe r = p;
				r.refusalOk = (count > rw);
				r.form = "COPY ptr &address *size";
				r.source = program(std::string(" COPY pa &") + lp.src + " *" + number(count) + "\n");
				r.inputs[0] = (Int)dsts[s];
				r.address = dsts[s];
				r.what = std::string("dst=") + hex(dsts[s]) + " &" + lp.src + " *" + number(count);
				probes.push_back(r);
			}
		}
	}
	return probes;
}

struct Machine {
	std::vector<Value> buffer;					// canaries + memory + canaries
	std::vector<Instruction> code;
	std::vector<UInt> functionTable;
	std::vector<CallStackEntry> callStack;
	ProgramSizes sizes;
	Symbols symbols;
	Machine() : buffer(CANARY_SIZE + MEMORY_SIZE + CANARY_SIZE), code(CODE_SIZE), functionTable(FUNCTION_COUNT)
			, callStack(CALL_STACK_SIZE) { }
	Value* memory() { return &buffer[CANARY_SIZE]; }
};

UInt canary(size_t i) { return 0xC0DE0000u + (UInt)i; }

/*
	Assembles `source` into `m` over a canary-filled buffer. Returns false, with the error, when the assembler
	refuses it.
*/
bool assemble(Machine& m, const std::string& source, Layout& layout, std::string& error) {
	for (size_t i = 0; i < m.buffer.size(); ++i) m.buffer[i].i = (Int)canary(i);
	m.symbols = Symbols();
	ProgramSizes sizes = { 0, 0, 0, 0 };
	try {
		Assembler assem(CODE_SIZE, &m.code[0], FUNCTION_COUNT, &m.functionTable[0], MEMORY_SIZE, m.memory(), m.symbols);
		assem.newUnit("sweep");
		const Char* cp = source.c_str();
		while (*cp != 0) cp = assem.feed(cp);
		assem.finalize(sizes);
	}
	catch (const Exception& e) {
		error = e.what();
		return false;
	}
	m.sizes = sizes;
	UInt size = 0;
	layout.globalsSize = sizes.globalsSize;
	layout.constsSize = sizes.constsSize;
	layout.inAddress = m.symbols.findGlobal("in", size);
	layout.outAddress = m.symbols.findGlobal("out", size);
	layout.gAddress = m.symbols.findGlobal("g", size);
	layout.cAddress = m.symbols.findGlobal("c", size);
	return true;
}

enum Engine { INTERPRETER, JIT, ENGINE_COUNT };

const char* const ENGINE_NAMES[] = { "interpreter", "JIT" };

/*
	Runs `main` on `engine` over the memory in `m`. With GAZL_JIT the program is compiled for each run, which costs
	little for programs this small; without it, the JIT engine is never asked for.
*/
Status run(Machine& m, Engine engine) {
	const Pointer mainFunction = m.symbols.findFunction("main");
#ifdef GAZL_JIT
	if (engine == JIT) {
		const AssembledProgram program = { &m.code[0], m.sizes.codeSize, &m.functionTable[0], m.sizes.functionCount
				, m.memory(), MEMORY_SIZE, m.sizes.globalsSize, m.sizes.constsSize };
		JitModule module;
		NativeJitCompiler compiler;
		compiler.compile(program, module);
		JitProcessor processor(module, program, CALL_STACK_SIZE, &m.callStack[0], 0);
		Status status = processor.enterCall(mainFunction);
		if (status != OK) return status;
		do {
			processor.resetTimeOut(1000000);
			status = processor.run();
		} while (status == TIME_OUT || status == BLOCK_RETRY);
		return status;
	}
#endif
	(void)engine;
	Processor processor(m.sizes.codeSize, &m.code[0], m.sizes.functionCount, &m.functionTable[0], MEMORY_SIZE
			, m.memory(), m.sizes.globalsSize, m.sizes.constsSize, CALL_STACK_SIZE, &m.callStack[0], 0);
	Status status = processor.enterCall(mainFunction);
	if (status != OK) return status;
	processor.resetTimeOut(1000000);
	return processor.run();
}

bool inRange(UInt pointer, UInt count, UInt limit) {
	const long long start = (long long)pointer - (long long)MEMORY_OFFSET;
	return start >= 0 && start + (long long)count <= (long long)limit;
}

/*
	Runs probe `p` on `engine`, starting from the memory image `before`, and checks the outcome against the oracle.
	Returns what is wrong, or an empty string.
*/
std::string check(Machine& m, const Probe& p, const Layout& layout, const std::vector<Value>& before, Engine engine) {
	m.buffer = before;
	const Status status = run(m, engine);
	const Value* mem = m.memory();
	const UInt rw = layout.rwSize();
	const UInt arAddress = (UInt)mem[layout.outAddress - MEMORY_OFFSET + 1].i;

	// The oracle: is the access in range, which status follows, and which cells must change to what.
	std::vector<Value> want(before);
	std::vector<bool> known(before.size(), true);
	const size_t g = CANARY_SIZE;
	for (UInt i = 0; i < FRAME_ZONE; ++i) known[g + layout.globalsSize + i] = false;
	known[g + layout.outAddress - MEMORY_OFFSET] = false;
	known[g + layout.outAddress - MEMORY_OFFSET + 1] = false;
	bool valid = false;
	Status failStatus = OK;
	Int wantRead = 0;
	bool checkRead = false;
	switch (p.op) {
		case PEEK:
			valid = inRange(p.address, 1, READABLE_SIZE);
			failStatus = BAD_PEEK;
			if (valid && known[g + p.address - MEMORY_OFFSET]) {
				wantRead = before[g + p.address - MEMORY_OFFSET].i;
				checkRead = true;
			}
			break;
		case POKE:
			valid = inRange(p.address, 1, rw);
			failStatus = BAD_POKE;
			if (valid) want[g + p.address - MEMORY_OFFSET].i = p.value;
			break;
		case GETL:
		case SETL: {
			const UInt slot = arAddress + (UInt)p.index;
			valid = p.index >= 0 && (long long)arAddress + p.index < (long long)(MEMORY_OFFSET + rw);
			failStatus = (p.op == GETL ? BAD_PEEK : BAD_POKE);
			if (valid && p.op == SETL) want[g + slot - MEMORY_OFFSET].i = p.value;
			if (valid && p.op == GETL && known[g + slot - MEMORY_OFFSET]) {
				wantRead = before[g + slot - MEMORY_OFFSET].i;
				checkRead = true;
			}
			break;
		}
		case COPY: {
			valid = inRange(p.address, p.count, rw) && inRange(p.source2, p.count, READABLE_SIZE);
			failStatus = ACCESS_VIOLATION;
			if (valid) {
				const UInt d = p.address - MEMORY_OFFSET;
				const UInt s = p.source2 - MEMORY_OFFSET;
				const bool overlap = (d < s + p.count && s < d + p.count);
				for (UInt i = 0; i < p.count; ++i) {
					if (overlap || !known[g + s + i]) known[g + d + i] = false;
					else want[g + d + i] = before[g + s + i];
				}
			}
			break;
		}
	}
	const Status wantStatus = valid ? OK : failStatus;
	if (p.allConst && !valid) return "assembled although out of range, status " + number(status);
	if (status != wantStatus) return "status " + number(status) + ", want " + number(wantStatus);
	if (checkRead && mem[layout.outAddress - MEMORY_OFFSET].i != wantRead) {
		return "read " + number(mem[layout.outAddress - MEMORY_OFFSET].i) + ", want " + number(wantRead);
	}
	for (size_t i = 0; i < want.size(); ++i) {
		if (known[i] && m.buffer[i].i != want[i].i) {
			const long long cell = (long long)i - (long long)g;
			return "cell " + number(cell) + " is " + hex((UInt)m.buffer[i].i) + ", want " + hex((UInt)want[i].i)
					+ (cell < 0 || cell >= (long long)MEMORY_SIZE ? " (outside memory)" : "");
		}
	}
	return "";
}

/*
	The memory a native reaches through `accessConstMemory` / `accessMemory`: a request for the whole readable (or
	writable) region succeeds, one word more fails, and the zero guard is out of reach of both. Returns the number
	of failures, each one printed.
*/
int sweepNativeAccess() {
	Machine m;
	Layout layout = Layout();
	std::string error;
	if (!assemble(m, program(""), layout, error)) {
		std::printf("native access: baseline program refused: %s\n", error.c_str());
		return 1;
	}
	const Processor processor(m.sizes.codeSize, &m.code[0], m.sizes.functionCount, &m.functionTable[0], MEMORY_SIZE
			, m.memory(), m.sizes.globalsSize, m.sizes.constsSize, CALL_STACK_SIZE, &m.callStack[0], 0);
	const UInt rw = layout.rwSize();
	struct Access { bool writable; UInt offset; UInt count; bool succeeds; const char* what; };
	const Access ACCESSES[] = {
		{ false, 0, READABLE_SIZE, true, "accessConstMemory, the whole readable region" },
		{ false, 0, READABLE_SIZE + 1, false, "accessConstMemory, one word more (the zero guard)" },
		{ false, 1, READABLE_SIZE, false, "accessConstMemory, the region shifted one word" },
		{ false, READABLE_SIZE - 1, 1, true, "accessConstMemory, the last readable word" },
		{ false, READABLE_SIZE, 1, false, "accessConstMemory, the zero guard alone" },
		{ false, 0, 0xFFFFFFFFu, false, "accessConstMemory, a count that wraps" },
		{ true, 0, rw, true, "accessMemory, the whole writable region" },
		{ true, 0, rw + 1, false, "accessMemory, one word more" },
		{ true, 1, rw, false, "accessMemory, the region shifted one word" },
		{ true, rw - 1, 1, true, "accessMemory, the last writable word" },
		{ true, rw, 1, false, "accessMemory, the first constant" },
		{ true, 0, 0xFFFFFFFFu, false, "accessMemory, a count that wraps" }
	};
	const int count = sizeof (ACCESSES) / sizeof (*ACCESSES);
	int failures = 0;
	for (int i = 0; i < count; ++i) {
		const Access& a = ACCESSES[i];
		const Pointer pointer = MEMORY_OFFSET + a.offset;
		const Value* got = a.writable ? processor.accessMemory(pointer, a.count)
				: processor.accessConstMemory(pointer, a.count);
		const Value* want = a.succeeds ? m.memory() + a.offset : 0;
		if (got != want) {
			std::printf("FAIL native access: %s: %s\n", a.what, got == 0 ? "refused" : "granted");
			++failures;
		}
	}
	std::printf("native access: %d probes, %d failed\n", count, failures);
	return failures;
}

/*
	Two refusals at the edges a host reaches: a frame summing past INT_MAX is refused at assembly, so a slot offset
	can never wrap, and `thawMemory` rejects every truncated blob and a name length running past the end of the
	blob, without reading past it. Returns the number of failures, each one printed.
*/
int sweepFrameAndThaw() {
	int failures = 0;
	{
		Machine m;
		bool refused = false;
		try {
			Assembler assem(CODE_SIZE, &m.code[0], FUNCTION_COUNT, &m.functionTable[0], MEMORY_SIZE, m.memory()
					, m.symbols);
			assem.newUnit("frame");
			const Char* cp = "f: FUNC\n$a: LOCA *2147483647\n$b: LOCA *1\n RETU\n";
			while (*cp != 0) cp = assem.feed(cp);
		}
		catch (const Exception& e) {
			refused = (e.error == FRAME_TOO_LARGE);
		}
		if (!refused) {
			std::printf("FAIL frame: a frame past INT_MAX was not refused with FRAME_TOO_LARGE\n");
			++failures;
		}
	}

	Machine m;
	Layout layout = Layout();
	std::string error;
	if (!assemble(m, program(""), layout, error)) {
		std::printf("thaw: baseline program refused: %s\n", error.c_str());
		return failures + 1;
	}
	Processor processor(m.sizes.codeSize, &m.code[0], m.sizes.functionCount, &m.functionTable[0], MEMORY_SIZE
			, m.memory(), m.sizes.globalsSize, m.sizes.constsSize, CALL_STACK_SIZE, &m.callStack[0], 0);
	std::vector<unsigned char> blob(freezeMemorySize(processor, m.symbols));
	freezeMemory(processor, m.symbols, &blob[0], (UInt)blob.size());
	int probes = 1;											// the frame above
	if (thawMemory(processor, m.symbols, &blob[0], (UInt)blob.size()) != MEMORY_OK) {
		std::printf("FAIL thaw: the intact blob was rejected\n");
		++failures;
	}
	++probes;
	for (size_t n = 1; n < blob.size(); ++n) {
		const std::vector<unsigned char> prefix(blob.begin(), blob.begin() + n);	// exactly `n` bytes, nothing after
		if (thawMemory(processor, m.symbols, &prefix[0], (UInt)n) == MEMORY_OK) {
			std::printf("FAIL thaw: a blob cut to %d bytes was accepted\n", (int)n);
			++failures;
		}
		++probes;
	}
	std::vector<unsigned char> bad(blob);
	bad[28] = 0xF0; bad[29] = 0xFF; bad[30] = 0xFF; bad[31] = 0xFF;		// the first global's name length
	if (thawMemory(processor, m.symbols, &bad[0], (UInt)bad.size()) != MEMORY_TRUNCATED) {
		std::printf("FAIL thaw: a name length past the end of the blob was not rejected as truncated\n");
		++failures;
	}
	++probes;
	std::printf("frame and thaw: %d probes, %d failed\n", probes, failures);
	return failures;
}

struct Totals { int probes; int failures; };

int sweep(int engineCount) {
	std::map<std::string, Totals> totals;
	int failures = 0;

	Machine m;
	Layout base = Layout();
	std::string error;
	if (!assemble(m, program(""), base, error)) {
		std::printf("baseline program refused: %s\n", error.c_str());
		return 1;
	}
	if (m.memory()[MEMORY_SIZE - 1].i != 0) {
		std::printf("bounds sweep: the last word of memory is not the zero guard\n");
		return 1;
	}
	const size_t probeCount = generate(base).size();

	for (size_t n = 0; n < probeCount; ++n) {
		// Regenerate the probe until the layout it was generated against is the layout its program gets.
		Layout layout = base;
		Probe p;
		bool assembled = false;
		bool stable = false;
		for (int tries = 0; tries < 4 && !stable; ++tries) {
			p = generate(layout)[n];
			Layout got = Layout();
			assembled = assemble(m, p.source, got, error);
			if (!assembled) break;
			stable = (got == layout);
			layout = got;
		}
		Totals& t = totals[p.form];
		++t.probes;
		const UInt rw = layout.rwSize();
		std::vector<std::string> problems;

		if (!assembled) {
			bool valid = true;
			if (p.op == PEEK) {
				valid = inRange(p.address, 1, READABLE_SIZE);
			} else if (p.op == POKE) {
				valid = inRange(p.address, 1, rw);
			} else if (p.op == COPY) {
				valid = inRange(p.address, p.count, rw) && inRange(p.source2, p.count, READABLE_SIZE);
			}
			if (p.refusalOk) {
				// the assembler's own rule, stricter than memory: see `refusalOk`
			} else if (valid || p.op == GETL || p.op == SETL) {
				problems.push_back("refused although in range: " + error);
			} else if (!p.allConst) {
				problems.push_back("refused: " + error);
			}
		} else if (!stable) {
			problems.push_back("the layout never settled, so the probe's edges would be the wrong ones");
		} else {
			if (p.fromEnd) {
				// A first run learns where `ar` is; the index is then set relative to the end of the data stack.
				const std::vector<Value> image(m.buffer);
				run(m, INTERPRETER);
				const UInt arAddress = (UInt)m.memory()[layout.outAddress - MEMORY_OFFSET + 1].i;
				m.buffer = image;
				p.index = (Int)(MEMORY_OFFSET + rw - arAddress) + p.index;
				p.inputs[1] = p.index;
			}
			if (p.op == GETL || p.op == SETL) p.what = "index=" + number(p.index);
			Value* mem = m.memory();
			for (int i = 0; i < 3; ++i) mem[layout.inAddress - MEMORY_OFFSET + i].i = p.inputs[i];
			const std::vector<Value> before(m.buffer);
			for (int e = 0; e < engineCount; ++e) {
				const std::string problem = check(m, p, layout, before, static_cast<Engine>(e));
				if (!problem.empty()) problems.push_back(std::string(ENGINE_NAMES[e]) + ": " + problem);
			}
		}
		if (!problems.empty()) {
			++t.failures;
			++failures;
			for (size_t i = 0; i < problems.size() && t.failures <= 5; ++i) {
				std::printf("FAIL %-28s %-40s %s\n", p.form.c_str(), p.what.c_str(), problems[i].c_str());
			}
		}
	}

	for (std::map<std::string, Totals>::const_iterator it = totals.begin(); it != totals.end(); ++it) {
		std::printf("%-30s %4d probes, %d failed\n", it->first.c_str(), it->second.probes, it->second.failures);
	}
	std::printf("bounds sweep (%s%s): %d probes, %d failed\n", ENGINE_NAMES[0], engineCount > 1 ? " and JIT" : ""
			, (int)probeCount, failures);
	return failures == 0 ? 0 : 1;
}

} // namespace

int main() {
	int engineCount = 1;
#ifdef GAZL_JIT
	if (jitAvailable()) {
		engineCount = 2;
	} else {
		std::printf("bounds sweep: this host forbids executable memory; the JIT is NOT swept\n");
	}
#endif
	const int hostFailures = sweepNativeAccess() + sweepFrameAndThaw();
	return (sweep(engineCount) != 0 || hostFailures != 0) ? 1 : 0;
}
