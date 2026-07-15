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
	C3 vertical slice (minimal) - proves that a function hand-lowered through the GAZLJit `Arm64Emitter` reproduces the GAZL
	*interpreter's* observable result, running under a JIT calling convention with a per-basic-block fuel check and a
	trap-free status-returning safepoint. This is roadmap spike C3 (docs/JitCompilerResearch.md §11.0).

	Scope of THIS pass (see docs/JitEmitterHandoff.md and the read-only-VM constraint below):
	  - src/GAZL.h is included READ-ONLY; src/GAZL.* are not modified. The interpreter side uses only the public API
	    (Assembler / Processor / Symbols).
	  - `Processor`'s runtime state (dsp / memoryBase / ip / clockCyclesLeft) is all `protected`, so this slice cannot
	    literally `br` into JIT code from inside `run()` nor write resume state (ip/fuel) back into a Processor from a
	    safepoint stub. A true in-VM dispatcher + suspend/resume therefore needs VM edits and is deferred to C3-full/C4.
	    Instead this is a STANDALONE dispatcher: it runs the same kernel in the real interpreter for the golden result
	    (read back through a global via the public API) and runs the Arm64Emitter's native code over an identical Value frame.

	What it demonstrates concretely: the frame/`dsp` slot-addressing model (operands are Value-indices off the frame
	base, matching the interpreter), a per-block fuel check (`subs`/`b.mi`) charging the whole block, and the trap-free
	"return a Status" safepoint model - the ABI decisions C3 exists to force. Exits non-zero on any mismatch.

	Note on registers: the real design pins ctx/dsp/membase/fuel in callee-saved x19/x20/x21/w22 (§5.8) so they survive
	GAZL calls. This standalone slice's kernel makes no calls and is invoked directly from C++, so to avoid a
	callee-saved save/restore trampoline it takes dsp in x0 and fuel in w1 (caller-saved) - the register *roles* are the
	same; only their homes differ. Moving them into the pinned callee-saved set is the trampoline's job in the deferred
	in-VM dispatcher.
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

static void unmapExec(void* p, size_t wordCount) {
	if (p != nullptr) {
		::munmap(p, wordCount * sizeof(uint32_t));
	}
}

// --- the interpreter side: assemble & run the sum-loop kernel, read the result back from a global ---

// sumTo(n) = 0 + 1 + ... + (n-1), stored to global `gOut`; input read from global `gIn`. Do-while loop (FORi does
// ++i then compares), so n <= 0 yields 0 with no guard - identical to the emitted kernel below.
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
	"       PEEK $n &gIn\n"
	"       MOVi $sum #0\n"
	"       MOVi $i #0\n"
	".loop: ADDi $sum $sum $i\n"		// sum += i
	"       FORi $i $n @.loop\n"		// ++i ; if i < n goto .loop
	"       POKE &gOut $sum\n"
	"       RETU\n";

namespace {
	const int CODE_SIZE = 64 * 1024;
	const int DATA_SIZE = 64 * 1024;
	const int FUNCTION_TABLE_SIZE = 1024;
	const int CALL_STACK_SIZE = 256;

	Instruction gCode[CODE_SIZE];
	Value gMemory[DATA_SIZE];
	UInt gFunctionTable[FUNCTION_TABLE_SIZE];
	CallStackEntry gCallStack[CALL_STACK_SIZE];

	UInt gGlobalsSize = 0;			// filled by assembleKernel; the data stack must sit AFTER the globals
	UInt gConstsSize = 0;
	UInt gFunctionCount = 0;
}

// Assemble KERNEL_SOURCE once; leaves the machine ready in `pmachine`, and reports the `gIn`/`gOut` global pointers.
static bool assembleKernel(Symbols& globals, Pointer& gInPtr, Pointer& gOutPtr) {
	UInt codeSize = 0, globalsSize = 0, constsSize = 0, functionCount = 0;
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assem.newUnit("sliceKernel");
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
	UInt sz = 0;
	gInPtr = globals.findGlobal("gIn", sz);
	gOutPtr = globals.findGlobal("gOut", sz);
	return (gInPtr != NULL_POINTER && gOutPtr != NULL_POINTER);
}

// Run the interpreter for one input and return sumTo(n) as the interpreter computes it.
static int interpreterSumTo(Symbols& globals, Pointer gInPtr, Pointer gOutPtr, int n) {
	Processor pmachine(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize,
			gConstsSize, CALL_STACK_SIZE, gCallStack, /*natives*/ nullptr, /*userData*/ nullptr);
	pmachine.accessMemory(gInPtr, 1)->i = n;					// set input global
	Status status = pmachine.enterCall(globals.findFunction("main"));	// enterCall only sets up; run() executes
	do {
		pmachine.resetTimeOut(0x7FFFFFFF);
		status = pmachine.run();
	} while (status == TIME_OUT);
	if (status != OK) {
		std::printf("  interpreter run returned status %d\n", status);
		++failures;
		return 0;
	}
	return pmachine.accessMemory(gOutPtr, 1)->i;				// read output global
}

// --- the JIT side: hand-lower the same kernel through the Arm64Emitter ---

// Emitted sumTo. Inputs: x0 = dsp (frame base), w1 = fuel. Frame: [+0] = n (in), [+4] = sum (out).
// Returns Status in w0 (OK on completion, TIME_OUT if the fuel ran out at a block boundary).
static void emitSumTo(Arm64Emitter& e) {
	e.ldrW(W9, X0, 0);				// n = frame[0]
	e.movz(W11, 0);					// i = 0
	e.strW(WZR, X0, 4);				// sum = 0  (frame[1])
	Label loop = e.newLabel();
	Label timeout = e.newLabel();
	e.bind(loop);					// basic-block head: fuel is charged for the whole block up front
	e.subsImm(W1, W1, 2);			// fuel -= blockWeight(2)
	e.bcond(MI, timeout);			//   fuel < 0 → safepoint
	e.ldrW(W10, X0, 4);				// sum
	e.add(W10, W10, W11);			// sum += i
	e.strW(W10, X0, 4);
	e.addImm(W11, W11, 1);			// ++i
	e.cmp(W11, W9);					// i < n ?
	e.bcond(LT, loop);
	e.movz(W0, 0);					// OK
	e.ret();
	e.bind(timeout);				// safepoint stub: no resume-state writeback in the minimal slice (that is C4)
	e.movn(W0, 0);					// -1 (TIME_OUT)
	e.ret();
	e.finalize();
}

typedef int (*SumFn)(Value* dsp, int fuel);

int main() {
	std::printf("GAZLJit C3 vertical slice (minimal): JIT vs interpreter (arm64)\n\n");

	Symbols globals;
	Pointer gInPtr = NULL_POINTER, gOutPtr = NULL_POINTER;
	if (!assembleKernel(globals, gInPtr, gOutPtr)) {
		std::printf("  kernel assembly/setup failed - aborting\n");
		return 1;
	}
	// Build the emitted kernel once and make it executable.
	Arm64Emitter e;
	emitSumTo(e);
	void* code = mapExecutable(e.code(), e.wordCount());
	if (code == nullptr) {
		std::printf("  ALLOC FAILED (W^X unavailable) - aborting\n");
		return 1;
	}
	SumFn jitSumTo = reinterpret_cast<SumFn>(code);

	std::printf("Result equivalence (ample fuel), interpreter vs JIT-over-frame:\n");
	const int inputs[] = { 0, 1, 2, 10, 100, 1000, 46341 };
	for (size_t k = 0; k < sizeof(inputs) / sizeof(*inputs); ++k) {
		const int n = inputs[k];
		const int want = interpreterSumTo(globals, gInPtr, gOutPtr, n);

		Value frame[8];
		std::memset(frame, 0, sizeof(frame));
		frame[0].i = n;											// input slot
		const int status = jitSumTo(frame, 0x7FFFFFFF);			// ample fuel
		const int got = frame[1].i;								// output slot
		const bool ok = (status == OK && got == want);
		std::printf("  n=%-8d interp=%-12d jit=%-12d status=%-3d %s\n", n, want, got, status, ok ? "OK" : "WRONG");
		if (!ok) {
			++failures;
		}
	}

	std::printf("\nFuel contract: tiny fuel must trip the per-block check and return TIME_OUT:\n");
	{
		Value frame[8];
		std::memset(frame, 0, sizeof(frame));
		frame[0].i = 1000000;									// large n so it cannot finish on tiny fuel
		const int status = jitSumTo(frame, 10);					// 10 fuel / 2 per block → ~5 iterations then timeout
		const int partial = frame[1].i;
		const bool ok = (status == TIME_OUT && partial != interpreterSumTo(globals, gInPtr, gOutPtr, 1000000));
		std::printf("  n=1000000 fuel=10 status=%-3d partialSum=%-12d %s\n", status, partial,
				ok ? "OK (timed out mid-loop, as expected)" : "WRONG");
		if (!ok) {
			++failures;
		}
	}

	unmapExec(code, e.wordCount());

	std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures,
			failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
