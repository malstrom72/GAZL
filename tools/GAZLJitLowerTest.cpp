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
	A real (if minimal) GAZL -> arm64 v1 lowering PASS — the step past hand-emitting one kernel. `lowerFunction()` walks
	a function's finalized `Instruction[]` and emits native code automatically: one v1 (memory-resident, §5.3/§5.8)
	sequence per GAZL instruction, basic-block detection, a per-loop fuel-check safepoint at back-edge targets, and
	`Label`/branch fixups. It is validated by compiling three DIFFERENT kernels and asserting each JIT run's whole memory
	image is byte-identical to the interpreter's (docs/JitCompilerResearch.md §5.2, §5.6, §5.7.4).

	Design (v1, no register allocation): every operand is a Value-index off `dsp`; the pass passes the native code a
	frame base at `dsp + minIndex` (minIndex = lowest slot index used) so all slots land at non-negative scaled offsets,
	then `read(slot)`/`def(slot)` are a plain load/store each — no LDUR needed, and memory is current at every block
	boundary, so a suspend is trivially interpreter-resumable. Supports the integer subset the kernels need (MOVi,
	PEEK/POKE globals, ADD/SUB/MUL/AND/IOR/XOR, FORi, LSS/NLS/EQU/NEQ branches, GOTO, RETU); an unsupported opcode is a
	hard error, not silent wrong code.

	Still deferred (noted, not hidden): multi-safepoint resume needs a resume-block id (the suspend/resume demo is kept
	to single-loop kernels); calls, pointer PEEK/POKE, floats, and v2 register allocation are later tiers. src/GAZL.* is
	compiled READ-ONLY. AArch64 only. Exits non-zero on any mismatch.
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

// --- W^X executable memory (spike A1 rung-1 strategy; see tools/GAZLJitExecTest.cpp) ---

static void* makeExecutable(const uint32_t* words, size_t wordCount) {
	const size_t bytes = wordCount * sizeof(uint32_t);
#if defined(__APPLE__)
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	if (p == MAP_FAILED) {
		return nullptr;
	}
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

// GAZL finalized opcodes (enum is internal to GAZL.cpp; base = FIRST_OPCODE_VALUE 0x2345, declaration order).
enum {
	OP_FUNC = 0x2345 + 0, OP_RETU = 0x2345 + 4, OP_MOVE_VV = 0x2345 + 5, OP_MOVE_VC = 0x2345 + 6,
	OP_PEEK_VC = 0x2345 + 7, OP_POKE_CV = 0x2345 + 8,
	OP_ADDI_VVV = 0x2345 + 21, OP_ADDI_VVC = 0x2345 + 22,
	OP_SUBI_VVV = 0x2345 + 23, OP_SUBI_VVC = 0x2345 + 24, OP_SUBI_VCV = 0x2345 + 25,
	OP_MULI_VVV = 0x2345 + 26, OP_MULI_VVC = 0x2345 + 27,
	OP_ANDI_VVV = 0x2345 + 34, OP_ANDI_VVC = 0x2345 + 35,
	OP_IORI_VVV = 0x2345 + 36, OP_IORI_VVC = 0x2345 + 37,
	OP_XORI_VVV = 0x2345 + 38, OP_XORI_VVC = 0x2345 + 39,
	OP_FORi_VVB = 0x2345 + 67, OP_FORi_VCB = 0x2345 + 68,
	OP_LSSI_VVB = 0x2345 + 69, OP_LSSI_VCB = 0x2345 + 70, OP_LSSI_CVB = 0x2345 + 71,
	OP_EQUI_VVB = 0x2345 + 72, OP_EQUI_VCB = 0x2345 + 73,
	OP_NLSI_VVB = 0x2345 + 74, OP_NLSI_VCB = 0x2345 + 75, OP_NLSI_CVB = 0x2345 + 76,
	OP_NEQI_VVB = 0x2345 + 77, OP_NEQI_VCB = 0x2345 + 78,
	OP_GOTO = 0x2345 + 89
};

// Operand kinds per position, for basic-block/frame analysis. Only SLOT operands are Value-indices off dsp.
enum Kind { K_NONE, K_SLOT, K_CONST, K_PTR, K_BRANCH };
struct OpInfo { Kind k[3]; bool isBranch; bool isReturn; };

// Static operand-shape table for the supported subset. `ok=false` marks an unsupported opcode.
static OpInfo opInfo(Int opcode, bool& ok) {
	ok = true;
	switch (opcode) {
		case OP_FUNC:     return { { K_CONST, K_CONST, K_NONE }, false, false };
		case OP_RETU:     return { { K_CONST, K_NONE, K_NONE }, false, true };
		case OP_MOVE_VV:  return { { K_SLOT, K_SLOT, K_NONE }, false, false };
		case OP_MOVE_VC:  return { { K_SLOT, K_CONST, K_NONE }, false, false };
		case OP_PEEK_VC:  return { { K_SLOT, K_PTR, K_NONE }, false, false };
		case OP_POKE_CV:  return { { K_PTR, K_SLOT, K_NONE }, false, false };
		case OP_ADDI_VVV: case OP_SUBI_VVV: case OP_MULI_VVV: case OP_ANDI_VVV: case OP_IORI_VVV: case OP_XORI_VVV:
			return { { K_SLOT, K_SLOT, K_SLOT }, false, false };
		case OP_ADDI_VVC: case OP_SUBI_VVC: case OP_MULI_VVC: case OP_ANDI_VVC: case OP_IORI_VVC: case OP_XORI_VVC:
			return { { K_SLOT, K_SLOT, K_CONST }, false, false };
		case OP_SUBI_VCV: return { { K_SLOT, K_CONST, K_SLOT }, false, false };
		case OP_FORi_VVB: return { { K_SLOT, K_SLOT, K_BRANCH }, true, false };
		case OP_FORi_VCB: return { { K_SLOT, K_CONST, K_BRANCH }, true, false };
		case OP_LSSI_VVB: case OP_EQUI_VVB: case OP_NLSI_VVB: case OP_NEQI_VVB:
			return { { K_SLOT, K_SLOT, K_BRANCH }, true, false };
		case OP_LSSI_VCB: case OP_EQUI_VCB: case OP_NLSI_VCB: case OP_NEQI_VCB:
			return { { K_SLOT, K_CONST, K_BRANCH }, true, false };
		case OP_LSSI_CVB: case OP_NLSI_CVB:
			return { { K_CONST, K_SLOT, K_BRANCH }, true, false };
		case OP_GOTO:     return { { K_BRANCH, K_NONE, K_NONE }, true, false };
		default: ok = false; return { { K_NONE, K_NONE, K_NONE }, false, false };
	}
}

// The native ABI: x0 = frame base (dsp + minIndex), x1 = memoryBase, w2 = fuel; returns Status in w0.
typedef int (*KernelFn)(Value* frameBase, Value* memoryBase, int fuel);

struct LowerInfo {
	int minIndex;					// lowest slot index; frame base = dsp + minIndex
	UInt localsSize;				// FUNC prologue advances dsp by this
	std::vector<UInt> loopHeads;	// GAZL indices of back-edge targets (safepoints)
	bool ok;
};

/*
	The JIT engine as a `Processor` subclass, sharing the base machine state (see tools/GAZLJitEngineTest.cpp).
*/
class JitEngine : public Processor {
	public:		JitEngine(UInt codeSize, const Instruction* code, UInt fnCount, const UInt* fnTable, UInt memSize
						, Value* mem, UInt globalsSize, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack)
					: Processor(codeSize, code, fnCount, fnTable, memSize, mem, globalsSize, constsSize, ipStackSize
						, ipStack, 0, 0) { }

		// Run compiled code for the function selected by a prior enterCall(); on fuel suspend at the single loop head,
		// publish interpreter-resumable state (post-prologue dsp + the loop-head GAZL ip).
		Status dispatch(KernelFn native, int fuel, const LowerInfo& info, UInt resumeIndex) {
			Value* const origDsp = dsp;
			// Slots resolve against the POST-prologue dsp (origDsp + localsSize); the frame base subtracts minIndex so
			// every slot lands at a non-negative offset. Cell = base + (slot - minIndex) = (origDsp + localsSize) + slot.
			const Status status = native(origDsp + info.localsSize + info.minIndex, memoryBase, fuel);
			if (status == TIME_OUT) {
				dsp = origDsp + info.localsSize;
				ip = codeBase + resumeIndex;
			}
			return status;
		}
};

// --- the lowering pass ---

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

// Emit the load/store/const helpers in terms of the frame (x0) and membase (x1).
static void loadSlot(Emitter& e, Reg r, Int slot, int minIndex) { e.ldrW(r, X0, static_cast<uint32_t>((slot - minIndex) * 4)); }
static void storeSlot(Emitter& e, Reg r, Int slot, int minIndex) { e.strW(r, X0, static_cast<uint32_t>((slot - minIndex) * 4)); }
static void loadGlobal(Emitter& e, Reg r, Pointer p) { e.ldrW(r, X1, static_cast<uint32_t>((p - MEMORY_OFFSET) * 4)); }
static void storeGlobal(Emitter& e, Reg r, Pointer p) { e.strW(r, X1, static_cast<uint32_t>((p - MEMORY_OFFSET) * 4)); }
static void matConst(Emitter& e, Reg r, Int v) { e.movImm32(r, static_cast<uint32_t>(v)); }

// Load an operand (slot or const) into `r`, per its kind.
static void loadOperand(Emitter& e, Reg r, const Value& p, Kind k, int minIndex) {
	if (k == K_SLOT) {
		loadSlot(e, r, p.i, minIndex);
	} else {
		matConst(e, r, p.i);
	}
}

// Emit a binary integer op `dst = s1 <op> s2` given the operand kinds (VVV / VVC / VCV).
static void emitBinary(Emitter& e, void (Emitter::*op)(Reg, Reg, Reg), const Instruction& in, const OpInfo& oi
		, int minIndex) {
	loadOperand(e, W10, in.p1, oi.k[1], minIndex);
	loadOperand(e, W11, in.p2, oi.k[2], minIndex);
	(e.*op)(W9, W10, W11);
	storeSlot(e, W9, in.p0.i, minIndex);
}

// Lower the whole function at `funcIndex` into `e`. Returns analysis info (frame base, loop heads).
static LowerInfo lowerFunction(Emitter& e, const Instruction* code, UInt funcIndex) {
	LowerInfo info;
	info.ok = true;
	info.minIndex = 0;
	info.localsSize = static_cast<UInt>(code[funcIndex].p0.i);

	// Find the function's instruction span [funcIndex .. retIndex].
	UInt retIndex = funcIndex;
	while (code[retIndex].opcode != OP_RETU) {
		bool ok = false;
		opInfo(code[retIndex].opcode, ok);
		if (!ok && code[retIndex].opcode != OP_FUNC) { info.ok = false; return info; }
		++retIndex;
	}

	// Pass 1 — analysis: min slot index; branch targets; back-edge targets (loop heads) + their block weights.
	std::set<UInt> targets;
	std::map<UInt, UInt> loopWeight;			// loop-head index -> block instruction count
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		bool ok = false;
		const OpInfo oi = opInfo(code[j].opcode, ok);
		if (!ok) { info.ok = false; return info; }
		const Value* ps[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int q = 0; q < 3; ++q) {
			if (oi.k[q] == K_SLOT && ps[q]->i < info.minIndex) { info.minIndex = ps[q]->i; }
		}
		if (oi.isBranch) {
			const Int off = (code[j].opcode == OP_GOTO) ? code[j].p0.i : code[j].p2.i;
			const UInt tgt = static_cast<UInt>(static_cast<Int>(j) + off);
			targets.insert(tgt);
			if (tgt <= j) {						// back-edge: its target is a loop head → needs a fuel-check safepoint
				loopWeight[tgt] = j - tgt + 1;
				info.loopHeads.push_back(tgt);
			}
		}
	}

	// One Label per branch-target instruction, plus a shared timeout stub.
	std::map<UInt, Label> labels;
	for (std::set<UInt>::const_iterator it = targets.begin(); it != targets.end(); ++it) {
		labels[*it] = e.newLabel();
	}
	Label timeout = e.newLabel();
	const int mi = info.minIndex;

	// Pass 2 — emit.
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		if (labels.count(j)) { e.bind(labels[j]); }
		if (loopWeight.count(j)) {				// loop head: charge the block, suspend if out of fuel
			e.subsImm(W2, W2, loopWeight[j]);
			e.bcond(MI, timeout);
		}
		const Instruction& in = code[j];
		const Int op = in.opcode;
		if (op == OP_FUNC) {
			continue;							// prologue stack/fuel check omitted for the prototype (§5.8)
		} else if (op == OP_RETU) {
			e.movz(W0, 0);						// OK
			e.ret();
		} else if (op == OP_MOVE_VC) {
			matConst(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i, mi);
		} else if (op == OP_MOVE_VV) {
			loadSlot(e, W9, in.p1.i, mi); storeSlot(e, W9, in.p0.i, mi);
		} else if (op == OP_PEEK_VC) {
			loadGlobal(e, W9, in.p1.p); storeSlot(e, W9, in.p0.i, mi);
		} else if (op == OP_POKE_CV) {
			loadSlot(e, W9, in.p1.i, mi); storeGlobal(e, W9, in.p0.p);
		} else if (op == OP_ADDI_VVV || op == OP_ADDI_VVC) {
			bool ok; emitBinary(e, &Emitter::add, in, opInfo(op, ok), mi);
		} else if (op == OP_SUBI_VVV || op == OP_SUBI_VVC || op == OP_SUBI_VCV) {
			bool ok; emitBinary(e, &Emitter::sub, in, opInfo(op, ok), mi);
		} else if (op == OP_MULI_VVV || op == OP_MULI_VVC) {
			bool ok; emitBinary(e, &Emitter::mul, in, opInfo(op, ok), mi);
		} else if (op == OP_ANDI_VVV || op == OP_ANDI_VVC) {
			bool ok; emitBinary(e, &Emitter::and_, in, opInfo(op, ok), mi);
		} else if (op == OP_IORI_VVV || op == OP_IORI_VVC) {
			bool ok; emitBinary(e, &Emitter::orr, in, opInfo(op, ok), mi);
		} else if (op == OP_XORI_VVV || op == OP_XORI_VVC) {
			bool ok; emitBinary(e, &Emitter::eor, in, opInfo(op, ok), mi);
		} else if (op == OP_FORi_VVB || op == OP_FORi_VCB) {
			bool ok; const OpInfo oi = opInfo(op, ok);
			loadSlot(e, W10, in.p0.i, mi);					// ++i
			e.addImm(W10, W10, 1);
			storeSlot(e, W10, in.p0.i, mi);
			loadOperand(e, W11, in.p1, oi.k[1], mi);		// n
			e.cmp(W10, W11);
			e.bcond(LT, labels[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
		} else {											// conditional branches LSS / EQU / NLS / NEQ
			bool ok; const OpInfo oi = opInfo(op, ok);
			Cond c = LT;
			if (op == OP_LSSI_VVB || op == OP_LSSI_VCB || op == OP_LSSI_CVB) { c = LT; }
			else if (op == OP_NLSI_VVB || op == OP_NLSI_VCB || op == OP_NLSI_CVB) { c = GE; }
			else if (op == OP_EQUI_VVB || op == OP_EQUI_VCB) { c = EQ; }
			else { c = NE; }								// NEQ
			loadOperand(e, W10, in.p0, oi.k[0], mi);
			loadOperand(e, W11, in.p1, oi.k[1], mi);
			e.cmp(W10, W11);
			e.bcond(c, labels[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
		}
	}

	e.bind(timeout);
	e.movn(W0, 0);											// TIME_OUT (-1)
	e.ret();
	e.finalize();
	return info;
}

// --- harness ---

static bool assemble(const char* source, Symbols& globals, UInt& mainFuncIndex, Pointer& gInPtr, Pointer& gOutPtr) {
	UInt codeSize = 0, gs = 0, cs = 0, fc = 0;
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assem.newUnit("lowerKernel");
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
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str());
		return false;
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
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].i != b[i].i) { firstDiff = static_cast<int>(i); return false; }
	}
	firstDiff = -1;
	return true;
}

// Assemble + lower one kernel, then check full-run equivalence over several inputs, and (if it has exactly one loop)
// a suspend-in-JIT / resume-in-interpreter run.
static void runKernel(const char* name, const char* source, const int* inputs, size_t nInputs) {
	std::printf("Kernel \"%s\":\n", name);
	Symbols globals;
	UInt mainFuncIndex = 0;
	Pointer gInPtr = NULL_POINTER, gOutPtr = NULL_POINTER;
	if (!assemble(source, globals, mainFuncIndex, gInPtr, gOutPtr)) {
		std::printf("  assemble/setup failed\n"); ++failures; return;
	}
	const Pointer mainPtr = globals.findFunction("main");

	Emitter e;
	const LowerInfo info = lowerFunction(e, gCode, mainFuncIndex);
	if (!info.ok) { std::printf("  lowering failed (unsupported opcode)\n"); ++failures; return; }
	void* code = makeExecutable(e.code(), e.wordCount());
	if (code == nullptr) { std::printf("  W^X alloc failed\n"); ++failures; return; }
	KernelFn jit = reinterpret_cast<KernelFn>(code);
	std::printf("  lowered to %zu native words; minIndex=%d localsSize=%u loops=%zu\n",
			e.wordCount(), info.minIndex, info.localsSize, info.loopHeads.size());

	for (size_t k = 0; k < nInputs; ++k) {
		const int n = inputs[k];
		const std::vector<Value> want = runInterpreter(mainPtr, gInPtr, n);

		restoreClean();
		JitEngine eng(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
				CALL_STACK_SIZE, gCallStack);
		eng.accessMemory(gInPtr, 1)->i = n;
		eng.enterCall(mainPtr);
		const Status s = eng.dispatch(jit, 0x7FFFFFFF, info, info.loopHeads.empty() ? 0 : info.loopHeads[0]);
		std::vector<Value> got(gMemory, gMemory + DATA_SIZE);
		int diff = -1;
		const bool ok = (s == OK) && imagesEqual(want, got, diff);
		std::printf("    n=%-7d status=%-3d gOut=%-12d %s\n", n, s, gMemory[gOutPtr - MEMORY_OFFSET].i,
				ok ? "IDENTICAL" : "DIFFERS");
		if (!ok) { if (diff >= 0) std::printf("      first diff at word %d\n", diff); ++failures; }
	}

	if (info.loopHeads.size() == 1) {			// suspend/resume-in-interpreter (single safepoint)
		const int n = inputs[nInputs - 1];
		const std::vector<Value> want = runInterpreter(mainPtr, gInPtr, n);
		restoreClean();
		JitEngine eng(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
				CALL_STACK_SIZE, gCallStack);
		eng.accessMemory(gInPtr, 1)->i = n;
		eng.enterCall(mainPtr);
		Status s = eng.dispatch(jit, 8, info, info.loopHeads[0]);
		const bool suspended = (s == TIME_OUT);
		while (s == TIME_OUT) { eng.resetTimeOut(0x7FFFFFFF); s = eng.run(); }
		std::vector<Value> got(gMemory, gMemory + DATA_SIZE);
		int diff = -1;
		const bool ok = (s == OK) && imagesEqual(want, got, diff);
		std::printf("    [suspend/resume] n=%d suspended=%s finalStatus=%d %s\n", n, suspended ? "yes" : "no", s,
				ok ? "IDENTICAL" : "DIFFERS");
		if (!ok) { ++failures; }
	}
	::munmap(code, e.wordCount() * sizeof(uint32_t));
}

// --- three distinct kernels (input gIn, result gOut) ---

static const char* const K_SUMTO =		// sum of 0..n-1        (ADDi, FORi)
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"main: FUNC\n      PARA *1\n$n: LOCi\n$s: LOCi\n$i: LOCi\n"
	"      PEEK $n &gIn\n      MOVi $s #0\n      MOVi $i #0\n"
	".l:   ADDi $s $s $i\n      FORi $i $n @.l\n      POKE &gOut $s\n      RETU\n";

static const char* const K_SUMSQ =		// sum of i*i for 0..n-1  (MULi, ADDi, FORi)
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"main: FUNC\n      PARA *1\n$n: LOCi\n$s: LOCi\n$i: LOCi\n$t: LOCi\n"
	"      PEEK $n &gIn\n      MOVi $s #0\n      MOVi $i #0\n"
	".l:   MULi $t $i $i\n      ADDi $s $s $t\n      FORi $i $n @.l\n      POKE &gOut $s\n      RETU\n";

static const char* const K_ABS =		// abs(x)                (GEQi forward branch, SUBi_VCV negate)
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"main: FUNC\n      PARA *1\n$x: LOCi\n"
	"      PEEK $x &gIn\n      GEQi $x #0 @.done\n      SUBi $x #0 $x\n"
	".done: POKE &gOut $x\n      RETU\n";

int main() {
	std::printf("GAZLJit v1 lowering pass: JIT (compiled from Instruction[]) vs interpreter (arm64)\n\n");

	const int loopInputs[] = { 0, 1, 2, 5, 10, 100, 1000 };
	const int absInputs[] = { 0, 1, -1, 7, -7, 123456, -123456 };
	runKernel("sumTo", K_SUMTO, loopInputs, sizeof(loopInputs) / sizeof(*loopInputs));
	std::printf("\n");
	runKernel("sumSq", K_SUMSQ, loopInputs, sizeof(loopInputs) / sizeof(*loopInputs));
	std::printf("\n");
	runKernel("abs", K_ABS, absInputs, sizeof(absInputs) / sizeof(*absInputs));

	std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures, failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
