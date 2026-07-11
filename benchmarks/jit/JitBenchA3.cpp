/*
	JitBenchA3 - Phase -1 de-risking spike A3 for the proposed GAZL native compiler.

	Measures the speedup a load-time baseline JIT would give over the current GAZL
	interpreter, by running representative kernels two ways:
	  - through the real Assembler + Processor (the interpreter, the reference),
	  - as hand-written AArch64 machine code (JitBenchA3.arm64.S) modelling what the
	    baseline JIT would emit, both memory-resident (v1) and register-allocated (v2),
	    including the per-basic-block fuel check and the software memory bounds check.
	An -O2 C version is included as the optimizing-compiler ceiling. All variants must
	produce bit-identical results (printed as MATCH), which validates both the hand-
	written code and that every variant does the same work.

	See docs/JitCompilerResearch.md (section 11.0 spike A3, worked example in 5.8).
	Currently AArch64 only; x64 / Windows lowerings are future work (they are, in effect,
	what the real JIT will emit).
*/
#if !defined(__aarch64__) && !defined(_M_ARM64)
	#error "JitBenchA3 currently ships only an AArch64 hand-JIT; build on arm64 (Apple Silicon / Linux arm64)."
#endif

#include "GAZL.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <algorithm>

using namespace GAZL;
using Clock = std::chrono::steady_clock;

extern "C" int bench_v2(int n, int fuel);
extern "C" int bench_v1(int n, int fuel, int* frame);
extern "C" int bench_mem_v2(int n, int fuel, int* buf, int bufsize);
extern "C" int bench_rnd_v2(int n, int fuel, int* buf, int bufsize);
extern "C" int bench_cache(int n, int fuel, int* frame);
extern "C" int bench_mem_cache_cons(int n, int fuel, int* buf, int bufsize, int* frame);
extern "C" int bench_mem_cache_prov(int n, int fuel, int* buf, int bufsize, int* frame);

// -O2 C references: what a fully-optimizing compiler (AOT / LLVM) produces. Ceiling.
static int bench_c(int n) {
	int acc = 12345, sum = 0;
	for (int i = 0; i < n; ++i) {
		acc = acc * 1103515245 + 12345;
		sum += acc & 0x3FFFFFFF;
	}
	return sum;
}

static int bench_mem_c(int n, int* buf, int bufsize) {
	int prev = 12345, mask = bufsize - 1;
	for (int i = 0; i < n; ++i) {
		int idx = i & mask;
		int x = buf[idx] + prev;
		buf[idx] = x;
		prev = x;
	}
	return prev;
}

static int bench_rnd_c(int n, int* buf) {
	unsigned st = 12345;
	int sum = 0;
	for (int i = 0; i < n; ++i) {
		st = st * 1103515245u + 12345u;
		int idx = (int)(st >> 10);						// 22-bit index
		int x = buf[idx] + (int)st;
		buf[idx] = x;
		sum += x;
	}
	return sum;
}

static double secs(Clock::time_point a, Clock::time_point b) {
	return std::chrono::duration<double>(b - a).count();
}

static const UInt MAX_CODE = 1000;
static const UInt IPSTK = 64;

/**
	Owns the buffers for one assembled GAZL program and its Processor.
**/
struct Program {
	Instruction* code;
	Value* mem;
	CallStackEntry* ipStack;
	NativeFunc natives[1];
	Symbols globals;
	UInt codeSize, globalsSize, constsSize;
	Processor* vm;

	Program(const char* src, UInt memWords) : code(new Instruction[MAX_CODE]), mem(new Value[memWords])
			, ipStack(new CallStackEntry[IPSTK]), codeSize(0), globalsSize(0), constsSize(0), vm(0) {
		natives[0] = 0;
		std::memset(mem, 0, memWords * sizeof(Value));
		Assembler assem(MAX_CODE, code, memWords, mem, globals);
		assem.newUnit("bench");
		const char* cp = src;
		while (*cp != 0) {
			cp = assem.feed(cp);
			if (*cp == 0) assem.finalize(codeSize, globalsSize, constsSize);
		}
		vm = new Processor(codeSize, code, memWords, mem, globalsSize, constsSize, IPSTK, ipStack, natives);
	}
	~Program() { delete vm; delete[] code; delete[] mem; delete[] ipStack; }

	Int runOnce() {
		vm->resetTimeOut(0x7FFFFFFF);
		Status st = vm->enterCall(globals.findFunction("bench"));
		if (st == OK) st = vm->run();
		return st;
	}
};

static void printRow(const char* name, double t, double tRef, int N) {
	std::printf("    %-27s %8.1f ms  %7.2f ns/iter  %6.2fx\n", name, t * 1e3, t * 1e9 / N, tRef / t);
}

// --- Arithmetic kernel: dispatch-bound scalar loop, no memory. ---
static void runArith(int N, int REPS) {
	char src[2048];
	std::snprintf(src, sizeof src,
		"resultG:   GLOB *1\n"
		"bench:     FUNC\n"
		"  acc:     LOCi\n"
		"  sum:     LOCi\n"
		"  i:       LOCi\n"
		"  tmp:     LOCi\n"
		" MOVi  acc  #12345\n"
		" MOVi  sum  #0\n"
		" MOVi  i    #0\n"
		"loop: NOOP\n"
		" MULi  acc  acc  #1103515245\n"
		" ADDi  acc  acc  #12345\n"
		" ANDi  tmp  acc  #1073741823\n"
		" ADDi  sum  sum  tmp\n"
		" FORi  i    #%d  @loop\n"
		" POKE  &resultG  sum\n"
		" RETU\n", N);
	Program p(src, 100000);
	UInt gs = 0;
	Pointer resPtr = p.globals.findGlobal("resultG", gs);

	int rInterp = 0; double tInterp = 1e30;
	for (int r = 0; r < REPS; ++r) {
		auto a = Clock::now();
		if (p.runOnce() != OK) { std::printf("run() failed\n"); return; }
		auto b = Clock::now();
		rInterp = p.vm->accessMemory(resPtr, 1)->i;
		tInterp = std::min(tInterp, secs(a, b));
	}
	int rV1 = 0, rV2 = 0, rBC = 0, rC = 0; double tV1 = 1e30, tV2 = 1e30, tBC = 1e30, tC = 1e30;
	int frame[8] = { 0 };
	volatile int sink = 0;
	for (int r = 0; r < REPS; ++r) { auto a = Clock::now(); rV2 = bench_v2(N, 0x7FFFFFFF);           auto b = Clock::now(); tV2 = std::min(tV2, secs(a, b)); }
	for (int r = 0; r < REPS; ++r) { auto a = Clock::now(); rV1 = bench_v1(N, 0x7FFFFFFF, frame);    auto b = Clock::now(); tV1 = std::min(tV1, secs(a, b)); }
	for (int r = 0; r < REPS; ++r) { auto a = Clock::now(); rBC = bench_cache(N, 0x7FFFFFFF, frame); auto b = Clock::now(); tBC = std::min(tBC, secs(a, b)); }
	for (int r = 0; r < REPS; ++r) { auto a = Clock::now(); rC  = bench_c(N);                        auto b = Clock::now(); sink += rC; tC = std::min(tC, secs(a, b)); }

	bool ok = (rInterp == rV1 && rInterp == rV2 && rInterp == rBC && rInterp == rC);
	std::printf("ARITHMETIC  N=%d  result=%d  %s\n", N, rInterp, ok ? "MATCH" : "*** MISMATCH ***");
	printRow("interpreter (GAZL)", tInterp, tInterp, N);
	printRow("JIT v1 (mem-resident)", tV1, tInterp, N);
	printRow("JIT cache (label-clear)", tBC, tInterp, N);
	printRow("JIT v2 (pinned registers)", tV2, tInterp, N);
	printRow("C -O2 (optimizing ceiling)", tC, tInterp, N);
	std::printf("\n");
}

// --- Sequential memory kernel: bounds-checked read+write per element. ---
static void runSeq(int bufsize, int N, int REPS) {
	char src[2048];
	std::snprintf(src, sizeof src,
		"buf:     GLOB *%d\n"
		"result:  GLOB *1\n"
		"bench:   FUNC\n"
		"  bufptr: LOCp\n"
		"  prev:  LOCi\n"
		"  i:     LOCi\n"
		"  idx:   LOCi\n"
		"  x:     LOCi\n"
		" MOVp bufptr &buf\n"
		" MOVi prev  #12345\n"
		" MOVi i     #0\n"
		"loop: NOOP\n"
		" ANDi idx  i    #%d\n"
		" PEEK x    bufptr idx\n"
		" ADDi x    x    prev\n"
		" POKE bufptr idx x\n"
		" MOVi prev x\n"
		" FORi i    #%d  @loop\n"
		" POKE &result prev\n"
		" RETU\n", bufsize, bufsize - 1, N);
	Program p(src, (UInt)bufsize + 8192);
	UInt gs = 0;
	Pointer bufPtr = p.globals.findGlobal("buf", gs);
	Pointer resPtr = p.globals.findGlobal("result", gs);

	int rInterp = 0; double tInterp = 1e30;
	for (int r = 0; r < REPS; ++r) {
		std::memset(p.vm->accessMemory(bufPtr, bufsize), 0, (size_t)bufsize * sizeof(Value));
		auto a = Clock::now();
		if (p.runOnce() != OK) { std::printf("run() failed\n"); return; }
		auto b = Clock::now();
		rInterp = p.vm->accessMemory(resPtr, 1)->i;
		tInterp = std::min(tInterp, secs(a, b));
	}
	int* buf = new int[bufsize];
	int frame[8] = { 0 };
	int rV2 = 0, rCC = 0, rCP = 0, rC = 0; double tV2 = 1e30, tCC = 1e30, tCP = 1e30, tC = 1e30; volatile int sink = 0;
	for (int r = 0; r < REPS; ++r) { std::memset(buf, 0, (size_t)bufsize * sizeof(int)); auto a = Clock::now(); rV2 = bench_mem_v2(N, 0x7FFFFFFF, buf, bufsize);              auto b = Clock::now(); tV2 = std::min(tV2, secs(a, b)); }
	for (int r = 0; r < REPS; ++r) { std::memset(buf, 0, (size_t)bufsize * sizeof(int)); auto a = Clock::now(); rCC = bench_mem_cache_cons(N, 0x7FFFFFFF, buf, bufsize, frame); auto b = Clock::now(); tCC = std::min(tCC, secs(a, b)); }
	for (int r = 0; r < REPS; ++r) { std::memset(buf, 0, (size_t)bufsize * sizeof(int)); auto a = Clock::now(); rCP = bench_mem_cache_prov(N, 0x7FFFFFFF, buf, bufsize, frame); auto b = Clock::now(); tCP = std::min(tCP, secs(a, b)); }
	for (int r = 0; r < REPS; ++r) { std::memset(buf, 0, (size_t)bufsize * sizeof(int)); auto a = Clock::now(); rC  = bench_mem_c(N, buf, bufsize);                            auto b = Clock::now(); sink += rC; tC = std::min(tC, secs(a, b)); }

	bool ok = (rInterp == rV2 && rInterp == rCC && rInterp == rCP && rInterp == rC);
	std::printf("SEQ MEMORY  buf=%d words (%.0f KB)  result=%d  %s\n",
		bufsize, bufsize * 4.0 / 1024.0, rInterp, ok ? "MATCH" : "*** MISMATCH ***");
	printRow("interpreter (GAZL)", tInterp, tInterp, N);
	printRow("JIT cache cons (flush@ptr)", tCC, tInterp, N);
	printRow("JIT cache prov (label only)", tCP, tInterp, N);
	printRow("JIT v2 (pinned registers)", tV2, tInterp, N);
	printRow("C -O2 (no check, ceiling)", tC, tInterp, N);
	std::printf("\n");
	delete[] buf;
}

// --- Random-access memory kernel: scattered indices -> cache misses. ---
static void runRnd(int N, int REPS) {
	const int BS = 1 << 22;								// 4M words = 16 MB
	char src[2048];
	std::snprintf(src, sizeof src,
		"buf:     GLOB *%d\n"
		"result:  GLOB *1\n"
		"bench:   FUNC\n"
		"  bufptr: LOCp\n"
		"  st:    LOCi\n"
		"  sum:   LOCi\n"
		"  i:     LOCi\n"
		"  idx:   LOCi\n"
		"  x:     LOCi\n"
		" MOVp bufptr &buf\n"
		" MOVi st   #12345\n"
		" MOVi sum  #0\n"
		" MOVi i    #0\n"
		"loop: NOOP\n"
		" MULi st  st  #1103515245\n"
		" ADDi st  st  #12345\n"
		" SHRu idx st  #10\n"
		" PEEK x   bufptr idx\n"
		" ADDi x   x   st\n"
		" POKE bufptr idx x\n"
		" ADDi sum sum x\n"
		" FORi i   #%d @loop\n"
		" POKE &result sum\n"
		" RETU\n", BS, N);
	Program p(src, (UInt)BS + 8192);
	UInt gs = 0;
	Pointer bufPtr = p.globals.findGlobal("buf", gs);
	Pointer resPtr = p.globals.findGlobal("result", gs);

	int rInterp = 0; double tInterp = 1e30;
	for (int r = 0; r < REPS; ++r) {
		std::memset(p.vm->accessMemory(bufPtr, BS), 0, (size_t)BS * sizeof(Value));
		auto a = Clock::now();
		if (p.runOnce() != OK) { std::printf("run() failed\n"); return; }
		auto b = Clock::now();
		rInterp = p.vm->accessMemory(resPtr, 1)->i;
		tInterp = std::min(tInterp, secs(a, b));
	}
	int* buf = new int[BS];
	int rV2 = 0, rC = 0; double tV2 = 1e30, tC = 1e30; volatile int sink = 0;
	for (int r = 0; r < REPS; ++r) { std::memset(buf, 0, (size_t)BS * sizeof(int)); auto a = Clock::now(); rV2 = bench_rnd_v2(N, 0x7FFFFFFF, buf, BS); auto b = Clock::now(); tV2 = std::min(tV2, secs(a, b)); }
	for (int r = 0; r < REPS; ++r) { std::memset(buf, 0, (size_t)BS * sizeof(int)); auto a = Clock::now(); rC  = bench_rnd_c(N, buf);                auto b = Clock::now(); sink += rC; tC = std::min(tC, secs(a, b)); }

	bool ok = (rInterp == rV2 && rInterp == rC);
	std::printf("RND MEMORY  buf=16 MB (cache-missing)  N=%d  result=%d  %s\n",
		N, rInterp, ok ? "MATCH" : "*** MISMATCH ***");
	printRow("interpreter (GAZL)", tInterp, tInterp, N);
	printRow("JIT v2 (bounds-checked)", tV2, tInterp, N);
	printRow("C -O2 (no check, ceiling)", tC, tInterp, N);
	std::printf("\n");
	delete[] buf;
}

int main() {
	const int N = 20000000, REPS = 5;
	std::printf("GAZL JIT spike A3 - interpreter vs hand-written baseline-JIT (min of %d runs)\n\n", REPS);
	runArith(N, REPS);
	runSeq(8192, N, REPS);								// 32 KB, L1/L2 resident
	runSeq(1 << 20, N, REPS);							// 4 MB
	runSeq(1 << 24, N, REPS);							// 64 MB, but sequential (prefetched)
	runRnd(5000000, REPS);								// random, cache-missing
	return 0;
}
