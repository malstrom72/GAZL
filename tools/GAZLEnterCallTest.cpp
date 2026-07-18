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
	                              the GAZL caller - the `^native` call behaves exactly like a `&function` call.
	                              Several pushCalls form a LIFO chain sharing one argument window.

	Covers: single calls, recursion THROUGH the native (both flavors), a forwarded callee that itself blocks,
	sequential forwards in a loop, fuel suspend/resume in the middle of forwarded calls, and pushCall chains
	(order + shared-window semantics). Interpreter only - the JIT side is TODO(jit-native-reentrancy) in
	GAZLJitLowerTest.cpp. Exits non-zero on any failure.

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
static const char* P1_SOURCE = R"GAZL(
 					GLOB *1
 gOut:				DATi #0
 					GLOB *1
 gTrace:			DATi #0
 helper:			FUNC
 					$r:			OUTi
 					$x:			INPi
 								MULi %0 $x $x
 								ADDi $r %0 #1
 								RETU
 mainDirect:		FUNC
 								PARA *1
 					$a:			LOCi
 								MOVi %1 #7
 								CALL &helper %0 *2
 								MOVi $a %0
 								POKE &gOut $a
 								RETU
 mainBlk:			FUNC
 								PARA *1
 					$a:			LOCi
 								MOVi %1 #7
 								CALL ^nblk %0 *2
 								MOVi $a %0
 								POKE &gOut $a
 								RETU
 mainFwd:			FUNC
 								PARA *1
 					$a:			LOCi
 								POKE &gTrace #1
 								MOVi %1 #7
 								CALL ^nfwd %0 *2
 								MOVi $a %0
 								PEEK %0 &gTrace
 								ADDi %0 %0 #2
 								POKE &gTrace %0
 								MULi %0 $a #10
 								PEEK %1 &gTrace
 								ADDi %0 %0 %1
 								POKE &gOut %0
 								RETU
)GAZL";

/*
	P2 recursion THROUGH the native (targets: nfwd -> helperF, nblk -> helperB). Impala:
		function helperF(int x) returns int r { if (x <= 0) { r = 0; } else { r = x + (int) nfwd(x - 1); } }
		function helperB(int x) returns int r { if (x <= 0) { r = 0; } else { r = x + (int) nblk(x - 1); } }
		function mainF() { global gOut = (int) nfwd(5); }		// 5+4+3+2+1+0 = 15
		function mainB() { global gOut = (int) nblk(5); }		// 15 (nested run() five levels deep)
*/
static const char* P2_SOURCE = R"GAZL(
 					GLOB *1
 gOut:				DATi #0
 helperF:			FUNC
 					$r:			OUTi
 					$x:			INPi
 								GRTi $x #0 @.f0
 								MOVi $r #0
 								GOTO @.e1
 					.f0:		SUBi %1 $x #1
 								CALL ^nfwd %0 *2
 								ADDi $r $x %0
 					.e1:		RETU
 helperB:			FUNC
 					$r:			OUTi
 					$x:			INPi
 								GRTi $x #0 @.f0
 								MOVi $r #0
 								GOTO @.e1
 					.f0:		SUBi %1 $x #1
 								CALL ^nblk %0 *2
 								ADDi $r $x %0
 					.e1:		RETU
 mainF:				FUNC
 								PARA *1
 								MOVi %1 #5
 								CALL ^nfwd %0 *2
 								POKE &gOut %0
 								RETU
 mainB:				FUNC
 								PARA *1
 								MOVi %1 #5
 								CALL ^nblk %0 *2
 								POKE &gOut %0
 								RETU
)GAZL";

/*
	P3 a forwarded callee that itself calls a blocking native (nfwd -> helper, nblk -> square). Impala:
		function square(int x) returns int r { r = x * x; }
		function helper(int x) returns int r { r = (int) nblk(x) + 1; }
		function main() { global gOut = (int) nfwd(6); }		// 6*6 + 1 = 37
*/
static const char* P3_SOURCE = R"GAZL(
 					GLOB *1
 gOut:				DATi #0
 square:			FUNC
 					$r:			OUTi
 					$x:			INPi
 								MULi $r $x $x
 								RETU
 helper:			FUNC
 					$r:			OUTi
 					$x:			INPi
 								MOVi %1 $x
 								CALL ^nblk %0 *2
 								ADDi $r %0 #1
 								RETU
 main:				FUNC
 								PARA *1
 								MOVi %1 #6
 								CALL ^nfwd %0 *2
 								POKE &gOut %0
 								RETU
)GAZL";

/*
	P4 sequential forwards in a loop (nfwd -> helper). Impala:
		function helper(int x) returns int r { r = x * x + 1; }
		function main() locals int i, int s {
			s = 0;
			for (i = 0 to 10) { s = s + (int) nfwd(i); }		// sum(i*i + 1, i = 0..9) = 295
			global gOut = s;
		}
*/
static const char* P4_SOURCE = R"GAZL(
 					GLOB *1
 gOut:				DATi #0
 helper:			FUNC
 					$r:			OUTi
 					$x:			INPi
 								MULi %0 $x $x
 								ADDi $r %0 #1
 								RETU
 main:				FUNC
 								PARA *1
 					$i:			LOCi
 					$s:			LOCi
 								MOVi $s #0
 								MOVi $i #0
 								GEQi #0 #10 @.e0
 					.l1:		MOVi %1 $i
 								CALL ^nfwd %0 *2
 								ADDi $s $s %0
 								FORi $i #10 @.l1
 					.e0:		POKE &gOut $s
 								RETU
)GAZL";

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
static const char* P5_SOURCE = R"GAZL(
 					GLOB *1
 gOut:				DATi #0
 					GLOB *1
 gOrder:			DATi #0
 double:			FUNC
 					$r:			OUTi
 					$x:			INPi
 								PEEK %0 &gOrder
 								MULi %0 %0 #10
 								ADDi %0 %0 #4
 								POKE &gOrder %0
 								MULi $r $x #2
 								RETU
 inc:				FUNC
 					$r:			OUTi
 					$x:			INPi
 								PEEK %0 &gOrder
 								MULi %0 %0 #10
 								ADDi %0 %0 #3
 								POKE &gOrder %0
 								ADDi $r $x #1
 								RETU
 mark1:				FUNC
 								PARA *1
 								PEEK %0 &gOrder
 								MULi %0 %0 #10
 								ADDi %0 %0 #1
 								POKE &gOrder %0
 								RETU
 mark2:				FUNC
 								PARA *1
 								PEEK %0 &gOrder
 								MULi %0 %0 #10
 								ADDi %0 %0 #2
 								POKE &gOrder %0
 								RETU
 mark3:				FUNC
 								PARA *1
 								PEEK %0 &gOrder
 								MULi %0 %0 #10
 								ADDi %0 %0 #3
 								POKE &gOrder %0
 								RETU
 mainChain:			FUNC
 								PARA *1
 					$t:			LOCi
 								POKE &gOrder #0
 								MOVi %1 #7
 								CALL ^nchain %0 *2
 								MOVi $t %0
 								MULi %0 $t #1000
 								PEEK %1 &gOrder
 								ADDi %0 %0 %1
 								POKE &gOut %0
 								RETU
 mainMarks:			FUNC
 								PARA *1
 					$t:			LOCi
 								POKE &gOrder #0
 								MOVi %1 #0
 								CALL ^nmark %0 *2
 								MOVi $t %0
 								PEEK %0 &gOrder
 								POKE &gOut %0
 								RETU
)GAZL";

// --- harness (assemble + run pattern shared with GAZLJitLowerTest) ---

static const int CODE_SIZE = 16384;
static const int FUNCTION_TABLE_SIZE = 256;
static const int DATA_SIZE = 16384;
static const int CALL_STACK_SIZE = 256;

static Instruction gCode[CODE_SIZE];
static UInt gFunctionTable[FUNCTION_TABLE_SIZE];
static Value gMemory[DATA_SIZE];
static CallStackEntry gCallStack[CALL_STACK_SIZE];
static UInt gGlobalsSize = 0, gConstsSize = 0, gFunctionCount = 0;

/*
	The native flavors under test. All satisfy a `^native` call site by running GAZL functions on the SAME
	processor; the difference is who drives them to completion and how many are queued.
*/
static Pointer gFwdTarget = 0, gBlkTarget = 0;
static Pointer gChainA = 0, gChainB = 0, gMark1 = 0, gMark2 = 0, gMark3 = 0;
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
	UInt codeSize = 0, gs = 0, cs = 0, fc = 0;
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
		assem.finalize(codeSize, fc, gs, cs);
	} catch (const Exception& e) {
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str());
		return false;
	}
	gGlobalsSize = gs; gConstsSize = cs; gFunctionCount = fc;
	return true;
}

/*
	Run `fn` to completion on a fresh interpreter over the assembled image with a given fuel grant per run() call
	(0x7FFFFFFF = effectively no suspends; small grants force TIME_OUT suspend/resume in the middle of everything,
	including inside forwarded calls).
*/
static Status runToCompletion(Symbols& globals, const char* fn, Int fuel, int& suspends) {
	Processor p(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
			CALL_STACK_SIZE, gCallStack, gNatives, 0);
	const Pointer mainPtr = globals.findFunction(fn);
	if (mainPtr == NULL_POINTER) { std::printf("  no function '%s'\n", fn); return BAD_CALL; }
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
	const Int out = (outPtr != NULL_POINTER) ? gMemory[outPtr - MEMORY_OFFSET].i : -999999;
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
