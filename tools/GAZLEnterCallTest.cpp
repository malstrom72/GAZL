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
	Interpreter tests for calls issued from inside a native callback, in both flavors:

	  - BLOCKING (enterCall):     the native calls enterCall() and then nests run() until the call returns (the
	                              documented interrupt pattern; the enterCall sentinel frame returns control to
	                              the native).
	  - NON-BLOCKING (pushCall):  the native calls pushCall() and simply returns OK. The pushed plain frame makes
	                              the current run() flow into the callee, and its RETU returns transparently into
	                              the GAZL caller, so the `^native` call behaves exactly like a `&function` call.
	                              Several pushCalls form a LIFO chain sharing one argument window.

	Covers: single calls, recursion THROUGH the native (both flavors), a forwarded callee that itself blocks,
	sequential forwards in a loop, fuel suspend/resume in the middle of forwarded calls, and pushCall chains
	(order + shared-window semantics). Interpreter only. Exits non-zero on any failure.

	The kernels are compiled Impala (comment-stripped); each kernel's Impala source is quoted above it.
*/

#include <cstdio>
#include <cstring>
#include <string>
#include "../src/GAZL.h"

using namespace GAZL;

static int failures = 0;

/*
	P1 basics. Impala:
		function helper(int x) returns int r { r = x * x + 1; }
		function mainDirect() locals int a { a = (int) helper(7); global gOut = a; }
		function mainBlk()    locals int a { a = (int) nblk(7); global gOut = a; }
		function mainFwd()    locals int a {
			global gTrace = 1;
			a = (int) nfwd(7);
			global gTrace = (int) global gTrace + 2;			// proves execution continues after the forwarded call
			global gOut = a * 10 + (int) global gTrace;			// 50*10 + 3 = 503
		}
*/
static const char* P1_SOURCE = "\n"
		" 					GLOB *1\n"
		" gOut:				DATi #0\n"
		" 					GLOB *1\n"
		" gTrace:			DATi #0\n"
		" helper:			FUNC\n"
		" 					$r:			OUTi\n"
		" 					$x:			INPi\n"
		" 								MULi %0 $x $x\n"
		" 								ADDi $r %0 #1\n"
		" 								RETU\n"
		" mainDirect:		FUNC\n"
		" 								PARA *1\n"
		" 					$a:			LOCi\n"
		" 								MOVi %1 #7\n"
		" 								CALL &helper %0 *2\n"
		" 								MOVi $a %0\n"
		" 								POKE &gOut $a\n"
		" 								RETU\n"
		" mainBlk:			FUNC\n"
		" 								PARA *1\n"
		" 					$a:			LOCi\n"
		" 								MOVi %1 #7\n"
		" 								CALL ^nblk %0 *2\n"
		" 								MOVi $a %0\n"
		" 								POKE &gOut $a\n"
		" 								RETU\n"
		" mainFwd:			FUNC\n"
		" 								PARA *1\n"
		" 					$a:			LOCi\n"
		" 								POKE &gTrace #1\n"
		" 								MOVi %1 #7\n"
		" 								CALL ^nfwd %0 *2\n"
		" 								MOVi $a %0\n"
		" 								PEEK %0 &gTrace\n"
		" 								ADDi %0 %0 #2\n"
		" 								POKE &gTrace %0\n"
		" 								MULi %0 $a #10\n"
		" 								PEEK %1 &gTrace\n"
		" 								ADDi %0 %0 %1\n"
		" 								POKE &gOut %0\n"
		" 								RETU\n";

/*
	P2 recursion THROUGH the native (targets: nfwd -> helperF, nblk -> helperB). Impala:
		function helperF(int x) returns int r { if (x <= 0) { r = 0; } else { r = x + (int) nfwd(x - 1); } }
		function helperB(int x) returns int r { if (x <= 0) { r = 0; } else { r = x + (int) nblk(x - 1); } }
		function mainF() { global gOut = (int) nfwd(5); }		// 5+4+3+2+1+0 = 15
		function mainB() { global gOut = (int) nblk(5); }		// 15 (nested run() five levels deep)
*/
static const char* P2_SOURCE = "\n"
		" 					GLOB *1\n"
		" gOut:				DATi #0\n"
		" helperF:			FUNC\n"
		" 					$r:			OUTi\n"
		" 					$x:			INPi\n"
		" 								GRTi $x #0 @.f0\n"
		" 								MOVi $r #0\n"
		" 								GOTO @.e1\n"
		" 					.f0:		SUBi %1 $x #1\n"
		" 								CALL ^nfwd %0 *2\n"
		" 								ADDi $r $x %0\n"
		" 					.e1:		RETU\n"
		" helperB:			FUNC\n"
		" 					$r:			OUTi\n"
		" 					$x:			INPi\n"
		" 								GRTi $x #0 @.f0\n"
		" 								MOVi $r #0\n"
		" 								GOTO @.e1\n"
		" 					.f0:		SUBi %1 $x #1\n"
		" 								CALL ^nblk %0 *2\n"
		" 								ADDi $r $x %0\n"
		" 					.e1:		RETU\n"
		" mainF:				FUNC\n"
		" 								PARA *1\n"
		" 								MOVi %1 #5\n"
		" 								CALL ^nfwd %0 *2\n"
		" 								POKE &gOut %0\n"
		" 								RETU\n"
		" mainB:				FUNC\n"
		" 								PARA *1\n"
		" 								MOVi %1 #5\n"
		" 								CALL ^nblk %0 *2\n"
		" 								POKE &gOut %0\n"
		" 								RETU\n";

/*
	P3 a forwarded callee that itself calls a blocking native (nfwd -> helper, nblk -> square). Impala:
		function square(int x) returns int r { r = x * x; }
		function helper(int x) returns int r { r = (int) nblk(x) + 1; }
		function main() { global gOut = (int) nfwd(6); }		// 6*6 + 1 = 37
*/
static const char* P3_SOURCE = "\n"
		" 					GLOB *1\n"
		" gOut:				DATi #0\n"
		" square:			FUNC\n"
		" 					$r:			OUTi\n"
		" 					$x:			INPi\n"
		" 								MULi $r $x $x\n"
		" 								RETU\n"
		" helper:			FUNC\n"
		" 					$r:			OUTi\n"
		" 					$x:			INPi\n"
		" 								MOVi %1 $x\n"
		" 								CALL ^nblk %0 *2\n"
		" 								ADDi $r %0 #1\n"
		" 								RETU\n"
		" main:				FUNC\n"
		" 								PARA *1\n"
		" 								MOVi %1 #6\n"
		" 								CALL ^nfwd %0 *2\n"
		" 								POKE &gOut %0\n"
		" 								RETU\n";

/*
	P4 sequential forwards in a loop (nfwd -> helper). Impala:
		function helper(int x) returns int r { r = x * x + 1; }
		function main() locals int i, int s {
			s = 0;
			for (i = 0 to 10) { s = s + (int) nfwd(i); }		// sum(i*i + 1, i = 0..9) = 295
			global gOut = s;
		}
*/
static const char* P4_SOURCE = "\n"
		" 					GLOB *1\n"
		" gOut:				DATi #0\n"
		" helper:			FUNC\n"
		" 					$r:			OUTi\n"
		" 					$x:			INPi\n"
		" 								MULi %0 $x $x\n"
		" 								ADDi $r %0 #1\n"
		" 								RETU\n"
		" main:				FUNC\n"
		" 								PARA *1\n"
		" 					$i:			LOCi\n"
		" 					$s:			LOCi\n"
		" 								MOVi $s #0\n"
		" 								MOVi $i #0\n"
		" 								GEQi #0 #10 @.e0\n"
		" 					.l1:		MOVi %1 $i\n"
		" 								CALL ^nfwd %0 *2\n"
		" 								ADDi $s $s %0\n"
		" 								FORi $i #10 @.l1\n"
		" 					.e0:		POKE &gOut $s\n"
		" 								RETU\n";

/*
	P5 pushCall chains (nchain -> LIFO [inc, double]; nmark -> LIFO [mark1, mark2, mark3]). Impala:
		function double(int x) returns int r { global gOrder = gOrder * 10 + 4; r = x * 2; }
		function inc(int x)    returns int r { global gOrder = gOrder * 10 + 3; r = x + 1; }
		function mark1() { global gOrder = gOrder * 10 + 1; }			// (mark2 / mark3 alike)
		function mainChain() locals int t {
			global gOrder = 0;
			t = (int) nchain(7);					// double first (order 4), then inc (order 43); both see x = 7,
			global gOut = t * 1000 + gOrder;		// [0] ends as inc(7) = 8  ->  8043
		}
		function mainMarks() locals int t {
			global gOrder = 0;
			t = (int) nmark(0);						// mark3, mark2, mark1  ->  321
			global gOut = gOrder;
		}
*/
static const char* P5_SOURCE = "\n"
		" 					GLOB *1\n"
		" gOut:				DATi #0\n"
		" 					GLOB *1\n"
		" gOrder:			DATi #0\n"
		" double:			FUNC\n"
		" 					$r:			OUTi\n"
		" 					$x:			INPi\n"
		" 								PEEK %0 &gOrder\n"
		" 								MULi %0 %0 #10\n"
		" 								ADDi %0 %0 #4\n"
		" 								POKE &gOrder %0\n"
		" 								MULi $r $x #2\n"
		" 								RETU\n"
		" inc:				FUNC\n"
		" 					$r:			OUTi\n"
		" 					$x:			INPi\n"
		" 								PEEK %0 &gOrder\n"
		" 								MULi %0 %0 #10\n"
		" 								ADDi %0 %0 #3\n"
		" 								POKE &gOrder %0\n"
		" 								ADDi $r $x #1\n"
		" 								RETU\n"
		" mark1:				FUNC\n"
		" 								PARA *1\n"
		" 								PEEK %0 &gOrder\n"
		" 								MULi %0 %0 #10\n"
		" 								ADDi %0 %0 #1\n"
		" 								POKE &gOrder %0\n"
		" 								RETU\n"
		" mark2:				FUNC\n"
		" 								PARA *1\n"
		" 								PEEK %0 &gOrder\n"
		" 								MULi %0 %0 #10\n"
		" 								ADDi %0 %0 #2\n"
		" 								POKE &gOrder %0\n"
		" 								RETU\n"
		" mark3:				FUNC\n"
		" 								PARA *1\n"
		" 								PEEK %0 &gOrder\n"
		" 								MULi %0 %0 #10\n"
		" 								ADDi %0 %0 #3\n"
		" 								POKE &gOrder %0\n"
		" 								RETU\n"
		" mainChain:			FUNC\n"
		" 								PARA *1\n"
		" 					$t:			LOCi\n"
		" 								POKE &gOrder #0\n"
		" 								MOVi %1 #7\n"
		" 								CALL ^nchain %0 *2\n"
		" 								MOVi $t %0\n"
		" 								MULi %0 $t #1000\n"
		" 								PEEK %1 &gOrder\n"
		" 								ADDi %0 %0 %1\n"
		" 								POKE &gOut %0\n"
		" 								RETU\n"
		" mainMarks:			FUNC\n"
		" 								PARA *1\n"
		" 					$t:			LOCi\n"
		" 								POKE &gOrder #0\n"
		" 								MOVi %1 #0\n"
		" 								CALL ^nmark %0 *2\n"
		" 								MOVi $t %0\n"
		" 								PEEK %0 &gOrder\n"
		" 								POKE &gOut %0\n"
		" 								RETU\n";

// --- harness (assemble + run pattern shared with GAZLJitLowerTest) ---

static const int CODE_SIZE = 16384;
static const int FUNCTION_TABLE_SIZE = 256;
static const int DATA_SIZE = 16384;
static const int CALL_STACK_SIZE = 256;

static Instruction gCode[CODE_SIZE];
static UInt gFunctionTable[FUNCTION_TABLE_SIZE];
static Value gMemory[DATA_SIZE];
static CallStackEntry gCallStack[CALL_STACK_SIZE];
static AssembledProgram gProgram;

/*
	The native flavors under test. All satisfy a `^native` call site by running GAZL functions on the SAME
	processor; the difference is who drives them to completion and how many are queued.
*/
static Target gFwdTarget = NULL_TARGET, gBlkTarget = NULL_TARGET;
static Target gChainA = NULL_TARGET, gChainB = NULL_TARGET, gMark1 = NULL_TARGET, gMark2 = NULL_TARGET
		, gMark3 = NULL_TARGET;
static long gFwdCalls = 0, gBlkCalls = 0;

static Status nfwd(Processor* p) {						// NON-BLOCKING: push the call and return; the caller's run() flows in
	++gFwdCalls;
	return p->pushCall(gFwdTarget) != 0 ? OK : BAD_CALL;
}
static Status nblk(Processor* p) {						// BLOCKING: enter the call and nest run() until it returns to us
	++gBlkCalls;
	Status s = p->enterCall(gBlkTarget);
	if (s != OK) return s;
	do { p->resetTimeOut(100000); s = p->run(); } while (s == TIME_OUT);
	return s;
}
static Status nchain(Processor* p) {					// LIFO pair: B runs first, then A; both see the SAME args, [0] = A's result
	if (p->pushCall(gChainA) == 0) return BAD_CALL;
	if (p->pushCall(gChainB) == 0) return BAD_CALL;
	return OK;
}
static Status nmark(Processor* p) {						// LIFO triple of argument-less handlers (the multi-interrupt shape)
	if (p->pushCall(gMark1) == 0) return BAD_CALL;
	if (p->pushCall(gMark2) == 0) return BAD_CALL;
	if (p->pushCall(gMark3) == 0) return BAD_CALL;
	return OK;
}
static NativeFunc const gNatives[] = { nfwd, nblk, nchain, nmark };
static const char* const gNativeNames[] = { "nfwd", "nblk", "nchain", "nmark" };

static bool assemble(const char* source, Symbols& globals) {
	std::memset(gMemory, 0, sizeof (gMemory));
	for (int i = 0; i < static_cast<int>(sizeof (gNatives) / sizeof (*gNatives)); ++i) {
		globals.registerNative(gNativeNames[i], i);
	}
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assem.newUnit("kernel");
		std::string src(source);
		size_t pos = 0;
		while (pos < src.size()) {
			const size_t nl = src.find('\n', pos);
			assem.feed(src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos).c_str());
			if (nl == std::string::npos) { break; }
			pos = nl + 1;
		}
		assem.finalize(gProgram);
	} catch (const Exception& e) {
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str());
		return false;
	}
	return true;
}

/*
	Run `fn` to completion on a fresh interpreter over the assembled image with a given fuel grant per run() call
	(0x7FFFFFFF = effectively no suspends; small grants force TIME_OUT suspend/resume in the middle of everything,
	including inside forwarded calls).
*/
static Status runToCompletion(Symbols& globals, const char* fn, Int fuel, int& suspends) {
	Processor p(gProgram, CALL_STACK_SIZE, gCallStack, gNatives, 0);
	const Target mainPtr = globals.findFunction(fn);
	if (mainPtr == NULL_TARGET) { std::printf("  no function '%s'\n", fn); return BAD_CALL; }
	Status s = p.enterCall(mainPtr);
	if (s != OK) { return s; }
	suspends = 0;
	do {
		p.resetTimeOut(fuel);
		s = p.run();
		if (s == TIME_OUT) { ++suspends; }
	} while (s == TIME_OUT);
	return s;
}

static void check(const char* label, Symbols& globals, const char* fn, Int fuel, Int wantOut, bool wantSuspends = false) {
	int suspends = 0;
	const Status s = runToCompletion(globals, fn, fuel, suspends);
	UInt sz = 0;
	const Pointer outPtr = globals.findGlobal("gOut", sz);
	const Int out = (outPtr != NULL_POINTER) ? gMemory[outPtr - MEMORY_OFFSET + (gProgram.constsSize - 1)].i : -999999;	// constants come first
	const bool good = (s == OK) && (out == wantOut) && (!wantSuspends || suspends > 0);
	std::printf("  %-36s status=%-3d gOut=%-8d want=%-8d suspends=%-5d %s\n",
			label, s, out, wantOut, suspends, good ? "OK" : "FAILED");
	if (!good) { ++failures; }
}

int main(int, const char**) {
	std::printf("GAZL enterCall-from-native tests (interpreter)\n");

	{
		std::printf("P1: basics\n");
		Symbols globals;
		if (assemble(P1_SOURCE, globals)) {
			gFwdTarget = gBlkTarget = globals.findFunction("helper");
			check("direct &call baseline", globals, "mainDirect", 0x7FFFFFFF, 50);
			check("blocking ^nblk", globals, "mainBlk", 0x7FFFFFFF, 50);
			check("forward ^nfwd (+continues after)", globals, "mainFwd", 0x7FFFFFFF, 503);
		} else { ++failures; }
	}
	{
		std::printf("P2: recursion through the native\n");
		Symbols globals;
		if (assemble(P2_SOURCE, globals)) {
			gFwdTarget = globals.findFunction("helperF");
			gBlkTarget = globals.findFunction("helperB");
			gFwdCalls = 0;
			check("forward recursion depth 5", globals, "mainF", 0x7FFFFFFF, 15);
			if (gFwdCalls != 6) { std::printf("  expected 6 nfwd calls, got %ld FAILED\n", gFwdCalls); ++failures; }
			check("blocking recursion depth 5", globals, "mainB", 0x7FFFFFFF, 15);
			check("forward recursion, tiny fuel", globals, "mainF", 25, 15, true);
		} else { ++failures; }
	}
	{
		std::printf("P3: forwarded callee that itself blocks\n");
		Symbols globals;
		if (assemble(P3_SOURCE, globals)) {
			gFwdTarget = globals.findFunction("helper");
			gBlkTarget = globals.findFunction("square");
			check("nfwd -> helper -> nblk -> square", globals, "main", 0x7FFFFFFF, 37);
		} else { ++failures; }
	}
	{
		std::printf("P4: sequential forwards in a loop\n");
		Symbols globals;
		if (assemble(P4_SOURCE, globals)) {
			gFwdTarget = globals.findFunction("helper");
			check("10 forwards in a loop", globals, "main", 0x7FFFFFFF, 295);
			check("10 forwards, tiny fuel", globals, "main", 25, 295, true);
		} else { ++failures; }
	}
	{
		std::printf("P5: pushCall chains\n");
		Symbols globals;
		if (assemble(P5_SOURCE, globals)) {
			gChainA = globals.findFunction("inc");
			gChainB = globals.findFunction("double");
			gMark1 = globals.findFunction("mark1");
			gMark2 = globals.findFunction("mark2");
			gMark3 = globals.findFunction("mark3");
			check("2-chain: order + shared window", globals, "mainChain", 0x7FFFFFFF, 8043);
			check("3-chain of handlers (LIFO)", globals, "mainMarks", 0x7FFFFFFF, 321);
			check("3-chain, tiny fuel", globals, "mainMarks", 25, 321, true);
		} else { ++failures; }
	}

	if (failures == 0) { std::printf("\nALL PASS (0 failures)\n"); return 0; }
	std::printf("\n%d FAILURE(S)\n", failures);
	return 1;
}
