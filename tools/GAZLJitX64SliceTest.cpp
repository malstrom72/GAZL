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
	X64 lowering vertical slice: a minimal, general lowering pass (integer + float; no calls, no fuel/suspend) that
	compiles an *assembled* GAZL function straight through the X64Emitter, runs the native code under Rosetta, and diffs
	the result against the GAZL interpreter over a range of inputs. Proves the SysV frame/dsp model, memory (PEEK/POKE)
	via a pinned base register, the integer op subset, and the Label/rel32 branch pass end to end. Sibling of the arm64
	GAZLJitSliceTest; built -arch x86_64. src/GAZL.* are used READ-ONLY through the public API.

	Register roles (SysV): rdi=dsp-in, rsi=mem-in on entry; pinned into callee-saved rbx=dsp (advanced by FUNC) and
	r14=memory base. Scratch ecx/edx per instruction; eax returns the Status. No GAZL calls, so no callee-saved
	trampoline beyond the two pushes.
*/

#include "GAZLJit.h"			// arch-neutral opcode enum (+ GAZL.h: Assembler / Processor / Instruction / Value)
#include "GAZLJitX64.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
#include <sys/mman.h>

using namespace GAZL;

static int failures = 0;

static void* mapExecutable(const uint8_t* code, size_t n) {
	void* p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (p == MAP_FAILED) { return 0; }
	std::memcpy(p, code, n);
	if (mprotect(p, 4096, PROT_READ | PROT_EXEC) != 0) { return 0; }
	return p;
}

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

// Exercises the completed integer set: div / mod / mul / shifts (arith + logical) / abs / and-or-xor, with signed inputs.
static const char* const ARITH_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$x:    LOCi\n" "$a:    LOCi\n" "$b:    LOCi\n"
	"       PEEK $x &gIn\n"
	"       DIVi $a $x #7\n"			// a = x / 7
	"       MODi $b $x #100\n"			// b = x % 100
	"       MULi $a $a $b\n"
	"       SHLi $a $a #2\n"
	"       SUBi $a $a $x\n"
	"       ABSi $a $a\n"				// a = |a|
	"       SHRi $b $x #1\n"			// arithmetic (signed) shift
	"       ADDi $a $a $b\n"
	"       SHRu $b $x #1\n"			// logical (unsigned) shift
	"       XORi $a $a $b\n"
	"       ANDi $a $a #16777215\n"
	"       POKE &gOut $a\n"
	"       RETU\n";

// Exercises the float set: ITOF/FTOI, add/sub/mul/div, abs, floor, and a float compare-branch. int in, int out.
static const char* const FLOAT_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$f:    LOCf\n" "$g:    LOCf\n"
	"       PEEK $n &gIn\n"
	"       iTOf $f $n #1.0\n"				// f = (float)n
	"       MULf $g $f #0.25\n"
	"       ADDf $g $g #10.5\n"
	"       SUBf $g $f $g\n"
	"       DIVf $g $g #3.0\n"
	"       ABSf $g $g\n"
	"       MOVf $f #0.0\n"
	"       GEQf $g #1000.0 @.big\n"	// if g >= 1000 goto big
	"       MOVf $f $g\n"
	"       GOTO @.done\n"
	".big:  MOVf $f #1000.0\n"
	".done: FLOf $f $f\n"				// floor
	"       fTOi $n $f #1.0\n"				// n = (int)f
	"       POKE &gOut $n\n"
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
	"       SETL $arr $i $t\n"			// arr[i] = i*n
	"       FORi $i #16 @.fill\n"
	"       MOVi $s #0\n"
	"       MOVi $i #0\n"
	".sum:  GETL $t $arr $i\n"			// t = arr[i]
	"       ADDi $s $s $t\n"
	"       FORi $i #16 @.sum\n"
	"       POKE &gOut $s\n"
	"       RETU\n";

// Two functions: main calls square(i) in a loop -> sum of i*i. Exercises CALL_CVC + the window/return convention.
static const char* const CALL_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"square: FUNC\n"					// square(x) -> x*x
	"$r:    OUTi\n" "$x:    INPi\n"
	"       MULi $r $x $x\n"
	"       RETU\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$s:    LOCi\n" "$i:    LOCi\n"
	"       PEEK $n &gIn\n"
	"       MOVi $s #0\n"
	"       MOVi $i #0\n"
	".loop: MOVi %1 $i\n"				// window: %0 = return, %1 = arg
	"       CALL &square %0 *2\n"
	"       ADDi $s $s %0\n"			// s += square(i)
	"       FORi $i $n @.loop\n"
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
	".sw:   MOVi $r #999\n"			// default (bare label): cases 3,4 and out-of-range clamp here
	".done: POKE &gOut $r\n"
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
	"       GEQi $n #0 @.go\n"			// n >= 0 -> square; else cube
	"       MOVp $fp &cube\n"
	".go:   MOVi %1 $n\n"
	"       CALL $fp %0 *2\n"			// indirect call through the pointer
	"       MOVi $rr %0\n"
	"       POKE &gOut $rr\n"
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
	"       ADRL %0 $buf *8\n"			// %0 = &buf
	"       COPY %0 &gSrc *8\n"			// copy 8 words gSrc -> buf
	"       MOVi $s #0\n"
	"       MOVi $i #0\n"
	".sum:  GETL $t $buf $i\n"
	"       ADDi $s $s $t\n"
	"       FORi $i #8 @.sum\n"
	"       ADDi $s $s $n\n"			// + input so it varies
	"       POKE &gOut $s\n"
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
	"       CALL ^nsq %0 *2\n"			// native call: %0 = return, %1 = arg
	"       ADDi $s $s %0\n"
	"       FORi $i $n @.loop\n"
	"       POKE &gOut $s\n"
	"       RETU\n";

// ADRL: output the pointer VALUE of a local; both interpreter and JIT must compute the same biased address.
static const char* const ADRL_SOURCE =
	"gIn:   GLOB *1\n" "       DATi #0\n"
	"gOut:  GLOB *1\n" "       DATi #0\n"
	"main:  FUNC\n" "       PARA *1\n"
	"$n:    LOCi\n" "$s:    LOCi\n" "$p:    LOCp\n"
	"       PEEK $n &gIn\n"
	"       ADRL $p $s *1\n"			// p = &s
	"       POKE &gOut $p\n"			// output the biased pointer value
	"       RETU\n";

namespace {
	const int CODE_SIZE = 64 * 1024, DATA_SIZE = 64 * 1024, FUNCTION_TABLE_SIZE = 1024, CALL_STACK_SIZE = 256;
	Instruction gCode[CODE_SIZE];
	Value gMemory[DATA_SIZE];
	Value gClean[DATA_SIZE];			// snapshot of the assembled image (globals + const inits), restored before each run
	UInt gFunctionTable[FUNCTION_TABLE_SIZE];
	CallStackEntry gCallStack[CALL_STACK_SIZE];
	UInt gGlobalsSize = 0, gConstsSize = 0, gFunctionCount = 0;
	void* gEntryAddr[FUNCTION_TABLE_SIZE];		// ordinal -> native entry address (filled after mapping; for CALL_VVC)
}

// A native, in both flavors: the interpreter's (Processor*, reads via accessParams) and the JIT's (params = dsp+window,
// baked as an absolute call). Both compute r = x*x, so interpreter and JIT agree. Ordinal 0 = "nsq".
static Status nsqInterp(Processor* p) { Value* q = p->accessParams(2); if (q == 0) { return DATA_STACK_OVERFLOW; } q[0].i = q[1].i * q[1].i; return OK; }
static int nsqJit(Value* params) { params[0].i = params[1].i * params[1].i; return 0; }
static NativeFunc const gNat[] = { nsqInterp };
static void* const gNatJit[] = { reinterpret_cast<void*>(&nsqJit) };
static const char* const gNatNames[] = { "nsq" };

static bool assembleKernel(Symbols& globals, const char* source, Pointer& gInPtr, Pointer& gOutPtr) {
	for (int i = 0; i < static_cast<int>(sizeof(gNatNames) / sizeof(*gNatNames)); ++i) { globals.registerNative(gNatNames[i], i); }
	UInt codeSize = 0, gs = 0, cs = 0, fc = 0;
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assem.newUnit("x64slice");
		std::string src(source);
		size_t pos = 0;
		while (pos < src.size()) {
			const size_t nl = src.find('\n', pos);
			assem.feed(src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos).c_str());
			if (nl == std::string::npos) { break; }
			pos = nl + 1;
		}
		assem.finalize(codeSize, gs, cs, fc);
	} catch (const Exception& e) {
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str());
		return false;
	}
	gGlobalsSize = gs; gConstsSize = cs; gFunctionCount = fc;
	UInt sz = 0;
	gInPtr = globals.findGlobal("gIn", sz);
	gOutPtr = globals.findGlobal("gOut", sz);
	return (gInPtr != NULL_POINTER && gOutPtr != NULL_POINTER);
}

static int interpreterRun(Symbols& globals, Pointer gInPtr, Pointer gOutPtr, int n) {
	Processor p(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize,
			gConstsSize, CALL_STACK_SIZE, gCallStack, gNat, 0);
	p.accessMemory(gInPtr, 1)->i = n;
	Status s = p.enterCall(globals.findFunction("main"));
	do { p.resetTimeOut(0x7FFFFFFF); s = p.run(); } while (s == TIME_OUT);
	if (s != OK) { std::printf("  interpreter status %d\n", s); ++failures; return 0; }
	return p.accessMemory(gOutPtr, 1)->i;
}

// --- the minimal x64 lowering (integer + float) ---

static const Reg DSP = RBX, MEM = R14, A = RCX, B = RDX;		// pinned + GP scratch roles
static const Reg F0 = static_cast<Reg>(0), F1 = static_cast<Reg>(1);	// XMM scratch (separate file from GP)

static bool branchTgt(const Instruction* code, UInt j, UInt& t) {
	const Int op = code[j].opcode;
	if (op == OP_GOTO) { t = static_cast<UInt>(static_cast<Int>(j) + code[j].p0.i); return true; }
	if (op >= OP_FORi_VVB && op <= OP_NEQF_VCB) { t = static_cast<UInt>(static_cast<Int>(j) + code[j].p2.i); return true; }
	return false;
}

// Render operand into reg A or B: a slot (load from dsp) or a constant (movImm).
static void toReg(X64Emitter& e, Reg r, const Value& p, bool isConst) {
	if (isConst) { e.movImm(r, static_cast<uint32_t>(p.i)); }
	else { e.load(r, DSP, p.i * 4); }
}

typedef void (X64Emitter::*BinOp)(Reg, Reg);

// dst = a <op> b, a = p1 (slot or const s1Const), b = p2 (slot or const s2Const). result via A.
static void emitBin(X64Emitter& e, BinOp op, const Instruction& in, bool s1Const, bool s2Const) {
	toReg(e, A, in.p1, s1Const);
	toReg(e, B, in.p2, s2Const);
	(e.*op)(A, B);
	e.store(DSP, in.p0.i * 4, A);
}

// Signed division: dividend p1 -> eax, divisor p2 -> ecx, cdq sign-extends into edx, idiv; store quotient (eax) or
// remainder (edx). (No div-by-zero / INT_MIN-over-minus-one guard yet — the test inputs avoid both; TODO with traps.)
static void emitDivMod(X64Emitter& e, const Instruction& in, bool rem, bool s1Const, bool s2Const) {
	if (s1Const) { e.movImm(RAX, static_cast<uint32_t>(in.p1.i)); } else { e.load(RAX, DSP, in.p1.i * 4); }
	if (s2Const) { e.movImm(RCX, static_cast<uint32_t>(in.p2.i)); } else { e.load(RCX, DSP, in.p2.i * 4); }
	e.cdq();
	e.idiv(RCX);
	e.store(DSP, in.p0.i * 4, rem ? RDX : RAX);
}

// Shift: value p1 -> eax, count p2 (const -> imm8, or slot -> cl). kind 0=shl, 1=shr(logical), 2=sar(arithmetic).
static void emitShift(X64Emitter& e, const Instruction& in, int kind, bool s1Const, bool s2Const) {
	if (s1Const) { e.movImm(RAX, static_cast<uint32_t>(in.p1.i)); } else { e.load(RAX, DSP, in.p1.i * 4); }
	if (s2Const) {
		const uint8_t n = static_cast<uint8_t>(in.p2.i & 31);
		if (kind == 0) { e.shlImm(RAX, n); } else if (kind == 1) { e.shrImm(RAX, n); } else { e.sarImm(RAX, n); }
	} else {
		e.load(RCX, DSP, in.p2.i * 4);
		if (kind == 0) { e.shlCl(RAX); } else if (kind == 1) { e.shrCl(RAX); } else { e.sarCl(RAX); }
	}
	e.store(DSP, in.p0.i * 4, RAX);
}

// if (a <cc> b) goto target — a = p0, b = p1 in the const modes named by the opcode.
static void emitBranch(X64Emitter& e, Cond cc, const Instruction& in, UInt j, bool c0Const, bool c1Const, std::map<UInt, Label>& lbl) {
	toReg(e, A, in.p0, c0Const);
	toReg(e, B, in.p1, c1Const);
	e.cmp(A, B);
	e.jcc(cc, lbl[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
}

// Render a float operand into an XMM reg: a slot (movss load) or a constant (int bits -> movd).
static void toXmm(X64Emitter& e, Reg xmm, const Value& p, bool isConst) {
	if (isConst) { e.movImm(A, static_cast<uint32_t>(p.i)); e.movdToXmm(xmm, A); }
	else { e.movssLoad(xmm, DSP, p.i * 4); }
}

// dst = a <fop> b  (fop = addss/subss/mulss/divss), operands in the const modes named by the opcode.
static void emitFBin(X64Emitter& e, BinOp fop, const Instruction& in, bool s1Const, bool s2Const) {
	toXmm(e, F0, in.p1, s1Const);
	toXmm(e, F1, in.p2, s2Const);
	(e.*fop)(F0, F1);
	e.movssStore(DSP, in.p0.i * 4, F0);
}

// Float compare-branch, NaN-correct vs C++: kind 0=< 1=>= 2=== 3=!=. a=p0, b=p1.
static void emitFBranch(X64Emitter& e, int kind, const Instruction& in, UInt j, bool c0Const, bool c1Const, std::map<UInt, Label>& lbl) {
	toXmm(e, F0, in.p0, c0Const);
	toXmm(e, F1, in.p1, c1Const);
	Label tgt = lbl[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)];
	if (kind == 0) { e.ucomiss(F1, F0); e.jcc(CC_A, tgt); }				// b>a ordered  == (a<b)
	else if (kind == 1) { e.ucomiss(F0, F1); e.jcc(CC_AE, tgt); }		// a>=b ordered
	else if (kind == 2) { e.ucomiss(F0, F1); Label over = e.newLabel(); e.jcc(CC_P, over); e.jcc(CC_E, tgt); e.bind(over); }
	else { e.ucomiss(F0, F1); e.jcc(CC_P, tgt); e.jcc(CC_NE, tgt); }		// unordered or not-equal
}

static bool lowerBody(X64Emitter& e, const Instruction* code, UInt funcStart, const std::vector<Label>& entry, Label epilogue, void* const* natives) {
	UInt end = funcStart;
	while (code[end].opcode != OP_RETU) { ++end; }

	std::map<UInt, Label> lbl;
	for (UInt j = funcStart; j <= end; ++j) {
		if (code[j].opcode == OP_SWCH) {			// every jump-table entry (read from const memory) is a target
			const UInt sz = static_cast<UInt>(code[j].p1.i) + 1;
			const UInt tbl = static_cast<UInt>(code[j].p2.p - MEMORY_OFFSET);
			for (UInt kk = 0; kk < sz; ++kk) {
				const UInt t = static_cast<UInt>(static_cast<Int>(j) + gMemory[tbl + kk].i);
				if (!lbl.count(t)) { lbl[t] = e.newLabel(); }
			}
			continue;
		}
		UInt t;
		if (branchTgt(code, j, t) && !lbl.count(t)) { lbl[t] = e.newLabel(); }
	}

	e.push(DSP); e.push(MEM);					// SysV prologue: save callee-saved pins
	e.addImmQ(RSP, 0xFFFFFFF8u);				// sub rsp, 8 — keep rsp 16-aligned at inner call sites
	e.movQ(DSP, RDI); e.movQ(MEM, RSI);			// dsp = arg0, memory base = arg1
	e.addImmQ(DSP, static_cast<uint32_t>(code[funcStart].p0.i) * 4u);	// FUNC: advance to this frame

	for (UInt j = funcStart; j <= end; ++j) {
		std::map<UInt, Label>::iterator it = lbl.find(j);
		if (it != lbl.end()) { e.bind(it->second); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		switch (op) {
			case OP_FUNC: break;
			case OP_RETU:
				e.movImm(RAX, 0); e.jmp(epilogue);	// Status OK -> shared epilogue
				break;
			case OP_CALL_CVC: {						// direct GAZL call: callee runs with dsp += window, returns Status in eax
				const UInt callee = in.p0.p - IP_OFFSET;
				e.movQ(RDI, DSP);
				if (in.p1.i != 0) { e.addImmQ(RDI, static_cast<uint32_t>(in.p1.i) * 4u); }	// rdi = dsp + window
				e.movQ(RSI, MEM);
				e.callRel(entry[callee]);
				e.cmpImm(RAX, 0); e.jcc(CC_NE, epilogue);	// propagate a non-OK status
				break;
			}
			case OP_CALL_VVC: {						// indirect call: fn pointer in a slot -> ordinal -> gEntryAddr[ordinal]
				e.load(RCX, DSP, in.p0.i * 4);		// fn pointer = IP_OFFSET + ordinal
				e.subImm(RCX, IP_OFFSET); e.shlImm(RCX, 3);		// ordinal * 8 (byte index into the entry table)
				e.movImm64(RAX, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(gEntryAddr)));
				e.addQ(RAX, RCX); e.loadQ(RAX, RAX, 0);			// RAX = gEntryAddr[ordinal]
				e.movQ(RDI, DSP);
				if (in.p1.i != 0) { e.addImmQ(RDI, static_cast<uint32_t>(in.p1.i) * 4u); }
				e.movQ(RSI, MEM);
				e.callReg(RAX);
				e.cmpImm(RAX, 0); e.jcc(CC_NE, epilogue);
				break;
			}
			case OP_CALL_NVC: {						// native call: params = dsp + window; native fn baked as an absolute call
				const UInt ord = static_cast<UInt>(in.p0.i);
				e.movQ(RDI, DSP);
				if (in.p1.i != 0) { e.addImmQ(RDI, static_cast<uint32_t>(in.p1.i) * 4u); }
				e.movImm64(RAX, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(natives[ord])));
				e.callReg(RAX);
				e.cmpImm(RAX, 0); e.jcc(CC_NE, epilogue);	// native returned a non-OK Status
				break;
			}

			case OP_MOVE_VV: e.load(A, DSP, in.p1.i * 4); e.store(DSP, in.p0.i * 4, A); break;
			case OP_MOVE_VC: e.movImm(A, static_cast<uint32_t>(in.p1.i)); e.store(DSP, in.p0.i * 4, A); break;

			case OP_PEEK_VC: e.load(A, MEM, static_cast<int32_t>((in.p1.p - MEMORY_OFFSET) * 4)); e.store(DSP, in.p0.i * 4, A); break;
			case OP_POKE_CV: e.load(A, DSP, in.p1.i * 4); e.store(MEM, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), A); break;
			case OP_POKE_CC: e.movImm(A, static_cast<uint32_t>(in.p1.i)); e.store(MEM, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), A); break;

			// var-indexed global memory: base const (p1/p0), index var, value var/const. [MEM + index*4 + base*4]
			case OP_PEEK_VCV:
				e.load(A, DSP, in.p2.i * 4);
				e.loadIdx(A, MEM, A, static_cast<int32_t>((in.p1.p - MEMORY_OFFSET) * 4));
				e.store(DSP, in.p0.i * 4, A); break;
			case OP_POKE_CVV:
				e.load(A, DSP, in.p1.i * 4); e.load(B, DSP, in.p2.i * 4);
				e.storeIdx(MEM, A, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), B); break;
			case OP_POKE_CVC:
				e.load(A, DSP, in.p1.i * 4); e.movImm(B, static_cast<uint32_t>(in.p2.i));
				e.storeIdx(MEM, A, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), B); break;

			// local array (frame): base = dsp + C (p1 for GETL, p0 for SETL), index var. [DSP + index*4 + C*4]
			case OP_GETL_VVV:
				e.load(A, DSP, in.p2.i * 4);
				e.loadIdx(A, DSP, A, in.p1.i * 4);
				e.store(DSP, in.p0.i * 4, A); break;
			case OP_SETL_VVV:
				e.load(A, DSP, in.p1.i * 4); e.load(B, DSP, in.p2.i * 4);
				e.storeIdx(DSP, A, in.p0.i * 4, B); break;
			case OP_SETL_VVC:
				e.load(A, DSP, in.p1.i * 4); e.movImm(B, static_cast<uint32_t>(in.p2.i));
				e.storeIdx(DSP, A, in.p0.i * 4, B); break;

			// bulk copy p2 words from src (p1) to dst (p0), via rep movsd. Each pointer is a const memory address or a
			// slot holding a GAZL pointer; resolve to a byte address (MEM + wordIndex*4).
			case OP_COPY_VVC: case OP_COPY_VCC: case OP_COPY_CVC: case OP_COPY_CCC: {
				const bool dstConst = (op == OP_COPY_CVC || op == OP_COPY_CCC);
				const bool srcConst = (op == OP_COPY_VCC || op == OP_COPY_CCC);
				if (dstConst) { e.movQ(RDI, MEM); e.addImmQ(RDI, static_cast<uint32_t>((in.p0.p - MEMORY_OFFSET) * 4)); }
				else { e.load(RAX, DSP, in.p0.i * 4); e.subImm(RAX, MEMORY_OFFSET); e.shlImm(RAX, 2); e.movQ(RDI, MEM); e.addQ(RDI, RAX); }
				if (srcConst) { e.movQ(RSI, MEM); e.addImmQ(RSI, static_cast<uint32_t>((in.p1.p - MEMORY_OFFSET) * 4)); }
				else { e.load(RAX, DSP, in.p1.i * 4); e.subImm(RAX, MEMORY_OFFSET); e.shlImm(RAX, 2); e.movQ(RSI, MEM); e.addQ(RSI, RAX); }
				e.movImm(RCX, static_cast<uint32_t>(in.p2.i));
				e.cld(); e.repMovsd();
				break;
			}

			// address of a local p1 -> dest p0. pointer = MEMORY_OFFSET + wordIndex(dsp) + slot.
			case OP_ADRL:
				e.movQ(RAX, DSP); e.subQ(RAX, MEM); e.shrImm(RAX, 2);	// (dsp - membase) / 4 = dsp word offset
				e.addImm(RAX, static_cast<uint32_t>(in.p1.i));			// + slot
				e.addImm(RAX, MEMORY_OFFSET);
				e.store(DSP, in.p0.i * 4, RAX); break;

			case OP_ADDI_VVV: emitBin(e, &X64Emitter::add, in, false, false); break;
			case OP_ADDI_VVC: emitBin(e, &X64Emitter::add, in, false, true); break;
			case OP_SUBI_VVV: emitBin(e, &X64Emitter::sub, in, false, false); break;
			case OP_SUBI_VVC: emitBin(e, &X64Emitter::sub, in, false, true); break;
			case OP_SUBI_VCV: emitBin(e, &X64Emitter::sub, in, true, false); break;
			case OP_MULI_VVV: emitBin(e, &X64Emitter::imul, in, false, false); break;
			case OP_MULI_VVC: emitBin(e, &X64Emitter::imul, in, false, true); break;
			case OP_ANDI_VVV: emitBin(e, &X64Emitter::and_, in, false, false); break;
			case OP_ANDI_VVC: emitBin(e, &X64Emitter::and_, in, false, true); break;
			case OP_IORI_VVV: emitBin(e, &X64Emitter::or_, in, false, false); break;
			case OP_IORI_VVC: emitBin(e, &X64Emitter::or_, in, false, true); break;
			case OP_XORI_VVV: emitBin(e, &X64Emitter::xor_, in, false, false); break;
			case OP_XORI_VVC: emitBin(e, &X64Emitter::xor_, in, false, true); break;

			case OP_DIVI_VVV: emitDivMod(e, in, false, false, false); break;
			case OP_DIVI_VVC: emitDivMod(e, in, false, false, true); break;
			case OP_DIVI_VCV: emitDivMod(e, in, false, true, false); break;
			case OP_MODI_VVV: emitDivMod(e, in, true, false, false); break;
			case OP_MODI_VVC: emitDivMod(e, in, true, false, true); break;
			case OP_MODI_VCV: emitDivMod(e, in, true, true, false); break;
			case OP_SHLI_VVV: emitShift(e, in, 0, false, false); break;
			case OP_SHLI_VVC: emitShift(e, in, 0, false, true); break;
			case OP_SHLI_VCV: emitShift(e, in, 0, true, false); break;
			case OP_SHRI_VVV: emitShift(e, in, 2, false, false); break;
			case OP_SHRI_VVC: emitShift(e, in, 2, false, true); break;
			case OP_SHRI_VCV: emitShift(e, in, 2, true, false); break;
			case OP_SHRU_VVV: emitShift(e, in, 1, false, false); break;
			case OP_SHRU_VVC: emitShift(e, in, 1, false, true); break;
			case OP_SHRU_VCV: emitShift(e, in, 1, true, false); break;
			case OP_ABSI:
				e.load(RAX, DSP, in.p1.i * 4);					// |x| = (x ^ (x>>31)) - (x>>31)
				e.mov(RDX, RAX); e.sarImm(RDX, 31); e.xor_(RAX, RDX); e.sub(RAX, RDX);
				e.store(DSP, in.p0.i * 4, RAX);
				break;

			case OP_FORi_VVB: case OP_FORi_VCB:
				e.load(A, DSP, in.p0.i * 4); e.addImm(A, 1); e.store(DSP, in.p0.i * 4, A);
				toReg(e, B, in.p1, op == OP_FORi_VCB);
				e.cmp(A, B);
				e.jcc(CC_L, lbl[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;

			case OP_ADDF_VVV: emitFBin(e, &X64Emitter::addss, in, false, false); break;
			case OP_ADDF_VVC: emitFBin(e, &X64Emitter::addss, in, false, true); break;
			case OP_SUBF_VVV: emitFBin(e, &X64Emitter::subss, in, false, false); break;
			case OP_SUBF_VVC: emitFBin(e, &X64Emitter::subss, in, false, true); break;
			case OP_SUBF_VCV: emitFBin(e, &X64Emitter::subss, in, true, false); break;
			case OP_MULF_VVV: emitFBin(e, &X64Emitter::mulss, in, false, false); break;
			case OP_MULF_VVC: emitFBin(e, &X64Emitter::mulss, in, false, true); break;
			case OP_DIVF_VVV: emitFBin(e, &X64Emitter::divss, in, false, false); break;
			case OP_DIVF_VVC: emitFBin(e, &X64Emitter::divss, in, false, true); break;
			case OP_DIVF_VCV: emitFBin(e, &X64Emitter::divss, in, true, false); break;
			// FTOI/ITOF carry a scale constant (p2): FTOI = (int)(src * scale), ITOF = (float)src * scale.
			case OP_FTOI_VVC:
				e.movssLoad(F0, DSP, in.p1.i * 4);
				e.movImm(A, static_cast<uint32_t>(in.p2.i)); e.movdToXmm(F1, A); e.mulss(F0, F1);
				e.cvttss2si(A, F0); e.store(DSP, in.p0.i * 4, A); break;
			case OP_ITOF_VVC:
				e.load(A, DSP, in.p1.i * 4); e.cvtsi2ss(F0, A);
				e.movImm(A, static_cast<uint32_t>(in.p2.i)); e.movdToXmm(F1, A); e.mulss(F0, F1);
				e.movssStore(DSP, in.p0.i * 4, F0); break;
			case OP_ABSF: e.load(A, DSP, in.p1.i * 4); e.movImm(B, 0x7fffffffu); e.and_(A, B); e.store(DSP, in.p0.i * 4, A); break;
			case OP_FLOF: e.movssLoad(F0, DSP, in.p1.i * 4); e.roundss(F1, F0, 1); e.movssStore(DSP, in.p0.i * 4, F1); break;
			case OP_LSSF_VVB: emitFBranch(e, 0, in, j, false, false, lbl); break;
			case OP_LSSF_VCB: emitFBranch(e, 0, in, j, false, true, lbl); break;
			case OP_LSSF_CVB: emitFBranch(e, 0, in, j, true, false, lbl); break;
			case OP_EQUF_VVB: emitFBranch(e, 2, in, j, false, false, lbl); break;
			case OP_EQUF_VCB: emitFBranch(e, 2, in, j, false, true, lbl); break;
			case OP_NLSF_VVB: emitFBranch(e, 1, in, j, false, false, lbl); break;
			case OP_NLSF_VCB: emitFBranch(e, 1, in, j, false, true, lbl); break;
			case OP_NLSF_CVB: emitFBranch(e, 1, in, j, true, false, lbl); break;
			case OP_NEQF_VVB: emitFBranch(e, 3, in, j, false, false, lbl); break;
			case OP_NEQF_VCB: emitFBranch(e, 3, in, j, false, true, lbl); break;

			case OP_GOTO: e.jmp(lbl[static_cast<UInt>(static_cast<Int>(j) + in.p0.i)]); break;

			case OP_SWCH: {						// index = min(unsigned(V0), C1); jump into a table of `jmp case`
				const UInt sz = static_cast<UInt>(in.p1.i) + 1;
				const UInt tbl = static_cast<UInt>(in.p2.p - MEMORY_OFFSET);
				e.load(A, DSP, in.p0.i * 4);
				e.cmpImm(A, static_cast<uint32_t>(in.p1.i));
				Label keep = e.newLabel();
				e.jcc(CC_BE, keep);				// (unsigned) val <= C1 -> keep; else clamp
				e.movImm(A, static_cast<uint32_t>(in.p1.i));
				e.bind(keep);
				e.mov(B, A); e.shlImm(B, 2); e.add(B, A);	// B = index * 5 (each table jmp is 5 bytes)
				Label tableBase = e.newLabel();
				e.leaRip(RAX, tableBase); e.addQ(RAX, B); e.jmpReg(RAX);
				e.bind(tableBase);
				for (UInt kk = 0; kk < sz; ++kk) {
					const UInt t = static_cast<UInt>(static_cast<Int>(j) + gMemory[tbl + kk].i);
					e.jmp(lbl[t]);
				}
				break;
			}

			case OP_LSSI_VVB: emitBranch(e, CC_L, in, j, false, false, lbl); break;
			case OP_LSSI_VCB: emitBranch(e, CC_L, in, j, false, true, lbl); break;
			case OP_LSSI_CVB: emitBranch(e, CC_L, in, j, true, false, lbl); break;
			case OP_EQUI_VVB: emitBranch(e, CC_E, in, j, false, false, lbl); break;
			case OP_EQUI_VCB: emitBranch(e, CC_E, in, j, false, true, lbl); break;
			case OP_NLSI_VVB: emitBranch(e, CC_GE, in, j, false, false, lbl); break;
			case OP_NLSI_VCB: emitBranch(e, CC_GE, in, j, false, true, lbl); break;
			case OP_NLSI_CVB: emitBranch(e, CC_GE, in, j, true, false, lbl); break;
			case OP_NEQI_VVB: emitBranch(e, CC_NE, in, j, false, false, lbl); break;
			case OP_NEQI_VCB: emitBranch(e, CC_NE, in, j, false, true, lbl); break;

			default: return false;			// unsupported opcode
		}
	}
	return true;
}

// Compile every function into one buffer: main first (so its entry is offset 0), then the rest, then one shared
// epilogue (all functions have identical prologue/epilogue). Direct CALLs resolve to entry labels via rel32 fixups.
static bool compileProgram(X64Emitter& e, const Instruction* code, const UInt* functionTable, UInt functionCount, UInt mainOrd, void* const* natives, size_t* entryOffsets) {
	std::vector<Label> entry(functionCount);
	for (UInt k = 0; k < functionCount; ++k) { entry[k] = e.newLabel(); }
	Label epilogue = e.newLabel();

	e.bind(entry[mainOrd]);
	if (!lowerBody(e, code, functionTable[mainOrd], entry, epilogue, natives)) { return false; }
	for (UInt k = 0; k < functionCount; ++k) {
		if (k == mainOrd) { continue; }
		e.bind(entry[k]);
		if (!lowerBody(e, code, functionTable[k], entry, epilogue, natives)) { return false; }
	}
	e.bind(epilogue);
	e.addImmQ(RSP, 8u); e.pop(MEM); e.pop(DSP); e.ret();
	e.finalize();
	for (UInt k = 0; k < functionCount; ++k) { entryOffsets[k] = static_cast<size_t>(e.labelOffset(entry[k])); }
	return true;
}

typedef int (*JitFn)(Value* dsp, Value* mem);

// Assemble + lower one kernel, then diff the JIT'd native code against the interpreter over a set of inputs.
static void runKernel(const char* name, const char* source, const int* inputs, size_t nInputs) {
	Symbols globals;
	Pointer gInPtr = NULL_POINTER, gOutPtr = NULL_POINTER;
	if (!assembleKernel(globals, source, gInPtr, gOutPtr)) { std::printf("  %s: assemble failed\n", name); ++failures; return; }
	X64Emitter e;
	static size_t entryOffsets[FUNCTION_TABLE_SIZE];
	if (!compileProgram(e, gCode, gFunctionTable, gFunctionCount, globals.findFunction("main") - IP_OFFSET, gNatJit, entryOffsets)) {
		std::printf("  %s: unsupported opcode\n", name); ++failures; return;
	}
	void* code = mapExecutable(e.code(), e.size());
	if (code == 0) { std::printf("  %s: mapExecutable failed\n", name); ++failures; return; }
	for (UInt k = 0; k < gFunctionCount; ++k) { gEntryAddr[k] = reinterpret_cast<char*>(code) + entryOffsets[k]; }
	JitFn jit = reinterpret_cast<JitFn>(code);
	std::memcpy(gClean, gMemory, sizeof(gMemory));		// clean image (globals + DATi/DATf inits) to restore each run
	std::printf("Kernel \"%s\" (%zu bytes):\n", name, e.size());
	for (size_t k = 0; k < nInputs; ++k) {
		const int n = inputs[k];
		std::memcpy(gMemory, gClean, sizeof(gMemory));	// interpreter run starts from the clean image
		const int want = interpreterRun(globals, gInPtr, gOutPtr, n);
		std::memcpy(gMemory, gClean, sizeof(gMemory));	// JIT run too
		gMemory[gInPtr - MEMORY_OFFSET].i = n;
		const int st = jit(&gMemory[gGlobalsSize], &gMemory[0]);
		const int got = gMemory[gOutPtr - MEMORY_OFFSET].i;
		const bool ok = (st == 0) && (got == want);
		std::printf("  n=%-8d interp=%-12d jit=%-12d %s\n", n, want, got, ok ? "OK" : "MISMATCH");
		if (!ok) { ++failures; }
	}
}

int main() {
	std::printf("GAZLJit X64 lowering slice: JIT (Rosetta) vs interpreter\n\n");
	const int sumInputs[] = { 0, 1, 2, 5, 10, 100, 1000, 65536 };
	const int arithInputs[] = { 0, 1, 7, 99, 100, 12345, -12345, 5000, -5000, 1000000 };
	runKernel("sumTo", SUMTO_SOURCE, sumInputs, sizeof(sumInputs) / sizeof(*sumInputs));
	std::printf("\n");
	runKernel("arith", ARITH_SOURCE, arithInputs, sizeof(arithInputs) / sizeof(*arithInputs));
	std::printf("\n");
	const int floatInputs[] = { 0, 1, 10, 100, 1000, 5000, -100, -5000, 12345 };
	runKernel("float", FLOAT_SOURCE, floatInputs, sizeof(floatInputs) / sizeof(*floatInputs));
	std::printf("\n");
	const int arrInputs[] = { 0, 1, 3, 10, 100 };
	runKernel("array", ARRAY_SOURCE, arrInputs, sizeof(arrInputs) / sizeof(*arrInputs));
	std::printf("\n");
	const int adrlInputs[] = { 0, 42 };
	runKernel("adrl", ADRL_SOURCE, adrlInputs, sizeof(adrlInputs) / sizeof(*adrlInputs));
	std::printf("\n");
	const int callInputs[] = { 0, 1, 2, 5, 10, 100 };
	runKernel("call", CALL_SOURCE, callInputs, sizeof(callInputs) / sizeof(*callInputs));
	std::printf("\n");
	runKernel("native", NATIVE_SOURCE, callInputs, sizeof(callInputs) / sizeof(*callInputs));
	std::printf("\n");
	const int copyInputs[] = { 0, 5, 1000 };
	runKernel("copy", COPY_SOURCE, copyInputs, sizeof(copyInputs) / sizeof(*copyInputs));
	std::printf("\n");
	const int indInputs[] = { 0, 3, 7, -3, -5 };		// n>=0 -> square, n<0 -> cube
	runKernel("indirect", INDIRECT_SOURCE, indInputs, sizeof(indInputs) / sizeof(*indInputs));
	std::printf("\n");
	const int swInputs[] = { 0, 1, 2, 3, 4, 5, -1 };	// 0/1/2 -> cases; 3/4/oob -> default
	runKernel("switch", SWITCH_SOURCE, swInputs, sizeof(swInputs) / sizeof(*swInputs));
	std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
	return failures == 0 ? 0 : 1;
}
