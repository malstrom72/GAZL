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

#include "GAZLJitX64.h"
#include "GAZLJit.h"			// arch-neutral opcode enum + Offsets / JitModule / JitProcessor / JitCompiler (+ GAZL.h)
#include "GAZLJitMem.h"			// makeExecutable() — platform-specific backend, architecture-neutral

#include <cstddef>
#include <map>
#include <vector>

namespace GAZL {

// --- low-level encoding primitives ---

void X64Emitter::d32(uint32_t v) {
	b(static_cast<uint8_t>(v)); b(static_cast<uint8_t>(v >> 8));
	b(static_cast<uint8_t>(v >> 16)); b(static_cast<uint8_t>(v >> 24));
}
void X64Emitter::d64(uint64_t v) {
	d32(static_cast<uint32_t>(v)); d32(static_cast<uint32_t>(v >> 32));
}

/*
	Emit a REX prefix only when it changes anything: REX.W for 64-bit operand size, REX.R when the ModRM.reg field
	names r8..r15, REX.B when the rm / base / opcode-embedded register does. `reg` is the ModRM.reg field (or 0 for
	opcode-embedded forms), `base` is the rm / SIB-base / opcode register. No index register is used here (REX.X = 0).
*/
void X64Emitter::rex(bool w, int reg, int base) {
	const uint8_t r = static_cast<uint8_t>(0x40 | (w ? 8 : 0) | ((reg >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0));
	if (r != 0x40) { b(r); }
}

void X64Emitter::modrmReg(int reg, int rm) {
	b(static_cast<uint8_t>(0xC0 | ((reg & 7) << 3) | (rm & 7)));
}

/*
	Encode a `[base + disp]` memory operand into ModRM (+ SIB + displacement). `reg` is the ModRM.reg field. RSP/R12 as
	base require a SIB byte (rm == 4 escapes to SIB); RBP/R13 (rm == 5) cannot use the mod=00 form (that means
	RIP-relative), so a zero displacement is forced to the disp8 form. Otherwise mod picks the smallest displacement.
*/
void X64Emitter::memOperand(int reg, Reg base, int32_t disp) {
	const int rm = base & 7;
	const bool needSIB = (rm == 4);
	int mod;
	if (disp == 0 && rm != 5) { mod = 0; }
	else if (disp >= -128 && disp <= 127) { mod = 1; }
	else { mod = 2; }
	b(static_cast<uint8_t>((mod << 6) | ((reg & 7) << 3) | (needSIB ? 4 : rm)));
	if (needSIB) { b(static_cast<uint8_t>((0 << 6) | (4 << 3) | rm)); }	// scale 1, no index, base = rm
	if (mod == 1) { b(static_cast<uint8_t>(disp)); }
	else if (mod == 2) { d32(static_cast<uint32_t>(disp)); }
}

void X64Emitter::rex3(bool w, int reg, int index, int base) {
	const uint8_t r = static_cast<uint8_t>(0x40 | (w ? 8 : 0) | ((reg >= 8) ? 4 : 0) | ((index >= 8) ? 2 : 0) | ((base >= 8) ? 1 : 0));
	if (r != 0x40) { b(r); }
}

// [base + index*4 + disp]: ModRM.rm = 4 (SIB escape), SIB = scale(2 => *4) | index | base.
void X64Emitter::memOperandIdx(int reg, Reg base, Reg index, int32_t disp) {
	const int bb = base & 7;
	int mod;
	if (disp == 0 && bb != 5) { mod = 0; }
	else if (disp >= -128 && disp <= 127) { mod = 1; }
	else { mod = 2; }
	b(static_cast<uint8_t>((mod << 6) | ((reg & 7) << 3) | 4));
	b(static_cast<uint8_t>((2 << 6) | ((index & 7) << 3) | bb));	// scale 4
	if (mod == 1) { b(static_cast<uint8_t>(disp)); }
	else if (mod == 2) { d32(static_cast<uint32_t>(disp)); }
}

// 81 /ext id  — immediate ALU op (ext = 0 add, 5 sub, 7 cmp, ...).
void X64Emitter::aluImm(int ext, Reg rd, uint32_t imm) {
	rex(false, 0, rd);
	b(0x81);
	b(static_cast<uint8_t>(0xC0 | (ext << 3) | (rd & 7)));
	d32(imm);
}

// --- moves / constants ---

void X64Emitter::movImm(Reg rd, uint32_t imm) { rex(false, 0, rd); b(static_cast<uint8_t>(0xB8 | (rd & 7))); d32(imm); }
void X64Emitter::movImm64(Reg rd, uint64_t imm) { rex(true, 0, rd); b(static_cast<uint8_t>(0xB8 | (rd & 7))); d64(imm); }
void X64Emitter::mov(Reg rd, Reg rs) { rex(false, rs, rd); b(0x89); modrmReg(rs, rd); }
void X64Emitter::movQ(Reg rd, Reg rs) { rex(true, rs, rd); b(0x89); modrmReg(rs, rd); }

// --- integer arithmetic / logic ---

void X64Emitter::add(Reg rd, Reg rs) { rex(false, rs, rd); b(0x01); modrmReg(rs, rd); }
void X64Emitter::sub(Reg rd, Reg rs) { rex(false, rs, rd); b(0x29); modrmReg(rs, rd); }
void X64Emitter::imul(Reg rd, Reg rs) { rex(false, rd, rs); b(0x0F); b(0xAF); modrmReg(rd, rs); }
void X64Emitter::and_(Reg rd, Reg rs) { rex(false, rs, rd); b(0x21); modrmReg(rs, rd); }
void X64Emitter::or_(Reg rd, Reg rs) { rex(false, rs, rd); b(0x09); modrmReg(rs, rd); }
void X64Emitter::xor_(Reg rd, Reg rs) { rex(false, rs, rd); b(0x31); modrmReg(rs, rd); }
void X64Emitter::cmp(Reg ra, Reg rb) { rex(false, rb, ra); b(0x39); modrmReg(rb, ra); }
void X64Emitter::addImm(Reg rd, uint32_t imm) { aluImm(0, rd, imm); }
void X64Emitter::subImm(Reg rd, uint32_t imm) { aluImm(5, rd, imm); }
void X64Emitter::cmpImm(Reg ra, uint32_t imm) { aluImm(7, ra, imm); }
void X64Emitter::addQ(Reg rd, Reg rs) { rex(true, rs, rd); b(0x01); modrmReg(rs, rd); }
void X64Emitter::addImmQ(Reg rd, uint32_t imm) { rex(true, 0, rd); b(0x81); b(static_cast<uint8_t>(0xC0 | (rd & 7))); d32(imm); }

// F7 /ext and shift group: `ext` is the ModRM.reg extension (neg=3, idiv=7; shl=4, shr=5, sar=7).
void X64Emitter::neg(Reg rd) { rex(false, 0, rd); b(0xF7); b(static_cast<uint8_t>(0xC0 | (3 << 3) | (rd & 7))); }
void X64Emitter::cdq() { b(0x99); }
void X64Emitter::idiv(Reg rs) { rex(false, 0, rs); b(0xF7); b(static_cast<uint8_t>(0xC0 | (7 << 3) | (rs & 7))); }
void X64Emitter::shlCl(Reg rd) { rex(false, 0, rd); b(0xD3); b(static_cast<uint8_t>(0xC0 | (4 << 3) | (rd & 7))); }
void X64Emitter::shrCl(Reg rd) { rex(false, 0, rd); b(0xD3); b(static_cast<uint8_t>(0xC0 | (5 << 3) | (rd & 7))); }
void X64Emitter::sarCl(Reg rd) { rex(false, 0, rd); b(0xD3); b(static_cast<uint8_t>(0xC0 | (7 << 3) | (rd & 7))); }
void X64Emitter::shlImm(Reg rd, uint8_t n) { rex(false, 0, rd); b(0xC1); b(static_cast<uint8_t>(0xC0 | (4 << 3) | (rd & 7))); b(n); }
void X64Emitter::shrImm(Reg rd, uint8_t n) { rex(false, 0, rd); b(0xC1); b(static_cast<uint8_t>(0xC0 | (5 << 3) | (rd & 7))); b(n); }
void X64Emitter::sarImm(Reg rd, uint8_t n) { rex(false, 0, rd); b(0xC1); b(static_cast<uint8_t>(0xC0 | (7 << 3) | (rd & 7))); b(n); }

// --- loads / stores ---

void X64Emitter::load(Reg rd, Reg base, int32_t disp) { rex(false, rd, base); b(0x8B); memOperand(rd, base, disp); }
void X64Emitter::store(Reg base, int32_t disp, Reg rs) { rex(false, rs, base); b(0x89); memOperand(rs, base, disp); }
void X64Emitter::loadQ(Reg rd, Reg base, int32_t disp) { rex(true, rd, base); b(0x8B); memOperand(rd, base, disp); }
void X64Emitter::storeQ(Reg base, int32_t disp, Reg rs) { rex(true, rs, base); b(0x89); memOperand(rs, base, disp); }
void X64Emitter::loadIdx(Reg rd, Reg base, Reg index, int32_t disp) { rex3(false, rd, index, base); b(0x8B); memOperandIdx(rd, base, index, disp); }
void X64Emitter::storeIdx(Reg base, Reg index, int32_t disp, Reg rs) { rex3(false, rs, index, base); b(0x89); memOperandIdx(rs, base, index, disp); }
void X64Emitter::subQ(Reg rd, Reg rs) { rex(true, rs, rd); b(0x29); modrmReg(rs, rd); }

// --- SSE scalar single-precision floats ---

// A register-direct SSE form: mandatory prefix (F3/66/none), optional REX, 0F, opcode, ModRM(mod=11).
void X64Emitter::sseRR(uint8_t prefix, uint8_t opcode, int reg, int rm) {
	if (prefix != 0) { b(prefix); }
	rex(false, reg, rm);
	b(0x0F); b(opcode);
	modrmReg(reg, rm);
}
void X64Emitter::movssLoad(Reg xd, Reg base, int32_t disp) { b(0xF3); rex(false, xd, base); b(0x0F); b(0x10); memOperand(xd, base, disp); }
void X64Emitter::movssStore(Reg base, int32_t disp, Reg xs) { b(0xF3); rex(false, xs, base); b(0x0F); b(0x11); memOperand(xs, base, disp); }
void X64Emitter::addss(Reg xd, Reg xs) { sseRR(0xF3, 0x58, xd, xs); }
void X64Emitter::subss(Reg xd, Reg xs) { sseRR(0xF3, 0x5C, xd, xs); }
void X64Emitter::mulss(Reg xd, Reg xs) { sseRR(0xF3, 0x59, xd, xs); }
void X64Emitter::divss(Reg xd, Reg xs) { sseRR(0xF3, 0x5E, xd, xs); }
void X64Emitter::ucomiss(Reg xa, Reg xb) { sseRR(0x00, 0x2E, xa, xb); }
void X64Emitter::cvttss2si(Reg rd, Reg xs) { sseRR(0xF3, 0x2C, rd, xs); }
void X64Emitter::cvtsi2ss(Reg xd, Reg rs) { sseRR(0xF3, 0x2A, xd, rs); }
void X64Emitter::movdToXmm(Reg xd, Reg rs) { sseRR(0x66, 0x6E, xd, rs); }
void X64Emitter::movdFromXmm(Reg rd, Reg xs) { sseRR(0x66, 0x7E, xs, rd); }	// 66 0F 7E /r: ModRM.reg = xmm source
void X64Emitter::roundss(Reg xd, Reg xs, uint8_t mode) { b(0x66); rex(false, xd, xs); b(0x0F); b(0x3A); b(0x0A); modrmReg(xd, xs); b(mode); }

// --- stack / control ---

void X64Emitter::push(Reg r) { rex(false, 0, r); b(static_cast<uint8_t>(0x50 | (r & 7))); }
void X64Emitter::pop(Reg r) { rex(false, 0, r); b(static_cast<uint8_t>(0x58 | (r & 7))); }
void X64Emitter::nop() { b(0x90); }
void X64Emitter::cld() { b(0xFC); }
void X64Emitter::repMovsd() { b(0xF3); b(0xA5); }
void X64Emitter::ret() { b(0xC3); }
void X64Emitter::jmp(Label target) { b(0xE9); relBranch(target.id); }
void X64Emitter::jcc(Cond cc, Label target) { b(0x0F); b(static_cast<uint8_t>(0x80 | cc)); relBranch(target.id); }
void X64Emitter::callRel(Label target) { b(0xE8); relBranch(target.id); }
void X64Emitter::callReg(Reg r) { rex(false, 0, r); b(0xFF); b(static_cast<uint8_t>(0xD0 | (r & 7))); }
void X64Emitter::leaRip(Reg rd, Label target) { rex(true, rd, 0); b(0x8D); b(static_cast<uint8_t>(((rd & 7) << 3) | 5)); relBranch(target.id); }
void X64Emitter::jmpReg(Reg r) { rex(false, 0, r); b(0xFF); b(static_cast<uint8_t>(0xE0 | (r & 7))); }

// --- labels / fixups ---

void X64Emitter::relBranch(int labelId) {
	Fixup f;
	f.site = bytes.size();
	f.labelId = labelId;
	fixups.push_back(f);
	d32(0);				// rel32 placeholder, patched by finalize()
}

Label X64Emitter::newLabel() {
	Label l;
	l.id = static_cast<int>(labelTargets.size());
	labelTargets.push_back(-1);
	return l;
}

void X64Emitter::bind(Label label) { labelTargets[label.id] = static_cast<ptrdiff_t>(bytes.size()); }

void X64Emitter::finalize() {
	for (size_t i = 0; i < fixups.size(); ++i) {
		const Fixup& f = fixups[i];
		const ptrdiff_t target = labelTargets[f.labelId];
		const int32_t rel = static_cast<int32_t>(target - static_cast<ptrdiff_t>(f.site + 4));
		const uint32_t u = static_cast<uint32_t>(rel);
		bytes[f.site + 0] = static_cast<uint8_t>(u);
		bytes[f.site + 1] = static_cast<uint8_t>(u >> 8);
		bytes[f.site + 2] = static_cast<uint8_t>(u >> 16);
		bytes[f.site + 3] = static_cast<uint8_t>(u >> 24);
	}
}


// ============================================================================================================
// JIT lowering, native dispatcher, and the JitCompiler driver (declarations in GAZLJit.h). Depends on GAZL.h and the
// X64Emitter above. The emit helpers are file-local (`static`); lowerFunction / emitDispatcher / JitCompiler::compile
// are the external surface. JitCompiler::compile calls makeExecutable(), so anything linking this file also links a
// per-platform GAZLJitMem*.cpp backend. This is the x86-64 counterpart of GAZLJitArm64.cpp's lowering; it reuses the
// verified per-instruction lowering from tools/GAZLJitX64SliceTest.cpp, re-hosted onto the JitProcessor field ABI.
//
// Register roles (SysV AMD64). Each GAZL function compiles to
//   Status function(Value* dsp, Value* memory, Value* dataStackEnd, JitProcessor* ctx)
// entered with rdi=dsp, rsi=memory, rdx=dataStackEnd, rcx=ctx, and pins them into callee-saved registers:
//   rbx=dsp (advanced by FUNC), r14=memoryBase, r15=dataStackEnd, r12=ctx.
// Scratch is rax + rcx/rdx (after the prologue reads the args) + xmm0/xmm1. GAZL->GAZL calls are ordinary C calls (the
// C stack is the call stack); one shared aligned epilogue returns the Status in eax. This is a run-to-completion
// backend — no fuel, no suspend, no RESUME transfers — so the whole program finishes inside one dispatcher entry.
// ============================================================================================================

// --- pinned registers + scratch roles ---

static const Reg DSP = RBX, MEMORY_BASE = R14, DATA_STACK_END = R15, CONTEXT = R12;
static const Reg SCRATCH_A = RCX, SCRATCH_B = RDX;					// general scratch (A also serves as the shift-count CL)
static const Reg FLOAT_0 = static_cast<Reg>(0), FLOAT_1 = static_cast<Reg>(1);	// xmm0 / xmm1 (a separate register file from GP)

// Render an integer operand into register `reg`: a frame slot (load off dsp) or an immediate constant.
static void loadOperand(X64Emitter& emitter, Reg reg, const Value& operand, bool isConst) {
	if (isConst) { emitter.movImm(reg, static_cast<uint32_t>(operand.i)); }
	else { emitter.load(reg, DSP, operand.i * 4); }
}

typedef void (X64Emitter::*BinaryOp)(Reg, Reg);

// destination = source1 <op> source2, each source a slot or a constant per its *Const flag; result flows through SCRATCH_A.
static void emitBinary(X64Emitter& emitter, BinaryOp op, const Instruction& instruction, bool source1Const, bool source2Const) {
	loadOperand(emitter, SCRATCH_A, instruction.p1, source1Const);
	loadOperand(emitter, SCRATCH_B, instruction.p2, source2Const);
	(emitter.*op)(SCRATCH_A, SCRATCH_B);
	emitter.store(DSP, instruction.p0.i * 4, SCRATCH_A);
}

// Signed division (rem=false) / modulo (rem=true). Dividend p1 -> eax, divisor p2 -> ecx. Guards match the interpreter:
// a runtime zero divisor traps DIVISION_BY_ZERO; divisor == -1 is special-cased (div -> -a, mod -> 0) to dodge the x86
// #DE on INT_MIN / -1. Result is left in eax before the store. (A const-zero divisor is an assemble-time error, so the
// zero guard is only for a variable divisor, matching arm64.)
static void emitDivMod(X64Emitter& emitter, const Instruction& instruction, bool rem, bool source1Const, bool source2Const, Label epilogue) {
	if (source1Const) { emitter.movImm(RAX, static_cast<uint32_t>(instruction.p1.i)); } else { emitter.load(RAX, DSP, instruction.p1.i * 4); }
	if (source2Const) { emitter.movImm(RCX, static_cast<uint32_t>(instruction.p2.i)); } else { emitter.load(RCX, DSP, instruction.p2.i * 4); }
	if (!source2Const) {
		emitter.cmpImm(RCX, 0);
		Label nonZero = emitter.newLabel();
		emitter.jcc(CC_NE, nonZero);
		emitter.movImm(RAX, static_cast<uint32_t>(DIVISION_BY_ZERO)); emitter.jmp(epilogue);
		emitter.bind(nonZero);
	}
	emitter.cmpImm(RCX, 0xFFFFFFFFu);								// divisor == -1 ?
	Label notMinusOne = emitter.newLabel(), done = emitter.newLabel();
	emitter.jcc(CC_NE, notMinusOne);
	if (rem) { emitter.movImm(RAX, 0); } else { emitter.neg(RAX); }
	emitter.jmp(done);
	emitter.bind(notMinusOne);
	emitter.cdq(); emitter.idiv(RCX);
	if (rem) { emitter.mov(RAX, RDX); }
	emitter.bind(done);
	emitter.store(DSP, instruction.p0.i * 4, RAX);
}

// Shift: value p1 -> eax, count p2 (const -> imm8, else slot -> cl). kind: 0 = shl, 1 = shr (logical), 2 = sar (arithmetic).
static void emitShift(X64Emitter& emitter, const Instruction& instruction, int kind, bool source1Const, bool source2Const) {
	if (source1Const) { emitter.movImm(RAX, static_cast<uint32_t>(instruction.p1.i)); } else { emitter.load(RAX, DSP, instruction.p1.i * 4); }
	if (source2Const) {
		const uint8_t count = static_cast<uint8_t>(instruction.p2.i & 31);
		if (kind == 0) { emitter.shlImm(RAX, count); } else if (kind == 1) { emitter.shrImm(RAX, count); } else { emitter.sarImm(RAX, count); }
	} else {
		emitter.load(RCX, DSP, instruction.p2.i * 4);
		if (kind == 0) { emitter.shlCl(RAX); } else if (kind == 1) { emitter.shrCl(RAX); } else { emitter.sarCl(RAX); }
	}
	emitter.store(DSP, instruction.p0.i * 4, RAX);
}

// if (a <condition> b) goto target — a = p0, b = p1, in the const modes named by the opcode; target = this index + p2.
static void emitBranch(X64Emitter& emitter, Cond condition, const Instruction& instruction, UInt instructionIndex,
		bool operand0Const, bool operand1Const, std::map<UInt, Label>& labels) {
	loadOperand(emitter, SCRATCH_A, instruction.p0, operand0Const);
	loadOperand(emitter, SCRATCH_B, instruction.p1, operand1Const);
	emitter.cmp(SCRATCH_A, SCRATCH_B);
	emitter.jcc(condition, labels[static_cast<UInt>(static_cast<Int>(instructionIndex) + instruction.p2.i)]);
}

// Render a float operand into an XMM register: a slot (movss load) or a constant (its int bits via movd).
static void loadOperandFloat(X64Emitter& emitter, Reg xmm, const Value& operand, bool isConst) {
	if (isConst) { emitter.movImm(SCRATCH_A, static_cast<uint32_t>(operand.i)); emitter.movdToXmm(xmm, SCRATCH_A); }
	else { emitter.movssLoad(xmm, DSP, operand.i * 4); }
}

// destination = source1 <fop> source2 (fop = addss/subss/mulss/divss), operands per the opcode's const modes.
static void emitBinaryFloat(X64Emitter& emitter, BinaryOp fop, const Instruction& instruction, bool source1Const, bool source2Const) {
	loadOperandFloat(emitter, FLOAT_0, instruction.p1, source1Const);
	loadOperandFloat(emitter, FLOAT_1, instruction.p2, source2Const);
	(emitter.*fop)(FLOAT_0, FLOAT_1);
	emitter.movssStore(DSP, instruction.p0.i * 4, FLOAT_0);
}

// Float compare-branch, NaN-correct versus C++: kind 0 = <, 1 = >=, 2 = ==, 3 = !=. a = p0, b = p1, target = index + p2.
static void emitBranchFloat(X64Emitter& emitter, int kind, const Instruction& instruction, UInt instructionIndex,
		bool operand0Const, bool operand1Const, std::map<UInt, Label>& labels) {
	loadOperandFloat(emitter, FLOAT_0, instruction.p0, operand0Const);
	loadOperandFloat(emitter, FLOAT_1, instruction.p1, operand1Const);
	Label target = labels[static_cast<UInt>(static_cast<Int>(instructionIndex) + instruction.p2.i)];
	if (kind == 0) { emitter.ucomiss(FLOAT_1, FLOAT_0); emitter.jcc(CC_A, target); }			// b > a ordered  == (a < b)
	else if (kind == 1) { emitter.ucomiss(FLOAT_0, FLOAT_1); emitter.jcc(CC_AE, target); }		// a >= b ordered
	else if (kind == 2) { emitter.ucomiss(FLOAT_0, FLOAT_1); Label unordered = emitter.newLabel(); emitter.jcc(CC_P, unordered); emitter.jcc(CC_E, target); emitter.bind(unordered); }
	else { emitter.ucomiss(FLOAT_0, FLOAT_1); emitter.jcc(CC_P, target); emitter.jcc(CC_NE, target); }	// unordered or not-equal
}

/*
	Lower one GAZL function (starting at `funcStart`, an OP_FUNC) into `emitter` (appended). Emits the SysV prologue that
	pins dsp/memory/dataStackEnd/ctx and runs the FUNC frame advance, then one x86-64 sequence per instruction up to the
	terminating RETU. Direct calls resolve to `entryLabels[ordinal]` via rel32 fixups; every trap and every non-OK return
	sets eax and jumps to the shared `epilogue`. Returns false on an opcode this backend cannot lower.
*/
static bool lowerFunction(X64Emitter& emitter, const Instruction* code, const Value* memory, UInt funcStart,
		const Offsets& offsets, const std::vector<Label>& entryLabels, Label epilogue, UInt functionCount) {
	UInt endIndex = funcStart;
	while (code[endIndex].opcode != OP_RETU) { ++endIndex; }

	// Pass 1 — collect branch / SWCH-case targets and allocate a label for each.
	std::map<UInt, Label> labels;
	for (UInt j = funcStart; j <= endIndex; ++j) {
		if (code[j].opcode == OP_SWCH) {							// every jump-table entry (read from const memory) is a target
			const UInt size = static_cast<UInt>(code[j].p1.i) + 1;
			const UInt table = static_cast<UInt>(code[j].p2.p - MEMORY_OFFSET);
			for (UInt k = 0; k < size; ++k) {
				const UInt target = static_cast<UInt>(static_cast<Int>(j) + memory[table + k].i);
				if (!labels.count(target)) { labels[target] = emitter.newLabel(); }
			}
			continue;
		}
		UInt target;
		if (jitBranchTarget(code, j, target) && !labels.count(target)) { labels[target] = emitter.newLabel(); }
	}

	// SysV prologue: save the callee-saved pins, keep rsp 16-aligned for inner calls, then set the pins from the args.
	emitter.push(DSP); emitter.push(MEMORY_BASE); emitter.push(DATA_STACK_END); emitter.push(CONTEXT);
	emitter.addImmQ(RSP, 0xFFFFFFF8u);							// sub rsp, 8
	emitter.movQ(DSP, RDI); emitter.movQ(MEMORY_BASE, RSI); emitter.movQ(DATA_STACK_END, RDX); emitter.movQ(CONTEXT, RCX);
	const UInt localsSize = static_cast<UInt>(code[funcStart].p0.i);
	if (localsSize != 0) { emitter.addImmQ(DSP, localsSize * 4u); }	// FUNC: advance dsp to this frame

	// Pass 2 — emit.
	for (UInt j = funcStart; j <= endIndex; ++j) {
		std::map<UInt, Label>::iterator labelIt = labels.find(j);
		if (labelIt != labels.end()) { emitter.bind(labelIt->second); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		switch (op) {
			case OP_FUNC: break;
			case OP_RETU:
				emitter.movImm(RAX, 0); emitter.jmp(epilogue);		// Status OK -> shared epilogue
				break;

			case OP_CALL_CVC: {										// direct GAZL call: callee runs with dsp += window, returns Status in eax
				const UInt callee = in.p0.p - IP_OFFSET;
				emitter.movQ(RDI, DSP);
				if (in.p1.i != 0) { emitter.addImmQ(RDI, static_cast<uint32_t>(in.p1.i) * 4u); }	// rdi = dsp + window
				emitter.movQ(RSI, MEMORY_BASE); emitter.movQ(RDX, DATA_STACK_END); emitter.movQ(RCX, CONTEXT);
				emitter.callRel(entryLabels[callee]);
				emitter.cmpImm(RAX, 0); emitter.jcc(CC_NE, epilogue);	// propagate a non-OK status
				break;
			}
			case OP_CALL_VVC: {										// indirect call: fn pointer in a slot -> ordinal -> ctx.funcEntries[ordinal]
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				emitter.load(RCX, DSP, in.p0.i * 4);				// fn pointer = IP_OFFSET + ordinal
				emitter.subImm(RCX, IP_OFFSET);						// ordinal
				emitter.cmpImm(RCX, functionCount);
				emitter.jcc(CC_AE, trap);							// (unsigned) ordinal >= functionCount -> BAD_CALL
				emitter.loadQ(RAX, CONTEXT, offsets.funcentries);	// funcEntries base
				emitter.shlImm(RCX, 3);								// ordinal * 8 (byte index)
				emitter.addQ(RAX, RCX); emitter.loadQ(RAX, RAX, 0);	// RAX = funcEntries[ordinal]
				emitter.movQ(RDI, DSP);
				if (in.p1.i != 0) { emitter.addImmQ(RDI, static_cast<uint32_t>(in.p1.i) * 4u); }
				emitter.movQ(RSI, MEMORY_BASE); emitter.movQ(RDX, DATA_STACK_END); emitter.movQ(RCX, CONTEXT);
				emitter.callReg(RAX);
				emitter.cmpImm(RAX, 0); emitter.jcc(CC_NE, epilogue);
				emitter.jmp(cont);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_CALL)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}
			case OP_CALL_NVC: {										// native call: params = dsp + window written to ctx.dsp; fn = ctx.natives[ordinal]
				const UInt ordinal = static_cast<UInt>(in.p0.i);
				emitter.movQ(RAX, DSP);
				if (in.p1.i != 0) { emitter.addImmQ(RAX, static_cast<uint32_t>(in.p1.i) * 4u); }	// rax = param window
				emitter.storeQ(CONTEXT, offsets.dsp, RAX);			// ctx.dsp = window (accessParams reads it; our dsp stays pinned in rbx)
				Label retry = emitter.newLabel();
				emitter.bind(retry);								// blocking-retry: re-issue the call until it stops returning BLOCK_RETRY
				emitter.loadQ(RAX, CONTEXT, offsets.natives);		// natives base
				emitter.loadQ(RAX, RAX, static_cast<int32_t>(ordinal * 8));	// rax = natives[ordinal]
				emitter.movQ(RDI, CONTEXT);							// the native takes Processor* (= ctx)
				emitter.callReg(RAX);
				emitter.cmpImm(RAX, static_cast<uint32_t>(BLOCK_RETRY)); emitter.jcc(CC_E, retry);
				emitter.cmpImm(RAX, 0); emitter.jcc(CC_NE, epilogue);	// any other non-OK status -> propagate
				break;
			}

			case OP_MOVE_VV: emitter.load(SCRATCH_A, DSP, in.p1.i * 4); emitter.store(DSP, in.p0.i * 4, SCRATCH_A); break;
			case OP_MOVE_VC: emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i)); emitter.store(DSP, in.p0.i * 4, SCRATCH_A); break;

			case OP_PEEK_VC: emitter.load(SCRATCH_A, MEMORY_BASE, static_cast<int32_t>((in.p1.p - MEMORY_OFFSET) * 4)); emitter.store(DSP, in.p0.i * 4, SCRATCH_A); break;
			case OP_POKE_CV: emitter.load(SCRATCH_A, DSP, in.p1.i * 4); emitter.store(MEMORY_BASE, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), SCRATCH_A); break;
			case OP_POKE_CC: emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i)); emitter.store(MEMORY_BASE, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), SCRATCH_A); break;

			// var-indexed global memory: base const (p1/p0), index var, value var/const. Bounds-checked against the
			// memory size the interpreter uses — memorySize (read) / rwMemorySize (write) — matching GAZLJitArm64.cpp.
			case OP_PEEK_VCV: {
				const int32_t base = static_cast<int32_t>(in.p1.p - MEMORY_OFFSET);
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				emitter.load(SCRATCH_A, DSP, in.p2.i * 4);			// index (ecx)
				emitter.mov(RAX, SCRATCH_A); emitter.addImm(RAX, static_cast<uint32_t>(base));	// word index = base + index
				emitter.load(SCRATCH_B, CONTEXT, offsets.memsize); emitter.cmp(RAX, SCRATCH_B); emitter.jcc(CC_AE, trap);	// >= memorySize -> BAD_PEEK
				emitter.loadIdx(RAX, MEMORY_BASE, SCRATCH_A, base * 4);
				emitter.store(DSP, in.p0.i * 4, RAX);
				emitter.jmp(cont);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_PEEK)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}
			case OP_POKE_CVV: case OP_POKE_CVC: {
				const int32_t base = static_cast<int32_t>(in.p0.p - MEMORY_OFFSET);
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				emitter.load(SCRATCH_A, DSP, in.p1.i * 4);			// index (ecx)
				emitter.mov(RAX, SCRATCH_A); emitter.addImm(RAX, static_cast<uint32_t>(base));	// word index = base + index
				emitter.load(SCRATCH_B, CONTEXT, offsets.rwmemsize); emitter.cmp(RAX, SCRATCH_B); emitter.jcc(CC_AE, trap);	// >= rwMemorySize -> BAD_POKE
				if (op == OP_POKE_CVC) { emitter.movImm(SCRATCH_B, static_cast<uint32_t>(in.p2.i)); } else { emitter.load(SCRATCH_B, DSP, in.p2.i * 4); }
				emitter.storeIdx(MEMORY_BASE, SCRATCH_A, base * 4, SCRATCH_B);
				emitter.jmp(cont);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_POKE)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}

			// local array (frame): base = dsp + C (p1 for GETL, p0 for SETL), index var. The bound is the free frame span
			// (dataStackEnd - dsp) in Value units, minus C — exactly GAZLJitArm64.cpp's GETL/SETL formula.
			case OP_GETL_VVV: {
				const int32_t frameBase = static_cast<int32_t>(in.p1.i);
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				emitter.loadQ(RAX, CONTEXT, offsets.dsend); emitter.subQ(RAX, DSP); emitter.shrImm(RAX, 2);	// (dataStackEnd - dsp) in words
				emitter.subImm(RAX, static_cast<uint32_t>(frameBase));	// limit = words - C
				emitter.load(SCRATCH_A, DSP, in.p2.i * 4); emitter.cmp(SCRATCH_A, RAX); emitter.jcc(CC_AE, trap);	// index >= limit -> BAD_PEEK
				emitter.loadIdx(RAX, DSP, SCRATCH_A, frameBase * 4);	// [dsp + index*4 + C*4]
				emitter.store(DSP, in.p0.i * 4, RAX);
				emitter.jmp(cont);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_PEEK)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}
			case OP_SETL_VVV: case OP_SETL_VVC: {
				const int32_t frameBase = static_cast<int32_t>(in.p0.i);
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				emitter.loadQ(RAX, CONTEXT, offsets.dsend); emitter.subQ(RAX, DSP); emitter.shrImm(RAX, 2);
				emitter.subImm(RAX, static_cast<uint32_t>(frameBase));	// limit = words - C
				emitter.load(SCRATCH_A, DSP, in.p1.i * 4); emitter.cmp(SCRATCH_A, RAX); emitter.jcc(CC_AE, trap);	// index >= limit -> BAD_POKE
				if (op == OP_SETL_VVC) { emitter.movImm(SCRATCH_B, static_cast<uint32_t>(in.p2.i)); } else { emitter.load(SCRATCH_B, DSP, in.p2.i * 4); }
				emitter.storeIdx(DSP, SCRATCH_A, frameBase * 4, SCRATCH_B);	// [dsp + index*4 + C*4] = value
				emitter.jmp(cont);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_POKE)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}

			// bulk copy p2 words from src (p1) to dst (p0) via rep movsd. Each pointer is a const memory address or a slot
			// holding a GAZL pointer; resolve to a byte address (memoryBase + wordIndex*4). Unchecked, as in the slice.
			case OP_COPY_VVC: case OP_COPY_VCC: case OP_COPY_CVC: case OP_COPY_CCC: {
				const bool destConst = (op == OP_COPY_CVC || op == OP_COPY_CCC);
				const bool srcConst = (op == OP_COPY_VCC || op == OP_COPY_CCC);
				if (destConst) { emitter.movQ(RDI, MEMORY_BASE); emitter.addImmQ(RDI, static_cast<uint32_t>((in.p0.p - MEMORY_OFFSET) * 4)); }
				else { emitter.load(RAX, DSP, in.p0.i * 4); emitter.subImm(RAX, MEMORY_OFFSET); emitter.shlImm(RAX, 2); emitter.movQ(RDI, MEMORY_BASE); emitter.addQ(RDI, RAX); }
				if (srcConst) { emitter.movQ(RSI, MEMORY_BASE); emitter.addImmQ(RSI, static_cast<uint32_t>((in.p1.p - MEMORY_OFFSET) * 4)); }
				else { emitter.load(RAX, DSP, in.p1.i * 4); emitter.subImm(RAX, MEMORY_OFFSET); emitter.shlImm(RAX, 2); emitter.movQ(RSI, MEMORY_BASE); emitter.addQ(RSI, RAX); }
				emitter.movImm(RCX, static_cast<uint32_t>(in.p2.i));
				emitter.cld(); emitter.repMovsd();
				break;
			}

			// address of a local p1 -> dest p0. pointer = MEMORY_OFFSET + wordIndex(dsp) + slot.
			case OP_ADRL:
				emitter.movQ(RAX, DSP); emitter.subQ(RAX, MEMORY_BASE); emitter.shrImm(RAX, 2);	// (dsp - memoryBase) / 4 = dsp word offset
				emitter.addImm(RAX, static_cast<uint32_t>(in.p1.i));		// + slot
				emitter.addImm(RAX, MEMORY_OFFSET);
				emitter.store(DSP, in.p0.i * 4, RAX); break;

			case OP_ADDI_VVV: emitBinary(emitter, &X64Emitter::add, in, false, false); break;
			case OP_ADDI_VVC: emitBinary(emitter, &X64Emitter::add, in, false, true); break;
			case OP_SUBI_VVV: emitBinary(emitter, &X64Emitter::sub, in, false, false); break;
			case OP_SUBI_VVC: emitBinary(emitter, &X64Emitter::sub, in, false, true); break;
			case OP_SUBI_VCV: emitBinary(emitter, &X64Emitter::sub, in, true, false); break;
			case OP_MULI_VVV: emitBinary(emitter, &X64Emitter::imul, in, false, false); break;
			case OP_MULI_VVC: emitBinary(emitter, &X64Emitter::imul, in, false, true); break;
			case OP_ANDI_VVV: emitBinary(emitter, &X64Emitter::and_, in, false, false); break;
			case OP_ANDI_VVC: emitBinary(emitter, &X64Emitter::and_, in, false, true); break;
			case OP_IORI_VVV: emitBinary(emitter, &X64Emitter::or_, in, false, false); break;
			case OP_IORI_VVC: emitBinary(emitter, &X64Emitter::or_, in, false, true); break;
			case OP_XORI_VVV: emitBinary(emitter, &X64Emitter::xor_, in, false, false); break;
			case OP_XORI_VVC: emitBinary(emitter, &X64Emitter::xor_, in, false, true); break;

			case OP_DIVI_VVV: emitDivMod(emitter, in, false, false, false, epilogue); break;
			case OP_DIVI_VVC: emitDivMod(emitter, in, false, false, true, epilogue); break;
			case OP_DIVI_VCV: emitDivMod(emitter, in, false, true, false, epilogue); break;
			case OP_MODI_VVV: emitDivMod(emitter, in, true, false, false, epilogue); break;
			case OP_MODI_VVC: emitDivMod(emitter, in, true, false, true, epilogue); break;
			case OP_MODI_VCV: emitDivMod(emitter, in, true, true, false, epilogue); break;
			case OP_SHLI_VVV: emitShift(emitter, in, 0, false, false); break;
			case OP_SHLI_VVC: emitShift(emitter, in, 0, false, true); break;
			case OP_SHLI_VCV: emitShift(emitter, in, 0, true, false); break;
			case OP_SHRI_VVV: emitShift(emitter, in, 2, false, false); break;
			case OP_SHRI_VVC: emitShift(emitter, in, 2, false, true); break;
			case OP_SHRI_VCV: emitShift(emitter, in, 2, true, false); break;
			case OP_SHRU_VVV: emitShift(emitter, in, 1, false, false); break;
			case OP_SHRU_VVC: emitShift(emitter, in, 1, false, true); break;
			case OP_SHRU_VCV: emitShift(emitter, in, 1, true, false); break;
			case OP_ABSI:
				emitter.load(RAX, DSP, in.p1.i * 4);				// |x| = (x ^ (x >> 31)) - (x >> 31)
				emitter.mov(RDX, RAX); emitter.sarImm(RDX, 31); emitter.xor_(RAX, RDX); emitter.sub(RAX, RDX);
				emitter.store(DSP, in.p0.i * 4, RAX);
				break;

			case OP_FORi_VVB: case OP_FORi_VCB:
				emitter.load(SCRATCH_A, DSP, in.p0.i * 4); emitter.addImm(SCRATCH_A, 1); emitter.store(DSP, in.p0.i * 4, SCRATCH_A);
				loadOperand(emitter, SCRATCH_B, in.p1, op == OP_FORi_VCB);
				emitter.cmp(SCRATCH_A, SCRATCH_B);
				emitter.jcc(CC_L, labels[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;

			case OP_ADDF_VVV: emitBinaryFloat(emitter, &X64Emitter::addss, in, false, false); break;
			case OP_ADDF_VVC: emitBinaryFloat(emitter, &X64Emitter::addss, in, false, true); break;
			case OP_SUBF_VVV: emitBinaryFloat(emitter, &X64Emitter::subss, in, false, false); break;
			case OP_SUBF_VVC: emitBinaryFloat(emitter, &X64Emitter::subss, in, false, true); break;
			case OP_SUBF_VCV: emitBinaryFloat(emitter, &X64Emitter::subss, in, true, false); break;
			case OP_MULF_VVV: emitBinaryFloat(emitter, &X64Emitter::mulss, in, false, false); break;
			case OP_MULF_VVC: emitBinaryFloat(emitter, &X64Emitter::mulss, in, false, true); break;
			case OP_DIVF_VVV: emitBinaryFloat(emitter, &X64Emitter::divss, in, false, false); break;
			case OP_DIVF_VVC: emitBinaryFloat(emitter, &X64Emitter::divss, in, false, true); break;
			case OP_DIVF_VCV: emitBinaryFloat(emitter, &X64Emitter::divss, in, true, false); break;
			// FTOI / ITOF carry a scale constant (p2): FTOI = (int)(src * scale) with the interpreter's saturation;
			// ITOF = (float)src * scale.
			case OP_FTOI_VVC: {
				emitter.movssLoad(FLOAT_0, DSP, in.p1.i * 4);
				emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p2.i)); emitter.movdToXmm(FLOAT_1, SCRATCH_A); emitter.mulss(FLOAT_0, FLOAT_1);	// * scale
				emitter.cvttss2si(SCRATCH_A, FLOAT_0);				// x86 yields 0x80000000 for overflow / inf / NaN
				emitter.cmpImm(SCRATCH_A, 0x80000000u);
				Label ftoiDone = emitter.newLabel();
				emitter.jcc(CC_NE, ftoiDone);						// in range -> done; else saturate like the interpreter's ftoi()
				emitter.ucomiss(FLOAT_0, FLOAT_0);					// NaN sets PF
				Label ftoiNotNan = emitter.newLabel();
				emitter.jcc(CC_NP, ftoiNotNan);
				emitter.movImm(SCRATCH_A, 0); emitter.jmp(ftoiDone);	// NaN -> 0
				emitter.bind(ftoiNotNan);
				emitter.movdFromXmm(SCRATCH_B, FLOAT_0); emitter.cmpImm(SCRATCH_B, 0);	// sign bit: signed >= 0 means positive / +inf
				Label ftoiNegative = emitter.newLabel();
				emitter.jcc(CC_L, ftoiNegative);					// negative / -inf -> keep INT_MIN
				emitter.movImm(SCRATCH_A, 0x7FFFFFFFu);				// positive / +inf -> INT_MAX
				emitter.bind(ftoiNegative); emitter.bind(ftoiDone);
				emitter.store(DSP, in.p0.i * 4, SCRATCH_A); break;
			}
			case OP_ITOF_VVC:
				emitter.load(SCRATCH_A, DSP, in.p1.i * 4); emitter.cvtsi2ss(FLOAT_0, SCRATCH_A);
				emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p2.i)); emitter.movdToXmm(FLOAT_1, SCRATCH_A); emitter.mulss(FLOAT_0, FLOAT_1);
				emitter.movssStore(DSP, in.p0.i * 4, FLOAT_0); break;
			case OP_ABSF: emitter.load(SCRATCH_A, DSP, in.p1.i * 4); emitter.movImm(SCRATCH_B, 0x7FFFFFFFu); emitter.and_(SCRATCH_A, SCRATCH_B); emitter.store(DSP, in.p0.i * 4, SCRATCH_A); break;
			case OP_FLOF: emitter.movssLoad(FLOAT_0, DSP, in.p1.i * 4); emitter.roundss(FLOAT_1, FLOAT_0, 1); emitter.movssStore(DSP, in.p0.i * 4, FLOAT_1); break;

			case OP_LSSF_VVB: emitBranchFloat(emitter, 0, in, j, false, false, labels); break;
			case OP_LSSF_VCB: emitBranchFloat(emitter, 0, in, j, false, true, labels); break;
			case OP_LSSF_CVB: emitBranchFloat(emitter, 0, in, j, true, false, labels); break;
			case OP_EQUF_VVB: emitBranchFloat(emitter, 2, in, j, false, false, labels); break;
			case OP_EQUF_VCB: emitBranchFloat(emitter, 2, in, j, false, true, labels); break;
			case OP_NLSF_VVB: emitBranchFloat(emitter, 1, in, j, false, false, labels); break;
			case OP_NLSF_VCB: emitBranchFloat(emitter, 1, in, j, false, true, labels); break;
			case OP_NLSF_CVB: emitBranchFloat(emitter, 1, in, j, true, false, labels); break;
			case OP_NEQF_VVB: emitBranchFloat(emitter, 3, in, j, false, false, labels); break;
			case OP_NEQF_VCB: emitBranchFloat(emitter, 3, in, j, false, true, labels); break;

			case OP_GOTO: emitter.jmp(labels[static_cast<UInt>(static_cast<Int>(j) + in.p0.i)]); break;

			case OP_SWCH: {											// index = min(unsigned(V0), C1); jump into a table of `jmp case`
				const UInt size = static_cast<UInt>(in.p1.i) + 1;
				const UInt table = static_cast<UInt>(in.p2.p - MEMORY_OFFSET);
				emitter.load(SCRATCH_A, DSP, in.p0.i * 4);
				emitter.cmpImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i));
				Label keep = emitter.newLabel();
				emitter.jcc(CC_BE, keep);							// (unsigned) val <= C1 -> keep; else clamp
				emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i));
				emitter.bind(keep);
				emitter.mov(SCRATCH_B, SCRATCH_A); emitter.shlImm(SCRATCH_B, 2); emitter.add(SCRATCH_B, SCRATCH_A);	// index * 5 (each table jmp is 5 bytes)
				Label tableBase = emitter.newLabel();
				emitter.leaRip(RAX, tableBase); emitter.addQ(RAX, SCRATCH_B); emitter.jmpReg(RAX);
				emitter.bind(tableBase);
				for (UInt k = 0; k < size; ++k) {
					const UInt target = static_cast<UInt>(static_cast<Int>(j) + memory[table + k].i);
					emitter.jmp(labels[target]);
				}
				break;
			}

			case OP_LSSI_VVB: emitBranch(emitter, CC_L, in, j, false, false, labels); break;
			case OP_LSSI_VCB: emitBranch(emitter, CC_L, in, j, false, true, labels); break;
			case OP_LSSI_CVB: emitBranch(emitter, CC_L, in, j, true, false, labels); break;
			case OP_EQUI_VVB: emitBranch(emitter, CC_E, in, j, false, false, labels); break;
			case OP_EQUI_VCB: emitBranch(emitter, CC_E, in, j, false, true, labels); break;
			case OP_NLSI_VVB: emitBranch(emitter, CC_GE, in, j, false, false, labels); break;
			case OP_NLSI_VCB: emitBranch(emitter, CC_GE, in, j, false, true, labels); break;
			case OP_NLSI_CVB: emitBranch(emitter, CC_GE, in, j, true, false, labels); break;
			case OP_NEQI_VVB: emitBranch(emitter, CC_NE, in, j, false, false, labels); break;
			case OP_NEQI_VCB: emitBranch(emitter, CC_NE, in, j, false, true, labels); break;

			default: return false;									// unsupported opcode -> caller falls back to the interpreter
		}
	}
	return true;
}

/*
	Emit the native dispatcher trampoline: `int dispatch(JitProcessor* ctx)`. Unlike the arm64 dispatcher (which threads
	TRANSFER / NATIVE_CALL segments), the x86-64 backend runs GAZL calls as real C calls, so a whole program completes
	inside a single compiled entry. The trampoline just loads the ctx run-state fields into the SysV argument registers
	and jumps to `ctx.resume` (the entry seeded by JitProcessor::enterCall). Returns its byte offset in the buffer.
*/
static size_t emitDispatcher(X64Emitter& emitter, const Offsets& offsets) {
	const size_t entry = emitter.size();
	// rdi = ctx on entry. Read the ctx fields BEFORE overwriting rdi (dsp is loaded last).
	emitter.movQ(RCX, RDI);										// rcx = ctx
	emitter.loadQ(RAX, RDI, offsets.resume);					// rax = resume (the entry to jump into)
	emitter.loadQ(RSI, RDI, offsets.mb);						// rsi = memoryBase
	emitter.loadQ(RDX, RDI, offsets.dsend);						// rdx = dataStackEnd
	emitter.loadQ(RDI, RDI, offsets.dsp);						// rdi = dsp (overwrites the ctx base last)
	emitter.addImmQ(RSP, 0xFFFFFFF8u);							// sub rsp, 8 — 16-align before the call
	emitter.callReg(RAX);
	emitter.addImmQ(RSP, 8u);
	emitter.ret();												// eax = Status
	return entry;
}

/*
	JitCompiler::compile — lowers a whole finalized program to x86-64 machine code and fills a JitModule (the executable
	page's dispatcher + ordinal->entry table, which the module then owns). Mirrors GAZLJitArm64.cpp's driver: lower every
	function into one buffer, append one shared epilogue, append the dispatcher, publish via makeExecutable(), and record
	the ordinal->entry table. Leaves `out` empty (ok() == false) on any opcode the backend cannot lower, so the caller
	falls back to the interpreter.
*/
void JitCompiler::compile(const Instruction* code, UInt functionCount, const UInt* functionTable,
		const Value* memory, JitModule& out) {
	const Offsets offsets = JitProcessor::layout();				// the run-state ABI, obtained without an engine
	X64Emitter emitter;
	std::vector<Label> entryLabels(functionCount);
	for (UInt k = 0; k < functionCount; ++k) { entryLabels[k] = emitter.newLabel(); }
	Label epilogue = emitter.newLabel();

	for (UInt ordinal = 0; ordinal < functionCount; ++ordinal) {
		emitter.bind(entryLabels[ordinal]);
		if (!lowerFunction(emitter, code, memory, functionTable[ordinal], offsets, entryLabels, epilogue, functionCount)) {
			return;												// unsupported opcode -> caller should fall back to the interpreter
		}
	}
	// One shared aligned epilogue (all functions have the identical prologue): undo the alignment pad, restore the pins.
	emitter.bind(epilogue);
	emitter.addImmQ(RSP, 8u);
	emitter.pop(CONTEXT); emitter.pop(DATA_STACK_END); emitter.pop(MEMORY_BASE); emitter.pop(DSP);
	emitter.ret();

	const size_t dispatcherOffset = emitDispatcher(emitter, offsets);
	emitter.finalize();

	// x86-64 code is a byte stream; makeExecutable() works in 32-bit words, so round up. Entry/dispatch offsets are
	// already byte offsets, so hand them to the shared publish step directly.
	const size_t wordCount = (emitter.size() + 3) / 4;
	std::vector<size_t> entryByteOffsets(functionCount);
	for (UInt ordinal = 0; ordinal < functionCount; ++ordinal) {
		entryByteOffsets[ordinal] = static_cast<size_t>(emitter.labelOffset(entryLabels[ordinal]));
	}
	publishModule(out, reinterpret_cast<const uint32_t*>(emitter.code()), wordCount
			, entryByteOffsets.empty() ? 0 : &entryByteOffsets[0], functionCount, dispatcherOffset);
}

} // namespace GAZL
