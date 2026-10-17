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
	memory, end of memory, the base between constants and globals, and the 32-bit wrap), and every outcome is checked
	against an oracle written here, not
	derived from the VM:

	  - memory: constants lie just below the base, `MEMORY_OFFSET`, and globals and the data stack from it up. An
	    address reads [base - C, base + rw) and writes [base, base + rw), where C is the constants' size and rw the
	    words from the base up, with the address arithmetic `base + index` wrapping at 32 bits. The last word, just
	    above the data stack, is the assembler's zero guard,
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
	So are a frame past INT_MAX at assembly and the frozen memory format: what freezing saves, and how thawing
	validates and restores.

	Built with GAZL_JIT, every probe also runs through the JIT, checked against the same oracle (not against the
	interpreter), unless the host forbids executable memory. Exits non-zero on any failure.
*/

#include <cstdio>
#include <stdint.h>
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

const UInt MEMORY_SIZE = 256;					// words, the whole arena: constants, globals, data stack and the zero guard
const UInt READABLE_SIZE = MEMORY_SIZE - 1;		// all but the zero guard in the last word
const UInt CANARY_SIZE = 64;						// canary words on each side of the arena
const UInt FRAME_ZONE = 32;						// words at the bottom of the data stack the probe's own frame may use
const int CODE_SIZE = 256;
const int FUNCTION_COUNT = 16;
const int CALL_STACK_SIZE = 16;

enum Op { PEEK, POKE, GETL, SETL, COPY };

/*
	Where the assembler put things. Constants grow down from the base and an immediate operand can add one, so the
	layout belongs to each program, and a probe is generated against the layout its own program ends up with.
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
	UInt rwSize() const { return MEMORY_SIZE - constsSize; }						// words from the base up
	UInt below() const { return constsSize - 1; }									// constant words below the base
	UInt low() const { return MEMORY_OFFSET - below(); }							// the lowest readable pointer
	UInt cell(UInt pointer) const { return pointer - low(); }						// the memory index of `pointer`
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

std::string number(int64_t v) {
	char buffer[32];
	snprintf(buffer, sizeof buffer, "%.0f", (double)v);				// exact: every value here is far below 2^53
	return buffer;
}

std::string hex(UInt v) {
	char buffer[16];
	snprintf(buffer, sizeof buffer, "0x%08X", v);
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
	Interesting targets, as pointers: each side of every edge, plus pointers far outside it (null, the sign boundary,
	the top of the address space). `layout` decides where the edges are.
*/
std::vector<UInt> targets(const Layout& layout) {
	const UInt rw = layout.rwSize();
	const UInt edges[] = { layout.low(), MEMORY_OFFSET, MEMORY_OFFSET + layout.globalsSize, MEMORY_OFFSET + rw
			, MEMORY_OFFSET + rw + 1 };									// start of memory, base, end of globals, guard, end
	std::vector<UInt> t;
	for (size_t e = 0; e < sizeof (edges) / sizeof (*edges); ++e) {
		for (int d = -1; d <= 1; ++d) t.push_back(edges[e] + (UInt)d);
	}
	t.push_back(0);
	t.push_back(layout.low() - (UInt)CANARY_SIZE);
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

	// Pointer bases for the `ptr` forms: the start of memory, the base, the last writable word, the end, and outside.
	std::vector<UInt> bases;
	bases.push_back(L.low());
	bases.push_back(MEMORY_OFFSET);
	bases.push_back(MEMORY_OFFSET + L.rwSize() - 1);
	bases.push_back(MEMORY_OFFSET + L.rwSize() + 1);
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
				snprintf(body, sizeof body, IMMEDIATE_INDEX_FORMS[f][1], number(EXTREMES[x]).c_str());
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
				snprintf(body, sizeof body, LABEL_INDEX_FORMS[f][1], LABELS[l].name, LABELS[l].offset);
				Probe p = Probe();
				p.form = form;
				p.op = op;
				p.source = program(body);
				p.inputs[0] = 0;
				p.inputs[1] = indexes[i];
				p.inputs[2] = POKE_VALUE;
				p.address = LABELS[l].pointer + (UInt)indexes[i];
				p.refusalOk = (op == POKE && LABELS[l].pointer < MEMORY_OFFSET);		// `&address(w)` must be writable
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
					, MEMORY_OFFSET + L.rwSize() + 1 - SECTIONS[s].pointer };						// to the end of memory
			for (size_t o = 0; o < sizeof (offsets) / sizeof (*offsets); ++o) {
				char body[96];
				snprintf(body, sizeof body, LABEL_ONLY_FORMS[f][1], SECTIONS[s].name, offsets[o]);
				Probe p = Probe();
				p.form = form;
				p.op = (form[1] == 'E' ? PEEK : POKE);
				p.allConst = true;
				p.refusalOk = (offsets[o] >= SECTIONS[s].size || (p.op == POKE && SECTIONS[s].pointer < MEMORY_OFFSET));
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
		const UInt srcs[] = { MEMORY_OFFSET + rw - small, MEMORY_OFFSET + rw - small + 1, L.low(), L.low() - 1
				, 0x80000000u };
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
			p.refusalOk = (count > rw || lp.dstPointer < MEMORY_OFFSET);
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
	A processor over the program in `m`, without natives, for the host-side sweeps; they never run it.
*/
Processor machineProcessor(Machine& m) {
	return Processor(m.sizes.codeSize, &m.code[0], m.sizes.functionCount, &m.functionTable[0], MEMORY_SIZE, m.memory()
			, m.sizes.globalsSize, m.sizes.constsSize, CALL_STACK_SIZE, &m.callStack[0], 0);
}

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

bool inRange(UInt pointer, UInt count, UInt from, UInt size) {						// [pointer, pointer + count) in [from, from + size)
	const int64_t start = (int64_t)pointer - (int64_t)from;
	return start >= 0 && start + (int64_t)count <= (int64_t)size;
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
	const UInt arAddress = (UInt)mem[layout.cell(layout.outAddress) + 1].i;

	// The oracle: is the access in range, which status follows, and which cells must change to what.
	std::vector<Value> want(before);
	std::vector<bool> known(before.size(), true);
	const size_t g = CANARY_SIZE;
	for (UInt i = 0; i < FRAME_ZONE; ++i) known[g + layout.cell(MEMORY_OFFSET + layout.globalsSize) + i] = false;
	known[g + layout.cell(layout.outAddress)] = false;
	known[g + layout.cell(layout.outAddress) + 1] = false;
	bool valid = false;
	Status failStatus = OK;
	Int wantRead = 0;
	bool checkRead = false;
	switch (p.op) {
		case PEEK:
			valid = inRange(p.address, 1, layout.low(), READABLE_SIZE);
			failStatus = BAD_PEEK;
			if (valid && known[g + layout.cell(p.address)]) {
				wantRead = before[g + layout.cell(p.address)].i;
				checkRead = true;
			}
			break;
		case POKE:
			valid = inRange(p.address, 1, MEMORY_OFFSET, rw);
			failStatus = BAD_POKE;
			if (valid) want[g + layout.cell(p.address)].i = p.value;
			break;
		case GETL:
		case SETL: {
			const UInt slot = arAddress + (UInt)p.index;
			valid = p.index >= 0 && (int64_t)arAddress + p.index < (int64_t)(MEMORY_OFFSET + rw);
			failStatus = (p.op == GETL ? BAD_PEEK : BAD_POKE);
			if (valid && p.op == SETL) want[g + layout.cell(slot)].i = p.value;
			if (valid && p.op == GETL && known[g + layout.cell(slot)]) {
				wantRead = before[g + layout.cell(slot)].i;
				checkRead = true;
			}
			break;
		}
		case COPY: {
			valid = inRange(p.address, p.count, MEMORY_OFFSET, rw)
					&& inRange(p.source2, p.count, layout.low(), READABLE_SIZE);
			failStatus = ACCESS_VIOLATION;
			if (valid) {
				const UInt d = layout.cell(p.address);
				const UInt s = layout.cell(p.source2);
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
	if (checkRead && mem[layout.cell(layout.outAddress)].i != wantRead) {
		return "read " + number(mem[layout.cell(layout.outAddress)].i) + ", want " + number(wantRead);
	}
	for (size_t i = 0; i < want.size(); ++i) {
		if (known[i] && m.buffer[i].i != want[i].i) {
			const int64_t cell = (int64_t)i - (int64_t)g;
			return "cell " + number(cell) + " is " + hex((UInt)m.buffer[i].i) + ", want " + hex((UInt)want[i].i)
					+ (cell < 0 || cell >= (int64_t)MEMORY_SIZE ? " (outside memory)" : "");
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
	const Processor processor = machineProcessor(m);
	const UInt rw = layout.rwSize();
	const UInt low = layout.low();
	struct Access { bool writable; UInt pointer; UInt count; bool succeeds; const char* what; };
	const Access ACCESSES[] = {
		{ false, low, READABLE_SIZE, true, "accessConstMemory, the whole readable region" },
		{ false, low, READABLE_SIZE + 1, false, "accessConstMemory, one word more (the zero guard)" },
		{ false, low + 1, READABLE_SIZE, false, "accessConstMemory, the region shifted one word" },
		{ false, low - 1, 1, false, "accessConstMemory, the word below the lowest constant" },
		{ false, low + READABLE_SIZE - 1, 1, true, "accessConstMemory, the last readable word" },
		{ false, low + READABLE_SIZE, 1, false, "accessConstMemory, the zero guard alone" },
		{ false, low, 0xFFFFFFFFu, false, "accessConstMemory, a count that wraps" },
		{ true, MEMORY_OFFSET, rw, true, "accessMemory, the whole writable region" },
		{ true, MEMORY_OFFSET, rw + 1, false, "accessMemory, one word more (the zero guard)" },
		{ true, MEMORY_OFFSET + 1, rw, false, "accessMemory, the region shifted one word" },
		{ true, MEMORY_OFFSET + rw - 1, 1, true, "accessMemory, the last writable word" },
		{ true, MEMORY_OFFSET - 1, 1, false, "accessMemory, the last constant" },
		{ true, MEMORY_OFFSET, 0xFFFFFFFFu, false, "accessMemory, a count that wraps" }
	};
	const int count = sizeof (ACCESSES) / sizeof (*ACCESSES);
	int failures = 0;
	for (int i = 0; i < count; ++i) {
		const Access& a = ACCESSES[i];
		const Value* got = a.writable ? processor.accessMemory(a.pointer, a.count)
				: processor.accessConstMemory(a.pointer, a.count);
		const Value* want = a.succeeds ? m.memory() + layout.cell(a.pointer) : 0;
		if (got != want) {
			std::printf("FAIL native access: %s: %s\n", a.what, got == 0 ? "refused" : "granted");
			++failures;
		}
	}
	std::printf("native access: %d probes, %d failed\n", count, failures);
	return failures;
}

std::vector<unsigned char> freeze(Machine& m) {
	const Processor processor = machineProcessor(m);
	std::vector<unsigned char> frozen(freezeMemorySize(processor, m.symbols));
	freezeMemory(processor, m.symbols, &frozen[0], (UInt)frozen.size());
	return frozen;
}

MemoryLoad thaw(Machine& m, const std::vector<unsigned char>& frozen, bool prefix) {
	Processor processor = machineProcessor(m);
	return thawMemory(processor, m.symbols, frozen.empty() ? 0 : &frozen[0], (UInt)frozen.size(), prefix);
}

/*
	The words of the global `name` in `m`, as a list of numbers ("1 2 3"), or "?" when there is no such global.
*/
std::string globalWords(Machine& m, const char* name) {
	UInt size = 0;
	const Pointer address = m.symbols.findGlobal(name, size);
	const Value* words = (address != NULL_POINTER ? machineProcessor(m).accessConstMemory(address, size) : 0);
	if (words == 0) return "?";
	std::string s;
	for (UInt i = 0; i < size; ++i) s += (i == 0 ? "" : " ") + number(words[i].i);
	return s;
}

/*
	The words the global `name` owns, from `getGlobalInfo`, or 99 when there is no such global.
*/
UInt ownedWords(const Symbols& symbols, const char* name) {
	GlobalInfo info;
	return (symbols.getGlobalInfo(name, info) ? info.size : 99);
}

void putWord(std::vector<unsigned char>& b, size_t at, UInt v) {
	b[at] = (unsigned char)v; b[at + 1] = (unsigned char)(v >> 8); b[at + 2] = (unsigned char)(v >> 16);
	b[at + 3] = (unsigned char)(v >> 24);
}

/*
	Two refusals at the edges a host reaches: a frame summing past INT_MAX is refused at assembly, so a slot offset
	can never wrap, and `thawMemory` rejects frozen memory cut at every length, without reading past it. Returns the
	number of failures, each one printed.
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
	const std::vector<unsigned char> frozen = freeze(m);
	int probes = 1;											// the frame above
	if (thaw(m, frozen, false) != MEMORY_OK) {
		std::printf("FAIL thaw: the intact frozen memory was rejected\n");
		++failures;
	}
	++probes;
	for (size_t n = 1; n < frozen.size(); ++n) {
		const std::vector<unsigned char> prefix(frozen.begin(), frozen.begin() + n);	// exactly `n` bytes, nothing after
		const MemoryLoad expected = (n < 4 ? MEMORY_BAD_MAGIC : MEMORY_TRUNCATED);
		if (thaw(m, prefix, false) != expected) {
			std::printf("FAIL thaw: frozen memory cut to %d bytes was not refused as %s\n", (int)n
					, n < 4 ? "bad magic" : "truncated");
			++failures;
		}
		++probes;
	}
	std::printf("frame and thaw: %d probes, %d failed\n", probes, failures);
	return failures;
}

/*
	What `freezeMemory` saves for each way of naming data (docs/gazl/Specification.md section 6): every label owns
	the words up to the next label, a section's own label included, and a label that shares its word with a later
	one owns none. Nothing is saved for `TEMP` or `CNST`. Checks the header, the check entry and every program entry
	byte for byte. Returns the number of failures, each one printed.
*/
int sweepFrozenEntries() {
	const char* const SOURCE =
		"\tGLOB *1\n"						// unnamed section, labelled scalar (Impala's scalar form)
		"clock:\tDATi #7\n"
		"params:\tGLOB *4\n"				// named section, nothing inside (Impala's array form)
		"state:\tGLOB *3\n"					// a section label sharing its word with `gain` owns nothing
		"gain:\tDATf #0.5\n"
		"level:\tDATi #3\n"
		"head:\tGLOB *3\n"					// a section label owns the words before its first inner label
		"\tDATi #61\n"
		"mid:\tDATi #62\n"
		"\tGLOB *3\n"						// unnamed section: the label owns the unlabelled line after it too
		"first:\tDATi #21\n"
		"\tDATi #22\n"
		"\tGLOB *8\n"						// labels over several values
		"multi:\tDATA #31 #32 #33\n"
		"text:\tDATs abc\n"
		"scratch:\tTEMP *2\n"				// TEMP, and a label inside it
		"tmpval:\tDATi #41\n"
		"table:\tCNST *2\n"					// CNST, and a label inside it
		"entry:\tDATi #51\n"
		"alias:\t! DEFp &multi:1\n"			// a pointer constant: an address, but no data of its own
		"main:\tFUNC\n"
		"v:\tLOCi\n"
		"\tPEEK v &multi:2\n"				// a label's offset is checked against all of its values
		"\tRETU\n";
	struct Entry { const char* name; UInt size; UInt words[8]; };
	const Entry EXPECTED[] = {				// ascending names
		{ "clock", 1, { 7 } },
		{ "first", 3, { 21, 22, 0 } },
		{ "gain", 1, { 0x3F000000u } },
		{ "head", 1, { 61 } },
		{ "level", 2, { 3, 0 } },
		{ "mid", 2, { 62, 0 } },
		{ "multi", 3, { 31, 32, 33 } },
		{ "params", 4, { 0, 0, 0, 0 } },
		{ "text", 5, { 'a', 'b', 'c', 0, 0 } }
	};
	const int count = sizeof (EXPECTED) / sizeof (*EXPECTED);
	const unsigned char HEADER_AND_CHECK[20 + 28] = {
		'G', 'Z', 'M', 'M', 1, 0, 0, 0, 1, 0, 0, 0, 32, 0, 0, 0, count + 1, 0, 0, 0
		, 5, 0, 0, 0, '$', 'g', 'a', 'z', 'l', 0, 0, 0, 3, 0, 0, 0
		, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x80, 0x3F, 0x01, 0x00, 0x80, 0x7F
	};

	Machine m;
	Layout layout = Layout();
	std::string error;
	if (!assemble(m, SOURCE, layout, error)) {
		std::printf("FAIL frozen entries: program refused: %s\n", error.c_str());
		return 1;
	}
	const std::vector<unsigned char> frozen = freeze(m);
	if (frozen.size() < sizeof (HEADER_AND_CHECK)
			|| memcmp(&frozen[0], HEADER_AND_CHECK, sizeof (HEADER_AND_CHECK)) != 0) {
		std::printf("FAIL frozen entries: the header or the check entry is not the specified bytes\n");
		return 1;
	}
	size_t at = sizeof (HEADER_AND_CHECK);
	int failures = 0;
	for (int i = 0; i < count && failures == 0; ++i) {
		std::vector<unsigned char> expected(4);
		const UInt length = (UInt)strlen(EXPECTED[i].name);
		putWord(expected, 0, length);
		expected.insert(expected.end(), EXPECTED[i].name, EXPECTED[i].name + length);
		expected.resize(expected.size() + (4 - length % 4) % 4 + 4, 0);
		putWord(expected, expected.size() - 4, EXPECTED[i].size);
		for (UInt w = 0; w < EXPECTED[i].size; ++w) {
			expected.resize(expected.size() + 4);
			putWord(expected, expected.size() - 4, EXPECTED[i].words[w]);
		}
		if (frozen.size() - at < expected.size() || memcmp(&frozen[at], &expected[0], expected.size()) != 0) {
			std::printf("FAIL frozen entries: entry %d is not %s (%u words)\n", i, EXPECTED[i].name, EXPECTED[i].size);
			++failures;
		}
		at += expected.size();
	}
	if (failures == 0 && at != frozen.size()) {
		std::printf("FAIL frozen entries: %d bytes follow the last expected entry\n", (int)(frozen.size() - at));
		++failures;
	}
	// A label that owns nothing is still a name: `findGlobal` resolves it, with what it owns.
	UInt stateSize = 99;
	UInt gainSize = 99;
	UInt levelSize = 99;
	const Pointer stateAddress = m.symbols.findGlobal("state", stateSize);
	if (stateSize != 0 || m.symbols.findGlobal("gain", gainSize) != stateAddress || gainSize != 1
			|| m.symbols.findGlobal("level", levelSize) != stateAddress + 1 || levelSize != 2) {
		std::printf("FAIL frozen entries: the labels in `state` do not own what they should\n");
		++failures;
	}
	// By name, `getGlobalInfo` knows all data, `TEMP` and `CNST` included, and nothing else.
	GlobalInfo temp;
	GlobalInfo head;
	GlobalInfo entry;
	GlobalInfo nosuch;
	GlobalInfo function;
	GlobalInfo alias;
	nosuch.size = 1;
	function.address = 1;
	alias.address = 1;
	if (!m.symbols.getGlobalInfo("entry", entry) || entry.section != CNST_SECTION || entry.size != 2
			|| m.symbols.getGlobalInfo("alias", alias) || alias.address != NULL_POINTER
			|| m.symbols.getGlobalInfo("nosuch", nosuch) || nosuch.size != 0 || nosuch.name != 0
			|| m.symbols.getGlobalInfo("main", function) || function.address != NULL_POINTER
			|| !m.symbols.getGlobalInfo("tmpval", temp) || temp.section != TEMP_SECTION || temp.size != 2
			|| !m.symbols.getGlobalInfo("head", head) || strcmp(head.name, "head") != 0
			|| head.size != 1 || head.section != GLOB_SECTION) {
		std::printf("FAIL frozen entries: getGlobalInfo by name reports the wrong global\n");
		++failures;
	}
	// A data line without values is legal and writes nothing, so its label owns no words.
	const char* const EMPTY[] = { "DATi", "DATf ; none", "DATp", "DATA" };
	const int emptyCount = sizeof (EMPTY) / sizeof (*EMPTY);
	for (int i = 0; i < emptyCount; ++i) {
		Machine e;
		Layout eLayout = Layout();
		UInt xSize = 99;
		UInt ySize = 99;
		const bool assembled = assemble(e, std::string("\tGLOB *2\nx:\t") + EMPTY[i] + "\ny:\tDATi #5\n", eLayout
				, error);
		const Pointer x = e.symbols.findGlobal("x", xSize);
		if (!assembled || x == NULL_POINTER || xSize != 0 || e.symbols.findGlobal("y", ySize) != x || ySize != 2) {
			std::printf("FAIL frozen entries: `x: %s` is not an empty label at `y`\n", EMPTY[i]);
			++failures;
		}
	}
	std::printf("frozen entries: %d checked, %d failed\n", count + 3 + emptyCount, failures);
	return failures;
}

/*
	Thawing (docs/gazl/Specification.md section 11): each row of the validation table, in its order, restoring
	nothing; then the restore rules under both policies. Returns the number of failures, each one printed.
*/
int sweepThawRules() {
	int failures = 0;
	int checks = 0;
	const char* const FROM = "a:\tGLOB *2\n\tDATi #1 #2\nb:\tGLOB *1\n\tDATi #3\nc:\tGLOB *1\n\tDATi #4\n";
	const std::string MAIN = "main:\tFUNC\n\tRETU\n";
	Machine from;
	Layout layout = Layout();
	std::string error;
	if (!assemble(from, FROM + MAIN, layout, error)) {
		std::printf("FAIL thaw rules: program refused: %s\n", error.c_str());
		return 1;
	}
	const std::vector<unsigned char> frozen = freeze(from);	// $gazl at 20, a at 48, b at 68, c at 84; 100 bytes

	struct Invalid { const char* what; size_t at; UInt word; int cut; MemoryLoad expected; };	// `cut` < 0 adds bytes
	const Invalid INVALID[] = {
		{ "wrong magic", 0, 0x4D4D5A48u, 0, MEMORY_BAD_MAGIC },
		{ "3 bytes", 0, 0, 97, MEMORY_BAD_MAGIC },
		{ "a header of 19 bytes", 0, 0, 81, MEMORY_TRUNCATED },
		{ "format version 2", 4, 2, 0, MEMORY_BAD_VERSION },
		{ "GAZL version 0", 8, 0, 0, MEMORY_BAD_VERSION },
		{ "a newer GAZL version", 8, (UInt)VERSION + 1, 0, MEMORY_BAD_VERSION },
		{ "word size 64", 12, 64, 0, MEMORY_BAD_WORDSIZE },
		{ "one entry too many", 16, 5, 0, MEMORY_TRUNCATED },
		{ "a name length past the end", 48, 0xFFFFFFF0u, 0, MEMORY_TRUNCATED },
		{ "a word count past the end", 92, 2, 0, MEMORY_TRUNCATED },
		{ "no entries", 16, 0, 0, MEMORY_BAD_CANARY },
		{ "a quiet NaN in the check entry", 44, 0x7FC00000u, 0, MEMORY_BAD_CANARY },
		{ "a name that is not an identifier", 52, '1', 0, MEMORY_MALFORMED },
		{ "padding that is not zero", 52, 'a' | ('x' << 8), 0, MEMORY_MALFORMED },
		{ "names out of order", 52, 'c', 0, MEMORY_MALFORMED },
		{ "the same name twice", 72, 'a', 0, MEMORY_MALFORMED },
		{ "bytes after the last entry", 100, 0, -4, MEMORY_MALFORMED },
		{ "trailing bytes and a damaged check entry", 36, 0x01020305u, -4, MEMORY_BAD_CANARY }
	};
	for (size_t i = 0; i < sizeof (INVALID) / sizeof (*INVALID); ++i) {
		const Invalid& v = INVALID[i];
		std::vector<unsigned char> bad(frozen);
		if (v.cut > 0) {
			bad.resize(bad.size() - v.cut);
		} else {
			bad.resize(bad.size() - v.cut, 0);
			putWord(bad, v.at, v.word);
		}
		Machine to;
		assemble(to, FROM + MAIN, layout, error);
		Value* const memory = to.memory();
		for (UInt w = 0; w < MEMORY_SIZE; ++w) memory[w].i = 99;
		const MemoryLoad got = thaw(to, bad, true);
		bool untouched = true;
		for (UInt w = 0; w < MEMORY_SIZE; ++w) untouched = untouched && memory[w].i == 99;
		if (got != v.expected || !untouched) {
			std::printf("FAIL thaw rules: %s gave status %d%s, expected %d\n", v.what, (int)got
					, untouched ? "" : " and restored words", (int)v.expected);
			++failures;
		}
		++checks;
	}

	struct Restore { const char* what; std::string to; bool prefix; MemoryLoad expected; const char* words; };
	const std::string B_AND_C = "b:\tGLOB *1\n\tDATi #7\nc:\tGLOB *1\n\tDATi #7\n";			// as frozen, but holding 7s
	const Restore RESTORES[] = {
		{ "the same program", FROM, false, MEMORY_OK, "1 2|3|4|?" },
		{ "a larger owner, prefix", "a:\tGLOB *3\n\tDATi #7 #7 #7\n" + B_AND_C, true, MEMORY_MISMATCH, "1 2 7|3|4|?" },
		{ "a larger owner, skip", "a:\tGLOB *3\n\tDATi #7 #7 #7\n" + B_AND_C, false, MEMORY_MISMATCH, "7 7 7|3|4|?" },
		{ "a smaller owner, prefix", "a:\tGLOB *1\n\tDATi #7\n" + B_AND_C, true, MEMORY_MISMATCH, "1|3|4|?" },
		{ "an entry with no owner", "a:\tGLOB *2\n\tDATi #7 #7\nc:\tGLOB *1\n\tDATi #7\n", false, MEMORY_MISMATCH
				, "1 2|?|4|?" },
		{ "an owner with no entry", "a:\tGLOB *2\n\tDATi #7 #7\n" + B_AND_C + "d:\tGLOB *1\n\tDATi #7\n", false
				, MEMORY_MISMATCH, "1 2|3|4|7" },
		{ "an owner moved into a TEMP section", "a:\tGLOB *2\n\tDATi #7 #7\nb:\tTEMP *1\n\tDATi #7\nc:\tGLOB *1\n"
				"\tDATi #7\n", false, MEMORY_MISMATCH, "1 2|7|4|?" },
		{ "a label that owns nothing", "s:\tGLOB *2\na:\tDATi #7 #7\n" + B_AND_C, false, MEMORY_OK, "1 2|3|4|?" }
	};
	const char* const NAMES[] = { "a", "b", "c", "d" };
	for (size_t i = 0; i < sizeof (RESTORES) / sizeof (*RESTORES); ++i) {
		const Restore& r = RESTORES[i];
		Machine to;
		if (!assemble(to, r.to + MAIN, layout, error)) {
			std::printf("FAIL thaw rules: %s: program refused: %s\n", r.what, error.c_str());
			++failures;
			continue;
		}
		const MemoryLoad got = thaw(to, frozen, r.prefix);
		std::string words;
		for (int n = 0; n < 4; ++n) words += (n == 0 ? "" : "|") + globalWords(to, NAMES[n]);
		if (got != r.expected || words != r.words) {
			std::printf("FAIL thaw rules: %s gave status %d and %s, expected %d and %s\n", r.what, (int)got
					, words.c_str(), (int)r.expected, r.words);
			++failures;
		}
		++checks;
	}
	std::printf("thaw rules: %d checked, %d failed\n", checks, failures);
	return failures;
}

/*
	The assembler's error for `source`, or -1 when it assembles. One native, `^native`, is registered.
*/
int assemblyError(const std::string& source) {
	Machine m;
	m.symbols.registerNative("native", 0);
	try {
		Assembler assem(CODE_SIZE, &m.code[0], FUNCTION_COUNT, &m.functionTable[0], MEMORY_SIZE, m.memory(), m.symbols);
		assem.newUnit("text");
		const Char* cp = source.c_str();
		while (*cp != 0) cp = assem.feed(cp);
		ProgramSizes sizes = { 0, 0, 0, 0 };
		assem.finalize(sizes);
	}
	catch (const Exception& e) {
		return e.error;
	}
	return -1;
}

/*
	Source text is ASCII: a byte above 0x7F in `DATs` text or in a quoted literal is refused, while a comment may
	hold any bytes. Returns the number of failures, each one printed.
*/
int sweepTextEncoding() {
	struct Case { const char* what; const char* source; int error; };
	const Case CASES[] = {
		{ "DATs with a byte above 0x7F", "\tGLOB *4\n\tDATs abc\xE5\n", NON_ASCII_TEXT },
		{ "a quoted literal with a byte above 0x7F", "\tGLOB *1\n\tDATi #'\xE5'\n", NON_ASCII_TEXT },
		{ "a comment with bytes above 0x7F", "t:\tGLOB *1\n\tDATi #1 ; \xE5\xE4\xF6 \xC3\xA5\n; \xFF\n", -1 },
		{ "ASCII DATs text", "t:\tGLOB *3\n\tDATs abc\n", -1 },
		{ "an ASCII quoted literal", "t:\tGLOB *1\n\tDATi #'AB'\n", -1 }
	};
	const int count = sizeof (CASES) / sizeof (*CASES);
	int failures = 0;
	for (int i = 0; i < count; ++i) {
		const int error = assemblyError(CASES[i].source);
		if (error != CASES[i].error) {
			std::printf("FAIL text encoding: %s gave error %d, expected %d\n", CASES[i].what, error, CASES[i].error);
			++failures;
		}
	}
	std::printf("text encoding: %d checked, %d failed\n", count, failures);
	return failures;
}

/*
	Data in an unnamed section before its first label belongs to no name, so freezing could not save it, and the
	assembler refuses it: at the statement, or for a section with no data, when the next section opens or at
	`finalize`. Returns the number of failures, each one printed.
*/
int sweepUnnamedSections() {
	struct Case { const char* what; const char* source; int error; };
	const Case CASES[] = {
		{ "an unnamed GLOB with unlabelled data", "\tGLOB *1\n\tDATi #0\n", UNNAMED_DATA_SECTION },
		{ "an unnamed GLOB followed by another section", "\tGLOB *1\n\tDATi #0\nb:\tGLOB *1\n", UNNAMED_DATA_SECTION },
		{ "an unnamed CNST with unlabelled data", "\tCNST *1\n\tDATi #0\n", UNNAMED_DATA_SECTION },
		{ "an unnamed TEMP without data", "\tTEMP *1\n", UNNAMED_DATA_SECTION },
		{ "a named section", "s:\tGLOB *1\n\tDATi #0\n", -1 },
		{ "an unnamed section with a labelled statement", "\tGLOB *1\na:\tDATi #0\n", -1 },
		{ "data before the first label of an unnamed section", "\tGLOB *2\n\tDATi #0\nb:\tDATi #0\n"
				, UNNAMED_DATA_SECTION }
	};
	const int count = sizeof (CASES) / sizeof (*CASES);
	int failures = 0;
	for (int i = 0; i < count; ++i) {
		const int error = assemblyError(CASES[i].source);
		if (error != CASES[i].error) {
			std::printf("FAIL unnamed sections: %s gave error %d, expected %d\n", CASES[i].what, error, CASES[i].error);
			++failures;
		}
	}
	// A label owns the unlabelled statements after it, up to the next label.
	Machine m;
	Layout layout = Layout();
	std::string error;
	UInt sSize = 0;
	UInt tSize = 0;
	const bool assembled = assemble(m, "\tGLOB *20\ns:\tDATs blahblah\n\tDATi #10\n\tDATs yaddayadda\nt:\tDATi #1\n"
			, layout, error);
	const Pointer sAddress = m.symbols.findGlobal("s", sSize);
	if (!assembled || sAddress == NULL_POINTER || sSize != 19 || m.symbols.findGlobal("t", tSize) != sAddress + 19
			|| tSize != 1) {
		std::printf("FAIL unnamed sections: a label does not cover the unlabelled lines after it (%s)\n", error.c_str());
		++failures;
	}
	// A label with a `#` part is declared as `name.n`, and the next label still ends what it owns.
	Machine h;
	if (!assemble(h, "h:\tGLOB *4\nx#1:\tDATi #1\ny:\tDATi #2\n", layout, error) || ownedWords(h.symbols, "h") != 0
			|| ownedWords(h.symbols, "x.1") != 1 || ownedWords(h.symbols, "y") != 3) {
		std::printf("FAIL unnamed sections: a `#` label does not end where the next label starts (%s)\n", error.c_str());
		++failures;
	}
	std::printf("unnamed sections: %d checked, %d failed\n", count + 2, failures);
	return failures;
}

/*
	An access through a label may reach any word of its section, before or after the label, and an offset on a local
	any declared word of its frame, but no further (docs/gazl/Specification.md sections 5 and 6). An address that is
	only a value is not checked. Returns the number of failures, each one printed.
*/
int sweepLabelReach() {
	const std::string NAMED = "s:\tGLOB *4\na:\tDATi #1\nb:\tDATi #2\nmain:\tFUNC\nv:\tLOCi\np:\tLOCp\n";
	const std::string UNNAMED = "\tGLOB *4\na:\tDATi #1\nb:\tDATi #2\nmain:\tFUNC\nv:\tLOCi\n";
	const std::string FRAME = "main:\tFUNC\nx:\tLOCi\nb:\tLOCA *3\ny:\tLOCi\n";
	const std::string END = "\tRETU\n";
	struct Text { const char* what; std::string source; int error; };
	const Text CASES[] = {
		{ "a section label, to its last word", NAMED + "\tPEEK v &s:3\n\tPOKE &s:3 v\n" + END, -1 },
		{ "a section label, past its end", NAMED + "\tPEEK v &s:4\n" + END, OFFSET_OUT_OF_BOUNDS },
		{ "a section label, before its start", NAMED + "\tPEEK v &s:-1\n" + END, OFFSET_OUT_OF_BOUNDS },
		{ "an inner label, past the next label", NAMED + "\tPEEK v &a:3\n\tPOKE &a:3 v\n" + END, -1 },
		{ "an inner label, past the section end", NAMED + "\tPEEK v &a:4\n" + END, OFFSET_OUT_OF_BOUNDS },
		{ "an inner label, back to the section start", NAMED + "\tPEEK v &b:-1\n\tPOKE &b:-1 v\n" + END, -1 },
		{ "an inner label, before the section start", NAMED + "\tPOKE &b:-2 v\n" + END, OFFSET_OUT_OF_BOUNDS },
		{ "an address that is only a value, before the section", NAMED + "\tMOVp p &s:-5\n" + END, -1 },
		{ "a label in an unnamed section, past the next label", UNNAMED + "\tPEEK v &a:3\n" + END, -1 },
		{ "a label in an unnamed section, before it", UNNAMED + "\tPEEK v &b:-1\n" + END, -1 },
		{ "a label that owns nothing, to the section end", "\tGLOB *4\na:\tDATi\nb:\tDATi #2\nmain:\tFUNC\nv:\tLOCi\n"
				"\tPEEK v &a:3\n" + END, -1 },
		{ "a named constant as a negative offset", "NEG:\t! DEFi #-1\n" + NAMED + "\tPEEK v &b:NEG\n" + END, -1 },
		{ "a local, back to the frame start", FRAME + "\tMOVi y:-4 #0\n\tMOVi b:-1 #0\n" + END, -1 },
		{ "a local, before the frame start", FRAME + "\tMOVi x:-1 #0\n" + END, OFFSET_OUT_OF_BOUNDS },
		{ "a local array, past itself to the frame end", FRAME + "\tMOVi b:3 #0\n\tMOVi x:4 #0\n" + END, -1 },
		{ "a local, past the frame end", FRAME + "\tMOVi y:1 #0\n" + END, OFFSET_OUT_OF_BOUNDS },
		{ "a local's address that is only a value, past the frame", std::string("main:\tFUNC\nx:\tLOCi\np:\tLOCp\n")
				+ "\tADRL p x:100 *0\n" + END, -1 }
	};
	const int count = sizeof (CASES) / sizeof (*CASES);
	int failures = 0;
	for (int i = 0; i < count; ++i) {
		const int error = assemblyError(CASES[i].source);
		if (error != CASES[i].error) {
			std::printf("FAIL label reach: %s gave error %d, expected %d\n", CASES[i].what, error, CASES[i].error);
			++failures;
		}
	}
	std::printf("label reach: %d checked, %d failed\n", count, failures);
	return failures;
}

/*
	Only data labels and locals take an offset, not even `:0` on anything else: `&main:1` would name whichever function
	was declared next, and `&NULL:1` no pointer at all. Checked where a function is already known and where it is a
	forward reference. Returns the number of failures, each one printed.
*/
int sweepFunctionOffsets() {
	struct Case { const char* what; const char* source; int error; };
	const Case CASES[] = {
		{ "an offset on a known function", "main:\tFUNC\np:\tLOCp\n\tMOVp p &main:1\n\tRETU\n", OFFSET_OUT_OF_BOUNDS },
		{ "an offset on a forward function", "\tGLOB *1\nfp:\tDATp &main:1\nmain:\tFUNC\n\tRETU\n"
				, OFFSET_OUT_OF_BOUNDS },
		{ "a zero offset on a function", "main:\tFUNC\np:\tLOCp\n\tMOVp p &main:0\n\tRETU\n", OFFSET_OUT_OF_BOUNDS },
		{ "a negative offset on a forward function", "\tGLOB *1\nfp:\tDATp &main:-1\nmain:\tFUNC\n\tRETU\n"
				, OFFSET_OUT_OF_BOUNDS },
		{ "an offset on NULL", "main:\tFUNC\np:\tLOCp\n\tMOVp p &NULL:1\n\tRETU\n", OFFSET_OUT_OF_BOUNDS },
		{ "a zero offset on NULL", "\tGLOB *1\nn:\tDATp &NULL:0\nmain:\tFUNC\n\tRETU\n", OFFSET_OUT_OF_BOUNDS },
		{ "a zero offset on a number", "\tGLOB *1\nv:\tDATA &GAZL_VERSION:0\nmain:\tFUNC\n\tRETU\n"
				, OFFSET_OUT_OF_BOUNDS },
		{ "an offset on a pointer constant", "g:\tGLOB *2\nP:\t! DEFp &g\nmain:\tFUNC\np:\tLOCp\n\tMOVp p &P:1\n"
				"\tRETU\n", OFFSET_OUT_OF_BOUNDS },
		{ "a zero offset on a pointer constant", "g:\tGLOB *2\nP:\t! DEFp &g\n\tDATp &P:0\nmain:\tFUNC\n\tRETU\n"
				, OFFSET_OUT_OF_BOUNDS },
		{ "a function address without an offset", "\tGLOB *1\nfp:\tDATp &main\nmain:\tFUNC\n\tRETU\n", -1 },
		{ "a zero offset on a called function", "f:\tFUNC\n\tRETU\nmain:\tFUNC\n\tCALL &f:0 %0 *0\n\tRETU\n"
				, OFFSET_OUT_OF_BOUNDS },
		{ "a zero offset on a native", "main:\tFUNC\n\tCALL ^native:0 %0 *0\n\tRETU\n", OFFSET_OUT_OF_BOUNDS },
		{ "an offset on a native", "main:\tFUNC\n\tCALL ^native:1 %0 *0\n\tRETU\n", OFFSET_OUT_OF_BOUNDS },
		{ "a native without an offset", "main:\tFUNC\n\tCALL ^native %0 *0\n\tRETU\n", -1 },
		{ "a number written as a pointer, which GAZL 1 accepts", "\tGLOB *1\ng:\tDATA &GAZL_VERSION\nmain:\tFUNC\n"
				"\tRETU\n", -1 },
		{ "an address written as a number, which GAZL 1 accepts", "g:\tGLOB *1\n\tDATA #g\nmain:\tFUNC\n\tRETU\n", -1 },
		{ "an address written as a number in DATp, which GAZL 1 accepts", "g:\tGLOB *1\n\tDATp #g\nmain:\tFUNC\n\tRETU\n"
				, -1 },
		{ "NULL written as a number, which GAZL 1 accepts", "main:\tFUNC\np:\tLOCp\n\tMOVp p #NULL\n\tRETU\n", -1 },
		{ "a pointer constant written as a number, as Impala writes it", "g:\tGLOB *2\nP:\t! DEFp &g:1\nmain:\tFUNC\n"
				"v:\tLOCi\n\tPEEK v #P\n\tRETU\n", -1 },
		{ "a number, a pointer constant and NULL, each as written", "N:\t! DEFi #2\ng:\tGLOB *4\nP:\t! DEFp &g:1\n"
				"\tDATA #N #GAZL_VERSION &NULL &P\nmain:\tFUNC\n\tRETU\n", -1 },
		{ "a number defined after its use", "g:\tGLOB *1\n\tDATA #LATER\nLATER:\t! DEFi #5\nmain:\tFUNC\n\tRETU\n"
				, -1 }
	};
	const int count = sizeof (CASES) / sizeof (*CASES);
	int failures = 0;
	for (int i = 0; i < count; ++i) {
		const int error = assemblyError(CASES[i].source);
		if (error != CASES[i].error) {
			std::printf("FAIL function offsets: %s gave error %d, expected %d\n", CASES[i].what, error, CASES[i].error);
			++failures;
		}
	}
	std::printf("function offsets: %d checked, %d failed\n", count, failures);
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
				valid = inRange(p.address, 1, layout.low(), READABLE_SIZE);
			} else if (p.op == POKE) {
				valid = inRange(p.address, 1, MEMORY_OFFSET, rw);
			} else if (p.op == COPY) {
				valid = inRange(p.address, p.count, MEMORY_OFFSET, rw)
						&& inRange(p.source2, p.count, layout.low(), READABLE_SIZE);
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
				const UInt arAddress = (UInt)m.memory()[layout.cell(layout.outAddress) + 1].i;
				m.buffer = image;
				p.index = (Int)(MEMORY_OFFSET + rw - arAddress) + p.index;
				p.inputs[1] = p.index;
			}
			if (p.op == GETL || p.op == SETL) p.what = "index=" + number(p.index);
			Value* mem = m.memory();
			for (int i = 0; i < 3; ++i) mem[layout.cell(layout.inAddress) + i].i = p.inputs[i];
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
	const int hostFailures = sweepNativeAccess() + sweepFrameAndThaw() + sweepFrozenEntries() + sweepThawRules()
			+ sweepTextEncoding() + sweepUnnamedSections() + sweepLabelReach() + sweepFunctionOffsets();
	return (sweep(engineCount) != 0 || hostFailures != 0) ? 1 : 0;
}
