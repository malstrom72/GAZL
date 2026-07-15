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
#include "GAZLJitMem.h"			// makeExecutable() - platform-specific backend, architecture-neutral

#include <cstddef>
#include <cstring>			// std::memcpy - pack the emitted byte stream into 32-bit words
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

// 81 /ext id  - immediate ALU op (ext = 0 add, 5 sub, 7 cmp, ...).
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
void X64Emitter::cmpQ(Reg ra, Reg rb) { rex(true, rb, ra); b(0x39); modrmReg(rb, ra); }
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
// Execution model: §5.4 dispatcher/TRANSFER, identical to GAZLJitArm64.cpp (and interoperating with the shared
// JitProcessor / ipStack). Each GAZL function is a `Status segment(JitProcessor* ctx)` - ctx in ARG_0, no frame of its
// own. It reloads the pins from ctx (rbx=dsp, r14=memoryBase, r13=fuel, r15=ipsp; dataStackEnd on demand; r12=ctx),
// runs, and hands control back by returning a Status: TRANSFER (the dispatcher threads the next segment - GAZL calls and
// returns push/pop the ipStack, never a host frame), NATIVE_CALL (the dispatcher makes the one host call), or a terminal
// OK / TIME_OUT / trap. Because the C stack is only ever the dispatcher's single frame, a fuel timeout can suspend and
// resume from ANY point, including inside nested GAZL calls. Scratch is rax + rcx/rdx + xmm0/xmm1. The pins are
// callee-saved and the scratch caller-saved on both SysV AMD64 and Win64, so the file is ABI-neutral except the
// dispatcher's frame, which flows through the ARG_0 / CALL_FRAME constants below.
// ============================================================================================================

// --- pinned registers + scratch roles ---

/*
	§5.4 dispatcher/TRANSFER model - the per-segment pins mirror arm64's (x1=dsp, x2=membase, w3=fuel, x4=ipsp, x0=ctx).
	dataStackEnd is NOT pinned (only bounds checks want it) - it is loaded from ctx on demand. Every segment reloads these
	from ctx at entry and writes them back before any TRANSFER, so the C stack stays a single frame (the dispatcher) and a
	timeout/suspend can return to the host and resume from any point - including inside nested GAZL calls.
*/
static const Reg DSP = RBX, MEMORY_BASE = R14, FUEL = R13, IP_STACK_PTR = R15, CONTEXT = R12;
static const Reg SCRATCH_A = RCX, SCRATCH_B = RDX;					// general scratch (A also serves as the shift-count CL)
static const Reg FLOAT_0 = static_cast<Reg>(0), FLOAT_1 = static_cast<Reg>(1);	// xmm0 / xmm1 (a separate register file from GP)

/*
	Calling convention. In the dispatcher/TRANSFER model only the dispatcher makes host-ABI calls (into each segment and
	into natives); segments are leaf code entered as `Status segment(JitProcessor* ctx)` with ctx in ARG_0 and never call
	out, so they need no frame of their own. ARG_0 is the one arg register that differs by ABI. CALL_FRAME is the stack the
	*dispatcher* reserves for its calls: SysV needs only the 16-align pad; Win64 also reserves the 32-byte shadow space a
	callee may spill its register args into. The pins (rbx/r12/r13/r14/r15) are callee-saved on both ABIs and the dispatcher
	saves them once; rbp holds ctx across the loop. Win64's callee-saved rsi/rdi bite only in the rep-movsd COPY (guarded
	there); xmm6-15 are untouched (segments use xmm0/1).
*/
#if defined(_WIN32)
static const Reg ARG_0 = RCX;
static const uint32_t CALL_FRAME = 40u;								// 32-byte shadow space + 8-byte align pad
#else
static const Reg ARG_0 = RDI;
static const uint32_t CALL_FRAME = 8u;								// just the 16-align pad; SysV has no shadow space
#endif

/*
	Segment state lives in ctx between transfers. reloadState loads the pins from ctx (ctx must already be in CONTEXT);
	enterSegment is a fresh dispatcher entry (ctx arrives in ARG_0); writebackState flushes before a TRANSFER. Mirrors the
	arm64 reloadState/writebackState. dataStackEnd is loaded on demand, not pinned.
*/
static void reloadState(X64Emitter& e, const Offsets& o) {
	e.loadQ(DSP, CONTEXT, o.dsp); e.loadQ(MEMORY_BASE, CONTEXT, o.mb);
	e.load(FUEL, CONTEXT, o.fuel); e.loadQ(IP_STACK_PTR, CONTEXT, o.ipsp);
}
static void enterSegment(X64Emitter& e, const Offsets& o) { e.movQ(CONTEXT, ARG_0); reloadState(e, o); }
static void writebackState(X64Emitter& e, const Offsets& o) {
	e.storeQ(CONTEXT, o.dsp, DSP); e.store(CONTEXT, o.fuel, FUEL); e.storeQ(CONTEXT, o.ipsp, IP_STACK_PTR);
}

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

/*
	Signed division (rem=false) / modulo (rem=true). Dividend p1 -> eax, divisor p2 -> ecx. Guards match the interpreter:
	a runtime zero divisor traps DIVISION_BY_ZERO; divisor == -1 is special-cased (div -> -a, mod -> 0) to dodge the x86
	#DE on INT_MIN / -1. Result is left in eax before the store. (A const-zero divisor is an assemble-time error, so the
	zero guard is only for a variable divisor, matching arm64.)
*/
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

// if (a <condition> b) goto target - a = p0, b = p1, in the const modes named by the opcode; target = this index + p2.
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
	Lower one GAZL function (starting at `funcStart`, an OP_FUNC) into `emitter` (appended) as §5.4 segments (encoding b).
	The dispatcher supplies live pins, so the entry just advances dsp by the frame and runs the FUNC stack-overflow check;
	then one x86-64 sequence per instruction to the terminating RETU. Every block leader gets a fuel-check safepoint
	(suspend → cold stub). GAZL calls/RETU tail-branch directly (the ipStack holds the return address, §5.7.5); native
	calls run inline; every terminal path sets eax and jumps to the shared `epilogue`. Returns false on an unlowerable opcode.
*/
void JitCompilerX64::lowerFunction(X64Emitter& emitter, const Instruction* code, const Value* memory, UInt funcStart,
		const Offsets& offsets, const std::vector<Label>& entryLabels, Label epilogue, UInt functionCount) {
	UInt endIndex = funcStart;
	while (code[endIndex].opcode != OP_RETU) { ++endIndex; }

	/*
		Pass 1 - fuel safepoints: every basic-block leader (arch-neutral, §5.5), each charged its block weight and each a
		resumable point. `labels` is the mainline label per leader (hot entry + branch + resume target); suspendL its
		suspend stub. Charging per block (not just loop heads) keeps fuel spend ≈ the interpreter's.
	*/
	std::map<UInt, UInt> loopWeight;
	jitFuelSafepoints(code, funcStart, endIndex, memory, loopWeight);
	std::map<UInt, Label> labels, suspendL;
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		labels[it->first] = emitter.newLabel(); suspendL[it->first] = emitter.newLabel();
	}

	/*
		Function entry: the dispatcher supplies live pins (encoding b - no per-segment reload), so just advance dsp by the
		frame (FUNC p0) and run the FUNC stack-overflow check.
	*/
	const UInt localsSize = static_cast<UInt>(code[funcStart].p0.i);
	if (localsSize != 0) { emitter.addImmQ(DSP, localsSize * 4u); }		// dsp += frame
	{
		const UInt paramsSize = static_cast<UInt>(code[funcStart].p1.i);
		Label stackOk = emitter.newLabel();
		emitter.movQ(RAX, DSP); if (paramsSize != 0) { emitter.addImmQ(RAX, paramsSize * 4u); }
		emitter.loadQ(SCRATCH_A, CONTEXT, offsets.dsend); emitter.cmpQ(RAX, SCRATCH_A); emitter.jcc(CC_BE, stackOk);
		emitter.movImm(RAX, static_cast<uint32_t>(DATA_STACK_OVERFLOW)); emitter.jmp(epilogue);	// dsp + params > dsend
		emitter.bind(stackOk);
	}

	// Pass 2 - emit.
	for (UInt j = funcStart; j <= endIndex; ++j) {
		std::map<UInt, Label>::iterator labelIt = labels.find(j);
		if (labelIt != labels.end()) { emitter.bind(labelIt->second); }
		std::map<UInt, UInt>::iterator weightIt = loopWeight.find(j);	// loop head: charge the block, suspend on timeout (§5.5)
		if (weightIt != loopWeight.end()) { emitter.subImm(FUEL, weightIt->second); emitter.jcc(CC_S, suspendL[j]); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		switch (op) {
			case OP_FUNC: break;
			case OP_RETU: {										// pop the ipStack; tail-branch back to the caller, or OK at the native/top marker
				Label notNative = emitter.newLabel();
				emitter.addImmQ(IP_STACK_PTR, 0u - 16u);				// ipsp -= 16 : pop {cont, dsp}
				emitter.loadQ(RAX, IP_STACK_PTR, 0);					// cont (caller continuation address)
				emitter.loadQ(DSP, IP_STACK_PTR, 8);					// caller dsp (0 = native/top marker)
				emitter.movImm(SCRATCH_A, 0); emitter.cmpQ(DSP, SCRATCH_A); emitter.jcc(CC_NE, notNative);
				emitter.addImmQ(IP_STACK_PTR, 0u - 16u); emitter.loadQ(DSP, IP_STACK_PTR, 8);	// native return: pop again for the true dsp
				emitter.movImm(RAX, 0); emitter.jmp(epilogue);			// OK - terminal (return to host); pins stay live in regs
				emitter.bind(notNative);
				emitter.jmpReg(RAX);									// GAZL return: jump straight into the caller's continuation
				break;
			}

			case OP_CALL_CVC: {										// direct GAZL call: push {after, dsp}, tail-branch into the callee entry
				const UInt callee = in.p0.p - IP_OFFSET;
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = emitter.newLabel(), ipOk = emitter.newLabel();
				emitter.loadQ(RAX, CONTEXT, offsets.ipsend); emitter.cmpQ(IP_STACK_PTR, RAX); emitter.jcc(CC_B, ipOk);
				emitter.movImm(RAX, static_cast<uint32_t>(IP_STACK_OVERFLOW)); emitter.jmp(epilogue); emitter.bind(ipOk);
				emitter.leaRip(RAX, after); emitter.storeQ(IP_STACK_PTR, 0, RAX); emitter.storeQ(IP_STACK_PTR, 8, DSP); emitter.addImmQ(IP_STACK_PTR, 16);
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }	// dsp += arg window
				emitter.jmp(entryLabels[callee]);						// direct tail-branch into the callee (state stays live)
				emitter.bind(after);									// callee's RETU lands here; pins already live
				break;
			}
			case OP_CALL_VVC: {										// indirect: slot -> ordinal -> ctx.funcEntries[ordinal], tail-branch
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = emitter.newLabel(), ipOk = emitter.newLabel(), trap = emitter.newLabel();
				emitter.loadQ(RAX, CONTEXT, offsets.ipsend); emitter.cmpQ(IP_STACK_PTR, RAX); emitter.jcc(CC_B, ipOk);
				emitter.movImm(RAX, static_cast<uint32_t>(IP_STACK_OVERFLOW)); emitter.jmp(epilogue); emitter.bind(ipOk);
				emitter.load(RCX, DSP, in.p0.i * 4); emitter.subImm(RCX, IP_OFFSET);	// ordinal
				emitter.cmpImm(RCX, functionCount); emitter.jcc(CC_AE, trap);		// >= functionCount -> BAD_CALL
				emitter.loadQ(RAX, CONTEXT, offsets.funcentries); emitter.shlImm(RCX, 3); emitter.addQ(RAX, RCX); emitter.loadQ(RDX, RAX, 0);	// rdx = funcEntries[ordinal]
				emitter.leaRip(RAX, after); emitter.storeQ(IP_STACK_PTR, 0, RAX); emitter.storeQ(IP_STACK_PTR, 8, DSP); emitter.addImmQ(IP_STACK_PTR, 16);
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }
				emitter.jmpReg(RDX);									// indirect tail-branch into the callee segment
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_CALL)); emitter.jmp(epilogue);
				emitter.bind(after);
				break;
			}
			case OP_CALL_NVC: {										// native: publish window/fuel/ipsp, call the native inline (no dispatcher round-trip)
				const UInt ordinal = static_cast<UInt>(in.p0.i);
				const UInt window = static_cast<UInt>(in.p1.i);
				Label hot = emitter.newLabel(), okStatus = emitter.newLabel();
				emitter.bind(hot);										// hot re-entry (blocking-retry / suspend re-issue target)
				emitter.storeQ(CONTEXT, offsets.saveddsp, DSP);			// stash original dsp (restored after the call)
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }
				emitter.storeQ(CONTEXT, offsets.dsp, DSP); emitter.store(CONTEXT, offsets.fuel, FUEL); emitter.storeQ(CONTEXT, offsets.ipsp, IP_STACK_PTR);
				emitter.leaRip(RAX, hot); emitter.storeQ(CONTEXT, offsets.resume, RAX);	// RESUME = call site (blocking retry / suspend re-issue)
				emitter.loadQ(RAX, CONTEXT, offsets.natives); emitter.loadQ(RAX, RAX, static_cast<int32_t>(ordinal * 8));	// rax = natives[ordinal]
				emitter.movQ(ARG_0, CONTEXT); emitter.callReg(RAX);		// native(ctx); eax = status (pins are callee-saved -> preserved)
				emitter.load(FUEL, CONTEXT, offsets.fuel);				// native may resetTimeOut(0): refresh the fuel pin from ctx
				emitter.loadQ(DSP, CONTEXT, offsets.saveddsp);			// restore original dsp (pop the arg window)
				emitter.cmpImm(RAX, 0); emitter.jcc(CC_E, okStatus);	// OK -> fall through and continue
				emitter.jmp(epilogue);									// nonzero (retry / suspend / trap): return to host, eax = status, RESUME = hot
				emitter.bind(okStatus);
				break;
			}

			case OP_MOVE_VV: emitter.load(SCRATCH_A, DSP, in.p1.i * 4); emitter.store(DSP, in.p0.i * 4, SCRATCH_A); break;
			case OP_MOVE_VC: emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i)); emitter.store(DSP, in.p0.i * 4, SCRATCH_A); break;

			case OP_PEEK_VC: emitter.load(SCRATCH_A, MEMORY_BASE, static_cast<int32_t>((in.p1.p - MEMORY_OFFSET) * 4)); emitter.store(DSP, in.p0.i * 4, SCRATCH_A); break;
			case OP_POKE_CV: emitter.load(SCRATCH_A, DSP, in.p1.i * 4); emitter.store(MEMORY_BASE, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), SCRATCH_A); break;
			case OP_POKE_CC: emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i)); emitter.store(MEMORY_BASE, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), SCRATCH_A); break;

			/*
				var-indexed global memory: base const (p1/p0), index var, value var/const. Bounds-checked against the
				memory size the interpreter uses - memorySize (read) / rwMemorySize (write) - matching GAZLJitArm64.cpp.
			*/
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

			// full pointer deref: base(var) + index(var) = a GAZL pointer; wordIndex = base + index - MEMORY_OFFSET, checked.
			case OP_PEEK_VVV: {
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				emitter.load(SCRATCH_A, DSP, in.p1.i * 4); emitter.load(RAX, DSP, in.p2.i * 4); emitter.add(SCRATCH_A, RAX);	// base + index
				emitter.subImm(SCRATCH_A, MEMORY_OFFSET);			// word index (ecx)
				emitter.load(SCRATCH_B, CONTEXT, offsets.memsize); emitter.cmp(SCRATCH_A, SCRATCH_B); emitter.jcc(CC_AE, trap);	// >= memorySize -> BAD_PEEK
				emitter.loadIdx(RAX, MEMORY_BASE, SCRATCH_A, 0); emitter.store(DSP, in.p0.i * 4, RAX);
				emitter.jmp(cont); emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_PEEK)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}
			case OP_POKE_VVV: case OP_POKE_VVC: {					// base(var) + index(var) = pointer; value var (VVV) or const (VVC)
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				emitter.load(SCRATCH_A, DSP, in.p0.i * 4); emitter.load(RAX, DSP, in.p1.i * 4); emitter.add(SCRATCH_A, RAX);	// base + index
				emitter.subImm(SCRATCH_A, MEMORY_OFFSET);
				emitter.load(SCRATCH_B, CONTEXT, offsets.rwmemsize); emitter.cmp(SCRATCH_A, SCRATCH_B); emitter.jcc(CC_AE, trap);	// >= rwMemorySize -> BAD_POKE
				if (op == OP_POKE_VVC) { emitter.movImm(SCRATCH_B, static_cast<uint32_t>(in.p2.i)); } else { emitter.load(SCRATCH_B, DSP, in.p2.i * 4); }
				emitter.storeIdx(MEMORY_BASE, SCRATCH_A, 0, SCRATCH_B);
				emitter.jmp(cont); emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_POKE)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}

			/*
				local array (frame): base = dsp + C (p1 for GETL, p0 for SETL), index var. The bound is the free frame span
				(dataStackEnd - dsp) in Value units, minus C - exactly GAZLJitArm64.cpp's GETL/SETL formula.
			*/
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

			/*
				bulk copy p2 words from src (p1) to dst (p0) via rep movsd. Each pointer is a const memory address or a slot
				holding a GAZL pointer; resolve to a byte address (memoryBase + wordIndex*4). Unchecked, as in the slice.
			*/
			case OP_COPY_VVC: case OP_COPY_VCC: case OP_COPY_CVC: case OP_COPY_CCC: {
				const bool destConst = (op == OP_COPY_CVC || op == OP_COPY_CCC);
				const bool srcConst = (op == OP_COPY_VCC || op == OP_COPY_CCC);
			#if defined(_WIN32)
				emitter.push(RSI); emitter.push(RDI);				// Win64: rsi/rdi are callee-saved; rep movsd clobbers them
			#endif
				if (destConst) { emitter.movQ(RDI, MEMORY_BASE); emitter.addImmQ(RDI, static_cast<uint32_t>((in.p0.p - MEMORY_OFFSET) * 4)); }
				else { emitter.load(RAX, DSP, in.p0.i * 4); emitter.subImm(RAX, MEMORY_OFFSET); emitter.shlImm(RAX, 2); emitter.movQ(RDI, MEMORY_BASE); emitter.addQ(RDI, RAX); }
				if (srcConst) { emitter.movQ(RSI, MEMORY_BASE); emitter.addImmQ(RSI, static_cast<uint32_t>((in.p1.p - MEMORY_OFFSET) * 4)); }
				else { emitter.load(RAX, DSP, in.p1.i * 4); emitter.subImm(RAX, MEMORY_OFFSET); emitter.shlImm(RAX, 2); emitter.movQ(RSI, MEMORY_BASE); emitter.addQ(RSI, RAX); }
				emitter.movImm(RCX, static_cast<uint32_t>(in.p2.i));
				emitter.cld(); emitter.repMovsd();
			#if defined(_WIN32)
				emitter.pop(RDI); emitter.pop(RSI);
			#endif
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
			/*
				FTOI / ITOF carry a scale constant (p2): FTOI = (int)(src * scale) with the interpreter's saturation;
				ITOF = (float)src * scale.
			*/
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

			default: throwUnlowerableOpcode(op);					// a finalized opcode the backend must cover (a bug, never routine)
		}
	}
	/*
		Cold section: one suspend stub per block leader - writeback the pins to ctx, set RESUME = this block's mainline head
		(the dispatcher reloads centrally on resume), return TIME_OUT to the host. Unreachable by fall-through - each
		preceding block ends in jmp. Mirrors the arm64 cold section.
	*/
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		const UInt head = it->first;
		emitter.bind(suspendL[head]);
		writebackState(emitter, offsets);
		emitter.leaRip(RAX, labels[head]); emitter.storeQ(CONTEXT, offsets.resume, RAX);		// RESUME = this block's mainline head
		emitter.movImm(RAX, static_cast<uint32_t>(TIME_OUT)); emitter.jmp(epilogue);
	}
}

/*
	Emit the native dispatcher (§5.4, encoding b): `int dispatch(JitProcessor* ctx)`. It owns the single native frame:
	save the callee-saved pins once, reserve CALL_FRAME (so a segment's inline native call is aligned + has Win64 shadow),
	pin ctx, reload the pins from ctx, then tail-jump into RESUME. Segments thread among themselves by direct jmp with no
	host round-trip; a segment returns here (to `epilogue`, in eax) only to suspend (TIME_OUT), finish (OK), or trap.
	Mirrors the arm64 dispatcher.
*/
static size_t emitDispatcher(X64Emitter& emitter, const Offsets& offsets, Label epilogue) {
	const size_t entry = emitter.size();
	emitter.push(DSP); emitter.push(RBP); emitter.push(CONTEXT); emitter.push(FUEL); emitter.push(MEMORY_BASE); emitter.push(IP_STACK_PTR);	// 6 pushes (even -> keeps rsp alignment)
	emitter.addImmQ(RSP, 0u - CALL_FRAME);						// dispatcher's call frame (16-align pad + Win64 shadow)
	enterSegment(emitter, offsets);								// ctx = arg0; reload the pins from ctx
	emitter.loadQ(RAX, CONTEXT, offsets.resume); emitter.jmpReg(RAX);	// tail-jump into RESUME (the hot entry / resume point)
	emitter.bind(epilogue);										// a segment jumps here with its Status in eax
	emitter.addImmQ(RSP, CALL_FRAME);
	emitter.pop(IP_STACK_PTR); emitter.pop(MEMORY_BASE); emitter.pop(FUEL); emitter.pop(CONTEXT); emitter.pop(RBP); emitter.pop(DSP);
	emitter.ret();												// eax = Status
	return entry;
}

/*
	JitCompilerX64::compile (declared in GAZLJitX64.h) - lowers a whole finalized program to x86-64 machine code into an
	EmittedModule, then makes it executable by handing it to JitModule and swaps that into `out` (the last three lines are
	the same in the arm64 backend). Lower every function into one buffer, append one shared epilogue, append the
	dispatcher, then record the byte stream + ordinal->entry byte offsets. Throws (via lowerFunction) on any finalized
	opcode the backend fails to cover.
*/
void JitCompilerX64::compile(const AssembledProgram& program, JitModule& out) {
	EmittedModule emitted;
	const Offsets offsets = JitProcessor::layout();				// the run-state ABI, obtained without an engine
	X64Emitter emitter;
	std::vector<Label> entryLabels(program.functionCount);
	for (UInt k = 0; k < program.functionCount; ++k) { entryLabels[k] = emitter.newLabel(); }
	Label epilogue = emitter.newLabel();

	for (UInt ordinal = 0; ordinal < program.functionCount; ++ordinal) {
		emitter.bind(entryLabels[ordinal]);
		lowerFunction(emitter, program.code, program.memory, program.functionTable[ordinal], offsets, entryLabels
				, epilogue, program.functionCount);
	}
	/*
		Shared exit `epilogue` (bound inside emitDispatcher): every terminal path - suspend, OK return, or trap - jumps
		there with its Status in eax; the dispatcher restores the frame and returns to the host.
	*/
	const size_t dispatcherOffset = emitDispatcher(emitter, offsets, epilogue);
	emitter.finalize();

	/*
		x86-64 is a byte stream; the module holds 32-bit words, so round up and zero-pad the last partial word. Entry and
		dispatch offsets are already byte offsets.
	*/
	const size_t byteCount = emitter.size();
	emitted.code.assign((byteCount + 3) / 4, 0);
	if (byteCount != 0) { std::memcpy(&emitted.code[0], emitter.code(), byteCount); }
	emitted.entryByteOffsets.resize(program.functionCount);
	for (UInt ordinal = 0; ordinal < program.functionCount; ++ordinal) {
		emitted.entryByteOffsets[ordinal] = static_cast<size_t>(emitter.labelOffset(entryLabels[ordinal]));
	}
	emitted.dispatchByteOffset = dispatcherOffset;
	JitModule built(emitted);									// makes the code executable (throws JitException on host denial)
	out.swap(built);
}

/*
	NativeJitCompiler compiles with this backend when x86-64 is the host arch; on other hosts it compiles out (the arm64 TU
	provides it there), so both backends may link together without a duplicate. A client that wants the x86-64 backend
	regardless of host names JitCompilerX64 directly instead.
*/
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
void NativeJitCompiler::compile(const AssembledProgram& program, JitModule& out) {
	JitCompilerX64().compile(program, out);
}
#endif

} // namespace GAZL
