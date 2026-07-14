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

} // namespace GAZL
