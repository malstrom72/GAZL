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
	C3-full + C4 prototype - the JIT as a `Processor` SUBCLASS sharing one machine state with the interpreter, exactly
	the engine factoring decided in docs/JitCompilerResearch.md §5.1. This settles the question raised during C3-minimal:
	`Processor`'s runtime state is `protected`, so a subclass reaches `dsp`/`ip`/`ipsp`/`clockCyclesLeft`/`memoryBase`
	directly - NO edit to src/GAZL.* is needed to build a dispatcher or write resume state. (src/GAZL.* is still compiled
	read-only here; the only VM edits the real design wants are making `run()`/`enterCall()` virtual + a `RESUME` field,
	so the *host* can pick an engine through a `Processor*` - orthogonal to this prototype.)

	`ProtoEngine` drives Arm64Emitter-produced native code over the *same* `Processor` state the interpreter uses, and this test
	demonstrates the three claims C3/C4 exist to prove (docs/JitCompilerResearch.md §5.2, §5.4, §5.5, §5.7.5):

	  1. C3 - run a whole GAZL function through the dispatcher; the final observable memory image is BYTE-IDENTICAL to a
	     pure interpreter run.
	  2. C4 suspend/resume + engine interchange - run with tiny fuel so the per-block fuel check suspends the JIT at a
	     basic-block boundary; because v1 keeps every local memory-resident, the suspended state is interpreter-identical,
	     so the *interpreter* resumes from the recorded GAZL ip and finishes to the same byte-identical memory.
	  3. C4 trap - a checked operation that fails returns a `Status` in the return register (no signal handler, no
	     longjmp); the dispatcher propagates it like any other status.

	Scope note: the emitted kernel is a hand lowering (v1, memory-resident, §5.8) of the assembled function, faithful
	instruction-for-instruction so both engines write the same cells. The single static safepoint (the loop head) lets
	the dispatcher recover the resume ip without the emitted code materializing a 64-bit pointer - that generality
	(a per-block RESUME continuation, register spills for v2) is later work. AArch64 only. Exits non-zero on any mismatch.
*/

#include "GAZLJit.h"
#include "GAZLJitArm64.h"
#include "GAZL.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sys/mman.h>

#if defined(__APPLE__)
	#include <pthread.h>
	#include <libkern/OSCacheControl.h>
#endif

using namespace GAZL;

static int failures = 0;

// --- W^X executable memory (reuses the spike A1 rung-1 strategy; see tools/GAZLJitExecTest.cpp) ---

static void* mapExecutable(const uint32_t* words, size_t wordCount) {
	const size_t bytes = wordCount * sizeof(uint32_t);
#if defined(__APPLE__)
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	if (p == MAP_FAILED) {
		return nullptr;
	}
	const bool perThreadToggle = (pthread_jit_write_protect_supported_np() != 0);
	if (perThreadToggle) {
		pthread_jit_write_protect_np(0);
	}
	std::memcpy(p, words, bytes);
	if (perThreadToggle) {
		pthread_jit_write_protect_np(1);
	}
	sys_icache_invalidate(p, bytes);
#else
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		return nullptr;
	}
	std::memcpy(p, words, bytes);
	if (::mprotect(p, bytes, PROT_READ | PROT_EXEC) != 0) {
		::munmap(p, bytes);
		return nullptr;
	}
	__builtin___clear_cache(reinterpret_cast<char*>(p), reinterpret_cast<char*>(p) + bytes);
#endif
	return p;
}

// GAZL finalized opcodes (OP_FUNC/OP_RETU/OP_MOVE_VC/OP_PEEK_VC/OP_POKE_CV/OP_ADDI_VVV/OP_FORi_VVB) come from GAZLJit.h
// (namespace GAZL) now that the JIT is graduated - no local copy needed.

// The native ABI for the emitted kernel (all caller-saved so the leaf needs no register save/restore; the pinned
// callee-saved x19/x20/x21/w22 homes of the real design are the in-VM dispatcher's job, §5.8):
//   x0 = frame base (== interpreter dsp BEFORE the FUNC prologue; locals live at positive byte offsets from here)
//   x1 = memoryBase (globals addressed directly, §5.3)
//   w2 = fuel
//   returns Status in w0.
typedef int (*KernelFn)(Value* frameBase, Value* memoryBase, int fuel);

// Byte offsets baked into the emitted kernel, all derived from the assembled instructions (nothing hard-coded).
struct KernelLayout {
	int nOff, sumOff, iOff;		// frame slots (bytes from frameBase)
	int gInOff, gOutOff;		// globals (bytes from memoryBase)
	UInt loopIndex;				// GAZL instruction index of the loop head (resume ip on suspend)
	UInt localsSize;			// FUNC prologue advances dsp by this
};

/*
	A JIT engine sharing the base `Processor`'s machine state. Being a subclass, it reaches the `protected` state
	(`dsp`, `ip`, `ipsp`, `clockCyclesLeft`, `memoryBase`, `codeBase`) with no change to src/GAZL.*.
*/
class ProtoEngine : public Processor {
	public:		ProtoEngine(UInt codeSize, const Instruction* code, UInt fnCount, const UInt* fnTable, UInt memSize
						, Value* mem, UInt globalsSize, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack
						, NativeFunc const* natives)
					: Processor(codeSize, code, fnCount, fnTable, memSize, mem, globalsSize, constsSize, ipStackSize
						, ipStack, natives, 0) { }

		// Run `native` for the function already selected by a prior `enterCall()` (which set `ip`=FUNC, pushed the call
		// stack, and left `dsp`=frame base). On a fuel suspend, publish interpreter-resumable state: the recorded GAZL
		// `ip` (the single static safepoint = the loop head) and the post-prologue `dsp`. Returns the native Status.
		Status dispatch(KernelFn native, int fuel, const KernelLayout& layout) {
			Value* const frameBase = dsp;						// == dataStackBase after enterCall (FUNC not yet run)
			const Status status = native(frameBase, memoryBase, fuel);
			if (status == TIME_OUT) {
				dsp = frameBase + layout.localsSize;			// what the FUNC prologue would have made dsp
				ip = codeBase + layout.loopIndex;				// resume at the loop head, in EITHER engine
			}
			return status;
		}
};

// --- the shared GAZL kernel: sumTo(n) = 0 + 1 + ... + (n-1); input from global gIn, result to global gOut ---

static const char* const KERNEL_SOURCE =
	"gIn:   GLOB *1\n"
	"       DATi #0\n"
	"gOut:  GLOB *1\n"
	"       DATi #0\n"
	"main:  FUNC\n"
	"       PARA *1\n"
	"$n:    LOCi\n"
	"$sum:  LOCi\n"
	"$i:    LOCi\n"
	"       PEEK $n &gIn\n"			// [1]
	"       MOVi $sum #0\n"			// [2]
	"       MOVi $i #0\n"			// [3]
	".loop: ADDi $sum $sum $i\n"	// [4]  loop head / safepoint
	"       FORi $i $n @.loop\n"	// [5]  ++i ; if i < n goto .loop
	"       POKE &gOut $sum\n"		// [6]
	"       RETU\n";				// [7]

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
	std::vector<Value> gCleanImage;			// gMemory right after assembly, restored before every run
}

// Assemble the kernel; fill in `layout` and the gIn/gOut pointers. Returns false on failure.
static bool assembleKernel(Symbols& globals, KernelLayout& layout, Pointer& gInPtr, Pointer& gOutPtr) {
	UInt codeSize = 0, globalsSize = 0, constsSize = 0, functionCount = 0;
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assem.newUnit("engineKernel");
		std::string src(KERNEL_SOURCE);
		size_t pos = 0;
		while (pos < src.size()) {
			const size_t nl = src.find('\n', pos);
			const std::string line = src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
			assem.feed(line.c_str());
			if (nl == std::string::npos) {
				break;
			}
			pos = nl + 1;
		}
		assem.finalize(codeSize, functionCount, globalsSize, constsSize);
	} catch (const Exception& e) {
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str());
		return false;
	}
	gGlobalsSize = globalsSize;
	gConstsSize = constsSize;
	gFunctionCount = functionCount;
	gCleanImage.assign(gMemory, gMemory + DATA_SIZE);

	UInt sz = 0;
	gInPtr = globals.findGlobal("gIn", sz);
	gOutPtr = globals.findGlobal("gOut", sz);
	const Pointer mainPtr = globals.findFunction("main");
	if (gInPtr == NULL_POINTER || gOutPtr == NULL_POINTER || mainPtr == NULL_POINTER) {
		std::printf("  symbol lookup failed\n");
		return false;
	}

	// Recover the frame/global byte offsets from the finalized instructions (see the [k] markers in KERNEL_SOURCE).
	const UInt f = gFunctionTable[mainPtr - IP_OFFSET];
	const Instruction* in = gCode + f;
	if (in[0].opcode != OP_FUNC || in[1].opcode != OP_PEEK_VC || in[2].opcode != OP_MOVE_VC
			|| in[3].opcode != OP_MOVE_VC || in[4].opcode != OP_ADDI_VVV || in[5].opcode != OP_FORi_VVB
			|| in[6].opcode != OP_POKE_CV || in[7].opcode != OP_RETU) {
		std::printf("  unexpected kernel layout (assembler drift)\n");
		return false;
	}
	layout.localsSize = static_cast<UInt>(in[0].p0.i);				// FUNC localsSize
	const Int nSlot = in[1].p0.i, sumSlot = in[2].p0.i, iSlot = in[3].p0.i;	// slot indices (negative, off dsp)
	// Frame base passed to native == dsp before FUNC; a slot's cell is (dsp + localsSize)[slot], so from the base:
	layout.nOff = static_cast<int>((static_cast<Int>(layout.localsSize) + nSlot) * 4);
	layout.sumOff = static_cast<int>((static_cast<Int>(layout.localsSize) + sumSlot) * 4);
	layout.iOff = static_cast<int>((static_cast<Int>(layout.localsSize) + iSlot) * 4);
	layout.gInOff = static_cast<int>((gInPtr - MEMORY_OFFSET) * 4);
	layout.gOutOff = static_cast<int>((gOutPtr - MEMORY_OFFSET) * 4);
	layout.loopIndex = (f + 5) + static_cast<UInt>(in[5].p2.i);		// FORi target = loop head
	return true;
}

// v1 (memory-resident) hand lowering of the kernel, faithful instruction-for-instruction so it writes exactly the same
// cells the interpreter does. Offsets are baked from `layout`.
static void emitKernel(Arm64Emitter& e, const KernelLayout& L) {
	e.ldrW(W9, X1, L.gInOff);		// [1] PEEK $n <- gIn
	e.strW(W9, X0, L.nOff);
	e.strW(WZR, X0, L.sumOff);		// [2] $sum = 0
	e.strW(WZR, X0, L.iOff);		// [3] $i = 0
	Label loop = e.newLabel();
	Label timeout = e.newLabel();
	e.bind(loop);					// [4] loop head = safepoint: fuel charged for the whole block up front
	e.subsImm(W2, W2, 2);			//     fuel -= blockWeight(2)
	e.bcond(MI, timeout);
	e.ldrW(W9, X0, L.sumOff);		// [4] ADDi $sum = $sum + $i
	e.ldrW(W10, X0, L.iOff);
	e.add(W9, W9, W10);
	e.strW(W9, X0, L.sumOff);
	e.ldrW(W10, X0, L.iOff);		// [5] FORi: ++i
	e.addImm(W10, W10, 1);
	e.strW(W10, X0, L.iOff);
	e.ldrW(W11, X0, L.nOff);		//     if i < n goto .loop
	e.cmp(W10, W11);
	e.bcond(LT, loop);
	e.ldrW(W9, X0, L.sumOff);		// [6] POKE gOut <- $sum
	e.strW(W9, X1, L.gOutOff);
	e.movz(W0, 0);					// [7] RETU -> OK
	e.ret();
	e.bind(timeout);				// safepoint stub: single static resume point (the loop head) - dispatcher sets ip
	e.movn(W0, 0);					// TIME_OUT (-1)
	e.ret();
	e.finalize();
}

// A trap kernel: force a bounds check to fail and return the trap Status in the return register - no signal, no
// longjmp (§5.4). x2 = limit; a deliberately out-of-range index takes the b.hs to the trap stub.
static void emitTrap(Arm64Emitter& e) {
	Label trap = e.newLabel();
	e.movz(W9, 0xFFFF);				// index = 65535 (deliberately out of range)
	e.cmp(W9, W2);					// index vs limit
	e.bcond(HS, trap);				// index >= limit → trap
	e.movz(W0, 0);					// (unreached) OK
	e.ret();
	e.bind(trap);
	e.movn(W0, 2);					// ~2 = -3 = BAD_POKE
	e.ret();
	e.finalize();
}

// --- run helpers ---

static void restoreCleanImage() {
	std::memcpy(gMemory, gCleanImage.data(), DATA_SIZE * sizeof(Value));
}

// Pure interpreter run for input n; returns the full final memory image.
static std::vector<Value> runInterpreter(Symbols& globals, Pointer gInPtr, int n) {
	restoreCleanImage();
	Processor p(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
			CALL_STACK_SIZE, gCallStack, nullptr, nullptr);
	p.accessMemory(gInPtr, 1)->i = n;
	Status s = p.enterCall(globals.findFunction("main"));
	do {
		p.resetTimeOut(0x7FFFFFFF);
		s = p.run();
	} while (s == TIME_OUT);
	if (s != OK) {
		std::printf("  interpreter status=%d\n", s);
		++failures;
	}
	return std::vector<Value>(gMemory, gMemory + DATA_SIZE);
}

static bool imagesEqual(const std::vector<Value>& a, const std::vector<Value>& b, int& firstDiff) {
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].i != b[i].i) {
			firstDiff = static_cast<int>(i);
			return false;
		}
	}
	firstDiff = -1;
	return true;
}

int main() {
	std::printf("GAZLJit C3-full + C4 prototype: ProtoEngine (Processor subclass) vs interpreter (arm64)\n\n");

	Symbols globals;
	KernelLayout layout;
	Pointer gInPtr = NULL_POINTER, gOutPtr = NULL_POINTER;
	if (!assembleKernel(globals, layout, gInPtr, gOutPtr)) {
		std::printf("  setup failed - aborting\n");
		return 1;
	}
	const Pointer mainPtr = globals.findFunction("main");

	Arm64Emitter ek;
	emitKernel(ek, layout);
	void* kernelCode = mapExecutable(ek.code(), ek.wordCount());
	Arm64Emitter et;
	emitTrap(et);
	void* trapCode = mapExecutable(et.code(), et.wordCount());
	if (kernelCode == nullptr || trapCode == nullptr) {
		std::printf("  W^X allocation failed - aborting\n");
		return 1;
	}
	KernelFn jitKernel = reinterpret_cast<KernelFn>(kernelCode);
	KernelFn jitTrap = reinterpret_cast<KernelFn>(trapCode);

	const int inputs[] = { 0, 1, 2, 10, 100, 1000 };

	// 1. C3 - full JIT run, memory byte-identical to the interpreter.
	std::printf("[C3] full JIT run vs interpreter (whole memory image):\n");
	for (size_t k = 0; k < sizeof(inputs) / sizeof(*inputs); ++k) {
		const int n = inputs[k];
		const std::vector<Value> want = runInterpreter(globals, gInPtr, n);

		restoreCleanImage();
		ProtoEngine eng(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
				CALL_STACK_SIZE, gCallStack, nullptr);
		eng.accessMemory(gInPtr, 1)->i = n;
		eng.enterCall(mainPtr);
		const Status s = eng.dispatch(jitKernel, 0x7FFFFFFF, layout);
		const std::vector<Value> got(gMemory, gMemory + DATA_SIZE);
		int diff = -1;
		const bool ok = (s == OK) && imagesEqual(want, got, diff);
		std::printf("  n=%-6d status=%-3d gOut=%-12d %s\n", n, s, gMemory[gOutPtr - MEMORY_OFFSET].i,
				ok ? "IDENTICAL" : "DIFFERS");
		if (!ok) {
			std::printf("    first differing word index=%d\n", diff);
			++failures;
		}
	}

	// 2. C4 - suspend in JIT on tiny fuel, resume in the INTERPRETER, still byte-identical.
	std::printf("\n[C4] suspend in JIT (tiny fuel) -> resume in interpreter -> vs full interpreter:\n");
	for (size_t k = 0; k < sizeof(inputs) / sizeof(*inputs); ++k) {
		const int n = inputs[k];
		const std::vector<Value> want = runInterpreter(globals, gInPtr, n);

		restoreCleanImage();
		ProtoEngine eng(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
				CALL_STACK_SIZE, gCallStack, nullptr);
		eng.accessMemory(gInPtr, 1)->i = n;
		eng.enterCall(mainPtr);
		Status s = eng.dispatch(jitKernel, 8, layout);		// 8 fuel: suspends after a few blocks (n>=5 cases)
		bool suspended = (s == TIME_OUT);
		while (s == TIME_OUT) {								// resume in the OTHER engine - the interpreter
			eng.resetTimeOut(0x7FFFFFFF);
			s = eng.run();
		}
		const std::vector<Value> got(gMemory, gMemory + DATA_SIZE);
		int diff = -1;
		const bool ok = (s == OK) && imagesEqual(want, got, diff);
		std::printf("  n=%-6d suspended=%s finalStatus=%-3d gOut=%-12d %s\n", n, suspended ? "yes" : "no ", s,
				gMemory[gOutPtr - MEMORY_OFFSET].i, ok ? "IDENTICAL" : "DIFFERS");
		if (!ok) {
			std::printf("    first differing word index=%d\n", diff);
			++failures;
		}
	}

	// 3. C4 - a trap returns a Status through the return register (no signal handler).
	std::printf("\n[C4] trap returns a Status (no signal/longjmp):\n");
	{
		const int status = jitTrap(nullptr, nullptr, /*limit*/ 100);
		const bool ok = (status == BAD_POKE);
		std::printf("  forced bounds failure -> status=%d (BAD_POKE=%d) %s\n", status, BAD_POKE, ok ? "OK" : "WRONG");
		if (!ok) {
			++failures;
		}
	}

	::munmap(kernelCode, ek.wordCount() * sizeof(uint32_t));
	::munmap(trapCode, et.wordCount() * sizeof(uint32_t));

	std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures, failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
