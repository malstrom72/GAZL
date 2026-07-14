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
	End-to-end engine test for the x86-64 JitCompiler / JitProcessor. Assembles a set of GAZL kernels, compiles each
	through JitCompiler::compile into a JitModule, runs it via JitProcessor over the same host loop the interpreter uses,
	and diffs BOTH the final Status AND the whole memory image against a plain Processor running the same kernel over an
	identical fresh image. Sibling of the arm64 GAZLJitLowerTest; built -arch x86_64 and run under Rosetta on Apple
	Silicon. src/GAZL.* are used READ-ONLY through the public API.

	Kernels: a compute loop (sumTo), a full integer set (arith), the float set (float), FTOI saturation (ftoi-sat), a
	GAZL->GAZL direct call (call), an indirect call through a function pointer (indirect), a native call (native), a
	local array via SETL/GETL (array), var-indexed global memory via PEEK_VCV/POKE_CVV (gmem), a bulk COPY (copy), a
	SWCH jump table (switch), a runtime div-by-zero trap (div-zero), an out-of-bounds GETL trap (oob-getl), and an
	out-of-bounds var-indexed PEEK trap (oob-peek).
*/

#include "GAZLJit.h"			// JitCompiler / JitProcessor / JitModule (+ GAZL.h: Assembler / Processor / Instruction / Value)

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

using namespace GAZL;

static int failures = 0;

// --- the host native, ordinal 0 = "nsq" (x*x). Reads its params via accessParams, so the SAME function backs both the
// interpreter and the JIT (the JIT calls it with the ctx = Processor* in rdi, after publishing the param window). ---
static Status nsq(Processor* processor) {
	Value* params = processor->accessParams(2);
	if (params == 0) { return DATA_STACK_OVERFLOW; }
	params[0].i = params[1].i * params[1].i;
	return OK;
}
static NativeFunc const gNatives[] = { nsq };
static const char* const gNativeNames[] = { "nsq" };

// --- kernels ---

// sumTo(n) = 0 + 1 + ... + (n-1): PEEK/POKE + a do-while loop.
static const char* const SUMTO_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$sum:  LOCi\n" "$i:    LOCi\n"
	"       PEEK $n &gIn\n"
	"       MOVi $sum #0\n"
	"       MOVi $i #0\n"
	".loop: ADDi $sum $sum $i\n"
	"       FORi $i $n @.loop\n"
	"       POKE &gOut $sum\n"
	"       RETU\n";

// Integer set: div / mod / mul / shifts (arith + logical) / abs / and-or-xor, with signed inputs.
static const char* const ARITH_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$x:    LOCi\n" "$a:    LOCi\n" "$b:    LOCi\n"
	"       PEEK $x &gIn\n"
	"       DIVi $a $x #7\n"
	"       MODi $b $x #100\n"
	"       MULi $a $a $b\n"
	"       SHLi $a $a #2\n"
	"       SUBi $a $a $x\n"
	"       ABSi $a $a\n"
	"       SHRi $b $x #1\n"
	"       ADDi $a $a $b\n"
	"       SHRu $b $x #1\n"
	"       XORi $a $a $b\n"
	"       ANDi $a $a #16777215\n"
	"       POKE &gOut $a\n"
	"       RETU\n";

// Float set: ITOF/FTOI, add/sub/mul/div, abs, floor, and a float compare-branch.
static const char* const FLOAT_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$f:    LOCf\n" "$g:    LOCf\n"
	"       PEEK $n &gIn\n"
	"       iTOf $f $n #1.0\n"
	"       MULf $g $f #0.25\n"
	"       ADDf $g $g #10.5\n"
	"       SUBf $g $f $g\n"
	"       DIVf $g $g #3.0\n"
	"       ABSf $g $g\n"
	"       MOVf $f #0.0\n"
	"       GEQf $g #1000.0 @.big\n"
	"       MOVf $f $g\n"
	"       GOTO @.done\n"
	".big:  MOVf $f #1000.0\n"
	".done: FLOf $f $f\n"
	"       fTOi $n $f #1.0\n"
	"       POKE &gOut $n\n"
	"       RETU\n";

// FTOI saturation: n * 1e9 overflows INT range for |n| >= 3 -> INT_MAX / INT_MIN like the interpreter's ftoi().
static const char* const FTOISAT_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$f:    LOCf\n"
	"       PEEK $n &gIn\n"
	"       iTOf $f $n #1000000000.0\n"
	"       fTOi $n $f #1.0\n"
	"       POKE &gOut $n\n"
	"       RETU\n";

// Two functions: main calls square(i) in a loop -> sum of i*i. Exercises CALL_CVC + the window/return convention.
static const char* const CALL_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"square: FUNC\n"
	"$r:    OUTi\n" "$x:    INPi\n"
	"       MULi $r $x $x\n"
	"       RETU\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$s:    LOCi\n" "$i:    LOCi\n"
	"       PEEK $n &gIn\n"
	"       MOVi $s #0\n"
	"       MOVi $i #0\n"
	".loop: MOVi %1 $i\n"
	"       CALL &square %0 *2\n"
	"       ADDi $s $s %0\n"
	"       FORi $i $n @.loop\n"
	"       POKE &gOut $s\n"
	"       RETU\n";

// Indirect call: pick square or cube via a function pointer chosen at runtime, then CALL through the slot (CALL_VVC).
static const char* const INDIRECT_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"square: FUNC\n" "$r:    OUTi\n" "$x:    INPi\n"
	"       MULi $r $x $x\n"
	"       RETU\n"
	"cube:  FUNC\n" "$cr:   OUTi\n" "$cx:   INPi\n"
	"       MULi $cr $cx $cx\n"
	"       MULi $cr $cr $cx\n"
	"       RETU\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$fp:   LOCp\n" "$rr:   LOCi\n"
	"       PEEK $n &gIn\n"
	"       MOVp $fp &square\n"
	"       GEQi $n #0 @.go\n"
	"       MOVp $fp &cube\n"
	".go:   MOVi %1 $n\n"
	"       CALL $fp %0 *2\n"
	"       MOVi $rr %0\n"
	"       POKE &gOut $rr\n"
	"       RETU\n";

// Native call: main calls the host native nsq(i) = i*i in a loop -> sum of i*i. Exercises CALL_NVC.
static const char* const NATIVE_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$s:    LOCi\n" "$i:    LOCi\n"
	"       PEEK $n &gIn\n"
	"       MOVi $s #0\n"
	"       MOVi $i #0\n"
	".loop: MOVi %1 $i\n"
	"       CALL ^nsq %0 *2\n"
	"       ADDi $s $s %0\n"
	"       FORi $i $n @.loop\n"
	"       POKE &gOut $s\n"
	"       RETU\n";

// Local array via SETL/GETL (indexed frame addressing): arr[i] = i*n, then sum -> n*120.
static const char* const ARRAY_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$i:    LOCi\n" "$s:    LOCi\n" "$t:    LOCi\n" "$arr:  LOCA *16\n"
	"       PEEK $n &gIn\n"
	"       MOVi $i #0\n"
	".fill: MULi $t $i $n\n"
	"       SETL $arr $i $t\n"
	"       FORi $i #16 @.fill\n"
	"       MOVi $s #0\n"
	"       MOVi $i #0\n"
	".sum:  GETL $t $arr $i\n"
	"       ADDi $s $s $t\n"
	"       FORi $i #16 @.sum\n"
	"       POKE &gOut $s\n"
	"       RETU\n";

// Var-indexed global memory: gArr[i] = i*n via POKE_CVV, then read back via PEEK_VCV and sum. Diffs the whole image so
// the global-array writes are checked too.
static const char* const GMEM_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gArr:  GLOB *8\n" "       DATi #0\n" "       DATi #0\n" "       DATi #0\n" "       DATi #0\n"
	"       DATi #0\n" "       DATi #0\n" "       DATi #0\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$i:    LOCi\n" "$s:    LOCi\n" "$t:    LOCi\n"
	"       PEEK $n &gIn\n"
	"       MOVi $i #0\n"
	".fill: MULi $t $i $n\n"
	"       POKE &gArr $i $t\n"			// POKE_CVV: gArr[i] = t
	"       FORi $i #8 @.fill\n"
	"       MOVi $s #0\n"
	"       MOVi $i #0\n"
	".sum:  PEEK $t &gArr $i\n"			// PEEK_VCV: t = gArr[i]
	"       ADDi $s $s $t\n"
	"       FORi $i #8 @.sum\n"
	"       POKE &gOut $s\n"
	"       RETU\n";

// COPY (the bank idiom): ADRL a local array, COPY a global block into it, sum it. Exercises COPY_VCC + ADRL + GETL.
static const char* const COPY_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gSrc:  GLOB *8\n" "       DATi #10\n" "       DATi #20\n" "       DATi #30\n" "       DATi #40\n"
	"       DATi #50\n" "       DATi #60\n" "       DATi #70\n" "       DATi #80\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$i:    LOCi\n" "$s:    LOCi\n" "$t:    LOCi\n" "$buf:  LOCA *8\n"
	"       PEEK $n &gIn\n"
	"       ADRL %0 $buf *8\n"
	"       COPY %0 &gSrc *8\n"
	"       MOVi $s #0\n"
	"       MOVi $i #0\n"
	".sum:  GETL $t $buf $i\n"
	"       ADDi $s $s $t\n"
	"       FORi $i #8 @.sum\n"
	"       ADDi $s $s $n\n"
	"       POKE &gOut $s\n"
	"       RETU\n";

// SWCH: switch(n clamped 0..4) -> distinct results; missing cases + out-of-range fall to the default. Jump table.
static const char* const SWITCH_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$r:    LOCi\n"
	"       PEEK $n &gIn\n"
	"       SWCH $n *4 @.sw\n"
	".sw#0: MOVi $r #100\n" "       GOTO @.done\n"
	".sw#1: MOVi $r #200\n" "       GOTO @.done\n"
	".sw#2: MOVi $r #300\n" "       GOTO @.done\n"
	".sw:   MOVi $r #999\n"
	".done: POKE &gOut $r\n"
	"       RETU\n";

// Runtime div-by-zero: d = n - n = 0, then 100 / d -> DIVISION_BY_ZERO status (matched, not a failure).
static const char* const DIVZERO_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$d:    LOCi\n" "$r:    LOCi\n"
	"       PEEK $n &gIn\n"
	"       MOVi $d $n\n"
	"       SUBi $d $d $n\n"
	"       DIVi $r #100 $d\n"
	"       POKE &gOut $r\n"
	"       RETU\n";

// Out-of-bounds GETL: index = n into a 4-word local array. Small n reads the (zeroed) frame; a huge n exceeds the frame
// span and traps BAD_PEEK in both engines.
static const char* const OOB_GETL_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$t:    LOCi\n" "$arr:  LOCA *4\n"
	"       PEEK $n &gIn\n"
	"       GETL $t $arr $n\n"
	"       POKE &gOut $t\n"
	"       RETU\n";

// Out-of-bounds var-indexed PEEK: index = n off &gArr. Small n reads within memory; a huge n exceeds memorySize and
// traps BAD_PEEK in both engines.
static const char* const OOB_PEEK_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gArr:  GLOB *4\n" "       DATi #0\n" "       DATi #0\n" "       DATi #0\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$t:    LOCi\n"
	"       PEEK $n &gIn\n"
	"       PEEK $t &gArr $n\n"
	"       POKE &gOut $t\n"
	"       RETU\n";

namespace {
	const int CODE_SIZE = 64 * 1024, DATA_SIZE = 64 * 1024, FUNCTION_TABLE_SIZE = 1024, CALL_STACK_SIZE = 256;
	Instruction gCode[CODE_SIZE];
	Value gMemory[DATA_SIZE];			// working image (mutated by each run)
	Value gClean[DATA_SIZE];			// snapshot of the assembled image (globals + const inits), restored before each run
	Value gInterpImage[DATA_SIZE];		// the interpreter's post-run image, to diff the JIT against
	UInt gFunctionTable[FUNCTION_TABLE_SIZE];
	CallStackEntry gInterpStack[CALL_STACK_SIZE];
	CallStackEntry gJitStack[CALL_STACK_SIZE];
	UInt gGlobalsSize = 0, gConstsSize = 0, gFunctionCount = 0, gCodeSize = 0;
}

static bool assembleKernel(Symbols& globals, const char* source, Pointer& gInPtr, Pointer& gOutPtr) {
	for (int i = 0; i < static_cast<int>(sizeof(gNativeNames) / sizeof(*gNativeNames)); ++i) { globals.registerNative(gNativeNames[i], i); }
	UInt codeSize = 0, globalsSize = 0, constsSize = 0, functionCount = 0;
	try {
		Assembler assembler(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assembler.newUnit("x64engine");
		std::string src(source);
		size_t pos = 0;
		while (pos < src.size()) {
			const size_t newline = src.find('\n', pos);
			assembler.feed(src.substr(pos, newline == std::string::npos ? std::string::npos : newline - pos).c_str());
			if (newline == std::string::npos) { break; }
			pos = newline + 1;
		}
		assembler.finalize(codeSize, functionCount, globalsSize, constsSize);
	} catch (const Exception& e) {
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str());
		return false;
	}
	gCodeSize = codeSize; gGlobalsSize = globalsSize; gConstsSize = constsSize; gFunctionCount = functionCount;
	UInt size = 0;
	gInPtr = globals.findGlobal("gIn", size);
	gOutPtr = globals.findGlobal("gOut", size);
	return (gInPtr != NULL_POINTER && gOutPtr != NULL_POINTER);
}

// Drive one engine (interpreter or JIT) from the standard host loop; returns the final Status.
static Status runEngine(Processor& processor, Pointer mainFunction) {
	Status status = processor.enterCall(mainFunction);
	if (status != OK) { return status; }
	do { processor.resetTimeOut(0x7FFFFFFF); status = processor.run(); } while (status == TIME_OUT || status == BLOCK_RETRY);
	return status;
}

// Assemble + compile one kernel, then diff the JIT against the interpreter over a set of inputs (status + full image).
static void runKernel(const char* name, const char* source, const int* inputs, size_t inputCount) {
	Symbols globals;
	Pointer gInPtr = NULL_POINTER, gOutPtr = NULL_POINTER;
	if (!assembleKernel(globals, source, gInPtr, gOutPtr)) { std::printf("  %s: assemble failed\n", name); ++failures; return; }

	JitModule module;
	JitCompiler compiler;
	compiler.compile(gCode, gFunctionCount, gFunctionTable, gMemory, module);	// gMemory is the clean image (SWCH tables live in const memory)
	if (!module.ok()) { std::printf("  %s: compile produced an unsupported opcode\n", name); ++failures; return; }

	const Pointer mainFunction = globals.findFunction("main");
	std::memcpy(gClean, gMemory, sizeof(gMemory));			// clean image (globals + DATi inits) to restore before each run
	std::printf("Kernel \"%s\" (%zu code words):\n", name, module.codeWords());
	for (size_t k = 0; k < inputCount; ++k) {
		const int n = inputs[k];

		// Interpreter: fresh image, run, snapshot the resulting image + status.
		std::memcpy(gMemory, gClean, sizeof(gMemory));
		gMemory[gInPtr - MEMORY_OFFSET].i = n;
		Processor interpreter(gCodeSize, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize,
				gConstsSize, CALL_STACK_SIZE, gInterpStack, gNatives, 0);
		const Status wantStatus = runEngine(interpreter, mainFunction);
		std::memcpy(gInterpImage, gMemory, sizeof(gMemory));

		// JIT: fresh image, run, diff status + whole image.
		std::memcpy(gMemory, gClean, sizeof(gMemory));
		gMemory[gInPtr - MEMORY_OFFSET].i = n;
		JitProcessor jit(module, gCodeSize, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize,
				gConstsSize, CALL_STACK_SIZE, gJitStack, gNatives, 0);
		const Status gotStatus = runEngine(jit, mainFunction);

		const bool statusOk = (gotStatus == wantStatus);
		const bool imageOk = (std::memcmp(gMemory, gInterpImage, sizeof(gMemory)) == 0);
		const int want = gInterpImage[gOutPtr - MEMORY_OFFSET].i;
		const int got = gMemory[gOutPtr - MEMORY_OFFSET].i;
		const bool ok = statusOk && imageOk;
		std::printf("  n=%-9d interp=%-12d/%d jit=%-12d/%d %s\n", n, want, static_cast<int>(wantStatus),
				got, static_cast<int>(gotStatus), ok ? "OK" : (statusOk ? "IMAGE MISMATCH" : "STATUS MISMATCH"));
		if (!ok) { ++failures; }
	}
}

int main() {
	std::printf("GAZLJit X64 engine test: JitProcessor (Rosetta) vs interpreter\n\n");
	const int sumInputs[] = { 0, 1, 2, 5, 10, 100, 1000, 65536 };
	runKernel("sumTo", SUMTO_SOURCE, sumInputs, sizeof(sumInputs) / sizeof(*sumInputs));
	std::printf("\n");
	const int arithInputs[] = { 0, 1, 7, 99, 100, 12345, -12345, 5000, -5000, 1000000 };
	runKernel("arith", ARITH_SOURCE, arithInputs, sizeof(arithInputs) / sizeof(*arithInputs));
	std::printf("\n");
	const int floatInputs[] = { 0, 1, 10, 100, 1000, 5000, -100, -5000, 12345 };
	runKernel("float", FLOAT_SOURCE, floatInputs, sizeof(floatInputs) / sizeof(*floatInputs));
	std::printf("\n");
	const int satInputs[] = { 0, 1, 2, 3, 5, -2, -3, -5 };
	runKernel("ftoi-sat", FTOISAT_SOURCE, satInputs, sizeof(satInputs) / sizeof(*satInputs));
	std::printf("\n");
	const int callInputs[] = { 0, 1, 2, 5, 10, 100 };
	runKernel("call", CALL_SOURCE, callInputs, sizeof(callInputs) / sizeof(*callInputs));
	std::printf("\n");
	const int indInputs[] = { 0, 3, 7, -3, -5 };
	runKernel("indirect", INDIRECT_SOURCE, indInputs, sizeof(indInputs) / sizeof(*indInputs));
	std::printf("\n");
	runKernel("native", NATIVE_SOURCE, callInputs, sizeof(callInputs) / sizeof(*callInputs));
	std::printf("\n");
	const int arrInputs[] = { 0, 1, 3, 10, 100 };
	runKernel("array", ARRAY_SOURCE, arrInputs, sizeof(arrInputs) / sizeof(*arrInputs));
	std::printf("\n");
	runKernel("gmem", GMEM_SOURCE, arrInputs, sizeof(arrInputs) / sizeof(*arrInputs));
	std::printf("\n");
	const int copyInputs[] = { 0, 5, 1000 };
	runKernel("copy", COPY_SOURCE, copyInputs, sizeof(copyInputs) / sizeof(*copyInputs));
	std::printf("\n");
	const int swInputs[] = { 0, 1, 2, 3, 4, 5, -1 };
	runKernel("switch", SWITCH_SOURCE, swInputs, sizeof(swInputs) / sizeof(*swInputs));
	std::printf("\n");
	const int dzInputs[] = { 0, 5, -7 };
	runKernel("div-zero", DIVZERO_SOURCE, dzInputs, sizeof(dzInputs) / sizeof(*dzInputs));
	std::printf("\n");
	const int oobInputs[] = { 0, 1, 2, 3, 1000000 };
	runKernel("oob-getl", OOB_GETL_SOURCE, oobInputs, sizeof(oobInputs) / sizeof(*oobInputs));
	std::printf("\n");
	runKernel("oob-peek", OOB_PEEK_SOURCE, oobInputs, sizeof(oobInputs) / sizeof(*oobInputs));
	std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
	return failures == 0 ? 0 : 1;
}
