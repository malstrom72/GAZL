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

#include "GAZLJit.h"

#include <cassert>

namespace GAZL {

// Each method ORs operand fields into the canonical base opcode for its instruction form. All data-processing forms
// here are 32-bit (`Wn`); loads/stores address through a 64-bit base (`Xn`). Field positions follow the Arm ARM
// (AArch64 instruction set encoding). See docs/JitEmitterHandoff.md for the covered subset.

// --- moves / constant materialization ---

void Emitter::movz(Reg wd, uint16_t imm16, unsigned shift) {
	assert(shift == 0 || shift == 16);
	emit(0x52800000u | (static_cast<uint32_t>(shift / 16) << 21) | (static_cast<uint32_t>(imm16) << 5) | wd);
}

void Emitter::movk(Reg wd, uint16_t imm16, unsigned shift) {
	assert(shift == 0 || shift == 16);
	emit(0x72800000u | (static_cast<uint32_t>(shift / 16) << 21) | (static_cast<uint32_t>(imm16) << 5) | wd);
}

void Emitter::movn(Reg wd, uint16_t imm16, unsigned shift) {
	assert(shift == 0 || shift == 16);
	emit(0x12800000u | (static_cast<uint32_t>(shift / 16) << 21) | (static_cast<uint32_t>(imm16) << 5) | wd);
}

void Emitter::mov(Reg wd, Reg wm) {
	orr(wd, WZR, wm);												// canonical register move: `orr wd, wzr, wm`
}

void Emitter::movImm32(Reg wd, uint32_t imm) {
	movz(wd, static_cast<uint16_t>(imm & 0xFFFF), 0);
	if ((imm >> 16) != 0) {
		movk(wd, static_cast<uint16_t>(imm >> 16), 16);
	}
}

// --- integer arithmetic / logic (32-bit) ---

void Emitter::add(Reg wd, Reg wn, Reg wm) {
	emit(0x0B000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::addImm(Reg wd, Reg wn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0x11000000u | (imm12 << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::sub(Reg wd, Reg wn, Reg wm) {
	emit(0x4B000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::subImm(Reg wd, Reg wn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0x51000000u | (imm12 << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::subs(Reg wd, Reg wn, Reg wm) {
	emit(0x6B000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::subsImm(Reg wd, Reg wn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0x71000000u | (imm12 << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::cmp(Reg wn, Reg wm) {
	subs(WZR, wn, wm);												// `cmp` is `subs wzr, wn, wm`
}

void Emitter::cmpImm(Reg wn, uint32_t imm12) {
	subsImm(WZR, wn, imm12);										// `cmp` is `subs wzr, wn, #imm12`
}

void Emitter::mul(Reg wd, Reg wn, Reg wm) {
	// `mul` is `madd wd, wn, wm, wzr`; the accumulator field (Ra, bits 14-10) is `wzr` (31).
	emit(0x1B000000u | (static_cast<uint32_t>(wm) << 16) | (31u << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::and_(Reg wd, Reg wn, Reg wm) {
	emit(0x0A000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::orr(Reg wd, Reg wn, Reg wm) {
	emit(0x2A000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::eor(Reg wd, Reg wn, Reg wm) {
	emit(0x4A000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::lslImm(Reg wd, Reg wn, unsigned shift) {
	// `lsl wd, wn, #shift` is `ubfm wd, wn, #((-shift) & 31), #(31 - shift)`.
	assert(shift < 32);
	const uint32_t immr = (static_cast<uint32_t>(-static_cast<int>(shift)) & 31u);
	const uint32_t imms = 31u - shift;
	emit(0x53000000u | (immr << 16) | (imms << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::lsrImm(Reg wd, Reg wn, unsigned shift) {
	// `lsr wd, wn, #shift` is `ubfm wd, wn, #shift, #31`.
	assert(shift < 32);
	emit(0x53000000u | (static_cast<uint32_t>(shift) << 16) | (31u << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::asrImm(Reg wd, Reg wn, unsigned shift) {
	// `asr wd, wn, #shift` is `sbfm wd, wn, #shift, #31`.
	assert(shift < 32);
	emit(0x13000000u | (static_cast<uint32_t>(shift) << 16) | (31u << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

// --- word loads / stores (32-bit) ---

void Emitter::ldrW(Reg wt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);	// unsigned offset, scaled by 4
	emit(0xB9400000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Emitter::strW(Reg wt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xB9000000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Emitter::ldrWx(Reg wt, Reg xn, Reg wm) {
	// `ldr wt, [xn, wm, uxtw #2]`: option=uxtw(010), S=1 (scale by 4 for a word).
	emit(0xB8605800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Emitter::strWx(Reg wt, Reg xn, Reg wm) {
	emit(0xB8205800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

// --- float scalar (single precision) ---

void Emitter::faddS(Reg sd, Reg sn, Reg sm) {
	emit(0x1E202800u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Emitter::fmulS(Reg sd, Reg sn, Reg sm) {
	emit(0x1E200800u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Emitter::fsubS(Reg sd, Reg sn, Reg sm) {
	emit(0x1E203800u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Emitter::fcvtzs(Reg wd, Reg sn) {
	emit(0x1E380000u | (static_cast<uint32_t>(sn) << 5) | wd);		// FTOI, round toward zero
}

void Emitter::scvtf(Reg sd, Reg wn) {
	emit(0x1E220000u | (static_cast<uint32_t>(wn) << 5) | sd);		// ITOF
}

void Emitter::ldrS(Reg st, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xBD400000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Emitter::strS(Reg st, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xBD000000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | st);
}

// --- branches ---

void Emitter::branch(uint32_t base, int labelId, FixupKind kind) {
	Fixup f;
	f.site = words.size();
	f.labelId = labelId;
	f.kind = kind;
	fixups.push_back(f);
	emit(base);														// displacement is patched in by finalize()
}

void Emitter::b(Label target) {
	branch(0x14000000u, target.id, FIXUP_IMM26);
}

void Emitter::bcond(Cond cond, Label target) {
	branch(0x54000000u | static_cast<uint32_t>(cond), target.id, FIXUP_IMM19);
}

void Emitter::cbz(Reg wt, Label target) {
	branch(0x34000000u | wt, target.id, FIXUP_IMM19);
}

void Emitter::cbnz(Reg wt, Label target) {
	branch(0x35000000u | wt, target.id, FIXUP_IMM19);
}

void Emitter::ret(Reg xn) {
	emit(0xD65F0000u | (static_cast<uint32_t>(xn) << 5));
}

// --- labels / fixups ---

Label Emitter::newLabel() {
	Label l;
	l.id = static_cast<int>(labelTargets.size());
	labelTargets.push_back(-1);
	return l;
}

void Emitter::bind(Label label) {
	assert(label.id >= 0 && static_cast<size_t>(label.id) < labelTargets.size());
	assert(labelTargets[label.id] == -1 && "label bound twice");
	labelTargets[label.id] = static_cast<ptrdiff_t>(words.size());
}

void Emitter::finalize() {
	for (size_t i = 0; i < fixups.size(); ++i) {
		const Fixup& f = fixups[i];
		const ptrdiff_t target = labelTargets[f.labelId];
		assert(target >= 0 && "branch to unbound label");
		const ptrdiff_t disp = target - static_cast<ptrdiff_t>(f.site);	// PC-relative, in instruction words
		if (f.kind == FIXUP_IMM26) {
			words[f.site] |= (static_cast<uint32_t>(disp) & 0x03FFFFFFu);
		} else {
			words[f.site] |= ((static_cast<uint32_t>(disp) & 0x0007FFFFu) << 5);
		}
	}
	fixups.clear();
}

} // namespace GAZL
