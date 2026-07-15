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
	End-to-end execution check for the GAZLJit arm64 Arm64Emitter: the encoding-vs-execution bridge that follows the
	byte-for-byte assemble-diff test (tools/GAZLJitTest.cpp) and precedes the full vertical slice (dispatcher / RESUME /
	fuel / suspend-resume).

	It emits real kernels through the Arm64Emitter, copies the bytes into W^X executable memory, invalidates the icache, and
	*calls* them - reusing the allocation + flush strategy proven GO by JIT spike A1 (see spike/jit-probe/,
	docs/JitSpikeA1-Results.md): on Apple Silicon, `mmap(MAP_JIT)` + a per-thread `pthread_jit_write_protect_np` toggle
	+ `sys_icache_invalidate`; on Linux arm64, `mmap(RW)` → `mprotect(RX)` → `__builtin___clear_cache`. The emitted
	results are compared against independent C reference implementations. Exits non-zero on any mismatch.
*/

#include "GAZLJit.h"
#include "GAZLJitArm64.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>

#if defined(__APPLE__)
	#include <pthread.h>
	#include <libkern/OSCacheControl.h>
#endif

using namespace GAZL;

static int failures = 0;

// Copy `words` instruction words into freshly-allocated W^X memory, flush the icache, and return an executable pointer.
// Reuses the spike A1 ladder's rung-1 strategy (see file header). Returns nullptr on allocation failure.
static void* mapExecutable(const uint32_t* words, size_t wordCount) {
	const size_t bytes = wordCount * sizeof(uint32_t);
#if defined(__APPLE__)
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	if (p == MAP_FAILED) {
		return nullptr;
	}
	const bool perThreadToggle = (pthread_jit_write_protect_supported_np() != 0);
	if (perThreadToggle) {
		pthread_jit_write_protect_np(0);						// this thread: MAP_JIT pages writable
	}
	std::memcpy(p, words, bytes);
	if (perThreadToggle) {
		pthread_jit_write_protect_np(1);						// this thread: back to executable
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

// --- kernel 1: int sumTo(int n) = 0 + 1 + ... + (n-1), all in registers (docs/JitCompilerResearch.md §5.8) ---

static void emitSumTo(Arm64Emitter& e) {
	e.movz(W9, 0);							// result = 0
	e.movz(W10, 0);							// i = 0
	Label loop = e.newLabel();
	Label done = e.newLabel();
	e.bind(loop);
	e.cmp(W10, W0);							// i < n ?
	e.bcond(GE, done);						//   i >= n → done  (handles n <= 0 too)
	e.add(W9, W9, W10);						// result += i
	e.addImm(W10, W10, 1);					// ++i
	e.b(loop);
	e.bind(done);
	e.mov(W0, W9);							// return result
	e.ret();
	e.finalize();
}

static int refSumTo(int n) {
	int result = 0;
	for (int i = 0; i < n; ++i) {
		result += i;
	}
	return result;
}

// --- kernel 2: bench_v2 (benchmarks/jit/JitBenchA3.arm64.S) - LCG sum with the per-block fuel check ---

static void emitBenchV2(Arm64Emitter& e) {
	e.movz(W9, 12345);						// acc
	e.movz(W10, 0);							// sum
	e.movz(W11, 0);							// i
	e.movz(W13, 0x4E6D);
	e.movk(W13, 0x41C6, 16);				// K = 1103515245
	e.movz(W14, 12345);						// C
	e.movz(W15, 0xFFFF);
	e.movk(W15, 0x3FFF, 16);				// mask = 0x3FFFFFFF
	Label loop = e.newLabel();
	Label done = e.newLabel();
	e.bind(loop);
	e.subsImm(W1, W1, 5);					// fuel -= blockWeight(5)
	e.bcond(MI, done);						//   fuel < 0 → done
	e.mul(W9, W9, W13);						// acc *= K
	e.add(W9, W9, W14);						// acc += C
	e.and_(W12, W9, W15);					// tmp = acc & mask
	e.add(W10, W10, W12);					// sum += tmp
	e.addImm(W11, W11, 1);					// ++i
	e.cmp(W11, W0);							// i < n ?
	e.bcond(LT, loop);
	e.bind(done);
	e.mov(W0, W10);							// return sum
	e.ret();
	e.finalize();
}

static int refBenchV2(int n, int fuel) {
	uint32_t acc = 12345, sum = 0;
	int i = 0, f = fuel;
	for (;;) {
		f -= 5;
		if (f < 0) {
			break;
		}
		acc = acc * 1103515245u + 12345u;
		sum += (acc & 0x3FFFFFFFu);
		++i;
		if (!(i < n)) {
			break;
		}
	}
	return static_cast<int>(sum);
}

typedef int (*Fn1)(int);
typedef int (*Fn2)(int, int);

// Emit a kernel, run it from W^X memory, and compare its return value against a reference.
static void runCheck1(const char* name, void (*emit)(Arm64Emitter&), int (*ref)(int), int arg) {
	Arm64Emitter e;
	emit(e);
	void* code = mapExecutable(e.code(), e.wordCount());
	if (code == nullptr) {
		std::printf("  %-20s ALLOC FAILED (W^X unavailable)\n", name);
		++failures;
		return;
	}
	const int got = reinterpret_cast<Fn1>(code)(arg);
	const int want = ref(arg);
	const bool ok = (got == want);
	std::printf("  %-20s arg=%-8d got=%-12d want=%-12d %s\n", name, arg, got, want, ok ? "OK" : "WRONG");
	if (!ok) {
		++failures;
	}
	unmapExec(code, e.wordCount());
}

static void runCheck2(const char* name, void (*emit)(Arm64Emitter&), int (*ref)(int, int), int a, int b) {
	Arm64Emitter e;
	emit(e);
	void* code = mapExecutable(e.code(), e.wordCount());
	if (code == nullptr) {
		std::printf("  %-20s ALLOC FAILED (W^X unavailable)\n", name);
		++failures;
		return;
	}
	const int got = reinterpret_cast<Fn2>(code)(a, b);
	const int want = ref(a, b);
	const bool ok = (got == want);
	std::printf("  %-20s n=%-6d fuel=%-8d got=%-12d want=%-12d %s\n", name, a, b, got, want, ok ? "OK" : "WRONG");
	if (!ok) {
		++failures;
	}
	unmapExec(code, e.wordCount());
}

int main() {
	std::printf("GAZLJit Arm64Emitter execution test (arm64, W^X)\n\n");

	std::printf("sumTo(n) = 0+1+...+(n-1), register-resident loop:\n");
	runCheck1("sumTo", emitSumTo, refSumTo, 0);
	runCheck1("sumTo", emitSumTo, refSumTo, 1);
	runCheck1("sumTo", emitSumTo, refSumTo, 10);
	runCheck1("sumTo", emitSumTo, refSumTo, 100);
	runCheck1("sumTo", emitSumTo, refSumTo, 46341);		// > sqrt(INT_MAX): exercises 32-bit wrap identically

	std::printf("\nbench_v2(n, fuel) = LCG sum with per-block fuel check:\n");
	runCheck2("bench_v2 (n-bound)",    emitBenchV2, refBenchV2, 20, 100000);	// terminates on i == n
	runCheck2("bench_v2 (fuel-bound)", emitBenchV2, refBenchV2, 100000, 50);	// terminates on fuel < 0
	runCheck2("bench_v2 (zero-iter)",  emitBenchV2, refBenchV2, 1, 4);		// fuel < 5 → done before body

	std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures,
			failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
