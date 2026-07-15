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
#include "GAZLJitArm64.h"
#include "GAZLJitMem.h"			// makeExecutable() — platform-specific backend, architecture-neutral

#include <stdint.h>
#include <cassert>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace GAZL {

/*
	Each method ORs operand fields into the canonical base opcode for its instruction form. All data-processing forms
	here are 32-bit (`Wn`); loads/stores address through a 64-bit base (`Xn`). Field positions follow the Arm ARM
	(AArch64 instruction set encoding). See docs/JitEmitterHandoff.md for the covered subset.
*/

// --- moves / constant materialization ---

void Arm64Emitter::movz(Reg wd, uint16_t imm16, unsigned shift) {
	assert(shift == 0 || shift == 16);
	emit(0x52800000u | (static_cast<uint32_t>(shift / 16) << 21) | (static_cast<uint32_t>(imm16) << 5) | wd);
}

void Arm64Emitter::movk(Reg wd, uint16_t imm16, unsigned shift) {
	assert(shift == 0 || shift == 16);
	emit(0x72800000u | (static_cast<uint32_t>(shift / 16) << 21) | (static_cast<uint32_t>(imm16) << 5) | wd);
}

void Arm64Emitter::movn(Reg wd, uint16_t imm16, unsigned shift) {
	assert(shift == 0 || shift == 16);
	emit(0x12800000u | (static_cast<uint32_t>(shift / 16) << 21) | (static_cast<uint32_t>(imm16) << 5) | wd);
}

void Arm64Emitter::mov(Reg wd, Reg wm) {
	orr(wd, WZR, wm);												// canonical register move: `orr wd, wzr, wm`
}

void Arm64Emitter::movImm32(Reg wd, uint32_t imm) {
	movz(wd, static_cast<uint16_t>(imm & 0xFFFF), 0);
	if ((imm >> 16) != 0) {
		movk(wd, static_cast<uint16_t>(imm >> 16), 16);
	}
}

// --- integer arithmetic / logic (32-bit) ---

void Arm64Emitter::add(Reg wd, Reg wn, Reg wm) {
	emit(0x0B000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::addImm(Reg wd, Reg wn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0x11000000u | (imm12 << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::sub(Reg wd, Reg wn, Reg wm) {
	emit(0x4B000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::subImm(Reg wd, Reg wn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0x51000000u | (imm12 << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::subs(Reg wd, Reg wn, Reg wm) {
	emit(0x6B000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::subsImm(Reg wd, Reg wn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0x71000000u | (imm12 << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::cmp(Reg wn, Reg wm) {
	subs(WZR, wn, wm);												// `cmp` is `subs wzr, wn, wm`
}

void Arm64Emitter::cmpImm(Reg wn, uint32_t imm12) {
	subsImm(WZR, wn, imm12);										// `cmp` is `subs wzr, wn, #imm12`
}

void Arm64Emitter::mul(Reg wd, Reg wn, Reg wm) {
	// `mul` is `madd wd, wn, wm, wzr`; the accumulator field (Ra, bits 14-10) is `wzr` (31).
	emit(0x1B000000u | (static_cast<uint32_t>(wm) << 16) | (31u << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::sdiv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC00C00u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::msub(Reg wd, Reg wn, Reg wm, Reg wa) {
	// `msub wd, wn, wm, wa` = wa - wn*wm. Modulo is `msub(rem, quotient, divisor, dividend)`.
	emit(0x1B008000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wa) << 10)
			| (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::lslv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC02000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::lsrv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC02400u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::asrv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC02800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::and_(Reg wd, Reg wn, Reg wm) {
	emit(0x0A000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::orr(Reg wd, Reg wn, Reg wm) {
	emit(0x2A000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::eor(Reg wd, Reg wn, Reg wm) {
	emit(0x4A000000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::lslImm(Reg wd, Reg wn, unsigned shift) {
	// `lsl wd, wn, #shift` is `ubfm wd, wn, #((-shift) & 31), #(31 - shift)`.
	assert(shift < 32);
	const uint32_t immr = (static_cast<uint32_t>(-static_cast<int>(shift)) & 31u);
	const uint32_t imms = 31u - shift;
	emit(0x53000000u | (immr << 16) | (imms << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::lsrImm(Reg wd, Reg wn, unsigned shift) {
	// `lsr wd, wn, #shift` is `ubfm wd, wn, #shift, #31`.
	assert(shift < 32);
	emit(0x53000000u | (static_cast<uint32_t>(shift) << 16) | (31u << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::asrImm(Reg wd, Reg wn, unsigned shift) {
	// `asr wd, wn, #shift` is `sbfm wd, wn, #shift, #31`.
	assert(shift < 32);
	emit(0x13000000u | (static_cast<uint32_t>(shift) << 16) | (31u << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

// --- word loads / stores (32-bit) ---

void Arm64Emitter::ldrW(Reg wt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);	// unsigned offset, scaled by 4
	emit(0xB9400000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::strW(Reg wt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xB9000000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::ldrWx(Reg wt, Reg xn, Reg wm) {
	// `ldr wt, [xn, wm, uxtw #2]`: option=uxtw(010), S=1 (scale by 4 for a word).
	emit(0xB8605800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::strWx(Reg wt, Reg xn, Reg wm) {
	emit(0xB8205800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::ldrWxs(Reg wt, Reg xn, Reg wm) {
	// `ldr wt, [xn, wm, sxtw #2]`: option=sxtw(110), S=1 (scale by 4). Signed index (frame slot can be below dsp).
	emit(0xB860D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::strWxs(Reg wt, Reg xn, Reg wm) {
	emit(0xB820D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::ldurW(Reg wt, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);							// unscaled signed 9-bit byte offset
	emit(0xB8400000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::sturW(Reg wt, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xB8000000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | wt);
}

// --- doubleword loads / stores (64-bit) ---

void Arm64Emitter::ldrX(Reg xt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 7u) == 0 && (byteOffset >> 3) < 0x1000);	// unsigned offset, scaled by 8
	emit(0xF9400000u | ((byteOffset >> 3) << 10) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Arm64Emitter::strX(Reg xt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 7u) == 0 && (byteOffset >> 3) < 0x1000);
	emit(0xF9000000u | ((byteOffset >> 3) << 10) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Arm64Emitter::ldrXr(Reg xt, Reg xn, Reg wm) {
	// `ldr xt, [xn, wm, uxtw #3]`: option=uxtw(010), S=1 (scale by 8 for a doubleword), 64-bit register offset.
	emit(0xF8605800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Arm64Emitter::adr(Reg xd, Label target) {
	branch(0x10000000u | xd, target.id, FIXUP_ADR);					// PC-relative; displacement patched by finalize()
}

// --- 64-bit address arithmetic / test ---

void Arm64Emitter::addImmX(Reg xd, Reg xn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0x91000000u | (imm12 << 10) | (static_cast<uint32_t>(xn) << 5) | xd);
}

void Arm64Emitter::subImmX(Reg xd, Reg xn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0xD1000000u | (imm12 << 10) | (static_cast<uint32_t>(xn) << 5) | xd);
}

void Arm64Emitter::cbnzX(Reg xt, Label target) {
	branch(0xB5000000u | xt, target.id, FIXUP_IMM19);
}

void Arm64Emitter::cmpX(Reg xn, Reg xm) {
	emit(0xEB00001Fu | (static_cast<uint32_t>(xm) << 16) | (static_cast<uint32_t>(xn) << 5));	// subs xzr, xn, xm
}

void Arm64Emitter::addX(Reg xd, Reg xn, Reg xm) {
	emit(0x8B000000u | (static_cast<uint32_t>(xm) << 16) | (static_cast<uint32_t>(xn) << 5) | xd);
}

// --- float scalar (single precision) ---

void Arm64Emitter::faddS(Reg sd, Reg sn, Reg sm) {
	emit(0x1E202800u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Arm64Emitter::fmulS(Reg sd, Reg sn, Reg sm) {
	emit(0x1E200800u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Arm64Emitter::fsubS(Reg sd, Reg sn, Reg sm) {
	emit(0x1E203800u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Arm64Emitter::fdivS(Reg sd, Reg sn, Reg sm) {
	emit(0x1E201800u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Arm64Emitter::fcmpS(Reg sn, Reg sm) {
	emit(0x1E202000u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5));
}

void Arm64Emitter::fabsS(Reg sd, Reg sn) {
	emit(0x1E20C000u | (static_cast<uint32_t>(sn) << 5) | sd);		// FP data-proc (1 source), single, opcode ABS
}

void Arm64Emitter::frintmS(Reg sd, Reg sn) {
	emit(0x1E254000u | (static_cast<uint32_t>(sn) << 5) | sd);		// round toward -inf (floorf)
}

void Arm64Emitter::fcvtzs(Reg wd, Reg sn) {
	emit(0x1E380000u | (static_cast<uint32_t>(sn) << 5) | wd);		// FTOI, round toward zero (saturating)
}

void Arm64Emitter::scvtf(Reg sd, Reg wn) {
	emit(0x1E220000u | (static_cast<uint32_t>(wn) << 5) | sd);		// ITOF
}

void Arm64Emitter::fmovSW(Reg sd, Reg wn) {
	emit(0x1E270000u | (static_cast<uint32_t>(wn) << 5) | sd);		// bit-copy Wn -> Sd (no conversion)
}

void Arm64Emitter::ldrS(Reg st, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xBD400000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Arm64Emitter::strS(Reg st, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xBD000000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Arm64Emitter::ldurS(Reg st, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xBC400000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Arm64Emitter::sturS(Reg st, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xBC000000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Arm64Emitter::ldrSxs(Reg st, Reg xn, Reg wm) {
	emit(0xBC60D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Arm64Emitter::strSxs(Reg st, Reg xn, Reg wm) {
	emit(0xBC20D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | st);
}

// --- branches ---

void Arm64Emitter::branch(uint32_t base, int labelId, FixupKind kind) {
	Fixup f;
	f.site = words.size();
	f.labelId = labelId;
	f.kind = kind;
	fixups.push_back(f);
	emit(base);														// displacement is patched in by finalize()
}

void Arm64Emitter::b(Label target) {
	branch(0x14000000u, target.id, FIXUP_IMM26);
}

void Arm64Emitter::bcond(Cond cond, Label target) {
	branch(0x54000000u | static_cast<uint32_t>(cond), target.id, FIXUP_IMM19);
}

void Arm64Emitter::cbz(Reg wt, Label target) {
	branch(0x34000000u | wt, target.id, FIXUP_IMM19);
}

void Arm64Emitter::cbnz(Reg wt, Label target) {
	branch(0x35000000u | wt, target.id, FIXUP_IMM19);
}

void Arm64Emitter::ret(Reg xn) {
	emit(0xD65F0000u | (static_cast<uint32_t>(xn) << 5));
}

void Arm64Emitter::br(Reg xn) {
	emit(0xD61F0000u | (static_cast<uint32_t>(xn) << 5));
}

void Arm64Emitter::blr(Reg xn) {
	emit(0xD63F0000u | (static_cast<uint32_t>(xn) << 5));
}

// --- labels / fixups ---

Label Arm64Emitter::newLabel() {
	Label l;
	l.id = static_cast<int>(labelTargets.size());
	labelTargets.push_back(-1);
	return l;
}

void Arm64Emitter::bind(Label label) {
	assert(label.id >= 0 && static_cast<size_t>(label.id) < labelTargets.size());
	assert(labelTargets[label.id] == -1 && "label bound twice");
	labelTargets[label.id] = static_cast<ptrdiff_t>(words.size());
}

void Arm64Emitter::finalize() {
	for (size_t i = 0; i < fixups.size(); ++i) {
		const Fixup& f = fixups[i];
		const ptrdiff_t target = labelTargets[f.labelId];
		assert(target >= 0 && "branch to unbound label");
		const ptrdiff_t disp = target - static_cast<ptrdiff_t>(f.site);	// PC-relative, in instruction words
		if (f.kind == FIXUP_IMM26) {
			words[f.site] |= (static_cast<uint32_t>(disp) & 0x03FFFFFFu);
		} else if (f.kind == FIXUP_IMM19) {
			words[f.site] |= ((static_cast<uint32_t>(disp) & 0x0007FFFFu) << 5);
		} else {														// FIXUP_ADR: 21-bit byte displacement, split lo/hi
			const uint32_t immBytes = static_cast<uint32_t>(disp * 4) & 0x001FFFFFu;
			words[f.site] |= ((immBytes & 0x3u) << 29) | (((immBytes >> 2) & 0x0007FFFFu) << 5);
		}
	}
	fixups.clear();
}


// ============================================================================================================
// JIT lowering, native dispatcher, and the JitCompiler driver (declarations in GAZLJit.h). Depends on GAZL.h and the
// Arm64Emitter above. The emit helpers are file-local (`static`); lowerFunction/emitDispatcher and JitCompiler::compile
// are external. JitCompiler::compile calls makeExecutable(), so anything linking this file also links a per-platform
// GAZLJitMem*.cpp backend (GAZLJitMemPosix.cpp is enough for the encoding tests).
// ============================================================================================================

// --- emit helpers (x0=ctx, x1=dsp, x2=membase, w3=fuel, x4=ipsp; scratch x9..x12) ---

static void reloadState(Arm64Emitter& e, const Offsets& o) {
	e.ldrX(X1, X0, o.dsp); e.ldrX(X2, X0, o.mb); e.ldrW(W3, X0, o.fuel); e.ldrX(X4, X0, o.ipsp);
}
static void writebackState(Arm64Emitter& e, const Offsets& o) {
	e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);
}
static void matConst(Arm64Emitter& e, Reg r, Int v) { e.movImm32(r, static_cast<uint32_t>(v)); }
// Frame slots are Value-indices off dsp (x1). ldur/stur reach ±64 words; far slots (big frames / LOCA arrays) fall back
// to a register-offset load (index in W13 — kept distinct from the W9..W12 operand scratches). See task #23.
static bool slotNear(Int slot) { return slot >= -64 && slot <= 63; }
static void loadSlot(Arm64Emitter& e, Reg r, Int slot) {
	if (slotNear(slot)) { e.ldurW(r, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.ldrWxs(r, X1, W13); }
}
static void storeSlot(Arm64Emitter& e, Reg r, Int slot) {
	if (slotNear(slot)) { e.sturW(r, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.strWxs(r, X1, W13); }
}
static void loadSlotF(Arm64Emitter& e, Reg s, Int slot) {
	if (slotNear(slot)) { e.ldurS(s, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.ldrSxs(s, X1, W13); }
}
static void storeSlotF(Arm64Emitter& e, Reg s, Int slot) {
	if (slotNear(slot)) { e.sturS(s, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.strSxs(s, X1, W13); }
}
// PEEK/POKE at a compile-time-constant memory word index (off memoryBase, X2). The scaled imm12 form reaches word
// index 4095 (16 KB in); past that — large globals/consts such as audio tables — materialize the index and use the
// scaled register-offset form, mirroring loadSlot/storeSlot. W13 is the dedicated addressing scratch (kept distinct
// from the W9..W12 operand scratches), so callers may still hold a value in W9..W12 across these.
static void loadMemConst(Arm64Emitter& e, Reg r, uint32_t wordIndex) {
	if (wordIndex < 0x1000u) { e.ldrW(r, X2, wordIndex * 4u); }
	else { matConst(e, W13, static_cast<Int>(wordIndex)); e.ldrWx(r, X2, W13); }
}
static void storeMemConst(Arm64Emitter& e, Reg r, uint32_t wordIndex) {
	if (wordIndex < 0x1000u) { e.strW(r, X2, wordIndex * 4u); }
	else { matConst(e, W13, static_cast<Int>(wordIndex)); e.strWx(r, X2, W13); }
}
static void loadOp(Arm64Emitter& e, Reg r, const Value& p, bool isConst) {
	if (isConst) { matConst(e, r, p.i); } else { loadSlot(e, r, p.i); }
}
// `dst = s1 <op> s2`, where s2 is a const (imm-form) or slot; s1 is always a slot for VVV/VVC, a const for VCV.
static void emitBinary(Arm64Emitter& e, void (Arm64Emitter::*op)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	loadOp(e, W10, in.p1, s1Const);
	loadOp(e, W11, in.p2, s2Const);
	(e.*op)(W9, W10, W11);
	storeSlot(e, W9, in.p0.i);
}

// Load a float operand into S-reg `s` (using `wtmp` for a const): a slot via ldur, or a float constant via fmov s,w.
static void loadOpF(Arm64Emitter& e, Reg s, Reg wtmp, const Value& p, bool isConst) {
	if (isConst) { matConst(e, wtmp, p.i); e.fmovSW(s, wtmp); }
	else { loadSlotF(e, s, p.i); }
}
// `dst = s1 <fop> s2` on float slots/consts.
static void emitBinaryF(Arm64Emitter& e, void (Arm64Emitter::*fop)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	loadOpF(e, S0, W9, in.p1, s1Const);
	loadOpF(e, S1, W10, in.p2, s2Const);
	(e.*fop)(S2, S0, S1);
	storeSlotF(e, S2, in.p0.i);
}

// Emit a shift `dst = value <shift> count`. kind: 0=lsl, 1=asr (SHRi), 2=lsr (SHRu). form: 0=VVV, 1=VVC, 2=VCV.
// value is p1 (a slot for VVV/VVC, a const for VCV); count is p2 (a const for VVC, else a slot, masked mod 32 by HW).
static void emitShift(Arm64Emitter& e, int kind, const Instruction& in, int form) {
	loadOp(e, W10, in.p1, form == 2);					// value
	if (form == 1) {									// VVC: constant count → immediate shift
		const unsigned sh = static_cast<unsigned>(in.p2.i) & 31u;
		if (kind == 0) { e.lslImm(W9, W10, sh); } else if (kind == 1) { e.asrImm(W9, W10, sh); } else { e.lsrImm(W9, W10, sh); }
	} else {											// register count
		loadSlot(e, W11, in.p2.i);
		if (kind == 0) { e.lslv(W9, W10, W11); } else if (kind == 1) { e.asrv(W9, W10, W11); } else { e.lsrv(W9, W10, W11); }
	}
	storeSlot(e, W9, in.p0.i);
}

// Emit an integer divide (rem=false) or modulo (rem=true) `dst = s1 </%> s2`. form: 0=VVV, 1=VVC, 2=VCV. A variable
// divisor (VVV/VCV) gets a divide-by-zero guard → DIVISION_BY_ZERO; a VVC divisor is a nonzero assembler constant.
static void emitDivMod(Arm64Emitter& e, bool rem, const Instruction& in, int form, Label exitLabel) {
	loadOp(e, W10, in.p1, form == 2);					// dividend (const for VCV)
	loadOp(e, W11, in.p2, form == 1);					// divisor (const for VVC)
	if (form != 1) {									// variable divisor → guard
		Label ok = e.newLabel();
		e.cbnz(W11, ok);
		e.movn(W0, 6); e.b(exitLabel);					// ~6 = -7 = DIVISION_BY_ZERO (terminal)
		e.bind(ok);
	}
	e.sdiv(W9, W10, W11);								// quotient (INT_MIN/-1 → INT_MIN, matches §6.1)
	if (rem) { e.msub(W9, W9, W11, W10); }				// remainder = dividend - quotient*divisor
	storeSlot(e, W9, in.p0.i);
}

/*
	Lower one function at `funcIndex` into `e` (appended). Emits an entry reload + FUNC prologue, a mainline per block,
	cold reload trampolines + §5.7.5 suspend stubs for loop heads, and the §5.4 call/return transfers. `entryLabels[ord]`
	are pre-created (for direct calls); `entryOffset[selfOrdinal]` is set to this function's native word offset. Returns
	false on an unsupported opcode.
*/
static bool lowerFunction(Arm64Emitter& e, const Instruction* code, const Value* memory, UInt funcIndex, const Offsets& o,
		std::vector<Label>& entryLabels, std::vector<size_t>& entryOffset, UInt selfOrdinal, UInt functionCount, Label exitLabel) {
	UInt retIndex = funcIndex;
	while (code[retIndex].opcode != OP_RETU) { ++retIndex; }

	// Pass 1 — fuel safepoints: every basic-block leader (arch-neutral, §5.5), each charged its block weight. Every leader
	// is a resumable point, so each gets a mainline label (hot entry + branch + resume target), a reload trampoline, and a
	// suspend stub. Charging per block (not just loop heads) makes fuel spend ≈ the interpreter's, so straight-line and
	// recursive code yields on time too.
	std::map<UInt, UInt> loopWeight;
	jitFuelSafepoints(code, funcIndex, retIndex, memory, MAX_BLOCK_WEIGHT, loopWeight);
	std::map<UInt, Label> mainline, suspendL;
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		mainline[it->first] = e.newLabel(); suspendL[it->first] = e.newLabel();
	}

	// Function entry (hot — reached by a tail-branch from a caller or by the dispatcher after it reloaded the pins, so
	// state is already live). FUNC prologue: dsp += localsSize.
	entryOffset[selfOrdinal] = e.wordCount();
	e.bind(entryLabels[selfOrdinal]);
	const UInt localsSize = static_cast<UInt>(code[funcIndex].p0.i);
	if (localsSize != 0) {							// dsp += localsSize (in bytes); register add if beyond the imm12 range
		if (localsSize * 4 < 0x1000) { e.addImmX(X1, X1, localsSize * 4); }
		else { matConst(e, W9, static_cast<Int>(localsSize * 4)); e.addX(X1, X1, X9); }
	}
	{												// FUNC stack-overflow: if (dsp + paramsSize > dataStackEnd) DATA_STACK_OVERFLOW
		const UInt paramsSize = static_cast<UInt>(code[funcIndex].p1.i);
		Label sok = e.newLabel();
		e.ldrX(X9, X0, o.dsend); e.addImmX(X10, X1, paramsSize * 4); e.cmpX(X10, X9);
		e.bcond(LS, sok); e.movn(W0, 4); e.b(exitLabel);	// > end → ~4 = -5 = DATA_STACK_OVERFLOW
		e.bind(sok);
	}

	// Pass 2 — emit.
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		if (mainline.count(j)) { e.bind(mainline[j]); }
		if (loopWeight.count(j)) { e.subsImm(W3, W3, loopWeight[j]); e.bcond(MI, suspendL[j]); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		switch (op) {
			case OP_FUNC: continue;	// prologue stack/fuel check omitted for the prototype
			case OP_RETU: {
				Label notNative = e.newLabel();
				e.subImmX(X4, X4, 16);								// ipsp-- ; pop {cont, dsp}
				e.ldrX(X9, X4, 0); e.ldrX(X1, X4, 8);				// cont ; caller dsp (or 0 = native/top marker)
				e.cbnzX(X1, notNative);
				e.subImmX(X4, X4, 16); e.ldrX(X1, X4, 8);			// native/top return: pop again for the true dsp
				writebackState(e, o);
				e.movz(W0, 0); e.b(exitLabel);						// OK — terminal (return to host)
				e.bind(notNative);
				e.br(X9);											// GAZL return: tail-branch to the continuation (state live)
				break;
			}
			case OP_CALL_CVC: {
				const UInt callee = in.p0.p - IP_OFFSET;			// ordinal known at compile time → direct tail-branch
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = e.newLabel(), iok = e.newLabel();
				e.ldrX(X9, X0, o.ipsend); e.cmpX(X4, X9); e.bcond(LO, iok);	// ipsp >= ipStackEnd → IP_STACK_OVERFLOW
				e.movn(W0, 5); e.b(exitLabel); e.bind(iok);			// ~5 = -6
				e.adr(X9, after); e.strX(X9, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);	// push {after, dsp}
				if (window != 0) { e.addImmX(X1, X1, window * 4); }	// dsp += arg window
				e.b(entryLabels[callee]);							// tail-branch to the callee entry (state stays live)
				e.bind(after);										// return lands here (hot; state live)
				break;
			}
			case OP_CALL_VVC: {
				const UInt window = static_cast<UInt>(in.p1.i);		// ordinal from a slot at runtime → resolve + bounds-check
				Label after = e.newLabel(), trap = e.newLabel(), iok = e.newLabel();
				e.ldrX(X9, X0, o.ipsend); e.cmpX(X4, X9); e.bcond(LO, iok);	// ipsp >= ipStackEnd → IP_STACK_OVERFLOW
				e.movn(W0, 5); e.b(exitLabel); e.bind(iok);
				loadSlot(e, W9, in.p0.i);							// fn pointer = IP_OFFSET + ordinal
				matConst(e, W10, static_cast<Int>(IP_OFFSET)); e.sub(W9, W9, W10);	// ordinal
				matConst(e, W10, static_cast<Int>(functionCount));
				e.cmp(W9, W10); e.bcond(HS, trap);					// ordinal >= functionCount → BAD_CALL
				e.ldrX(X10, X0, o.funcentries); e.ldrXr(X9, X10, W9);	// entry = funcEntries[ordinal] (hot)
				e.adr(X10, after); e.strX(X10, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);	// push {after, dsp}
				if (window != 0) { e.addImmX(X1, X1, window * 4); }
				e.br(X9);											// tail-branch to the callee entry (state stays live)
				e.bind(trap); e.movn(W0, 3); e.b(exitLabel);		// ~3 = -4 = BAD_CALL
				e.bind(after);
				break;
			}
			case OP_CALL_NVC: {
				const UInt ordinal = static_cast<UInt>(in.p0.i);	// native ordinal (C0)
				const UInt window = static_cast<UInt>(in.p1.i);		// param-window offset (C1)
				Label hot = e.newLabel(), okStatus = e.newLabel();
				e.bind(hot);										// re-entry for blocking retry / suspend-resume
				e.strX(X1, X0, o.saveddsp);							// stash original dsp (the window advance is transient)
				if (window != 0) { e.addImmX(X1, X1, window * 4); }
				e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);	// publish window/fuel/ipsp (interpreter-shaped)
				e.adr(X9, hot); e.strX(X9, X0, o.resume);			// RESUME = call site (nonzero native → re-issue: blocking retry)
				e.ldrX(X9, X0, o.natives); e.ldrX(X9, X9, ordinal * 8);	// natives[ordinal]
				e.blr(X9);											// inline host call (x0 = ctx); w0 = status
				e.mov(W12, W0);										// save the status BEFORE we overwrite x0 with ctx
				e.addImmX(X0, X19, 0); reloadState(e, o); e.ldrX(X1, X0, o.saveddsp);	// native clobbered the pins: restore ctx + reload (dsp = original)
				e.cmpImm(W12, 0); e.bcond(EQ, okStatus);			// OK → continue inline
				e.mov(W0, W12); e.b(exitLabel);						// nonzero (BLOCK_RETRY / TIME_OUT / trap): return to host; RESUME = call site so it re-issues
				e.bind(okStatus);									// OK: continue (state live)
				break;
			}
			case OP_MOVE_VC: matConst(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i); break;
			case OP_MOVE_VV: loadSlot(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i); break;
			case OP_PEEK_VC: loadMemConst(e, W9, static_cast<uint32_t>(in.p1.p - MEMORY_OFFSET)); storeSlot(e, W9, in.p0.i); break;
			case OP_POKE_CV: loadSlot(e, W9, in.p1.i); storeMemConst(e, W9, static_cast<uint32_t>(in.p0.p - MEMORY_OFFSET)); break;
			case OP_POKE_CC: matConst(e, W9, in.p1.i); storeMemConst(e, W9, static_cast<uint32_t>(in.p0.p - MEMORY_OFFSET)); break;
			case OP_PEEK_VCV: {
				Label trap = e.newLabel(), cont = e.newLabel();
				matConst(e, W9, static_cast<Int>(in.p1.p - MEMORY_OFFSET)); loadSlot(e, W10, in.p2.i); e.add(W9, W9, W10);
				e.ldrW(W10, X0, o.memsize); e.cmp(W9, W10); e.bcond(HS, trap);
				e.ldrWx(W11, X2, W9); storeSlot(e, W11, in.p0.i);
				e.b(cont); e.bind(trap); e.movn(W0, 1); e.b(exitLabel);	// ~1 = -2 = BAD_PEEK
				e.bind(cont);
				break;
			}
			case OP_POKE_CVV: case OP_POKE_CVC: {					// base const, index var; value var (CVV) or const (CVC)
				Label trap = e.newLabel(), cont = e.newLabel();
				matConst(e, W9, static_cast<Int>(in.p0.p - MEMORY_OFFSET)); loadSlot(e, W10, in.p1.i); e.add(W9, W9, W10);
				e.ldrW(W10, X0, o.rwmemsize); e.cmp(W9, W10); e.bcond(HS, trap);
				if (op == OP_POKE_CVC) { matConst(e, W11, in.p2.i); } else { loadSlot(e, W11, in.p2.i); }
				e.strWx(W11, X2, W9);
				e.b(cont); e.bind(trap); e.movn(W0, 2); e.b(exitLabel);	// ~2 = -3 = BAD_POKE
				e.bind(cont);
				break;
			}
			case OP_GETL_VVV: {
				Label trap = e.newLabel(), cont = e.newLabel();
				e.ldrX(X9, X0, o.dsend); e.sub(W9, W9, W1); e.lsrImm(W9, W9, 2);	// (dataStackEnd - dsp) in Value units
				matConst(e, W10, in.p1.i); e.sub(W9, W9, W10);					// limit = that - C1
				loadSlot(e, W10, in.p2.i); e.cmp(W10, W9); e.bcond(HS, trap);	// index >= limit → BAD_PEEK
				matConst(e, W11, in.p1.i); e.add(W11, W11, W10);				// C1 + index (signed offset off dsp)
				e.ldrWxs(W11, X1, W11); storeSlot(e, W11, in.p0.i);
				e.b(cont); e.bind(trap); e.movn(W0, 1); e.b(exitLabel);
				e.bind(cont);
				break;
			}
			case OP_SETL_VVV: case OP_SETL_VVC: {
				Label trap = e.newLabel(), cont = e.newLabel();
				e.ldrX(X9, X0, o.dsend); e.sub(W9, W9, W1); e.lsrImm(W9, W9, 2);
				matConst(e, W10, in.p0.i); e.sub(W9, W9, W10);					// limit = (end-dsp) - C0
				loadSlot(e, W10, in.p1.i); e.cmp(W10, W9); e.bcond(HS, trap);	// index >= limit → BAD_POKE
				matConst(e, W11, in.p0.i); e.add(W11, W11, W10);				// C0 + index
				if (op == OP_SETL_VVC) { matConst(e, W12, in.p2.i); } else { loadSlot(e, W12, in.p2.i); }
				e.strWxs(W12, X1, W11);
				e.b(cont); e.bind(trap); e.movn(W0, 2); e.b(exitLabel);
				e.bind(cont);
				break;
			}
			case OP_COPY_VVC: case OP_COPY_VCC: case OP_COPY_CVC: case OP_COPY_CCC: {
				// block copy of `count` words src->dest (both MEMORY_OFFSET-biased ptrs); checked → ACCESS_VIOLATION.
				const bool destConst = (op == OP_COPY_CVC || op == OP_COPY_CCC);
				const bool srcConst = (op == OP_COPY_VCC || op == OP_COPY_CCC);
				Label trap = e.newLabel(), cont = e.newLabel(), lp = e.newLabel(), ldone = e.newLabel();
				// destIdx (W9) and srcIdx (W10) = ptr - MEMORY_OFFSET (word index into memoryBase)
				if (destConst) { matConst(e, W9, in.p0.i - static_cast<Int>(MEMORY_OFFSET)); }
				else { loadSlot(e, W9, in.p0.i); matConst(e, W12, static_cast<Int>(MEMORY_OFFSET)); e.sub(W9, W9, W12); }
				if (srcConst) { matConst(e, W10, in.p1.i - static_cast<Int>(MEMORY_OFFSET)); }
				else { loadSlot(e, W10, in.p1.i); matConst(e, W12, static_cast<Int>(MEMORY_OFFSET)); e.sub(W10, W10, W12); }
				matConst(e, W15, in.p2.i);							// count
				e.add(W12, W9, W15); e.ldrW(W14, X0, o.rwmemsize); e.cmp(W12, W14); e.bcond(HS, trap);	// destIdx+count < rwMemorySize
				e.add(W12, W10, W15); e.ldrW(W14, X0, o.memsize); e.cmp(W12, W14); e.bcond(HS, trap);	// srcIdx+count < memorySize
				e.movz(W11, 0);										// i = 0
				e.bind(lp);
				e.cmp(W11, W15); e.bcond(HS, ldone);				// i >= count → done
				e.add(W12, W10, W11); e.ldrWx(W12, X2, W12);		// val = memoryBase[srcIdx+i]
				e.add(W14, W9, W11); e.strWx(W12, X2, W14);			// memoryBase[destIdx+i] = val
				e.addImm(W11, W11, 1); e.b(lp);
				e.bind(ldone); e.b(cont);
				e.bind(trap); e.movn(W0, 7); e.b(exitLabel);				// ~7 = -8 = ACCESS_VIOLATION
				e.bind(cont);
				break;
			}
			case OP_ADRL: {
				e.sub(W9, W1, W2);									// (dsp - memoryBase) in bytes (low 32 valid within buffer)
				e.lsrImm(W9, W9, 2);								//   -> Value units
				matConst(e, W10, static_cast<Int>(MEMORY_OFFSET) + in.p1.i); e.add(W9, W9, W10);
				storeSlot(e, W9, in.p0.i);
				break;
			}
			case OP_PEEK_VVV: {
				Label trap = e.newLabel(), cont = e.newLabel();
				loadSlot(e, W9, in.p1.i); loadSlot(e, W10, in.p2.i); e.add(W9, W9, W10);	// base + index
				matConst(e, W10, static_cast<Int>(MEMORY_OFFSET)); e.sub(W9, W9, W10);	// - MEMORY_OFFSET
				e.ldrW(W10, X0, o.memsize); e.cmp(W9, W10); e.bcond(HS, trap);
				e.ldrWx(W11, X2, W9); storeSlot(e, W11, in.p0.i);
				e.b(cont); e.bind(trap); e.movn(W0, 1); e.b(exitLabel);	// BAD_PEEK
				e.bind(cont);
				break;
			}
			case OP_POKE_VVV: case OP_POKE_VVC: {					// base var, index var; value var (VVV) or const (VVC)
				Label trap = e.newLabel(), cont = e.newLabel();
				loadSlot(e, W9, in.p0.i); loadSlot(e, W10, in.p1.i); e.add(W9, W9, W10);	// base + index
				matConst(e, W10, static_cast<Int>(MEMORY_OFFSET)); e.sub(W9, W9, W10);
				e.ldrW(W10, X0, o.rwmemsize); e.cmp(W9, W10); e.bcond(HS, trap);
				if (op == OP_POKE_VVC) { matConst(e, W11, in.p2.i); } else { loadSlot(e, W11, in.p2.i); }
				e.strWx(W11, X2, W9);
				e.b(cont); e.bind(trap); e.movn(W0, 2); e.b(exitLabel);	// BAD_POKE
				e.bind(cont);
				break;
			}
			case OP_ADDI_VVV: emitBinary(e, &Arm64Emitter::add, in, false, false); break;
			case OP_ADDI_VVC: emitBinary(e, &Arm64Emitter::add, in, false, true); break;
			case OP_SUBI_VVV: emitBinary(e, &Arm64Emitter::sub, in, false, false); break;
			case OP_SUBI_VVC: emitBinary(e, &Arm64Emitter::sub, in, false, true); break;
			case OP_SUBI_VCV: emitBinary(e, &Arm64Emitter::sub, in, true, false); break;
			case OP_MULI_VVV: emitBinary(e, &Arm64Emitter::mul, in, false, false); break;
			case OP_MULI_VVC: emitBinary(e, &Arm64Emitter::mul, in, false, true); break;
			case OP_DIVI_VVV: emitDivMod(e, false, in, 0, exitLabel); break;
			case OP_DIVI_VVC: emitDivMod(e, false, in, 1, exitLabel); break;
			case OP_DIVI_VCV: emitDivMod(e, false, in, 2, exitLabel); break;
			case OP_MODI_VVV: emitDivMod(e, true, in, 0, exitLabel); break;
			case OP_MODI_VVC: emitDivMod(e, true, in, 1, exitLabel); break;
			case OP_MODI_VCV: emitDivMod(e, true, in, 2, exitLabel); break;
			case OP_SHLI_VVV: emitShift(e, 0, in, 0); break;
			case OP_SHLI_VVC: emitShift(e, 0, in, 1); break;
			case OP_SHLI_VCV: emitShift(e, 0, in, 2); break;
			case OP_SHRI_VVV: emitShift(e, 1, in, 0); break;
			case OP_SHRI_VVC: emitShift(e, 1, in, 1); break;
			case OP_SHRI_VCV: emitShift(e, 1, in, 2); break;
			case OP_SHRU_VVV: emitShift(e, 2, in, 0); break;
			case OP_SHRU_VVC: emitShift(e, 2, in, 1); break;
			case OP_SHRU_VCV: emitShift(e, 2, in, 2); break;
			case OP_ABSI: {
				loadSlot(e, W10, in.p1.i); e.asrImm(W11, W10, 31); e.eor(W9, W10, W11); e.sub(W9, W9, W11);
				storeSlot(e, W9, in.p0.i);
				break;
			}
			case OP_ABSF: loadSlotF(e, S0, in.p1.i); e.fabsS(S0, S0); storeSlotF(e, S0, in.p0.i); break;	// V0 = fabs(V1)
			case OP_FLOF: loadSlotF(e, S0, in.p1.i); e.frintmS(S0, S0); storeSlotF(e, S0, in.p0.i); break;	// V0 = floorf(V1)
			case OP_ADDF_VVV: emitBinaryF(e, &Arm64Emitter::faddS, in, false, false); break;
			case OP_ADDF_VVC: emitBinaryF(e, &Arm64Emitter::faddS, in, false, true); break;
			case OP_SUBF_VVV: emitBinaryF(e, &Arm64Emitter::fsubS, in, false, false); break;
			case OP_SUBF_VVC: emitBinaryF(e, &Arm64Emitter::fsubS, in, false, true); break;
			case OP_SUBF_VCV: emitBinaryF(e, &Arm64Emitter::fsubS, in, true, false); break;
			case OP_MULF_VVV: emitBinaryF(e, &Arm64Emitter::fmulS, in, false, false); break;
			case OP_MULF_VVC: emitBinaryF(e, &Arm64Emitter::fmulS, in, false, true); break;
			case OP_DIVF_VVV: emitBinaryF(e, &Arm64Emitter::fdivS, in, false, false); break;
			case OP_DIVF_VVC: emitBinaryF(e, &Arm64Emitter::fdivS, in, false, true); break;
			case OP_DIVF_VCV: emitBinaryF(e, &Arm64Emitter::fdivS, in, true, false); break;
			case OP_FTOI_VVC: {
				loadSlotF(e, S0, in.p1.i); matConst(e, W9, in.p2.i); e.fmovSW(S1, W9);
				e.fmulS(S0, S0, S1); e.fcvtzs(W9, S0); storeSlot(e, W9, in.p0.i);
				break;
			}
			case OP_ITOF_VVC: {
				loadSlot(e, W9, in.p1.i); e.scvtf(S0, W9); matConst(e, W10, in.p2.i); e.fmovSW(S1, W10);
				e.fmulS(S0, S0, S1); storeSlotF(e, S0, in.p0.i);
				break;
			}
			case OP_ANDI_VVV: emitBinary(e, &Arm64Emitter::and_, in, false, false); break;
			case OP_ANDI_VVC: emitBinary(e, &Arm64Emitter::and_, in, false, true); break;
			case OP_IORI_VVV: emitBinary(e, &Arm64Emitter::orr, in, false, false); break;
			case OP_IORI_VVC: emitBinary(e, &Arm64Emitter::orr, in, false, true); break;
			case OP_XORI_VVV: emitBinary(e, &Arm64Emitter::eor, in, false, false); break;
			case OP_XORI_VVC: emitBinary(e, &Arm64Emitter::eor, in, false, true); break;
			case OP_FORi_VVB: case OP_FORi_VCB: {
				loadSlot(e, W10, in.p0.i); e.addImm(W10, W10, 1); storeSlot(e, W10, in.p0.i);
				loadOp(e, W11, in.p1, op == OP_FORi_VCB);
				e.cmp(W10, W11);
				e.bcond(LT, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;
			}
			case OP_GOTO: e.b(mainline[static_cast<UInt>(static_cast<Int>(j) + in.p0.i)]); break;
			case OP_SWCH: {											// index = min(unsigned(V0), C1); br into a table of `b target`
				const UInt sz = static_cast<UInt>(in.p1.i) + 1;
				const UInt tbl = static_cast<UInt>(in.p2.p - MEMORY_OFFSET);
				loadSlot(e, W9, in.p0.i);							// switch value
				matConst(e, W10, in.p1.i);							// clamp max = C1 = sz-1
				e.cmp(W9, W10);
				Label useVal = e.newLabel();
				e.bcond(LS, useVal);								// (unsigned) val <= C1 → keep; else clamp to C1
				e.mov(W9, W10);
				e.bind(useVal);
				Label caseBase = e.newLabel();
				e.adr(X10, caseBase);								// base of the branch table (below)
				e.lslImm(W11, W9, 2); e.addX(X10, X10, X11);		// += index * 4  (W-write zero-extends into X11)
				e.br(X10);
				e.bind(caseBase);									// sz consecutive `b target` — br lands on the index'th
				for (UInt k = 0; k < sz; ++k) {
					const UInt t = static_cast<UInt>(static_cast<Int>(j) + memory[tbl + k].i);
					e.b(mainline[t]);
				}
				break;
			}
			case OP_LSSF_VVB: case OP_LSSF_VCB: case OP_LSSF_CVB: case OP_EQUF_VVB: case OP_EQUF_VCB:
			case OP_NLSF_VVB: case OP_NLSF_VCB: case OP_NLSF_CVB: case OP_NEQF_VVB: case OP_NEQF_VCB: {
				// float compare-branch. Conditions chosen so NaN matches C++ (a<b false, !(a<b) true): LSS→MI, NLS→PL.
				Cond c; bool c0const, c1const;
				switch (op) {
					case OP_LSSF_VVB: c = MI; c0const = false; c1const = false; break;
					case OP_LSSF_VCB: c = MI; c0const = false; c1const = true; break;
					case OP_LSSF_CVB: c = MI; c0const = true; c1const = false; break;
					case OP_EQUF_VVB: c = EQ; c0const = false; c1const = false; break;
					case OP_EQUF_VCB: c = EQ; c0const = false; c1const = true; break;
					case OP_NLSF_VVB: c = PL; c0const = false; c1const = false; break;
					case OP_NLSF_VCB: c = PL; c0const = false; c1const = true; break;
					case OP_NLSF_CVB: c = PL; c0const = true; c1const = false; break;
					case OP_NEQF_VVB: c = NE; c0const = false; c1const = false; break;
					default: c = NE; c0const = false; c1const = true; break;	// NEQF_VCB
				}
				loadOpF(e, S0, W9, in.p0, c0const);
				loadOpF(e, S1, W10, in.p1, c1const);
				e.fcmpS(S0, S1);
				e.bcond(c, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;
			}
			case OP_LSSI_VVB: case OP_LSSI_VCB: case OP_LSSI_CVB: case OP_EQUI_VVB: case OP_EQUI_VCB:
			case OP_NLSI_VVB: case OP_NLSI_VCB: case OP_NLSI_CVB: case OP_NEQI_VVB: case OP_NEQI_VCB: {
				Cond c; bool c0const, c1const;
				switch (op) {
					case OP_LSSI_VVB: c = LT; c0const = false; c1const = false; break;
					case OP_LSSI_VCB: c = LT; c0const = false; c1const = true; break;
					case OP_LSSI_CVB: c = LT; c0const = true; c1const = false; break;
					case OP_EQUI_VVB: c = EQ; c0const = false; c1const = false; break;
					case OP_EQUI_VCB: c = EQ; c0const = false; c1const = true; break;
					case OP_NLSI_VVB: c = GE; c0const = false; c1const = false; break;
					case OP_NLSI_VCB: c = GE; c0const = false; c1const = true; break;
					case OP_NLSI_CVB: c = GE; c0const = true; c1const = false; break;
					case OP_NEQI_VVB: c = NE; c0const = false; c1const = false; break;
					case OP_NEQI_VCB: c = NE; c0const = false; c1const = true; break;
					default: return false;							// unsupported opcode
				}
				loadOp(e, W10, in.p0, c0const);
				loadOp(e, W11, in.p1, c1const);
				e.cmp(W10, W11);
				e.bcond(c, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;
			}
			default: return false;									// unsupported opcode → caller falls back to the interpreter
		}
	}

	// Cold section: §5.7.5 suspend stubs. Each writes state back, points RESUME at its block's hot mainline (the
	// dispatcher reloads the pins on re-entry, so no per-block reload trampoline is needed), and branches to EXIT.
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		e.bind(suspendL[it->first]);
		writebackState(e, o);
		e.adr(X9, mainline[it->first]); e.strX(X9, X0, o.resume);		// RESUME = this block's hot mainline
		e.movn(W0, 0); e.b(exitLabel);									// TIME_OUT
	}
	return true;
}

/*
	Emit the native dispatcher (§5.4 encoding (b)). `int dispatch(JitProcessor* ctx)`: park ctx in a callee-saved reg,
	reload the pins from ctx once, then branch into RESUME. Segments tail-branch among themselves for GAZL calls/returns
	and call natives inline (no host round-trip per call/return); a suspend, a top-level return, or a trap branches to
	EXIT with the Status in w0. Returns the trampoline's word offset in the buffer.
*/
static size_t emitDispatcher(Arm64Emitter& e, const Offsets& o, Label exitLabel) {
	const size_t entry = e.wordCount();
	e.subImmX(SP, SP, 16); e.strX(X19, SP, 0); e.strX(X30, SP, 8);	// save the ctx holder (x19) + return addr (x30)
	e.addImmX(X19, X0, 0);								// x19 = ctx (callee-saved → survives inline native calls)
	reloadState(e, o);									// load the pins from ctx once (x0 = ctx)
	e.ldrX(X9, X0, o.resume); e.br(X9);					// branch into RESUME (hot; pins live)
	e.bind(exitLabel);									// segments branch here with w0 = Status (suspend / finish / trap)
	e.ldrX(X19, SP, 0); e.ldrX(X30, SP, 8); e.addImmX(SP, SP, 16); e.ret();
	return entry;
}

/*
	JitCompiler — the JIT's counterpart of Assembler: lowers a whole finalized program to native code and fills a
	JitModule (the executable page's dispatcher + ordinal→native-entry table, which the module then owns). Targets the
	static JitProcessor::layout() ABI; never touches a processor instance. This is where the substrate above (Arm64Emitter +
	lowerFunction + emitDispatcher) meets makeExecutable() to publish.
*/
void JitCompiler::compile(const Instruction* code, UInt functionCount, const UInt* functionTable,
		const Value* memory, JitModule& out) {
	// `out` starts empty (ok() == false); we only populate it once the whole program has lowered and published.
	const Offsets o = JitProcessor::layout();		// the run-state ABI, obtained without an engine
	Arm64Emitter e;
	std::vector<Label> entryLabels(functionCount);
	std::vector<size_t> entryOffset(functionCount, 0);
	for (UInt k = 0; k < functionCount; ++k) { entryLabels[k] = e.newLabel(); }
	Label exitLabel = e.newLabel();					// the one dispatcher EXIT; every segment terminal branches here (§5.4 (b))
	for (UInt ord = 0; ord < functionCount; ++ord) {
		if (!lowerFunction(e, code, memory, functionTable[ord], o, entryLabels, entryOffset, ord, functionCount, exitLabel)) {
			return;								// unsupported opcode → caller should fall back to the interpreter
		}
	}
	const size_t dispatchOffset = emitDispatcher(e, o, exitLabel);
	e.finalize();
	const size_t words = e.wordCount();
	// AArch64 offsets are in words; the shared publish step takes bytes, so scale by 4.
	std::vector<size_t> entryByteOffsets(functionCount);
	for (UInt ord = 0; ord < functionCount; ++ord) { entryByteOffsets[ord] = entryOffset[ord] * 4; }
	publishModule(out, e.code(), words, entryByteOffsets.empty() ? 0 : &entryByteOffsets[0], functionCount
			, dispatchOffset * 4);
}

} // namespace GAZL
