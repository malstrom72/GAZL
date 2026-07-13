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
	The comprehensive GAZLJit test: compiles a spread of GAZL kernels straight from their `Instruction[]` through the one
	shared lowering pass (src/GAZLJit.h) and checks each JIT run's whole memory image AND final Status against the
	interpreter. Exercises arithmetic, forward + back branches, multi-loop fuel suspend/resume (§5.7.5 RESUME), the §5.4
	dispatcher/segment model for direct/indirect/native calls, and bounds-checked memory with traps. src/GAZL.* compiled
	READ-ONLY; the native dispatcher keeps GAZL calls/returns out of C++ mid-run. AArch64 only. Exits non-zero on any
	mismatch.
*/

#include "GAZLJit.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace GAZL;
using namespace GAZLJitLower;

static int failures = 0;

// A host native (CALL_NVC), invoked exactly as the interpreter invokes it: r = x*x, OUT at params[0], arg at params[1].
static long gNativeCallCount = 0;
static Status nativeSquare(Processor* p) {
	Value* q = p->accessParams(2);
	if (q == 0) { return DATA_STACK_OVERFLOW; }
	q[0].i = q[1].i * q[1].i;
	++gNativeCallCount;
	return OK;
}
// A BLOCKING native: returns BLOCK_RETRY twice (suspend-and-retry), then computes r = x*x. Exercises §5.4 blocking
// retry — each invocation is re-issued from the call site until it succeeds.
static Status nativeBlock(Processor* p) {
	static int retries = 0;
	if (retries < 2) { ++retries; return static_cast<Status>(BLOCK_RETRY); }
	retries = 0;
	Value* q = p->accessParams(2);
	if (q == 0) { return DATA_STACK_OVERFLOW; }
	q[0].i = q[1].i * q[1].i;
	++gNativeCallCount;
	return OK;
}
static NativeFunc const gNativeTable[] = { nativeSquare, nativeBlock };
static const char* const gNativeNames[] = { "nsq", "blk" };

namespace {
	const int CODE_SIZE = 64 * 1024, DATA_SIZE = 64 * 1024, FUNCTION_TABLE_SIZE = 1024, CALL_STACK_SIZE = 256;
	Instruction gCode[CODE_SIZE];
	Value gMemory[DATA_SIZE];
	UInt gFunctionTable[FUNCTION_TABLE_SIZE];
	CallStackEntry gCallStack[CALL_STACK_SIZE];
	UInt gGlobalsSize = 0, gConstsSize = 0, gFunctionCount = 0;
	std::vector<Value> gCleanImage;
}

static bool assemble(const char* source, Symbols& globals) {
	for (int i = 0; i < static_cast<int>(sizeof(gNativeTable) / sizeof(*gNativeTable)); ++i) {
		globals.registerNative(gNativeNames[i], i);
	}
	UInt codeSize = 0, gs = 0, cs = 0, fc = 0;
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assem.newUnit("kernel");
		std::string src(source); size_t pos = 0;
		while (pos < src.size()) {
			const size_t nl = src.find('\n', pos);
			assem.feed(src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos).c_str());
			if (nl == std::string::npos) { break; }
			pos = nl + 1;
		}
		assem.finalize(codeSize, gs, cs, fc);
	} catch (const Exception& e) {
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str()); return false;
	}
	gGlobalsSize = gs; gConstsSize = cs; gFunctionCount = fc;
	gCleanImage.assign(gMemory, gMemory + DATA_SIZE);
	return true;
}

static void restoreClean() { std::memcpy(gMemory, gCleanImage.data(), DATA_SIZE * sizeof(Value)); }

static std::vector<Value> runInterpreter(Pointer mainPtr, Pointer gInPtr, int n, Status& outStatus) {
	restoreClean();
	Processor p(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
			CALL_STACK_SIZE, gCallStack, gNativeTable, nullptr);
	p.accessMemory(gInPtr, 1)->i = n;
	Status s = p.enterCall(mainPtr);
	do { p.resetTimeOut(0x7FFFFFFF); s = p.run(); } while (s == TIME_OUT || s == BLOCK_RETRY);	// trap valid; blocking native retries
	outStatus = s;
	return std::vector<Value>(gMemory, gMemory + DATA_SIZE);
}

static bool imagesEqual(const std::vector<Value>& a, const std::vector<Value>& b, int& firstDiff) {
	for (size_t i = 0; i < a.size(); ++i) { if (a[i].i != b[i].i) { firstDiff = static_cast<int>(i); return false; } }
	firstDiff = -1; return true;
}

// Assemble + lower every function through the shared pass, then check the JIT against the interpreter (status AND whole
// memory image) at full fuel and at tiny fuel (forcing repeated suspend/resume).
static void runKernel(const char* name, const char* source, const int* inputs, size_t nInputs) {
	std::printf("Kernel \"%s\":\n", name);
	Symbols globals;
	if (!assemble(source, globals)) { ++failures; return; }
	const Pointer mainPtr = globals.findFunction("main");
	UInt sz = 0;
	const Pointer gInPtr = globals.findGlobal("gIn", sz), gOutPtr = globals.findGlobal("gOut", sz);
	if (mainPtr == NULL_POINTER || gInPtr == NULL_POINTER || gOutPtr == NULL_POINTER) {
		std::printf("  symbol lookup failed\n"); ++failures; return;
	}
	const UInt mainOrd = mainPtr - IP_OFFSET;

	JitEngine probe(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
			CALL_STACK_SIZE, gCallStack, gNativeTable);
	const Offsets o = probe.offsets();

	Emitter e;
	std::vector<Label> entryLabels(gFunctionCount);
	std::vector<size_t> entryOffset(gFunctionCount, 0);
	for (UInt k = 0; k < gFunctionCount; ++k) { entryLabels[k] = e.newLabel(); }
	bool ok = true;
	for (UInt ord = 0; ord < gFunctionCount; ++ord) {
		ok = ok && lowerFunction(e, gCode, gFunctionTable[ord], o, entryLabels, entryOffset, ord, gFunctionCount);
	}
	if (!ok) { std::printf("  lowering failed (unsupported opcode)\n"); ++failures; return; }
	const size_t dispatchOffset = emitDispatcher(e, o);
	e.finalize();
	void* code = makeExecutable(e.code(), e.wordCount());
	if (code == nullptr) { std::printf("  W^X alloc failed\n"); ++failures; return; }
	void* const dispatchAddr = reinterpret_cast<char*>(code) + dispatchOffset * 4;
	std::vector<void*> funcEntries(gFunctionCount);
	for (UInt ord = 0; ord < gFunctionCount; ++ord) {
		funcEntries[ord] = reinterpret_cast<char*>(code) + entryOffset[ord] * 4;
	}
	std::printf("  lowered %zu native words for %u function(s); dispatcher @ word %zu\n",
			e.wordCount(), gFunctionCount, dispatchOffset);

	for (size_t k = 0; k < nInputs; ++k) {
		const int n = inputs[k];
		Status wantStatus = OK;
		const std::vector<Value> want = runInterpreter(mainPtr, gInPtr, n, wantStatus);

		for (int pass = 0; pass < 2; ++pass) {
			const int fuel = (pass == 0) ? 0x7FFFFFFF : 6;
			restoreClean();
			JitEngine eng(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize,
					gConstsSize, CALL_STACK_SIZE, gCallStack, gNativeTable);
			eng.setCompiled(dispatchAddr, funcEntries.data());
			eng.accessMemory(gInPtr, 1)->i = n;
			gNativeCallCount = 0;
			// Drive the JIT through the polymorphic base interface — the exact host loop the interpreter uses (§5.1).
			Processor* proc = &eng;
			Status s = proc->enterCall(mainPtr);
			int suspends = 0;
			do {
				proc->resetTimeOut(fuel);
				s = proc->run();
				if (s == TIME_OUT || s == BLOCK_RETRY) { ++suspends; }
			} while (s == TIME_OUT || s == BLOCK_RETRY);
			std::vector<Value> got(gMemory, gMemory + DATA_SIZE);
			int diff = -1;
			const bool good = (s == wantStatus) && imagesEqual(want, got, diff);
			std::printf("  n=%-8d %-11s status=%-3d hostcalls=%-5ld suspends=%-5d gOut=%-12d %s\n", n,
					pass == 0 ? "[fullfuel]" : "[tinyfuel]", s, gNativeCallCount, suspends,
					gMemory[gOutPtr - MEMORY_OFFSET].i, good ? "OK" : "MISMATCH");
			if (!good) { std::printf("    want status=%d, diff word=%d\n", wantStatus, diff); ++failures; }
		}
	}
	::munmap(code, e.wordCount() * sizeof(uint32_t));
	std::printf("\n");
}

// --- kernels: input from global gIn, result to global gOut ---

static const char* const K_SUMTO =			// one loop (ADDi, FORi)
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$n: LOCi\n$s: LOCi\n$i: LOCi\n"
	" PEEK $n &gIn\n MOVi $s #0\n MOVi $i #0\n"
	".l: ADDi $s $s $i\n FORi $i $n @.l\n POKE &gOut $s\n RETU\n";

static const char* const K_SUMSQ =			// MULi + ADDi + FORi
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$n: LOCi\n$s: LOCi\n$i: LOCi\n$t: LOCi\n"
	" PEEK $n &gIn\n MOVi $s #0\n MOVi $i #0\n"
	".l: MULi $t $i $i\n ADDi $s $s $t\n FORi $i $n @.l\n POKE &gOut $s\n RETU\n";

static const char* const K_INTOPS =			// DIVi/MODi (+ div-by-zero trap), shifts, ABSi
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$n: LOCi\n$k: LOCi\n$t: LOCi\n$u: LOCi\n$sh: LOCi\n"
	" PEEK $n &gIn\n MOVi $k #1000000\n MOVi $sh #2\n"
	" DIVi $t $k $n\n MODi $u $k $n\n ADDi $t $t $u\n"		// k/n + k%n  (traps if n==0)
	" SHLi $u $n #3\n ADDi $t $t $u\n"						// + n<<3
	" SHRi $u $k #4\n ADDi $t $t $u\n"						// + k>>4 (arithmetic)
	" SHRu $u $k $sh\n ADDi $t $t $u\n"						// + k>>u 2 (register count)
	" ABSi $u $n\n ADDi $t $t $u\n"							// + |n|
	" POKE &gOut $t\n RETU\n";

static const char* const K_FLOAT =			// iTOf/fTOi (scaled), ADDf/MULf/DIVf, float compare-branch (LSSf), MOVf
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$n: LOCi\n$f: LOCf\n$g: LOCf\n$r: LOCi\n"
	" PEEK $n &gIn\n iTOf $f $n #1.0\n"						// f = (float)n
	" MULf $g $f $f\n DIVf $g $g #2.0\n"					// g = f*f/2
	" LSSf $g #1000000.0 @.ok\n MOVf $g #1000000.0\n"		// clamp g to 1e6 if g >= 1e6
	".ok: ADDf $g $g $f\n"									// g += f
	" fTOi $r $g #1.0\n POKE &gOut $r\n RETU\n";

static const char* const K_ABS =			// forward conditional (GEQi -> NLSI) + SUBi_VCV negate
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$x: LOCi\n"
	" PEEK $x &gIn\n GEQi $x #0 @.done\n SUBi $x #0 $x\n"
	".done: POKE &gOut $x\n RETU\n";

static const char* const K_TWOLOOP =		// two sequential loops -> two safepoints
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$n: LOCi\n$a: LOCi\n$b: LOCi\n$i: LOCi\n$t: LOCi\n"
	" PEEK $n &gIn\n MOVi $a #0\n MOVi $i #0\n"
	".l1: ADDi $a $a $i\n FORi $i $n @.l1\n"
	" MOVi $b #0\n MOVi $i #0\n"
	".l2: MULi $t $i $i\n ADDi $b $b $t\n FORi $i $n @.l2\n"
	" ADDi $a $a $b\n POKE &gOut $a\n RETU\n";

static const char* const K_GAZLCALL =		// direct GAZL call in a loop (CALL_CVC / RETU)
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"sq: FUNC\n$r: OUTi\n$x: INPi\n MULi $r $x $x\n RETU\n"
	"main: FUNC\n PARA *3\n$n: LOCi\n$acc: LOCi\n$i: LOCi\n$t: LOCi\n"
	" PEEK $n &gIn\n MOVi $acc #0\n MOVi $i #0\n"
	".l: MOVi %1 $i\n CALL &sq %0 *2\n MOVi $t %0\n ADDi $acc $acc $t\n FORi $i $n @.l\n"
	" POKE &gOut $acc\n RETU\n";

static const char* const K_CALLLOOP =		// callee that ITSELF loops -> tiny fuel suspends mid-call (nested resume)
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"tri: FUNC\n$r: OUTi\n$m: INPi\n$s: LOCi\n$i: LOCi\n"
	" MOVi $s #0\n MOVi $i #0\n.sl: ADDi $s $s $i\n FORi $i $m @.sl\n MOVi $r $s\n RETU\n"
	"main: FUNC\n PARA *3\n$n: LOCi\n$acc: LOCi\n$i: LOCi\n$t: LOCi\n"
	" PEEK $n &gIn\n MOVi $acc #0\n MOVi $i #0\n"
	".l: MOVi %1 $i\n CALL &tri %0 *2\n MOVi $t %0\n ADDi $acc $acc $t\n FORi $i $n @.l\n"
	" POKE &gOut $acc\n RETU\n";

static const char* const K_INDIRECT =		// indirect GAZL call through a fn pointer (CALL_VVC)
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"sq: FUNC\n$r: OUTi\n$x: INPi\n MULi $r $x $x\n RETU\n"
	"main: FUNC\n PARA *3\n$fp: LOCp\n$n: LOCi\n$acc: LOCi\n$i: LOCi\n$t: LOCi\n"
	" MOVp $fp &sq\n PEEK $n &gIn\n MOVi $acc #0\n MOVi $i #0\n"
	".l: MOVi %1 $i\n CALL $fp %0 *2\n MOVi $t %0\n ADDi $acc $acc $t\n FORi $i $n @.l\n"
	" POKE &gOut $acc\n RETU\n";

static const char* const K_BLOCKING =		// host native that suspends-and-retries each call (blocking retry, §5.4)
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *2\n$n: LOCi\n$acc: LOCi\n$i: LOCi\n$t: LOCi\n"
	" PEEK $n &gIn\n MOVi $acc #0\n MOVi $i #0\n"
	".l: MOVi %1 $i\n CALL ^blk %0 *2\n MOVi $t %0\n ADDi $acc $acc $t\n FORi $i $n @.l\n"
	" POKE &gOut $acc\n RETU\n";

static const char* const K_NATIVE =			// host native call in a loop (CALL_NVC)
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *2\n$n: LOCi\n$acc: LOCi\n$i: LOCi\n$t: LOCi\n"
	" PEEK $n &gIn\n MOVi $acc #0\n MOVi $i #0\n"
	".l: MOVi %1 $i\n CALL ^nsq %0 *2\n MOVi $t %0\n ADDi $acc $acc $t\n FORi $i $n @.l\n"
	" POKE &gOut $acc\n RETU\n";

static const char* const K_POINTER =		// ADRL local array + pointer deref (POKE_VVV / PEEK_VVV) + bounds trap
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$arr: LOCA *8\n$p: LOCp\n$n: LOCi\n$x: LOCi\n"
	" ADRL $p $arr *0\n PEEK $n &gIn\n MOVi $x #777\n"
	" POKE $p $n $x\n PEEK $x $p $n\n POKE &gOut $x\n RETU\n";

static const char* const K_STACK =			// checked stack-local access by index (SETL / GETL) + bounds trap
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$arr: LOCA *8\n$n: LOCi\n$x: LOCi\n"
	" PEEK $n &gIn\n MOVi $x #777\n SETL $arr $n $x\n GETL $x $arr $n\n POKE &gOut $x\n RETU\n";

static const char* const K_FARSLOT =		// scalars declared before a big LOCA -> slots far from dsp (register-offset path)
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"main: FUNC\n PARA *1\n$fint: LOCi\n$ff: LOCf\n$huge: LOCA *200\n$near: LOCi\n"
	" PEEK $fint &gIn\n iTOf $ff $fint #1.0\n MULf $ff $ff #3.0\n fTOi $near $ff #1.0\n"
	" ADDi $fint $fint $near\n POKE &gOut $fint\n RETU\n";	// gOut = fint + 3*fint = 4*n (far int + far float slots)

static const char* const K_RECURSE =		// deep recursion -> IP_STACK_OVERFLOW when depth exceeds the call stack
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"rec: FUNC\n$r: OUTi\n$m: INPi\n$t: LOCi\n"
	" GEQi #0 $m @.base\n SUBi $t $m #1\n MOVi %1 $t\n CALL &rec %0 *2\n MOVi $t %0\n ADDi $r $t $m\n GOTO @.done\n"
	".base: MOVi $r #0\n.done: RETU\n"
	"main: FUNC\n PARA *3\n$n: LOCi\n$x: LOCi\n"
	" PEEK $n &gIn\n MOVi %1 $n\n CALL &rec %0 *2\n MOVi $x %0\n POKE &gOut $x\n RETU\n";

static const char* const K_BIGFRAME =		// a function whose frame exceeds the data stack -> DATA_STACK_OVERFLOW
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"big: FUNC\n$huge: LOCA *70000\n$r: OUTi\n MOVi $r #123\n RETU\n"
	"main: FUNC\n PARA *2\n$x: LOCi\n"
	" CALL &big %0 *1\n MOVi $x %0\n POKE &gOut $x\n RETU\n";

static const char* const K_COPY =			// block copy of a global array (COPY) + read back
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"gsrc: GLOB *4\n DATi #10\n DATi #20\n DATi #30\n DATi #40\n"
	"gdst: GLOB *4\n DATi #0\n DATi #0\n DATi #0\n DATi #0\n"
	"main: FUNC\n PARA *1\n$x: LOCi\n$y: LOCi\n"
	" PEEK $x &gIn\n POKE &gsrc:0 $x\n COPY &gdst &gsrc *4\n"			// gsrc[0]=gIn ; copy 4 words gsrc->gdst
	" PEEK $x &gdst:0\n PEEK $y &gdst:2\n ADDi $x $x $y\n POKE &gOut $x\n RETU\n";	// gOut = gdst[0]+gdst[2] = gIn+30

static const char* const K_MEMORY =			// bounds-checked indexed memory (POKE_CVV / PEEK_VCV) + trap
	"gIn: GLOB *1\n DATi #0\n" "gOut: GLOB *1\n DATi #0\n"
	"buf: GLOB *8\n DATi #0\n DATi #0\n DATi #0\n DATi #0\n DATi #0\n DATi #0\n DATi #0\n DATi #0\n"
	"main: FUNC\n PARA *1\n$n: LOCi\n$x: LOCi\n"
	" PEEK $n &gIn\n MOVi $x #12345\n POKE &buf $n $x\n PEEK $x &buf $n\n POKE &gOut $x\n RETU\n";

int main() {
	std::printf("GAZLJit consolidated lowering test: JIT (compiled from Instruction[]) vs interpreter (arm64)\n\n");
	const int counts[] = { 0, 1, 2, 5, 10, 100, 1000 };
	const int signed_[] = { 0, 1, -1, 7, -7, 123456, -123456 };
	const int indices[] = { 0, 3, 7, 100, 2000000 };			// last is out of range -> BAD_POKE trap
	const int depths[] = { 1, 5, 50, 200, 300 };				// last overflows the call stack -> IP_STACK_OVERFLOW
	const int one[] = { 0 };
	const int divs[] = { 1, 2, 3, 7, -7, 13, -13, 1000000, 0 };	// last traps DIVISION_BY_ZERO
	const int floats[] = { 0, 1, 2, 10, -10, 100, 1414, 2000, -2000 };	// large n exercises the LSSf clamp

	runKernel("sumTo        [loop]", K_SUMTO, counts, sizeof(counts) / sizeof(*counts));
	runKernel("intops       [DIVi/MODi/shift/ABSi]", K_INTOPS, divs, sizeof(divs) / sizeof(*divs));
	runKernel("floats       [iTOf/fTOi/arith/LSSf]", K_FLOAT, floats, sizeof(floats) / sizeof(*floats));
	runKernel("sumSq        [MULi]", K_SUMSQ, counts, sizeof(counts) / sizeof(*counts));
	runKernel("abs          [fwd branch + SUBi_VCV]", K_ABS, signed_, sizeof(signed_) / sizeof(*signed_));
	runKernel("twoLoop      [2 safepoints]", K_TWOLOOP, counts, sizeof(counts) / sizeof(*counts));
	runKernel("gazl-call    [CALL_CVC]", K_GAZLCALL, counts, sizeof(counts) / sizeof(*counts));
	runKernel("callee-loop  [suspend inside callee]", K_CALLLOOP, counts, sizeof(counts) / sizeof(*counts));
	runKernel("indirect     [CALL_VVC]", K_INDIRECT, counts, sizeof(counts) / sizeof(*counts));
	runKernel("native       [CALL_NVC]", K_NATIVE, counts, sizeof(counts) / sizeof(*counts));
	runKernel("blocking nat [CALL_NVC retry]", K_BLOCKING, counts, sizeof(counts) / sizeof(*counts));
	runKernel("pointer      [ADRL + PEEK/POKE_VVV]", K_POINTER, indices, sizeof(indices) / sizeof(*indices));
	runKernel("stack access [GETL/SETL + trap]", K_STACK, indices, sizeof(indices) / sizeof(*indices));
	runKernel("block copy   [COPY]", K_COPY, signed_, sizeof(signed_) / sizeof(*signed_));
	runKernel("far slots    [big frame, register-offset]", K_FARSLOT, floats, sizeof(floats) / sizeof(*floats));
	runKernel("recursion    [IP_STACK_OVERFLOW]", K_RECURSE, depths, sizeof(depths) / sizeof(*depths));
	runKernel("big frame    [DATA_STACK_OVERFLOW]", K_BIGFRAME, one, sizeof(one) / sizeof(*one));
	runKernel("checked mem  [PEEK/POKE + trap]", K_MEMORY, indices, sizeof(indices) / sizeof(*indices));

	std::printf("%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures, failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
