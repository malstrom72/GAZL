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
	The §5.7.5 RESUME continuation, done as the design specifies — NOT the resume-block-id/side-table I earlier proposed
	and the design explicitly forbids ("no GAZL-ip side table and no reverse lookup: resume is one field and one jump").

	Each basic-block safepoint gets a cold suspend stub that materializes ITS OWN continuation with `adr` (PC-relative,
	one instruction) and stores it into the engine's `RESUME` field; suspend then returns TIME_OUT. "Resume" is simply
	calling `RESUME` again — a fresh call and a resume are the same operation (§5.7.5). Because each stub names its own
	continuation via `adr`, multiple safepoints need no table and no id: the mechanism the earlier detour was working
	around doesn't exist.

	This lowers a function's `Instruction[]` (v1, memory-resident, building on tools/GAZLJitLowerTest.cpp), emits a
	per-loop-head suspend stub, and drives a minimal dispatcher loop (`while ((s = RESUME(...)) == TIME_OUT)`). Validated
	on a single-loop and a TWO-loop kernel: each JIT run — including ones forced to suspend/resume many times, at either
	loop head — produces a memory image byte-identical to the interpreter. src/GAZL.* is compiled READ-ONLY; the RESUME
	field lives on the `JitEngine` subclass. AArch64 only. Exits non-zero on any mismatch.
*/

#include "GAZLJit.h"
#include "GAZL.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sys/mman.h>

#if defined(__APPLE__)
	#include <pthread.h>
	#include <libkern/OSCacheControl.h>
#endif

using namespace GAZL;

static int failures = 0;

static void* makeExecutable(const uint32_t* words, size_t wordCount) {
	const size_t bytes = wordCount * sizeof(uint32_t);
#if defined(__APPLE__)
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	if (p == MAP_FAILED) { return nullptr; }
	const bool toggle = (pthread_jit_write_protect_supported_np() != 0);
	if (toggle) { pthread_jit_write_protect_np(0); }
	std::memcpy(p, words, bytes);
	if (toggle) { pthread_jit_write_protect_np(1); }
	sys_icache_invalidate(p, bytes);
#else
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) { return nullptr; }
	std::memcpy(p, words, bytes);
	if (::mprotect(p, bytes, PROT_READ | PROT_EXEC) != 0) { ::munmap(p, bytes); return nullptr; }
	__builtin___clear_cache(reinterpret_cast<char*>(p), reinterpret_cast<char*>(p) + bytes);
#endif
	return p;
}

// GAZL finalized opcodes (base = FIRST_OPCODE_VALUE 0x2345).
enum {
	OP_FUNC = 0x2345 + 0, OP_RETU = 0x2345 + 4, OP_MOVE_VV = 0x2345 + 5, OP_MOVE_VC = 0x2345 + 6,
	OP_PEEK_VC = 0x2345 + 7, OP_POKE_CV = 0x2345 + 8,
	OP_ADDI_VVV = 0x2345 + 21, OP_ADDI_VVC = 0x2345 + 22,
	OP_SUBI_VVV = 0x2345 + 23, OP_SUBI_VVC = 0x2345 + 24, OP_SUBI_VCV = 0x2345 + 25,
	OP_MULI_VVV = 0x2345 + 26, OP_MULI_VVC = 0x2345 + 27,
	OP_FORi_VVB = 0x2345 + 67, OP_FORi_VCB = 0x2345 + 68,
	OP_LSSI_VVB = 0x2345 + 69, OP_LSSI_VCB = 0x2345 + 70, OP_LSSI_CVB = 0x2345 + 71,
	OP_EQUI_VVB = 0x2345 + 72, OP_EQUI_VCB = 0x2345 + 73,
	OP_NLSI_VVB = 0x2345 + 74, OP_NLSI_VCB = 0x2345 + 75, OP_NLSI_CVB = 0x2345 + 76,
	OP_NEQI_VVB = 0x2345 + 77, OP_NEQI_VCB = 0x2345 + 78,
	OP_GOTO = 0x2345 + 89
};

enum Kind { K_NONE, K_SLOT, K_CONST, K_PTR, K_BRANCH };
struct OpInfo { Kind k[3]; bool isBranch; };

static OpInfo opInfo(Int opcode, bool& ok) {
	ok = true;
	switch (opcode) {
		case OP_FUNC:     return { { K_CONST, K_CONST, K_NONE }, false };
		case OP_RETU:     return { { K_CONST, K_NONE, K_NONE }, false };
		case OP_MOVE_VV:  return { { K_SLOT, K_SLOT, K_NONE }, false };
		case OP_MOVE_VC:  return { { K_SLOT, K_CONST, K_NONE }, false };
		case OP_PEEK_VC:  return { { K_SLOT, K_PTR, K_NONE }, false };
		case OP_POKE_CV:  return { { K_PTR, K_SLOT, K_NONE }, false };
		case OP_ADDI_VVV: case OP_SUBI_VVV: case OP_MULI_VVV: return { { K_SLOT, K_SLOT, K_SLOT }, false };
		case OP_ADDI_VVC: case OP_SUBI_VVC: case OP_MULI_VVC: return { { K_SLOT, K_SLOT, K_CONST }, false };
		case OP_SUBI_VCV: return { { K_SLOT, K_CONST, K_SLOT }, false };
		case OP_FORi_VVB: return { { K_SLOT, K_SLOT, K_BRANCH }, true };
		case OP_FORi_VCB: return { { K_SLOT, K_CONST, K_BRANCH }, true };
		case OP_LSSI_VVB: case OP_EQUI_VVB: case OP_NLSI_VVB: case OP_NEQI_VVB:
			return { { K_SLOT, K_SLOT, K_BRANCH }, true };
		case OP_LSSI_VCB: case OP_EQUI_VCB: case OP_NLSI_VCB: case OP_NEQI_VCB:
			return { { K_SLOT, K_CONST, K_BRANCH }, true };
		case OP_LSSI_CVB: case OP_NLSI_CVB: return { { K_CONST, K_SLOT, K_BRANCH }, true };
		case OP_GOTO:     return { { K_BRANCH, K_NONE, K_NONE }, true };
		default: ok = false; return { { K_NONE, K_NONE, K_NONE }, false };
	}
}

// The native ABI: x0 = frame base (dsp + localsSize + minIndex), x1 = memoryBase, w2 = fuel, x3 = ctx (the JitEngine,
// for the RESUME store). Returns Status in w0.
typedef int (*KernelFn)(Value* frameBase, Value* memoryBase, int fuel, void* ctx);

struct LowerInfo { int minIndex; UInt localsSize; UInt loops; bool ok; };

/*
	JIT engine subclass carrying the §5.7.5 `RESUME` continuation field (a native code address). It lives on the
	subclass, so no edit to src/GAZL.* is needed.
*/
class JitEngine : public Processor {
	public:		void* resume;						// native continuation: where the next dispatch jumps

		JitEngine(UInt codeSize, const Instruction* code, UInt fnCount, const UInt* fnTable, UInt memSize, Value* mem
					, UInt globalsSize, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack)
			: Processor(codeSize, code, fnCount, fnTable, memSize, mem, globalsSize, constsSize, ipStackSize, ipStack
				, 0, 0), resume(0) { }

		uint32_t resumeFieldOffset() const { return static_cast<uint32_t>(
				reinterpret_cast<const char*>(&resume) - reinterpret_cast<const char*>(this)); }
		Value* frameBaseFor(const LowerInfo& info) const { return dsp + info.localsSize + info.minIndex; }
		Value* membase() const { return memoryBase; }

		// The minimal dispatcher (§5.7.5): jump to RESUME; if it suspends (TIME_OUT), reload fuel and jump again. A
		// fresh call and a resume are the same operation — only who last wrote `resume` differs. Returns final Status.
		Status run(void* entry, Value* frame, int perCallFuel, int& suspends) {
			typedef int (*Fn)(Value*, Value*, int, void*);
			resume = entry;
			suspends = 0;
			Status s;
			do {
				s = static_cast<Status>(reinterpret_cast<Fn>(resume)(frame, membase(), perCallFuel, this));
				if (s == TIME_OUT) { ++suspends; }
			} while (s == TIME_OUT);
			return s;
		}
};

// --- machine buffers ---

namespace {
	const int CODE_SIZE = 64 * 1024;
	const int DATA_SIZE = 64 * 1024;
	const int FUNCTION_TABLE_SIZE = 1024;
	const int CALL_STACK_SIZE = 256;

	Instruction gCode[CODE_SIZE];
	Value gMemory[DATA_SIZE];
	UInt gFunctionTable[FUNCTION_TABLE_SIZE];
	CallStackEntry gCallStack[CALL_STACK_SIZE];
	UInt gGlobalsSize = 0, gConstsSize = 0, gFunctionCount = 0;
	std::vector<Value> gCleanImage;
}

static void loadSlot(Emitter& e, Reg r, Int slot, int mi) { e.ldrW(r, X0, static_cast<uint32_t>((slot - mi) * 4)); }
static void storeSlot(Emitter& e, Reg r, Int slot, int mi) { e.strW(r, X0, static_cast<uint32_t>((slot - mi) * 4)); }
static void matConst(Emitter& e, Reg r, Int v) { e.movImm32(r, static_cast<uint32_t>(v)); }
static void loadOperand(Emitter& e, Reg r, const Value& p, Kind k, int mi) {
	if (k == K_SLOT) { loadSlot(e, r, p.i, mi); } else { matConst(e, r, p.i); }
}
static void emitBinary(Emitter& e, void (Emitter::*op)(Reg, Reg, Reg), const Instruction& in, const OpInfo& oi, int mi) {
	loadOperand(e, W10, in.p1, oi.k[1], mi);
	loadOperand(e, W11, in.p2, oi.k[2], mi);
	(e.*op)(W9, W10, W11);
	storeSlot(e, W9, in.p0.i, mi);
}

// Lower `funcIndex` into `e`, emitting a §5.7.5 RESUME suspend stub per loop head. `resumeOff` = byte offset of the
// engine's `resume` field from the ctx pointer (x3).
static LowerInfo lowerFunction(Emitter& e, const Instruction* code, UInt funcIndex, uint32_t resumeOff) {
	LowerInfo info; info.ok = true; info.minIndex = 0; info.loops = 0;
	info.localsSize = static_cast<UInt>(code[funcIndex].p0.i);

	UInt retIndex = funcIndex;
	while (code[retIndex].opcode != OP_RETU) {
		bool ok = false; opInfo(code[retIndex].opcode, ok);
		if (!ok && code[retIndex].opcode != OP_FUNC) { info.ok = false; return info; }
		++retIndex;
	}

	std::set<UInt> targets;
	std::map<UInt, UInt> loopWeight;
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		bool ok = false; const OpInfo oi = opInfo(code[j].opcode, ok);
		if (!ok) { info.ok = false; return info; }
		const Value* ps[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int q = 0; q < 3; ++q) {
			if (oi.k[q] == K_SLOT && ps[q]->i < info.minIndex) { info.minIndex = ps[q]->i; }
		}
		if (oi.isBranch) {
			const Int off = (code[j].opcode == OP_GOTO) ? code[j].p0.i : code[j].p2.i;
			const UInt tgt = static_cast<UInt>(static_cast<Int>(j) + off);
			targets.insert(tgt);
			if (tgt <= j) { loopWeight[tgt] = j - tgt + 1; }
		}
	}
	info.loops = static_cast<UInt>(loopWeight.size());

	std::map<UInt, Label> labels, stubs;
	for (std::set<UInt>::const_iterator it = targets.begin(); it != targets.end(); ++it) { labels[*it] = e.newLabel(); }
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		stubs[it->first] = e.newLabel();
	}
	const int mi = info.minIndex;

	for (UInt j = funcIndex; j <= retIndex; ++j) {
		if (labels.count(j)) { e.bind(labels[j]); }
		if (loopWeight.count(j)) {						// loop head: charge the block, suspend to this head's own stub
			e.subsImm(W2, W2, loopWeight[j]);
			e.bcond(MI, stubs[j]);
		}
		const Instruction& in = code[j];
		const Int op = in.opcode;
		if (op == OP_FUNC) { continue; }
		else if (op == OP_RETU) { e.movz(W0, 0); e.ret(); }
		else if (op == OP_MOVE_VC) { matConst(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i, mi); }
		else if (op == OP_MOVE_VV) { loadSlot(e, W9, in.p1.i, mi); storeSlot(e, W9, in.p0.i, mi); }
		else if (op == OP_PEEK_VC) { e.ldrW(W9, X1, static_cast<uint32_t>((in.p1.p - MEMORY_OFFSET) * 4)); storeSlot(e, W9, in.p0.i, mi); }
		else if (op == OP_POKE_CV) { loadSlot(e, W9, in.p1.i, mi); e.strW(W9, X1, static_cast<uint32_t>((in.p0.p - MEMORY_OFFSET) * 4)); }
		else if (op == OP_ADDI_VVV || op == OP_ADDI_VVC) { bool ok; emitBinary(e, &Emitter::add, in, opInfo(op, ok), mi); }
		else if (op == OP_SUBI_VVV || op == OP_SUBI_VVC || op == OP_SUBI_VCV) { bool ok; emitBinary(e, &Emitter::sub, in, opInfo(op, ok), mi); }
		else if (op == OP_MULI_VVV || op == OP_MULI_VVC) { bool ok; emitBinary(e, &Emitter::mul, in, opInfo(op, ok), mi); }
		else if (op == OP_FORi_VVB || op == OP_FORi_VCB) {
			bool ok; const OpInfo oi = opInfo(op, ok);
			loadSlot(e, W10, in.p0.i, mi); e.addImm(W10, W10, 1); storeSlot(e, W10, in.p0.i, mi);
			loadOperand(e, W11, in.p1, oi.k[1], mi);
			e.cmp(W10, W11);
			e.bcond(LT, labels[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
		} else if (op == OP_GOTO) {
			e.b(labels[static_cast<UInt>(static_cast<Int>(j) + in.p0.i)]);
		} else {										// conditional branches
			bool ok; const OpInfo oi = opInfo(op, ok);
			Cond c = (op == OP_NLSI_VVB || op == OP_NLSI_VCB || op == OP_NLSI_CVB) ? GE
					: (op == OP_EQUI_VVB || op == OP_EQUI_VCB) ? EQ
					: (op == OP_NEQI_VVB || op == OP_NEQI_VCB) ? NE : LT;
			loadOperand(e, W10, in.p0, oi.k[0], mi);
			loadOperand(e, W11, in.p1, oi.k[1], mi);
			e.cmp(W10, W11);
			e.bcond(c, labels[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
		}
	}

	// Cold suspend stubs (§5.7.5): each writes ITS OWN continuation (adr) into RESUME, then returns TIME_OUT.
	for (std::map<UInt, Label>::const_iterator it = stubs.begin(); it != stubs.end(); ++it) {
		e.bind(it->second);
		e.adr(X9, labels[it->first]);				// continuation = this loop head
		e.strX(X9, X3, resumeOff);					// ctx->resume = continuation
		e.movn(W0, 0);								// TIME_OUT (-1)
		e.ret();
	}
	e.finalize();
	return info;
}

// --- harness ---

static bool assemble(const char* source, Symbols& globals, UInt& mainFuncIndex, Pointer& gInPtr, Pointer& gOutPtr) {
	UInt codeSize = 0, gs = 0, cs = 0, fc = 0;
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assem.newUnit("resumeKernel");
		std::string src(source);
		size_t pos = 0;
		while (pos < src.size()) {
			const size_t nl = src.find('\n', pos);
			const std::string line = src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
			assem.feed(line.c_str());
			if (nl == std::string::npos) { break; }
			pos = nl + 1;
		}
		assem.finalize(codeSize, gs, cs, fc);
	} catch (const Exception& e) {
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str()); return false;
	}
	gGlobalsSize = gs; gConstsSize = cs; gFunctionCount = fc;
	gCleanImage.assign(gMemory, gMemory + DATA_SIZE);
	UInt sz = 0;
	gInPtr = globals.findGlobal("gIn", sz);
	gOutPtr = globals.findGlobal("gOut", sz);
	const Pointer mainPtr = globals.findFunction("main");
	if (gInPtr == NULL_POINTER || gOutPtr == NULL_POINTER || mainPtr == NULL_POINTER) { return false; }
	mainFuncIndex = gFunctionTable[mainPtr - IP_OFFSET];
	return true;
}

static void restoreClean() { std::memcpy(gMemory, gCleanImage.data(), DATA_SIZE * sizeof(Value)); }

static std::vector<Value> runInterpreter(Pointer mainPtr, Pointer gInPtr, int n) {
	restoreClean();
	Processor p(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
			CALL_STACK_SIZE, gCallStack, nullptr, nullptr);
	p.accessMemory(gInPtr, 1)->i = n;
	Status s = p.enterCall(mainPtr);
	do { p.resetTimeOut(0x7FFFFFFF); s = p.run(); } while (s == TIME_OUT);
	if (s != OK) { std::printf("  interpreter status=%d\n", s); ++failures; }
	return std::vector<Value>(gMemory, gMemory + DATA_SIZE);
}

static bool imagesEqual(const std::vector<Value>& a, const std::vector<Value>& b, int& firstDiff) {
	for (size_t i = 0; i < a.size(); ++i) { if (a[i].i != b[i].i) { firstDiff = static_cast<int>(i); return false; } }
	firstDiff = -1; return true;
}

// Assemble + lower, then run at full fuel (no suspend) and at tiny fuel (many suspends), comparing memory to the
// interpreter each time. `perCallFuelTiny` forces repeated RESUME cycles.
static void runKernel(const char* name, const char* source, const int* inputs, size_t nInputs, int perCallFuelTiny) {
	std::printf("Kernel \"%s\":\n", name);
	Symbols globals;
	UInt mainFuncIndex = 0;
	Pointer gInPtr = NULL_POINTER, gOutPtr = NULL_POINTER;
	if (!assemble(source, globals, mainFuncIndex, gInPtr, gOutPtr)) { std::printf("  setup failed\n"); ++failures; return; }
	const Pointer mainPtr = globals.findFunction("main");

	// Any engine instance gives the (fixed) resume-field offset.
	JitEngine probe(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
			CALL_STACK_SIZE, gCallStack);
	const uint32_t resumeOff = probe.resumeFieldOffset();

	Emitter e;
	const LowerInfo info = lowerFunction(e, gCode, mainFuncIndex, resumeOff);
	if (!info.ok) { std::printf("  lowering failed\n"); ++failures; return; }
	void* code = makeExecutable(e.code(), e.wordCount());
	if (code == nullptr) { std::printf("  W^X alloc failed\n"); ++failures; return; }
	std::printf("  lowered to %zu native words; loops=%u resumeFieldOffset=%u\n", e.wordCount(), info.loops, resumeOff);

	for (size_t k = 0; k < nInputs; ++k) {
		const int n = inputs[k];
		const std::vector<Value> want = runInterpreter(mainPtr, gInPtr, n);

		for (int pass = 0; pass < 2; ++pass) {			// pass 0: ample fuel (0 suspends); pass 1: tiny fuel (many)
			const int fuel = (pass == 0) ? 0x7FFFFFFF : perCallFuelTiny;
			restoreClean();
			JitEngine eng(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize,
					gConstsSize, CALL_STACK_SIZE, gCallStack);
			eng.accessMemory(gInPtr, 1)->i = n;
			int suspends = 0;
			const Status s = eng.run(code, eng.frameBaseFor(info), fuel, suspends);
			std::vector<Value> got(gMemory, gMemory + DATA_SIZE);
			int diff = -1;
			const bool ok = (s == OK) && imagesEqual(want, got, diff);
			std::printf("    n=%-7d %-11s suspends=%-5d gOut=%-12d %s\n", n, pass == 0 ? "[fullfuel]" : "[tinyfuel]",
					suspends, gMemory[gOutPtr - MEMORY_OFFSET].i, ok ? "IDENTICAL" : "DIFFERS");
			if (!ok) { if (diff >= 0) std::printf("      first diff at word %d\n", diff); ++failures; }
		}
	}
	::munmap(code, e.wordCount() * sizeof(uint32_t));
}

// --- kernels ---

static const char* const K_SUMTO =		// one loop -> one safepoint
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"main: FUNC\n      PARA *1\n$n: LOCi\n$s: LOCi\n$i: LOCi\n"
	"      PEEK $n &gIn\n      MOVi $s #0\n      MOVi $i #0\n"
	".l:   ADDi $s $s $i\n      FORi $i $n @.l\n      POKE &gOut $s\n      RETU\n";

static const char* const K_TWOLOOP =	// two sequential loops -> TWO safepoints; result = sum(i) + sum(i*i)
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"main: FUNC\n      PARA *1\n$n: LOCi\n$a: LOCi\n$b: LOCi\n$i: LOCi\n$t: LOCi\n"
	"      PEEK $n &gIn\n      MOVi $a #0\n      MOVi $i #0\n"
	".l1:  ADDi $a $a $i\n      FORi $i $n @.l1\n"
	"      MOVi $b #0\n      MOVi $i #0\n"
	".l2:  MULi $t $i $i\n      ADDi $b $b $t\n      FORi $i $n @.l2\n"
	"      ADDi $a $a $b\n      POKE &gOut $a\n      RETU\n";

int main() {
	std::printf("GAZLJit RESUME continuation (§5.7.5): suspend/resume via one field, no side table (arm64)\n\n");
	const int inputs[] = { 0, 1, 5, 10, 100, 1000 };
	runKernel("sumTo", K_SUMTO, inputs, sizeof(inputs) / sizeof(*inputs), /*tiny fuel*/ 6);
	std::printf("\n");
	runKernel("twoLoop", K_TWOLOOP, inputs, sizeof(inputs) / sizeof(*inputs), /*tiny fuel*/ 6);

	std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures, failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
