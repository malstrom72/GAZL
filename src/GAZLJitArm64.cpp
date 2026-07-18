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
#include "GAZLJitMem.h"																									// makeExecutable() - platform-specific backend, architecture-neutral

#include <stdint.h>
#include "assert.h"
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
	orr(wd, WZR, wm);																									// canonical register move: `orr wd, wzr, wm`
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
	subs(WZR, wn, wm);																									// `cmp` is `subs wzr, wn, wm`
}

void Arm64Emitter::cmpImm(Reg wn, uint32_t imm12) {
	subsImm(WZR, wn, imm12);																							// `cmp` is `subs wzr, wn, #imm12`
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
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);														// unsigned offset, scaled by 4
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
	assert(simm9 >= -256 && simm9 <= 255);																				// unscaled signed 9-bit byte offset
	emit(0xB8400000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::sturW(Reg wt, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xB8000000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | wt);
}

// --- doubleword loads / stores (64-bit) ---

void Arm64Emitter::ldrX(Reg xt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 7u) == 0 && (byteOffset >> 3) < 0x1000);														// unsigned offset, scaled by 8
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
	branch(0x10000000u | xd, target.id, FIXUP_ADR);																		// PC-relative; displacement patched by finalize()
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
	emit(0xEB00001Fu | (static_cast<uint32_t>(xm) << 16) | (static_cast<uint32_t>(xn) << 5));							// subs xzr, xn, xm
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
	emit(0x1E20C000u | (static_cast<uint32_t>(sn) << 5) | sd);															// FP data-proc (1 source), single, opcode ABS
}

void Arm64Emitter::frintmS(Reg sd, Reg sn) {
	emit(0x1E254000u | (static_cast<uint32_t>(sn) << 5) | sd);															// round toward -inf (floorf)
}

void Arm64Emitter::fcvtzs(Reg wd, Reg sn) {
	emit(0x1E380000u | (static_cast<uint32_t>(sn) << 5) | wd);															// FTOI, round toward zero (saturating)
}

void Arm64Emitter::scvtf(Reg sd, Reg wn) {
	emit(0x1E220000u | (static_cast<uint32_t>(wn) << 5) | sd);															// ITOF
}

void Arm64Emitter::fmovSW(Reg sd, Reg wn) {
	emit(0x1E270000u | (static_cast<uint32_t>(wn) << 5) | sd);															// bit-copy Wn -> Sd (no conversion)
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
	emit(base);																											// displacement is patched in by finalize()
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
		const ptrdiff_t disp = target - static_cast<ptrdiff_t>(f.site);													// PC-relative, in instruction words
		if (f.kind == FIXUP_IMM26) {
			words[f.site] |= (static_cast<uint32_t>(disp) & 0x03FFFFFFu);
		} else if (f.kind == FIXUP_IMM19) {
			words[f.site] |= ((static_cast<uint32_t>(disp) & 0x0007FFFFu) << 5);
		} else {																										// FIXUP_ADR: 21-bit byte displacement, split lo/hi
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
/*
	Frame slots are Value-indices off dsp (x1). ldur/stur reach ±64 words; far slots (big frames / LOCA arrays) fall back
	to a register-offset load (index in W13 - kept distinct from the W9..W12 operand scratches). See task #23.
*/
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
/*
	PEEK/POKE at a compile-time-constant memory word index (off memoryBase, X2). The scaled imm12 form reaches word
	index 4095 (16 KB in); past that - large globals/consts such as audio tables - materialize the index and use the
	scaled register-offset form, mirroring loadSlot/storeSlot. W13 is the dedicated addressing scratch (kept distinct
	from the W9..W12 operand scratches), so callers may still hold a value in W9..W12 across these.
*/
static void loadMemConst(Arm64Emitter& e, Reg r, uint32_t wordIndex) {
	if (wordIndex < 0x1000u) { e.ldrW(r, X2, wordIndex * 4u); }
	else { matConst(e, W13, static_cast<Int>(wordIndex)); e.ldrWx(r, X2, W13); }
}
static void storeMemConst(Arm64Emitter& e, Reg r, uint32_t wordIndex) {
	if (wordIndex < 0x1000u) { e.strW(r, X2, wordIndex * 4u); }
	else { matConst(e, W13, static_cast<Int>(wordIndex)); e.strWx(r, X2, W13); }
}

/*
	arm64 register pool + fill/spill backend (§5.7). Caller-saved registers only (no prologue save), disjoint from the
	W9-W15 fixed scratch of uncached opcodes. General entries are X-registers, float entries V-registers.
*/
static const int ARM64_GENERAL_POOL[] = { W5, W6, W7, W8, W16, W17 };
static const int ARM64_FLOAT_POOL[] = { 16, 17, 18, 19, 20, 21, 22, 23 };												// V16-V23 (caller-saved; unused until floats are cached)

namespace {
class Arm64SlotBackend : public RegisterCacheBackend {
	public:		Arm64SlotBackend(Arm64Emitter& emitter) : e(emitter) { }
	public:		virtual void emitFill(int physicalRegister, Int slot, RegisterClass registerClass) {
					const Reg r = static_cast<Reg>(physicalRegister);
					if (registerClass == GENERAL_REGISTER) { loadSlot(e, r, slot); } else { loadSlotF(e, r, slot); }
				}
	public:		virtual void emitSpill(Int slot, int physicalRegister, RegisterClass registerClass) {
					const Reg r = static_cast<Reg>(physicalRegister);
					if (registerClass == GENERAL_REGISTER) { storeSlot(e, r, slot); } else { storeSlotF(e, r, slot); }
				}
	private:	Arm64Emitter& e;
};

// Store every entry of a captured dirty snapshot to its home (terminal trap arms; see RegisterCache::captureDirtyLines).
static void emitDirtyStores(Arm64Emitter& e, const ResidencyMap& map) {
	for (size_t k = 0; k < map.entries.size(); ++k) {
		const ResidencyMap::Entry& entry = map.entries[k];
		const Reg r = static_cast<Reg>(entry.physicalRegister);
		if (entry.registerClass == GENERAL_REGISTER) { storeSlot(e, r, entry.slot); } else { storeSlotF(e, r, entry.slot); }
	}
}

// A checked op's terminal trap, deferred to the function's cold section so the hot path stays branch-and-continue.
struct ColdTrap {
	Label label;
	ResidencyMap dirty;								// the captureDirtyLines snapshot to store before exiting
	unsigned statusComplement;						// movn immediate: ~1 = BAD_PEEK, ~2 = BAD_POKE, ~6 = DIVISION_BY_ZERO
};
}

// Opcodes whose operands route through the RegisterCache; everything else barriers the cache and lowers as v1 (§5.7).
static bool cacheLowered(Int op) {
	switch (op) {
		case OP_MOVE_VV: case OP_MOVE_VC: case OP_ABSI:
		case OP_ADDI_VVV: case OP_ADDI_VVC:
		case OP_SUBI_VVV: case OP_SUBI_VVC: case OP_SUBI_VCV:
		case OP_MULI_VVV: case OP_MULI_VVC:
		case OP_ANDI_VVV: case OP_ANDI_VVC:
		case OP_IORI_VVV: case OP_IORI_VVC:
		case OP_XORI_VVV: case OP_XORI_VVC:
		case OP_SHLI_VVV: case OP_SHLI_VVC: case OP_SHLI_VCV:
		case OP_SHRI_VVV: case OP_SHRI_VVC: case OP_SHRI_VCV:
		case OP_SHRU_VVV: case OP_SHRU_VVC: case OP_SHRU_VCV:
		case OP_ABSF: case OP_FLOF:
		case OP_ADDF_VVV: case OP_ADDF_VVC:
		case OP_SUBF_VVV: case OP_SUBF_VVC: case OP_SUBF_VCV:
		case OP_MULF_VVV: case OP_MULF_VVC:
		case OP_DIVF_VVV: case OP_DIVF_VVC: case OP_DIVF_VCV:
		case OP_FTOI_VVC: case OP_ITOF_VVC:
		case OP_PEEK_VC: case OP_POKE_CV: case OP_POKE_CC: case OP_ADRL:
		case OP_PEEK_VCV: case OP_PEEK_VVV:
		case OP_POKE_CVV: case OP_POKE_CVC: case OP_POKE_VVV: case OP_POKE_VVC:
		case OP_GETL_VVV: case OP_SETL_VVV: case OP_SETL_VVC:
		case OP_DIVI_VVV: case OP_DIVI_VVC: case OP_DIVI_VCV:
		case OP_MODI_VVV: case OP_MODI_VVC: case OP_MODI_VCV:
		case OP_FORi_VVB: case OP_FORi_VCB:
		case OP_LSSI_VVB: case OP_LSSI_VCB: case OP_LSSI_CVB:
		case OP_EQUI_VVB: case OP_EQUI_VCB:
		case OP_NLSI_VVB: case OP_NLSI_VCB: case OP_NLSI_CVB:
		case OP_NEQI_VVB: case OP_NEQI_VCB:
		case OP_LSSF_VVB: case OP_LSSF_VCB: case OP_LSSF_CVB:
		case OP_EQUF_VVB: case OP_EQUF_VCB:
		case OP_NLSF_VVB: case OP_NLSF_VCB: case OP_NLSF_CVB:
		case OP_NEQF_VVB: case OP_NEQF_VCB:
		case OP_GOTO:																									// flushes inside its case: reconcile (backward, qualified) or barrier
			return true;
		default:
			return false;
	}
}

// `dst = s1 <op> s2` through the register cache; s1 is a slot (VVV/VVC) or a const (VCV), s2 a const (VVC) or a slot.
static void emitBinary(Arm64Emitter& e, RegisterCache& cache, void (Arm64Emitter::*op)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	int a;
	if (s1Const) { a = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(a), in.p1.i); }
	else { a = cache.read(in.p1.i, GENERAL_REGISTER); }
	int b;
	if (s2Const) { b = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(b), in.p2.i); }
	else { b = cache.read(in.p2.i, GENERAL_REGISTER); }
	const int d = cache.define(in.p0.i, GENERAL_REGISTER);
	(e.*op)(static_cast<Reg>(d), static_cast<Reg>(a), static_cast<Reg>(b));
	cache.endInstruction();
}

// Load a float operand into a cache register: a float slot (read), or a float constant materialized via a GP scratch + fmov.
static int loadFloatOperand(Arm64Emitter& e, RegisterCache& cache, const Value& p, bool isConst) {
	if (!isConst) { return cache.read(p.i, FLOAT_REGISTER); }
	const int bits = cache.scratch(GENERAL_REGISTER);
	const int s = cache.scratch(FLOAT_REGISTER);
	matConst(e, static_cast<Reg>(bits), p.i);
	e.fmovSW(static_cast<Reg>(s), static_cast<Reg>(bits));
	return s;
}
// `dst = s1 <fop> s2` on float slots/consts, through the register cache.
static void emitBinaryF(Arm64Emitter& e, RegisterCache& cache, void (Arm64Emitter::*fop)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	const int a = loadFloatOperand(e, cache, in.p1, s1Const);
	const int b = loadFloatOperand(e, cache, in.p2, s2Const);
	const int d = cache.define(in.p0.i, FLOAT_REGISTER);
	(e.*fop)(static_cast<Reg>(d), static_cast<Reg>(a), static_cast<Reg>(b));
	cache.endInstruction();
}

// DIVf with a runtime divisor: the interpreter traps a zero divisor (GAZL.cpp CHECK_FLOAT_DIV_BY_ZERO), so match it.
// VVC (const divisor) is assemble-time-checked, so it stays on emitBinaryF.
static void emitDivFChecked(Arm64Emitter& e, RegisterCache& cache, const Instruction& in, bool s1Const, std::vector<ColdTrap>& coldTraps) {
	const int a = loadFloatOperand(e, cache, in.p1, s1Const);
	const int b = loadFloatOperand(e, cache, in.p2, false);
	ColdTrap trap; trap.label = e.newLabel(); trap.statusComplement = 6;												// ~6 = -7 = DIVISION_BY_ZERO
	const int zero = cache.scratch(FLOAT_REGISTER);
	e.fmovSW(static_cast<Reg>(zero), WZR);																				// 0.0f
	e.fcmpS(static_cast<Reg>(b), static_cast<Reg>(zero));
	cache.captureDirtyLines(trap.dirty);																				// the trap exit must leave memory interpreter-identical
	e.bcond(EQ, trap.label);																							// == 0 (ordered) → trap arm, cold; NaN is unordered → divide
	coldTraps.push_back(trap);
	const int d = cache.define(in.p0.i, FLOAT_REGISTER);
	e.fdivS(static_cast<Reg>(d), static_cast<Reg>(a), static_cast<Reg>(b));
	cache.endInstruction();
}

/*
	Emit a shift `dst = value <shift> count`. kind: 0=lsl, 1=asr (SHRi), 2=lsr (SHRu). form: 0=VVV, 1=VVC, 2=VCV.
	value is p1 (a slot for VVV/VVC, a const for VCV); count is p2 (a const for VVC, else a slot, masked mod 32 by HW).
*/
static void emitShift(Arm64Emitter& e, RegisterCache& cache, int kind, const Instruction& in, int form) {
	int v;																												// value: a const for VCV (form 2), else a slot
	if (form == 2) { v = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(v), in.p1.i); }
	else { v = cache.read(in.p1.i, GENERAL_REGISTER); }
	if (form == 1) {																									// VVC: constant count → immediate shift
		const unsigned sh = static_cast<unsigned>(in.p2.i) & 31u;
		const int d = cache.define(in.p0.i, GENERAL_REGISTER);
		if (kind == 0) { e.lslImm(static_cast<Reg>(d), static_cast<Reg>(v), sh); }
		else if (kind == 1) { e.asrImm(static_cast<Reg>(d), static_cast<Reg>(v), sh); }
		else { e.lsrImm(static_cast<Reg>(d), static_cast<Reg>(v), sh); }
	} else {																											// register count (VVV/VCV): count is a slot, masked mod 32 by HW
		const int cnt = cache.read(in.p2.i, GENERAL_REGISTER);
		const int d = cache.define(in.p0.i, GENERAL_REGISTER);
		if (kind == 0) { e.lslv(static_cast<Reg>(d), static_cast<Reg>(v), static_cast<Reg>(cnt)); }
		else if (kind == 1) { e.asrv(static_cast<Reg>(d), static_cast<Reg>(v), static_cast<Reg>(cnt)); }
		else { e.lsrv(static_cast<Reg>(d), static_cast<Reg>(v), static_cast<Reg>(cnt)); }
	}
	cache.endInstruction();
}

/*
	Emit an integer divide (rem=false) or modulo (rem=true) `dst = s1 </%> s2`. form: 0=VVV, 1=VVC, 2=VCV. A variable
	divisor (VVV/VCV) gets a divide-by-zero guard → DIVISION_BY_ZERO; a VVC divisor is a nonzero assembler constant.
*/
static void emitDivMod(Arm64Emitter& e, RegisterCache& cache, bool rem, const Instruction& in, int form, std::vector<ColdTrap>& coldTraps) {
	int dividend;
	if (form == 2) { dividend = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(dividend), in.p1.i); }
	else { dividend = cache.read(in.p1.i, GENERAL_REGISTER); }
	int divisor;
	if (form == 1) { divisor = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(divisor), in.p2.i); }
	else { divisor = cache.read(in.p2.i, GENERAL_REGISTER); }
	if (form != 1) {																									// variable divisor → divide-by-zero guard
		ColdTrap trap; trap.label = e.newLabel(); trap.statusComplement = 6;											// ~6 = -7 = DIVISION_BY_ZERO
		cache.captureDirtyLines(trap.dirty);																			// the trap exit must leave memory interpreter-identical
		e.cbz(static_cast<Reg>(divisor), trap.label);																	// trap arm is cold, after the mainline
		coldTraps.push_back(trap);
	}
	if (rem) {																											// modulo: msub needs the dividend intact, so quotient goes to a distinct scratch
		const int q = cache.scratch(GENERAL_REGISTER);
		e.sdiv(static_cast<Reg>(q), static_cast<Reg>(dividend), static_cast<Reg>(divisor));
		const int d = cache.define(in.p0.i, GENERAL_REGISTER);
		e.msub(static_cast<Reg>(d), static_cast<Reg>(q), static_cast<Reg>(divisor), static_cast<Reg>(dividend));		// dividend - q*divisor
	} else {																											// divide: one instruction, so writing dst over an operand is safe
		const int d = cache.define(in.p0.i, GENERAL_REGISTER);
		e.sdiv(static_cast<Reg>(d), static_cast<Reg>(dividend), static_cast<Reg>(divisor));								// INT_MIN/-1 → INT_MIN, matches §6.1
	}
	cache.endInstruction();
}

/*
	Lower one function at `funcIndex` into `e` (appended). Emits an entry reload + FUNC prologue, a mainline per block,
	cold reload trampolines + §5.7.5 suspend stubs for loop heads, and the §5.4 call/return transfers. `entryLabels[ord]`
	are pre-created (for direct calls); `entryOffset[selfOrdinal]` is set to this function's native word offset. Returns
	false on an unsupported opcode.
*/
void JitCompilerArm64::lowerFunction(Arm64Emitter& e, const Instruction* code, const Value* memory, UInt funcIndex, const Offsets& o,
		std::vector<Label>& entryLabels, std::vector<size_t>& entryOffset, UInt selfOrdinal, UInt functionCount, Label exitLabel) {
	UInt retIndex = funcIndex;
	while (code[retIndex].opcode != OP_RETU) { ++retIndex; }

	/*
		Pass 1 - fuel safepoints: every basic-block leader (arch-neutral, §5.5), each charged its block weight. Every leader
		is a resumable point, so each gets a mainline label (hot entry + branch + resume target), a reload trampoline, and a
		suspend stub. Charging per block (not just loop heads) makes fuel spend ≈ the interpreter's, so straight-line and
		recursive code yields on time too.
	*/
	std::map<UInt, UInt> loopWeight;
	jitFuelSafepoints(code, funcIndex, retIndex, memory, loopWeight);
	std::map<UInt, Label> mainline, suspendL;
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		mainline[it->first] = e.newLabel(); suspendL[it->first] = e.newLabel();
	}

	/*
		Function entry (hot - reached by a tail-branch from a caller or by the dispatcher after it reloaded the pins, so
		state is already live). FUNC prologue: dsp += localsSize.
	*/
	entryOffset[selfOrdinal] = e.wordCount();
	e.bind(entryLabels[selfOrdinal]);
	const UInt localsSize = static_cast<UInt>(code[funcIndex].p0.i);
	if (localsSize != 0) {																								// dsp += localsSize (in bytes); register add if beyond the imm12 range
		if (localsSize * 4 < 0x1000) { e.addImmX(X1, X1, localsSize * 4); }
		else { matConst(e, W9, static_cast<Int>(localsSize * 4)); e.addX(X1, X1, X9); }
	}
	{																													// FUNC stack-overflow: if (dsp + paramsSize > dataStackEnd) DATA_STACK_OVERFLOW
		const UInt paramsSize = static_cast<UInt>(code[funcIndex].p1.i);
		Label sok = e.newLabel();
		e.ldrX(X9, X0, o.dsend); e.addImmX(X10, X1, paramsSize * 4); e.cmpX(X10, X9);
		e.bcond(LS, sok); e.movn(W0, 4); e.b(exitLabel);																// > end → ~4 = -5 = DATA_STACK_OVERFLOW
		e.bind(sok);
	}

	Arm64SlotBackend slotBackend(e);
	const RegisterPool registerPool = { ARM64_GENERAL_POOL, sizeof(ARM64_GENERAL_POOL) / sizeof(ARM64_GENERAL_POOL[0])
			, ARM64_FLOAT_POOL, sizeof(ARM64_FLOAT_POOL) / sizeof(ARM64_FLOAT_POOL[0]) };
	RegisterCache cache(registerPool, slotBackend);
	UseSchedule useSchedule;
	buildUseSchedule(code, funcIndex, retIndex, useSchedule);															// Belady next-read lists (§5.7 v2.0.5)
	cache.setUseSchedule(&useSchedule);
	std::map<UInt, UInt> loopExtent;
	jitResidencyLeaders(code, funcIndex, retIndex, memory, loopExtent);													// v2.2: loop headers whose entry state stays register-resident
	std::map<UInt, ResidencyMap> entryMaps;
	std::vector<ColdTrap> coldTraps;																					// checked-op trap arms, emitted after the mainline

	// Pass 2 - emit.
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		cache.setInstructionIndex(j);
		if (mainline.count(j)) {
			std::map<UInt, UInt>::const_iterator loopIt = loopExtent.find(j);
			if (loopIt != loopExtent.end()) {																			// loop header: the fall-through state (pruned) becomes the fixed entry state
				std::set<Int> readSlots, writtenSlots;
				buildLoopSlotSets(code, j, loopIt->second, readSlots, writtenSlots);
				cache.capture(entryMaps[j], readSlots, writtenSlots);
			} else { cache.barrier(); }																					// any other leader: starts empty as in v2.0
			e.bind(mainline[j]);
		}
		if (loopWeight.count(j)) { e.subsImm(W3, W3, loopWeight[j]); e.bcond(MI, suspendL[j]); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		if (!cacheLowered(op)) { cache.barrier(); }																		// uncached opcode: lower it as v1 over an empty cache
		switch (op) {
			case OP_FUNC: continue;																						// prologue stack/fuel check omitted for the prototype
			case OP_RETU: {
				Label notNative = e.newLabel();
				e.subImmX(X4, X4, 16);																					// ipsp-- ; pop {cont, dsp}
				e.ldrX(X9, X4, 0); e.ldrX(X1, X4, 8);																	// cont ; caller dsp (or 0 = native/top marker)
				e.cbnzX(X1, notNative);
				e.subImmX(X4, X4, 16); e.ldrX(X1, X4, 8);																// native/top return: pop again for the true dsp
				writebackState(e, o);
				e.movz(W0, 0); e.b(exitLabel);																			// OK - terminal (return to host)
				e.bind(notNative);
				e.br(X9);																								// GAZL return: tail-branch to the continuation (state live)
				break;
			}
			case OP_CALL_CVC: {
				const UInt callee = in.p0.p - IP_OFFSET;																// ordinal known at compile time → direct tail-branch
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = e.newLabel(), iok = e.newLabel();
				e.ldrX(X9, X0, o.ipsend); e.cmpX(X4, X9); e.bcond(LO, iok);												// ipsp >= ipStackEnd → IP_STACK_OVERFLOW
				e.movn(W0, 5); e.b(exitLabel); e.bind(iok);																// ~5 = -6
				e.adr(X9, after); e.strX(X9, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);							// push {after, dsp}
				if (window != 0) { e.addImmX(X1, X1, window * 4); }														// dsp += arg window
				e.b(entryLabels[callee]);																				// tail-branch to the callee entry (state stays live)
				e.bind(after);																							// return lands here (hot; state live)
				break;
			}
			case OP_CALL_VVC: {
				const UInt window = static_cast<UInt>(in.p1.i);															// ordinal from a slot at runtime → resolve + bounds-check
				Label after = e.newLabel(), trap = e.newLabel(), iok = e.newLabel();
				e.ldrX(X9, X0, o.ipsend); e.cmpX(X4, X9); e.bcond(LO, iok);												// ipsp >= ipStackEnd → IP_STACK_OVERFLOW
				e.movn(W0, 5); e.b(exitLabel); e.bind(iok);
				loadSlot(e, W9, in.p0.i);																				// fn pointer = IP_OFFSET + ordinal
				matConst(e, W10, static_cast<Int>(IP_OFFSET)); e.sub(W9, W9, W10);										// ordinal
				matConst(e, W10, static_cast<Int>(functionCount));
				e.cmp(W9, W10); e.bcond(HS, trap);																		// ordinal >= functionCount → BAD_CALL
				e.ldrX(X10, X0, o.funcentries); e.ldrXr(X9, X10, W9);													// entry = funcEntries[ordinal] (hot)
				e.adr(X10, after); e.strX(X10, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);						// push {after, dsp}
				if (window != 0) { e.addImmX(X1, X1, window * 4); }
				e.br(X9);																								// tail-branch to the callee entry (state stays live)
				e.bind(trap); e.movn(W0, 3); e.b(exitLabel);															// ~3 = -4 = BAD_CALL
				e.bind(after);
				break;
			}
			case OP_CALL_NVC: {
				/*
					Re-entrancy-safe shape, mirroring the x64 backend (see its OP_CALL_NVC comment): post-call state
					comes from ctx fields a nested enterCall()+run() episode leaves stable (reloadState's ctx.dsp IS
					the published window; the sentinel discipline restores it); the OK path continues INDIRECTLY
					through ctx.nativeafter so JitProcessor::pushCall can retarget it at a pushed callee's entry;
					`after` re-arms the guard and normalizes dsp to the caller frame (pushed frames store the WINDOW
					uniformly); RESUME = hot is set on the nonzero path, which republishes the CALLER dsp because
					the hot re-issue advances it again.
				*/
				const UInt ordinal = static_cast<UInt>(in.p0.i);														// native ordinal (C0)
				const UInt window = static_cast<UInt>(in.p1.i);															// param-window offset (C1)
				Label hot = e.newLabel(), retry = e.newLabel(), after = e.newLabel();
				e.bind(hot);																							// re-entry for blocking retry / suspend-resume
				if (window != 0) { e.addImmX(X1, X1, window * 4); }
				e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);									// publish window/fuel/ipsp (interpreter-shaped)
				e.adr(X9, after); e.strX(X9, X0, o.nativeafter);														// redirectable OK continuation (pushCall retargets)
				e.ldrX(X9, X0, o.natives); e.ldrX(X9, X9, ordinal * 8);													// natives[ordinal]
				e.blr(X9);																								// inline host call (x0 = ctx); w0 = status
				e.mov(W12, W0);																							// save the status BEFORE we overwrite x0 with ctx
				e.addImmX(X0, X19, 0); reloadState(e, o);																// restore ctx + reload; x1 = ctx.dsp = the WINDOW, x4 adopts pushCall frames
				e.cmpImm(W12, 0); e.bcond(NE, retry);
				e.ldrX(X9, X0, o.nativeafter); e.br(X9);																// OK: `after`, or the last-pushed callee's entry
				e.bind(retry);
				if (window != 0) {																						// publish the CALLER dsp (register AND ctx): the hot
					e.subImmX(X1, X1, window * 4);																		// re-issue reloads ctx.dsp and advances it again
					e.strX(X1, X0, o.dsp);
				}
				e.adr(X9, hot); e.strX(X9, X0, o.resume);																// RESUME = call site (blocking retry / suspend re-issue)
				e.mov(W0, W12); e.b(exitLabel);																			// nonzero (BLOCK_RETRY / TIME_OUT / trap): return to host
				e.bind(after);
				e.strX(XZR, X0, o.nativeafter);																			// re-arm the "only inside a native call" guard
				if (window != 0) { e.subImmX(X1, X1, window * 4); }														// normalize to the caller frame base
																	// state live; w3 holds ctx.fuel - a native that yielded via resetTimeOut(0) leaves it 0
																	// and the MANDATORY fuel check at the next block leader suspends immediately.
				break;
			}
			case OP_MOVE_VC: { const int d = cache.define(in.p0.i, GENERAL_REGISTER); matConst(e, static_cast<Reg>(d), in.p1.i); cache.endInstruction(); break; }
			case OP_MOVE_VV: { const int s = cache.read(in.p1.i, GENERAL_REGISTER); const int d = cache.define(in.p0.i, GENERAL_REGISTER); e.mov(static_cast<Reg>(d), static_cast<Reg>(s)); cache.endInstruction(); break; }
			// constant-address global access: assembler-validated, cannot touch the frame, so no cache coherence is needed.
			case OP_PEEK_VC: { const int d = cache.define(in.p0.i, GENERAL_REGISTER); loadMemConst(e, static_cast<Reg>(d), static_cast<uint32_t>(in.p1.p - MEMORY_OFFSET)); cache.endInstruction(); break; }
			case OP_POKE_CV: { const int r = cache.read(in.p1.i, GENERAL_REGISTER); storeMemConst(e, static_cast<Reg>(r), static_cast<uint32_t>(in.p0.p - MEMORY_OFFSET)); cache.endInstruction(); break; }
			case OP_POKE_CC: { const int r = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(r), in.p1.i); storeMemConst(e, static_cast<Reg>(r), static_cast<uint32_t>(in.p0.p - MEMORY_OFFSET)); cache.endInstruction(); break; }
			case OP_PEEK_VCV: {																							// dst = memory[C1 + index]; globals/constants-realm read
				ColdTrap trap; trap.label = e.newLabel(); trap.statusComplement = 1;									// ~1 = -2 = BAD_PEEK
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				const int addr = cache.scratch(GENERAL_REGISTER);														// no flush: a const-base access cannot reach the frame (§1.1 realms)
				matConst(e, static_cast<Reg>(addr), static_cast<Int>(in.p1.p - MEMORY_OFFSET)); e.add(static_cast<Reg>(addr), static_cast<Reg>(addr), static_cast<Reg>(idx));
				const int lim = cache.scratch(GENERAL_REGISTER);
				e.ldrW(static_cast<Reg>(lim), X0, o.memsize); e.cmp(static_cast<Reg>(addr), static_cast<Reg>(lim));
				cache.captureDirtyLines(trap.dirty);																	// the trap exit must leave memory interpreter-identical
				e.bcond(HS, trap.label);																				// trap arm is cold, after the mainline
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.ldrWx(static_cast<Reg>(d), X2, static_cast<Reg>(addr));
				cache.endInstruction();
				coldTraps.push_back(trap);
				break;
			}
			case OP_PEEK_VVV: {																							// dst = memory[base + index]; pointer read
				Label trap = e.newLabel(), cont = e.newLabel();
				const int base = cache.read(in.p1.i, GENERAL_REGISTER);
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				cache.spillDirtyResident();
				const int addr = cache.scratch(GENERAL_REGISTER);
				const int tmp = cache.scratch(GENERAL_REGISTER);														// MEMORY_OFFSET, then reused for the size limit
				e.add(static_cast<Reg>(addr), static_cast<Reg>(base), static_cast<Reg>(idx));
				matConst(e, static_cast<Reg>(tmp), static_cast<Int>(MEMORY_OFFSET)); e.sub(static_cast<Reg>(addr), static_cast<Reg>(addr), static_cast<Reg>(tmp));
				e.ldrW(static_cast<Reg>(tmp), X0, o.memsize); e.cmp(static_cast<Reg>(addr), static_cast<Reg>(tmp)); e.bcond(HS, trap);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.ldrWx(static_cast<Reg>(d), X2, static_cast<Reg>(addr));
				cache.endInstruction();
				e.b(cont); e.bind(trap); e.movn(W0, 1); e.b(exitLabel);
				e.bind(cont);
				break;
			}
			case OP_POKE_CVV: case OP_POKE_CVC: {																		// memory[C0 + index] = value; globals-realm write
				ColdTrap trap; trap.label = e.newLabel(); trap.statusComplement = 2;									// ~2 = -3 = BAD_POKE
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_POKE_CVC) { val = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(val), in.p2.i); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				const int addr = cache.scratch(GENERAL_REGISTER);														// no flush: a const-base access cannot reach the frame (§1.1 realms)
				matConst(e, static_cast<Reg>(addr), static_cast<Int>(in.p0.p - MEMORY_OFFSET)); e.add(static_cast<Reg>(addr), static_cast<Reg>(addr), static_cast<Reg>(idx));
				const int lim = cache.scratch(GENERAL_REGISTER);
				e.ldrW(static_cast<Reg>(lim), X0, o.rwmemsize); e.cmp(static_cast<Reg>(addr), static_cast<Reg>(lim));
				cache.captureDirtyLines(trap.dirty);																	// the trap exit must leave memory interpreter-identical
				e.bcond(HS, trap.label);																				// trap arm is cold, after the mainline
				e.strWx(static_cast<Reg>(val), X2, static_cast<Reg>(addr));
				cache.endInstruction();
				coldTraps.push_back(trap);
				break;
			}
			case OP_POKE_VVV: case OP_POKE_VVC: {																		// memory[base + index] = value; pointer write
				Label trap = e.newLabel(), cont = e.newLabel();
				const int base = cache.read(in.p0.i, GENERAL_REGISTER);
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_POKE_VVC) { val = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(val), in.p2.i); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				cache.spillDirtyResident();
				const int addr = cache.scratch(GENERAL_REGISTER);
				const int tmp = cache.scratch(GENERAL_REGISTER);
				e.add(static_cast<Reg>(addr), static_cast<Reg>(base), static_cast<Reg>(idx));
				matConst(e, static_cast<Reg>(tmp), static_cast<Int>(MEMORY_OFFSET)); e.sub(static_cast<Reg>(addr), static_cast<Reg>(addr), static_cast<Reg>(tmp));
				e.ldrW(static_cast<Reg>(tmp), X0, o.rwmemsize); e.cmp(static_cast<Reg>(addr), static_cast<Reg>(tmp)); e.bcond(HS, trap);
				e.strWx(static_cast<Reg>(val), X2, static_cast<Reg>(addr));
				cache.endInstruction();
				cache.invalidateAll();
				e.b(cont); e.bind(trap); e.movn(W0, 2); e.b(exitLabel);
				e.bind(cont);
				break;
			}
			case OP_GETL_VVV: {																							// dst = frame[C1 + index]; frame read (bounds vs free frame span)
				Label trap = e.newLabel(), cont = e.newLabel();
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				cache.spillDirtyResident();																				// indexes the frame: may alias a cached slot
				const int lim = cache.scratch(GENERAL_REGISTER);
				e.ldrX(static_cast<Reg>(lim), X0, o.dsend); e.sub(static_cast<Reg>(lim), static_cast<Reg>(lim), W1); e.lsrImm(static_cast<Reg>(lim), static_cast<Reg>(lim), 2);
				const int c = cache.scratch(GENERAL_REGISTER);
				matConst(e, static_cast<Reg>(c), in.p1.i); e.sub(static_cast<Reg>(lim), static_cast<Reg>(lim), static_cast<Reg>(c)); // limit = (end-dsp)/4 - C1
				e.cmp(static_cast<Reg>(idx), static_cast<Reg>(lim)); e.bcond(HS, trap);
				e.add(static_cast<Reg>(c), static_cast<Reg>(c), static_cast<Reg>(idx));									// C1 + index (reuse c as the signed frame offset)
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.ldrWxs(static_cast<Reg>(d), X1, static_cast<Reg>(c));
				cache.endInstruction();
				e.b(cont); e.bind(trap); e.movn(W0, 1); e.b(exitLabel);
				e.bind(cont);
				break;
			}
			case OP_SETL_VVV: case OP_SETL_VVC: {																		// frame[C0 + index] = value; frame write
				Label trap = e.newLabel(), cont = e.newLabel();
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_SETL_VVC) { val = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(val), in.p2.i); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				cache.spillDirtyResident();
				const int lim = cache.scratch(GENERAL_REGISTER);
				e.ldrX(static_cast<Reg>(lim), X0, o.dsend); e.sub(static_cast<Reg>(lim), static_cast<Reg>(lim), W1); e.lsrImm(static_cast<Reg>(lim), static_cast<Reg>(lim), 2);
				const int c = cache.scratch(GENERAL_REGISTER);
				matConst(e, static_cast<Reg>(c), in.p0.i); e.sub(static_cast<Reg>(lim), static_cast<Reg>(lim), static_cast<Reg>(c)); // limit = (end-dsp)/4 - C0
				e.cmp(static_cast<Reg>(idx), static_cast<Reg>(lim)); e.bcond(HS, trap);
				e.add(static_cast<Reg>(c), static_cast<Reg>(c), static_cast<Reg>(idx));									// C0 + index
				e.strWxs(static_cast<Reg>(val), X1, static_cast<Reg>(c));
				cache.endInstruction();
				cache.invalidateAll();
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
				matConst(e, W15, in.p2.i);																				// count
				e.add(W12, W9, W15); e.ldrW(W14, X0, o.rwmemsize); e.cmp(W12, W14); e.bcond(HS, trap);					// destIdx+count < rwMemorySize
				e.add(W12, W10, W15); e.ldrW(W14, X0, o.memsize); e.cmp(W12, W14); e.bcond(HS, trap);					// srcIdx+count < memorySize
				e.movz(W11, 0);																							// i = 0
				e.bind(lp);
				e.cmp(W11, W15); e.bcond(HS, ldone);																	// i >= count → done
				e.add(W12, W10, W11); e.ldrWx(W12, X2, W12);															// val = memoryBase[srcIdx+i]
				e.add(W14, W9, W11); e.strWx(W12, X2, W14);																// memoryBase[destIdx+i] = val
				e.addImm(W11, W11, 1); e.b(lp);
				e.bind(ldone); e.b(cont);
				e.bind(trap); e.movn(W0, 7); e.b(exitLabel);															// ~7 = -8 = ACCESS_VIOLATION
				e.bind(cont);
				break;
			}
			case OP_ADRL: {																								// address of a frame local as a MEMORY_OFFSET-biased pointer
				const int t = cache.scratch(GENERAL_REGISTER);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.sub(static_cast<Reg>(t), W1, W2);																		// (dsp - memoryBase) in bytes (low 32 valid within buffer)
				e.lsrImm(static_cast<Reg>(t), static_cast<Reg>(t), 2);													//   -> Value units
				matConst(e, static_cast<Reg>(d), static_cast<Int>(MEMORY_OFFSET) + in.p1.i);
				e.add(static_cast<Reg>(d), static_cast<Reg>(d), static_cast<Reg>(t));
				cache.endInstruction();
				break;
			}
			case OP_ADDI_VVV: emitBinary(e, cache, &Arm64Emitter::add, in, false, false); break;
			case OP_ADDI_VVC: emitBinary(e, cache, &Arm64Emitter::add, in, false, true); break;
			case OP_SUBI_VVV: emitBinary(e, cache, &Arm64Emitter::sub, in, false, false); break;
			case OP_SUBI_VVC: emitBinary(e, cache, &Arm64Emitter::sub, in, false, true); break;
			case OP_SUBI_VCV: emitBinary(e, cache, &Arm64Emitter::sub, in, true, false); break;
			case OP_MULI_VVV: emitBinary(e, cache, &Arm64Emitter::mul, in, false, false); break;
			case OP_MULI_VVC: emitBinary(e, cache, &Arm64Emitter::mul, in, false, true); break;
			case OP_DIVI_VVV: emitDivMod(e, cache, false, in, 0, coldTraps); break;
			case OP_DIVI_VVC: emitDivMod(e, cache, false, in, 1, coldTraps); break;
			case OP_DIVI_VCV: emitDivMod(e, cache, false, in, 2, coldTraps); break;
			case OP_MODI_VVV: emitDivMod(e, cache, true, in, 0, coldTraps); break;
			case OP_MODI_VVC: emitDivMod(e, cache, true, in, 1, coldTraps); break;
			case OP_MODI_VCV: emitDivMod(e, cache, true, in, 2, coldTraps); break;
			case OP_SHLI_VVV: emitShift(e, cache, 0, in, 0); break;
			case OP_SHLI_VVC: emitShift(e, cache, 0, in, 1); break;
			case OP_SHLI_VCV: emitShift(e, cache, 0, in, 2); break;
			case OP_SHRI_VVV: emitShift(e, cache, 1, in, 0); break;
			case OP_SHRI_VVC: emitShift(e, cache, 1, in, 1); break;
			case OP_SHRI_VCV: emitShift(e, cache, 1, in, 2); break;
			case OP_SHRU_VVV: emitShift(e, cache, 2, in, 0); break;
			case OP_SHRU_VVC: emitShift(e, cache, 2, in, 1); break;
			case OP_SHRU_VCV: emitShift(e, cache, 2, in, 2); break;
			case OP_ABSI: {
				const int s = cache.read(in.p1.i, GENERAL_REGISTER);
				const int t = cache.scratch(GENERAL_REGISTER);															// sign mask (v >> 31)
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);													// (v ^ mask) - mask = |v|
				e.asrImm(static_cast<Reg>(t), static_cast<Reg>(s), 31);
				e.eor(static_cast<Reg>(d), static_cast<Reg>(s), static_cast<Reg>(t));
				e.sub(static_cast<Reg>(d), static_cast<Reg>(d), static_cast<Reg>(t));
				cache.endInstruction();
				break;
			}
			case OP_ABSF: { const int s = cache.read(in.p1.i, FLOAT_REGISTER); const int d = cache.define(in.p0.i, FLOAT_REGISTER); e.fabsS(static_cast<Reg>(d), static_cast<Reg>(s)); cache.endInstruction(); break; } // |V|
			case OP_FLOF: { const int s = cache.read(in.p1.i, FLOAT_REGISTER); const int d = cache.define(in.p0.i, FLOAT_REGISTER); e.frintmS(static_cast<Reg>(d), static_cast<Reg>(s)); cache.endInstruction(); break; } // floorf(V)
			case OP_ADDF_VVV: emitBinaryF(e, cache, &Arm64Emitter::faddS, in, false, false); break;
			case OP_ADDF_VVC: emitBinaryF(e, cache, &Arm64Emitter::faddS, in, false, true); break;
			case OP_SUBF_VVV: emitBinaryF(e, cache, &Arm64Emitter::fsubS, in, false, false); break;
			case OP_SUBF_VVC: emitBinaryF(e, cache, &Arm64Emitter::fsubS, in, false, true); break;
			case OP_SUBF_VCV: emitBinaryF(e, cache, &Arm64Emitter::fsubS, in, true, false); break;
			case OP_MULF_VVV: emitBinaryF(e, cache, &Arm64Emitter::fmulS, in, false, false); break;
			case OP_MULF_VVC: emitBinaryF(e, cache, &Arm64Emitter::fmulS, in, false, true); break;
			case OP_DIVF_VVV: emitDivFChecked(e, cache, in, false, coldTraps); break;
			case OP_DIVF_VVC: emitBinaryF(e, cache, &Arm64Emitter::fdivS, in, false, true); break;						// const divisor: assemble-checked
			case OP_DIVF_VCV: emitDivFChecked(e, cache, in, true, coldTraps); break;
			case OP_FTOI_VVC: {																							// int(V * scale), scale = C2 (saturating fcvtzs)
				const int src = cache.read(in.p1.i, FLOAT_REGISTER);
				const int bits = cache.scratch(GENERAL_REGISTER);
				const int scale = cache.scratch(FLOAT_REGISTER);
				matConst(e, static_cast<Reg>(bits), in.p2.i); e.fmovSW(static_cast<Reg>(scale), static_cast<Reg>(bits));
				const int prod = cache.scratch(FLOAT_REGISTER);
				e.fmulS(static_cast<Reg>(prod), static_cast<Reg>(src), static_cast<Reg>(scale));
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.fcvtzs(static_cast<Reg>(d), static_cast<Reg>(prod));
				cache.endInstruction();
				break;
			}
			case OP_ITOF_VVC: {																							// float(V) * scale, scale = C2
				const int src = cache.read(in.p1.i, GENERAL_REGISTER);
				const int d = cache.define(in.p0.i, FLOAT_REGISTER);
				e.scvtf(static_cast<Reg>(d), static_cast<Reg>(src));
				const int bits = cache.scratch(GENERAL_REGISTER);
				const int scale = cache.scratch(FLOAT_REGISTER);
				matConst(e, static_cast<Reg>(bits), in.p2.i); e.fmovSW(static_cast<Reg>(scale), static_cast<Reg>(bits));
				e.fmulS(static_cast<Reg>(d), static_cast<Reg>(d), static_cast<Reg>(scale));
				cache.endInstruction();
				break;
			}
			case OP_ANDI_VVV: emitBinary(e, cache, &Arm64Emitter::and_, in, false, false); break;
			case OP_ANDI_VVC: emitBinary(e, cache, &Arm64Emitter::and_, in, false, true); break;
			case OP_IORI_VVV: emitBinary(e, cache, &Arm64Emitter::orr, in, false, false); break;
			case OP_IORI_VVC: emitBinary(e, cache, &Arm64Emitter::orr, in, false, true); break;
			case OP_XORI_VVV: emitBinary(e, cache, &Arm64Emitter::eor, in, false, false); break;
			case OP_XORI_VVC: emitBinary(e, cache, &Arm64Emitter::eor, in, false, true); break;
			case OP_FORi_VVB: case OP_FORi_VCB: {																		// ++counter; if (counter < limit) branch
				const int r = cache.read(in.p0.i, GENERAL_REGISTER);
				e.addImm(static_cast<Reg>(r), static_cast<Reg>(r), 1);
				cache.define(in.p0.i, GENERAL_REGISTER);																// same register, now dirty (barrier will spill the increment)
				int lim;
				if (op == OP_FORi_VCB) { lim = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(lim), in.p1.i); }
				else { lim = cache.read(in.p1.i, GENERAL_REGISTER); }
				e.cmp(static_cast<Reg>(r), static_cast<Reg>(lim));
				cache.endInstruction();
				reconcileOrBarrier(cache, entryMaps, static_cast<UInt>(static_cast<Int>(j) + in.p2.i));					// block ends here (loads/stores don't touch NZCV)
				e.bcond(LT, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;
			}
			case OP_GOTO: {
				const UInt target = static_cast<UInt>(static_cast<Int>(j) + in.p0.i);
				reconcileOrBarrier(cache, entryMaps, target);
				e.b(mainline[target]);
				break;
			}
			case OP_SWCH: {																								// index = min(unsigned(V0), C1); br into a table of `b target`
				const UInt sz = static_cast<UInt>(in.p1.i) + 1;
				const UInt tbl = static_cast<UInt>(in.p2.p - MEMORY_OFFSET);
				loadSlot(e, W9, in.p0.i);																				// switch value
				matConst(e, W10, in.p1.i);																				// clamp max = C1 = sz-1
				e.cmp(W9, W10);
				Label useVal = e.newLabel();
				e.bcond(LS, useVal);																					// (unsigned) val <= C1 → keep; else clamp to C1
				e.mov(W9, W10);
				e.bind(useVal);
				Label caseBase = e.newLabel();
				e.adr(X10, caseBase);																					// base of the branch table (below)
				e.lslImm(W11, W9, 2); e.addX(X10, X10, X11);															// += index * 4  (W-write zero-extends into X11)
				e.br(X10);
				e.bind(caseBase);																						// sz consecutive `b target` - br lands on the index'th
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
					default: c = NE; c0const = false; c1const = true; break;											// NEQF_VCB
				}
				const int a = loadFloatOperand(e, cache, in.p0, c0const);
				const int b = loadFloatOperand(e, cache, in.p1, c1const);
				e.fcmpS(static_cast<Reg>(a), static_cast<Reg>(b));
				cache.endInstruction();
				reconcileOrBarrier(cache, entryMaps, static_cast<UInt>(static_cast<Int>(j) + in.p2.i));					// block ends here (loads/stores don't touch NZCV)
				e.bcond(c, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;
			}
			case OP_LSSI_VVB: case OP_LSSI_VCB: case OP_LSSI_CVB: case OP_EQUI_VVB: case OP_EQUI_VCB:
			case OP_NLSI_VVB: case OP_NLSI_VCB: case OP_NLSI_CVB: case OP_NEQI_VVB: case OP_NEQI_VCB: {
				Cond c = LT; bool c0const = false, c1const = false;														// (the sub-switch is exhaustive; its default throws)
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
					default: throwUnlowerableOpcode(in.opcode);															// unreachable: the outer case only enters here for these
				}
				int a;
				if (c0const) { a = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(a), in.p0.i); }
				else { a = cache.read(in.p0.i, GENERAL_REGISTER); }
				int b;
				if (c1const) { b = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(b), in.p1.i); }
				else { b = cache.read(in.p1.i, GENERAL_REGISTER); }
				e.cmp(static_cast<Reg>(a), static_cast<Reg>(b));
				cache.endInstruction();
				reconcileOrBarrier(cache, entryMaps, static_cast<UInt>(static_cast<Int>(j) + in.p2.i));					// block ends here (loads/stores don't touch NZCV)
				e.bcond(c, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;
			}
			default: throwUnlowerableOpcode(op);																		// a finalized opcode the backend must cover (a bug, never routine)
		}
	}

	// Cold section: checked-op trap arms (see ColdTrap) - store the dirty snapshot, set the status, exit.
	for (size_t k = 0; k < coldTraps.size(); ++k) {
		e.bind(coldTraps[k].label);
		emitDirtyStores(e, coldTraps[k].dirty);
		e.movn(W0, coldTraps[k].statusComplement); e.b(exitLabel);
	}

	/*
		Cold section: §5.7.5 suspend stubs. Each writes state back and branches to EXIT. A leader with register-resident
		entry state (v2.2) first spills its ResidencyMap - memory must be interpreter-identical at a suspend - and parks
		RESUME at a reload stub that refills the map before re-entering the mainline; an empty map points RESUME straight
		at the hot mainline as before (the dispatcher reloads only the pins on re-entry).
	*/
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		e.bind(suspendL[it->first]);
		const std::map<UInt, ResidencyMap>::const_iterator m = entryMaps.find(it->first);
		const bool resident = (m != entryMaps.end() && !m->second.entries.empty());
		if (resident) {
			for (size_t k = 0; k < m->second.entries.size(); ++k) {
				const ResidencyMap::Entry& entry = m->second.entries[k];
				if (!entry.expectDirty) { continue; }																	// read-only in the loop: register==home already
				const Reg r = static_cast<Reg>(entry.physicalRegister);
				if (entry.registerClass == GENERAL_REGISTER) { storeSlot(e, r, entry.slot); } else { storeSlotF(e, r, entry.slot); }
			}
		}
		writebackState(e, o);
		if (resident) {
			Label reload = e.newLabel();
			e.adr(X9, reload); e.strX(X9, X0, o.resume);																// RESUME = reload stub (below)
			e.movn(W0, 0); e.b(exitLabel);																				// TIME_OUT
			e.bind(reload);
			for (size_t k = 0; k < m->second.entries.size(); ++k) {
				const ResidencyMap::Entry& entry = m->second.entries[k];
				const Reg r = static_cast<Reg>(entry.physicalRegister);
				if (entry.registerClass == GENERAL_REGISTER) { loadSlot(e, r, entry.slot); } else { loadSlotF(e, r, entry.slot); }
			}
			e.b(mainline[it->first]);
		} else {
			e.adr(X9, mainline[it->first]); e.strX(X9, X0, o.resume);													// RESUME = this block's hot mainline
			e.movn(W0, 0); e.b(exitLabel);																				// TIME_OUT
		}
	}
}

/*
	Emit the native dispatcher (§5.4 encoding (b)). `int dispatch(JitProcessor* ctx)`: park ctx in a callee-saved reg,
	reload the pins from ctx once, then branch into RESUME. Segments tail-branch among themselves for GAZL calls/returns
	and call natives inline (no host round-trip per call/return); a suspend, a top-level return, or a trap branches to
	EXIT with the Status in w0. Returns the trampoline's word offset in the buffer.
*/
static size_t emitDispatcher(Arm64Emitter& e, const Offsets& o, Label exitLabel) {
	const size_t entry = e.wordCount();
	e.subImmX(SP, SP, 16); e.strX(X19, SP, 0); e.strX(X30, SP, 8);														// save the ctx holder (x19) + return addr (x30)
	e.addImmX(X19, X0, 0);																								// x19 = ctx (callee-saved → survives inline native calls)
	reloadState(e, o);																									// load the pins from ctx once (x0 = ctx)
	e.ldrX(X9, X0, o.resume); e.br(X9);																					// branch into RESUME (hot; pins live)
	e.bind(exitLabel);																									// segments branch here with w0 = Status (suspend / finish / trap)
	e.ldrX(X19, SP, 0); e.ldrX(X30, SP, 8); e.addImmX(SP, SP, 16); e.ret();
	return entry;
}

/*
	JitCompilerArm64::compile (declared in GAZLJitArm64.h) - lowers a whole finalized program to AArch64 machine code (the
	substrate above: Arm64Emitter + lowerFunction + emitDispatcher) into an EmittedModule, then makes it executable by
	handing it to JitModule and swaps that into `out` (the last three lines are the same in the x86-64 backend). Targets
	the static JitProcessor::layout() ABI; never touches a processor instance.
*/
void JitCompilerArm64::compile(const AssembledProgram& program, JitModule& out) {
	EmittedModule emitted;
	const Offsets o = JitProcessor::layout();																			// the run-state ABI, obtained without an engine
	Arm64Emitter e;
	std::vector<Label> entryLabels(program.functionCount);
	std::vector<size_t> entryOffset(program.functionCount, 0);
	for (UInt k = 0; k < program.functionCount; ++k) { entryLabels[k] = e.newLabel(); }
	Label exitLabel = e.newLabel();																						// the one dispatcher EXIT; every segment terminal branches here (§5.4 (b))
	for (UInt ord = 0; ord < program.functionCount; ++ord) {
		lowerFunction(e, program.code, program.memory, program.functionTable[ord], o, entryLabels, entryOffset, ord
				, program.functionCount, exitLabel);
	}
	const size_t dispatchOffset = emitDispatcher(e, o, exitLabel);
	e.finalize();
	// AArch64 emits 32-bit instruction words directly; entry/dispatch offsets are in words, scaled to bytes for the module.
	const size_t words = e.wordCount();
	emitted.code.assign(e.code(), e.code() + words);
	emitted.entryByteOffsets.resize(program.functionCount);
	for (UInt ord = 0; ord < program.functionCount; ++ord) { emitted.entryByteOffsets[ord] = entryOffset[ord] * 4; }
	emitted.dispatchByteOffset = dispatchOffset * 4;
	JitModule built(emitted);																							// makes the code executable (throws JitException on host denial)
	out.swap(built);
}

/*
	NativeJitCompiler compiles with this backend when arm64 is the host arch; on other hosts it compiles out (the x86-64 TU
	provides it there), so both backends may link together without a duplicate. A client that wants the arm64 backend
	regardless of host names JitCompilerArm64 directly instead.
*/
#if defined(__aarch64__) || defined(_M_ARM64)
void NativeJitCompiler::compile(const AssembledProgram& program, JitModule& out) {
	JitCompilerArm64().compile(program, out);
}
#endif

}																														// namespace GAZL
