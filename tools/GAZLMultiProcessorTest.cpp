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
	Several processors running one program in one memory, each with its own part of the data stack (the lower-level
	`Processor` constructor). Three scenarios:

	  - COOPERATIVE: processors interleaved on one thread with tiny, uneven step budgets, so each is suspended in the
	                 middle of deep recursion over and over while the others run. They share a global counter.
	  - THREADS:     processors running at the same time on OS threads. They share code, constants and globals but
	                 write only their own result word, since GAZL does no synchronization.
	  - FENCES:      a processor whose part is too small for its recursion stops with DATA_STACK_OVERFLOW, and the
	                 neighbouring part is untouched.

	Exits non-zero on any failure.
*/

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "../src/GAZL.h"
#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <windows.h>
#else
	#include <pthread.h>
#endif

using namespace GAZL;

static const int PROCESSOR_COUNT = 4;
static const UInt PART_SIZE = 2048;																	// data stack words per processor
static const UInt CODE_SIZE = 1024;
static const UInt FUNCTION_TABLE_SIZE = 64;
static const UInt DATA_SIZE = 1024 + PROCESSOR_COUNT * PART_SIZE;
static const UInt CALL_STACK_SIZE = 256;
static const Int ROUNDS = 40;
static const Int BASE_DEPTH = 60;																		// processor k recurses BASE_DEPTH + k deep

/*
	`sum` recurses `n` deep with real frames. `task` calls it `rounds` times, accumulates into a local, optionally
	counts every round in the shared global `shared`, and stores its total in `results:id`. The `coop` and `thread`
	entry points run `task` for processor 0 to 3.
*/
static const char* SOURCE = "\n"
		"\t\tGLOB *1\n"
		"shared:\tDATi #0\n"
		"results:\tGLOB *4\n"
		"sum:\tFUNC\n"
		"r:\tOUTi\n"
		"n:\tINPi\n"
		"x:\tLOCi\n"
		"\tLEQi n #0 @base\n"
		"\tSUBi x n #1\n"
		"\tMOVi %1 x\n"
		"\tCALL &sum %0 *2\n"
		"\tADDi r n %0\n"
		"\tRETU\n"
		"base:\tMOVi r #0\n"
		"\tRETU\n"
		"task:\tFUNC\n"
		"id:\tINPi\n"
		"rounds:\tINPi\n"
		"share:\tINPi\n"
		"acc:\tLOCi\n"
		"i:\tLOCi\n"
		"d:\tLOCi\n"
		"\tMOVi acc #0\n"
		"\tADDi d id #60\n"
		"\tMOVi i #0\n"
		"loop:\tGEQi i rounds @done\n"
		"\tMOVi %1 d\n"
		"\tCALL &sum %0 *2\n"
		"\tADDi acc acc %0\n"
		"\tEQUi share #0 @next\n"
		"\tPEEK %0 &shared\n"
		"\tADDi %0 %0 #1\n"
		"\tPOKE &shared %0\n"
		"next:\tADDi i i #1\n"
		"\tGOTO @loop\n"
		"done:\tPOKE &results id acc\n"
		"\tRETU\n";

static Instruction gCode[CODE_SIZE];
static UInt gFunctionTable[FUNCTION_TABLE_SIZE];
static Value gMemory[DATA_SIZE];
static CallStackEntry gCallStacks[PROCESSOR_COUNT][CALL_STACK_SIZE];
static AssembledProgram gProgram;
static Symbols gGlobals;
static int gFailures = 0;

static void fail(const char* what) {
	std::printf("FAIL %s\n", what);
	++gFailures;
}

static std::string entrySource() {																		// coop0..3 and thread0..3
	std::string s;
	char buffer[160];
	for (int k = 0; k < PROCESSOR_COUNT; ++k) {
		for (int share = 1; share >= 0; --share) {
			std::snprintf(buffer, sizeof (buffer), "%s%d:\tFUNC\n\tMOVi %%0 #%d\n\tMOVi %%1 #%d\n\tMOVi %%2 #%d\n"
					"\tCALL &task %%0 *3\n\tRETU\n", (share != 0 ? "coop" : "thread"), k, k, static_cast<int>(ROUNDS), share);
			s += buffer;
		}
	}
	return s;
}

static bool assemble() {
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, gGlobals);
		assem.newUnit("program");
		const std::string source = std::string(SOURCE) + entrySource();
		const Char* p = source.c_str();
		while (*p != 0) p = assem.feed(p);
		assem.finalize(gProgram);
		return true;
	}
	catch (const Exception& e) {
		std::printf("FAIL assembly: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str());
		return false;
	}
}

static Processor* makeProcessor(int k, UInt partSize) {													// processor k gets the k-th part
	return new Processor(gProgram, DATA_SIZE - gProgram.sizes.constsSize, gProgram.sizes.globalsSize + k * PART_SIZE
			, partSize, CALL_STACK_SIZE, gCallStacks[k], 0);
}

static Int expected(int k) {
	const Int d = BASE_DEPTH + k;
	return ROUNDS * (d * (d + 1) / 2);
}

static Value& global(const char* name, UInt index) {														// the constants come first in memory
	UInt size = 0;
	const Pointer p = gGlobals.findGlobal(name, size);
	return gMemory[p - MEMORY_OFFSET + (gProgram.sizes.constsSize - 1) + index];
}

static void clearResults() {
	global("shared", 0).i = 0;
	for (int k = 0; k < PROCESSOR_COUNT; ++k) global("results", k).i = 0;
}

static void checkResults(const char* scenario, bool shared) {
	for (int k = 0; k < PROCESSOR_COUNT; ++k) {
		if (global("results", k).i != expected(k)) {
			char what[96];
			std::snprintf(what, sizeof (what), "%s: processor %d computed %d, expected %d", scenario, k
					, static_cast<int>(global("results", k).i), static_cast<int>(expected(k)));
			fail(what);
		}
	}
	if (shared && global("shared", 0).i != PROCESSOR_COUNT * ROUNDS) fail("cooperative: shared counter is wrong");
}

/*
	Round robin on one thread. Budgets of 3 to 6 steps suspend every processor inside `sum`'s recursion hundreds of
	times while the others run in the same memory.
*/
static void cooperative() {
	clearResults();
	Processor* processors[PROCESSOR_COUNT];
	bool done[PROCESSOR_COUNT];
	int suspends = 0;
	for (int k = 0; k < PROCESSOR_COUNT; ++k) {
		char name[16];
		std::snprintf(name, sizeof (name), "coop%d", k);
		processors[k] = makeProcessor(k, PART_SIZE);
		done[k] = (processors[k]->enterCall(gGlobals.findFunction(name)) != OK);
		if (done[k]) fail("cooperative: enterCall failed");
	}
	for (int remaining = PROCESSOR_COUNT; remaining > 0;) {
		for (int k = 0; k < PROCESSOR_COUNT; ++k) {
			if (done[k]) continue;
			processors[k]->resetTimeOut(3 + k);
			const Status s = processors[k]->run();
			if (s == TIME_OUT) {
				++suspends;
			} else {
				if (s != OK) fail("cooperative: a processor stopped with an error");
				done[k] = true;
				--remaining;
			}
		}
	}
	for (int k = 0; k < PROCESSOR_COUNT; ++k) delete processors[k];
	if (suspends < 100) fail("cooperative: the processors were hardly ever suspended");
	checkResults("cooperative", true);
	std::printf("multiple processors, cooperative: %d processors, %d suspends\n", PROCESSOR_COUNT, suspends);
}

struct ThreadJob {
	Processor* processor;
	Target entry;
	Status status;
};

static void runJob(ThreadJob* job) {
	Status s = job->processor->enterCall(job->entry);
	while (s == OK || s == TIME_OUT) {
		job->processor->resetTimeOut(1000);
		s = job->processor->run();
		if (s == OK) break;
	}
	job->status = s;
}

#if defined(_WIN32)
	static DWORD WINAPI threadMain(LPVOID job) { runJob(static_cast<ThreadJob*>(job)); return 0; }
#else
	static void* threadMain(void* job) { runJob(static_cast<ThreadJob*>(job)); return 0; }
#endif

/*
	One OS thread per processor, all running at once. Each writes only its own result word.
*/
static void threads() {
	clearResults();
	ThreadJob jobs[PROCESSOR_COUNT];
	for (int k = 0; k < PROCESSOR_COUNT; ++k) {
		char name[16];
		std::snprintf(name, sizeof (name), "thread%d", k);
		jobs[k].processor = makeProcessor(k, PART_SIZE);
		jobs[k].entry = gGlobals.findFunction(name);
		jobs[k].status = -1;
	}
#if defined(_WIN32)
	HANDLE handles[PROCESSOR_COUNT];
	for (int k = 0; k < PROCESSOR_COUNT; ++k) handles[k] = CreateThread(0, 0, threadMain, &jobs[k], 0, 0);
	WaitForMultipleObjects(PROCESSOR_COUNT, handles, TRUE, INFINITE);
	for (int k = 0; k < PROCESSOR_COUNT; ++k) CloseHandle(handles[k]);
#else
	pthread_t handles[PROCESSOR_COUNT];
	for (int k = 0; k < PROCESSOR_COUNT; ++k) pthread_create(&handles[k], 0, threadMain, &jobs[k]);
	for (int k = 0; k < PROCESSOR_COUNT; ++k) pthread_join(handles[k], 0);
#endif
	for (int k = 0; k < PROCESSOR_COUNT; ++k) {
		if (jobs[k].status != OK) fail("threads: a processor stopped with an error");
		delete jobs[k].processor;
	}
	checkResults("threads", false);
	std::printf("multiple processors, threads: %d processors at once\n", PROCESSOR_COUNT);
}

/*
	Processor 0 gets a part too small for its recursion, while processor 1's part is filled with a pattern. The
	overflow must stop processor 0 and leave every word of processor 1's part as it was.
*/
static void fences() {
	clearResults();
	const UInt base1 = gProgram.sizes.constsSize - 1 + gProgram.sizes.globalsSize + PART_SIZE;							// processor 1's part in gMemory
	for (UInt i = 0; i < PART_SIZE; ++i) gMemory[base1 + i].p = 0xA5A5A5A5u;
	Processor* small = makeProcessor(0, 64);
	Status s = small->enterCall(gGlobals.findFunction("thread0"));
	while (s == OK || s == TIME_OUT) {
		small->resetTimeOut(1000);
		s = small->run();
		if (s == OK) break;
	}
	delete small;
	if (s != DATA_STACK_OVERFLOW) fail("fences: the small part did not overflow");
	for (UInt i = 0; i < PART_SIZE; ++i) {
		if (gMemory[base1 + i].p != 0xA5A5A5A5u) {
			fail("fences: the overflow reached the next processor's part");
			break;
		}
	}
	std::printf("multiple processors, fences: status %d\n", static_cast<int>(s));
}

int main() {
	if (!assemble()) return 1;
	cooperative();
	threads();
	fences();
	std::printf("multiple processors: %d failed\n", gFailures);
	return (gFailures == 0 ? 0 : 1);
}
