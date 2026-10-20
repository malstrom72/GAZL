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
#include "GAZLJitMem.h"																									// makeExecutable()

#include <stdint.h>
#include "assert.h"
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace GAZL {

// Field positions follow the Arm ARM. Data processing is 32-bit unless suffixed `X`; loads and stores use an `Xn` base.

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
	orr(wd, WZR, wm);
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
	subs(WZR, wn, wm);
}

void Arm64Emitter::cmpImm(Reg wn, uint32_t imm12) {
	subsImm(WZR, wn, imm12);
}

void Arm64Emitter::mul(Reg wd, Reg wn, Reg wm) {
	// `madd wd, wn, wm, wzr`
	emit(0x1B000000u | (static_cast<uint32_t>(wm) << 16) | (31u << 10) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::sdiv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC00C00u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Arm64Emitter::msub(Reg wd, Reg wn, Reg wm, Reg wa) {
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
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xB9400000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::strW(Reg wt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xB9000000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::ldrWx(Reg wt, Reg xn, Reg wm) {
	// option = uxtw (010), S = 1
	emit(0xB8605800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::strWx(Reg wt, Reg xn, Reg wm) {
	emit(0xB8205800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::ldrWxs(Reg wt, Reg xn, Reg wm) {
	// option = sxtw (110), S = 1; signed because a frame slot can lie below dsp
	emit(0xB860D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::strWxs(Reg wt, Reg xn, Reg wm) {
	emit(0xB820D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::ldurW(Reg wt, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xB8400000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Arm64Emitter::sturW(Reg wt, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xB8000000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | wt);
}

// --- doubleword loads / stores (64-bit) ---

void Arm64Emitter::ldrX(Reg xt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 7u) == 0 && (byteOffset >> 3) < 0x1000);
	emit(0xF9400000u | ((byteOffset >> 3) << 10) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Arm64Emitter::strX(Reg xt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 7u) == 0 && (byteOffset >> 3) < 0x1000);
	emit(0xF9000000u | ((byteOffset >> 3) << 10) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Arm64Emitter::ldrXr(Reg xt, Reg xn, Reg wm) {
	// option = uxtw (010), S = 1
	emit(0xF8605800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Arm64Emitter::adr(Reg xd, Label target) {
	branch(0x10000000u | xd, target.id, FIXUP_ADR);
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

void Arm64Emitter::subX(Reg xd, Reg xn, Reg xm) {
	emit(0xCB000000u | (static_cast<uint32_t>(xm) << 16) | (static_cast<uint32_t>(xn) << 5) | xd);
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
	emit(0x1E20C000u | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Arm64Emitter::frintmS(Reg sd, Reg sn) {
	emit(0x1E254000u | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Arm64Emitter::fcvtzs(Reg wd, Reg sn) {
	emit(0x1E380000u | (static_cast<uint32_t>(sn) << 5) | wd);
}

void Arm64Emitter::scvtf(Reg sd, Reg wn) {
	emit(0x1E220000u | (static_cast<uint32_t>(wn) << 5) | sd);
}

void Arm64Emitter::fmovSW(Reg sd, Reg wn) {
	emit(0x1E270000u | (static_cast<uint32_t>(wn) << 5) | sd);
}
void Arm64Emitter::fmovWS(Reg wd, Reg sn) {
	emit(0x1E260000u | (static_cast<uint32_t>(sn) << 5) | wd);
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
		/*
			Range-check before masking: a masked over-long displacement branches silently to the wrong address.
			Function size is unbounded, so a large function's fuel-check `b.mi` can exceed the imm19 reach. The throw
			fails the whole compile, not one function.
		*/
		if (f.kind == FIXUP_IMM26) {
			if (disp < -(1 << 25) || disp >= (1 << 25)) throw JitException("JIT: arm64 branch beyond imm26 reach");
			words[f.site] |= (static_cast<uint32_t>(disp) & 0x03FFFFFFu);
		} else if (f.kind == FIXUP_IMM19) {
			if (disp < -(1 << 18) || disp >= (1 << 18)) throw JitException("JIT: arm64 conditional branch beyond imm19 reach");
			words[f.site] |= ((static_cast<uint32_t>(disp) & 0x0007FFFFu) << 5);
		} else {																										// FIXUP_ADR: 21-bit byte displacement, split lo/hi
			if (disp * 4 < -(1 << 20) || disp * 4 >= (1 << 20)) throw JitException("JIT: arm64 ADR beyond imm21 reach");
			const uint32_t immBytes = static_cast<uint32_t>(disp * 4) & 0x001FFFFFu;
			words[f.site] |= ((immBytes & 0x3u) << 29) | (((immBytes >> 2) & 0x0007FFFFu) << 5);
		}
	}
	fixups.clear();
}


/*
	JIT lowering, native dispatcher and the `JitCompiler` driver. `compile()` calls `makeExecutable()`, so anything
	linking this file also needs a GAZLJitMem*.cpp backend.
*/

// --- emit helpers (x0=ctx, x1=dsp, x2=membase, w3=fuel, x4=ipsp, x20=GLOBALS_BASE; scratch x9..x12) ---

/*
	Reads are range-checked and addressed from `memoryBase` (x2, the lowest constant). Writes and constant-address
	globals are addressed from `GLOBALS_BASE` (`MEMORY_OFFSET`, the first global), which keeps globals in imm12 reach.
	Set once by the dispatcher; callee-saved, so natives keep it.
*/
static const Reg GLOBALS_BASE = X20;

static void reloadState(Arm64Emitter& e, const Offsets& o) {
	e.ldrX(X1, X0, o.dsp); e.ldrX(X2, X0, o.mb); e.ldrW(W3, X0, o.fuel); e.ldrX(X4, X0, o.ipsp);
}
static void writebackState(Arm64Emitter& e, const Offsets& o) {
	e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);
}
static void matConst(Arm64Emitter& e, Reg r, Int v) { e.movImm32(r, static_cast<uint32_t>(v)); }
/*
	64-bit add/sub of a byte amount that may exceed imm12 (large frames and call windows). Clobbers W15, which is dead
	at every dsp-adjust site.
*/
static void addImmXBig(Arm64Emitter& e, Reg xd, Reg xn, uint32_t imm) {
	if (imm < 0x1000) { e.addImmX(xd, xn, imm); } else { matConst(e, W15, static_cast<Int>(imm)); e.addX(xd, xn, W15); }
}
static void subImmXBig(Arm64Emitter& e, Reg xd, Reg xn, uint32_t imm) {
	if (imm < 0x1000) { e.subImmX(xd, xn, imm); } else { matConst(e, W15, static_cast<Int>(imm)); e.subX(xd, xn, W15); }
}
/*
	TAIL's window slide: the window moves down onto this frame's base and dsp rewinds to it, so the callee returns
	straight to our caller. The destination is below the source, so an ascending copy is overlap-safe. Clobbers X12,
	W11, W13 and W15 but keeps X9 (the indirect target). A large window loops, since `ldrW` reaches only word 4095.
*/
static void emitTailWindow(Arm64Emitter& e, UInt window, UInt frame) {
	subImmXBig(e, X12, X1, frame * 4);																					// x12 = dp = dsp - frame
	if (window <= 8) {
		for (UInt k = 0; k < window; ++k) {
			e.ldrW(W11, X1, k * 4);
			e.strW(W11, X12, k * 4);
		}
	} else {
		Label loop = e.newLabel();
		matConst(e, W15, static_cast<Int>(window)); e.movz(W13, 0);
		e.bind(loop);																									// window > 8, so at least one pass
		e.ldrWx(W11, X1, W13); e.strWx(W11, X12, W13);
		e.addImm(W13, W13, 1); e.cmp(W13, W15); e.bcond(LO, loop);
	}
	if (frame != 0) { e.addImmX(X1, X12, 0); }																			// dsp = dp (`mov x1, x12`)
}

/*
	Frame slots are word indices off dsp (x1). `ldur`/`stur` reach -64..63; a far slot's index goes in W13, which is
	distinct from the W9..W12 operand scratches.
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
	PEEK/POKE at a constant word index off `base`. Past word 4095 (the imm12 reach) the index goes in W13, so callers
	may hold values in W9..W12 across these.
*/
static void loadMemConst(Arm64Emitter& e, Reg r, Reg base, uint32_t wordIndex) {
	if (wordIndex < 0x1000u) { e.ldrW(r, base, wordIndex * 4u); }
	else { matConst(e, W13, static_cast<Int>(wordIndex)); e.ldrWx(r, base, W13); }
}
static void storeMemConst(Arm64Emitter& e, Reg r, Reg base, uint32_t wordIndex) {
	if (wordIndex < 0x1000u) { e.strW(r, base, wordIndex * 4u); }
	else { matConst(e, W13, static_cast<Int>(wordIndex)); e.strWx(r, base, W13); }
}

// Register cache pools: caller-saved only (no prologue save) and disjoint from the W9-W15 fixed scratch.
static const int ARM64_GENERAL_POOL[] = { W5, W6, W7, W8, W16, W17 };
static const int ARM64_FLOAT_POOL[] = { 16, 17, 18, 19, 20, 21, 22, 23 };												// V16-V23 (caller-saved)

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
	public:		virtual bool bridgeWritesHome() const { return false; }							// see RegisterCacheBackend
	public:		virtual void emitCrossMove(int dstRegister, RegisterClass dstClass, int srcRegister) {
					const Reg d = static_cast<Reg>(dstRegister), s = static_cast<Reg>(srcRegister);
					if (dstClass == FLOAT_REGISTER) { e.fmovSW(d, s); } else { e.fmovWS(d, s); }						// bit copy either way; a Value is one word
				}
	private:	Arm64Emitter& e;
};

typedef BasicColdTrap<Label> ColdTrap;
typedef BasicColdEdge<Label> ColdEdge;
}

// Emit a conditional branch through the shared edge policy (planConditionalEdge); its loads/stores leave NZCV intact.
static void emitConditionalEdge(Arm64Emitter& e, RegisterCache& cache, std::map<UInt, ResidencyMap>& entryMaps
		, std::map<UInt, Label>& mainline, UInt target, Cond c, bool resident, std::vector<ColdEdge>& coldEdges) {
	ColdEdge edge;
	if (!planConditionalEdge(cache, entryMaps, target, resident, edge.dirty)) { e.bcond(c, mainline[target]); return; }
	edge.label = e.newLabel(); edge.target = target;
	coldEdges.push_back(edge);
	e.bcond(c, edge.label);
}

static int loadIntOperand(Arm64Emitter& e, RegisterCache& cache, const Value& p, bool isConst) {
	if (!isConst) { return cache.read(p.i, GENERAL_REGISTER); }
	const int r = cache.scratch(GENERAL_REGISTER);
	matConst(e, static_cast<Reg>(r), p.i);
	return r;
}

static void emitBinary(Arm64Emitter& e, RegisterCache& cache, void (Arm64Emitter::*op)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	const int a = loadIntOperand(e, cache, in.p1, s1Const);
	const int b = loadIntOperand(e, cache, in.p2, s2Const);
	const int d = cache.define(in.p0.i, GENERAL_REGISTER);
	(e.*op)(static_cast<Reg>(d), static_cast<Reg>(a), static_cast<Reg>(b));
	cache.endInstruction();
}

static int loadFloatOperand(Arm64Emitter& e, RegisterCache& cache, const Value& p, bool isConst) {
	if (!isConst) { return cache.read(p.i, FLOAT_REGISTER); }
	const int bits = cache.scratch(GENERAL_REGISTER);
	const int s = cache.scratch(FLOAT_REGISTER);
	matConst(e, static_cast<Reg>(bits), p.i);
	e.fmovSW(static_cast<Reg>(s), static_cast<Reg>(bits));
	return s;
}
#ifdef GAZL_CANONICAL_NAN
// Forces a NaN in `d` to the interpreter's canonical 0x7FC00000; only fuzz builds define GAZL_CANONICAL_NAN.
static void emitCanonicalizeNaN(Arm64Emitter& e, RegisterCache& cache, int d) {
	const int w = cache.scratch(GENERAL_REGISTER);			// before the branch, so any spill it emits is unconditional
	Label notNaN = e.newLabel();
	e.fcmpS(static_cast<Reg>(d), static_cast<Reg>(d));		// unordered (NaN) sets V
	e.bcond(VC, notNaN);
	e.movImm32(static_cast<Reg>(w), 0x7FC00000u);
	e.fmovSW(static_cast<Reg>(d), static_cast<Reg>(w));
	e.bind(notNaN);
}
#define EMIT_CANON_NAN(e, cache, d) emitCanonicalizeNaN((e), (cache), (d))
#else
#define EMIT_CANON_NAN(e, cache, d) ((void)0)
#endif

static void emitBinaryF(Arm64Emitter& e, RegisterCache& cache, void (Arm64Emitter::*fop)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	const int a = loadFloatOperand(e, cache, in.p1, s1Const);
	const int b = loadFloatOperand(e, cache, in.p2, s2Const);
	const int d = cache.define(in.p0.i, FLOAT_REGISTER);
	(e.*fop)(static_cast<Reg>(d), static_cast<Reg>(a), static_cast<Reg>(b));
	EMIT_CANON_NAN(e, cache, d);
	cache.endInstruction();
}

// `kind`: 0 = lsl, 1 = asr, 2 = lsr. `form`: 0 = VVV, 1 = VVC, 2 = VCV. The hardware masks a register count mod 32.
static void emitShift(Arm64Emitter& e, RegisterCache& cache, int kind, const Instruction& in, int form) {
	int v;
	if (form == 2) { v = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(v), in.p1.i); }
	else { v = cache.read(in.p1.i, GENERAL_REGISTER); }
	if (form == 1) {
		const unsigned sh = static_cast<unsigned>(in.p2.i) & 31u;
		const int d = cache.define(in.p0.i, GENERAL_REGISTER);
		if (kind == 0) { e.lslImm(static_cast<Reg>(d), static_cast<Reg>(v), sh); }
		else if (kind == 1) { e.asrImm(static_cast<Reg>(d), static_cast<Reg>(v), sh); }
		else { e.lsrImm(static_cast<Reg>(d), static_cast<Reg>(v), sh); }
	} else {
		const int cnt = cache.read(in.p2.i, GENERAL_REGISTER);
		const int d = cache.define(in.p0.i, GENERAL_REGISTER);
		if (kind == 0) { e.lslv(static_cast<Reg>(d), static_cast<Reg>(v), static_cast<Reg>(cnt)); }
		else if (kind == 1) { e.asrv(static_cast<Reg>(d), static_cast<Reg>(v), static_cast<Reg>(cnt)); }
		else { e.lsrv(static_cast<Reg>(d), static_cast<Reg>(v), static_cast<Reg>(cnt)); }
	}
	cache.endInstruction();
}

// `form`: 0 = VVV, 1 = VVC, 2 = VCV. Only a variable divisor needs a zero guard: a VVC divisor is a nonzero constant.
static void emitDivMod(Arm64Emitter& e, RegisterCache& cache, bool rem, const Instruction& in, int form, std::vector<ColdTrap>& coldTraps) {
	int dividend;
	if (form == 2) { dividend = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(dividend), in.p1.i); }
	else { dividend = cache.read(in.p1.i, GENERAL_REGISTER); }
	int divisor;
	if (form == 1) { divisor = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(divisor), in.p2.i); }
	else { divisor = cache.read(in.p2.i, GENERAL_REGISTER); }
	if (form != 1) {
		ColdTrap trap; trap.label = e.newLabel(); trap.status = DIVISION_BY_ZERO;
		cache.captureDirtyLines(trap.dirty);																			// the trap exit leaves memory as the interpreter would
		e.cbz(static_cast<Reg>(divisor), trap.label);
		coldTraps.push_back(trap);
	}
	if (rem) {																											// `msub` needs the dividend intact, so the quotient gets its own scratch
		const int q = cache.scratch(GENERAL_REGISTER);
		e.sdiv(static_cast<Reg>(q), static_cast<Reg>(dividend), static_cast<Reg>(divisor));
		const int d = cache.define(in.p0.i, GENERAL_REGISTER);
		e.msub(static_cast<Reg>(d), static_cast<Reg>(q), static_cast<Reg>(divisor), static_cast<Reg>(dividend));
	} else {																											// one instruction, so `d` may alias an operand
		const int d = cache.define(in.p0.i, GENERAL_REGISTER);
		e.sdiv(static_cast<Reg>(d), static_cast<Reg>(dividend), static_cast<Reg>(divisor));								// INT_MIN / -1 gives INT_MIN, as the spec requires
	}
	cache.endInstruction();
}

// Appends the function at `funcIndex` to `e`; `entryLabels` must be pre-created for every function (direct calls).
void JitCompilerArm64::lowerFunction(Arm64Emitter& e, const AssembledProgram& program, UInt funcIndex, UInt funcEnd
		, const Offsets& o, std::vector<Label>& entryLabels, UInt selfOrdinal, Label exitLabel) {
	/*
		A read's index is `pointer + readOffset`, off X2; a write's is `pointer - MEMORY_OFFSET`, off GLOBALS_BASE. Each
		is the value its range check tests. `memory`, for the analyses, is the word at `MEMORY_OFFSET`.
	*/
	const Instruction* const code = program.code;
	const UInt functionCount = program.functionCount;
	const UInt constantsSize = program.constsSize - 1;
	const Value* const memory = program.memory + constantsSize;
	const UInt readOffset = constantsSize - MEMORY_OFFSET;
	const Pointer lowestAddress = MEMORY_OFFSET - constantsSize;
	const UInt retIndex = funcEnd;																						// to the next FUNC, not the first RETU: a function may have several RETUs or end in GOTO

	/*
		Pass 1, fuel safepoints: every basic-block leader is charged its block weight and is resumable, so it gets a
		mainline label, a reload trampoline and a suspend stub.
	*/
	std::map<UInt, UInt> loopWeight;
	jitFuelSafepoints(code, funcIndex, retIndex, memory, loopWeight);
	std::map<UInt, Label> mainline, suspendL;
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		mainline[it->first] = e.newLabel(); suspendL[it->first] = e.newLabel();
	}

	// Entered from a caller's branch or the dispatcher, with state already live.
	e.bind(entryLabels[selfOrdinal]);
	const UInt localsSize = static_cast<UInt>(code[funcIndex].p0.i);
	if (localsSize >= MAX_JIT_FRAME_WORDS || static_cast<UInt>(code[funcIndex].p1.i) >= MAX_JIT_FRAME_WORDS) {
		throw JitException("JIT: frame too large for a 32-bit byte offset");								// the host runs it interpreted
	}
	if (localsSize != 0) {
		if (localsSize * 4 < 0x1000) { e.addImmX(X1, X1, localsSize * 4); }
		else { matConst(e, W9, static_cast<Int>(localsSize * 4)); e.addX(X1, X1, X9); }
	}
	{																													// DATA_STACK_OVERFLOW if dsp + paramsSize > dataStackEnd
		const UInt paramsSize = static_cast<UInt>(code[funcIndex].p1.i);
		Label sok = e.newLabel();
		e.ldrX(X9, X0, o.dsend); addImmXBig(e, X10, X1, paramsSize * 4); e.cmpX(X10, X9);
		e.bcond(LS, sok); e.movn(W0, static_cast<uint16_t>(~DATA_STACK_OVERFLOW)); e.b(exitLabel);
		e.bind(sok);
	}

	Arm64SlotBackend slotBackend(e);
	const RegisterPool registerPool = { ARM64_GENERAL_POOL, sizeof(ARM64_GENERAL_POOL) / sizeof(ARM64_GENERAL_POOL[0])
			, ARM64_FLOAT_POOL, sizeof(ARM64_FLOAT_POOL) / sizeof(ARM64_FLOAT_POOL[0]) };
	RegisterCache cache(registerPool, slotBackend);
	UseSchedule useSchedule;
	buildUseSchedule(code, funcIndex, retIndex, useSchedule);
	cache.setUseSchedule(&useSchedule);
	std::map<Int, int> pointerRealm;
	buildPointerRealms(program, funcIndex, retIndex, pointerRealm);															// lets non-frame pointer ops skip the flush
	const std::map<Int, int>::const_iterator realmEnd = pointerRealm.end();
	std::map<UInt, UInt> loopExtent;
	jitResidencyLeaders(code, funcIndex, retIndex, memory, loopExtent);													// loop headers whose entry state stays in registers
	LiveSets liveIn;
	if (!loopExtent.empty()) {
		liveIn.build(code, funcIndex, retIndex, memory);
	}
	std::map<UInt, ResidencyMap> entryMaps;
	std::vector<ColdTrap> coldTraps;																					// emitted after the mainline
	std::vector<ColdEdge> coldEdges;																					// emitted after the mainline
	UInt residentEnd = 0;
	bool resident = false;																								// inside a register-resident loop body [header, residentEnd]

	// Pass 2: emit.
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		cache.setInstructionIndex(j);
		if (resident && j > residentEnd) { resident = false; }
		const std::map<UInt, Label>::const_iterator mainlineIt = mainline.find(j);
		if (mainlineIt != mainline.end()) {
			establishLeader(cache, code, j, loopExtent, loopWeight, liveIn, entryMaps, resident, residentEnd);
			e.bind(mainlineIt->second);
		}
		const std::map<UInt, UInt>::const_iterator weightIt = loopWeight.find(j);
		if (weightIt != loopWeight.end()) { e.subsImm(W3, W3, weightIt->second); e.bcond(MI, suspendL[j]); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		if (!isCacheLowered(op)) { cache.barrier(); }																		// uncached opcodes run over an empty cache
		switch (op) {
			case OP_FUNC: continue;																						// prologue emitted above
			case OP_RETU: {
				Label notNative = e.newLabel();
				e.subImmX(X4, X4, 16);																					// pop {cont, dsp}
				e.ldrX(X9, X4, 0); e.ldrX(X1, X4, 8);																	// dsp 0 marks a native or top-level return
				e.cbnzX(X1, notNative);
				e.subImmX(X4, X4, 16); e.ldrX(X1, X4, 8);																// pop again for the true dsp
				writebackState(e, o);
				e.movz(W0, 0); e.b(exitLabel);
				e.bind(notNative);
				e.br(X9);
				break;
			}
			case OP_TAIL_CC: {																							// target validated at assembly
				emitTailWindow(e, static_cast<UInt>(in.p1.i), static_cast<UInt>(in.p2.i));
				e.b(entryLabels[in.p0.t - FUNCTION_OFFSET]);
				break;
			}
			case OP_TAIL_VC: {
				Label trap = e.newLabel();
				loadSlot(e, W9, in.p0.i);																				// before the slide overwrites the window
				matConst(e, W10, static_cast<Int>(FUNCTION_OFFSET)); e.sub(W9, W9, W10);
				matConst(e, W10, static_cast<Int>(functionCount));
				e.cmp(W9, W10); e.bcond(HS, trap);
				e.ldrX(X10, X0, o.funcentries); e.ldrXr(X9, X10, W9);													// X9 survives the slide
				emitTailWindow(e, static_cast<UInt>(in.p1.i), static_cast<UInt>(in.p2.i));
				e.br(X9);
				e.bind(trap); e.movn(W0, static_cast<uint16_t>(~BAD_CALL)); e.b(exitLabel);
				break;
			}
			case OP_CALL_CVC: {
				const UInt callee = in.p0.t - FUNCTION_OFFSET;
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = e.newLabel(), iok = e.newLabel();
				e.ldrX(X9, X0, o.ipsend); e.cmpX(X4, X9); e.bcond(LO, iok);
				e.movn(W0, static_cast<uint16_t>(~IP_STACK_OVERFLOW)); e.b(exitLabel); e.bind(iok);
				e.adr(X9, after); e.strX(X9, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);							// push {after, dsp}
				if (window != 0) { addImmXBig(e, X1, X1, window * 4); }
				e.b(entryLabels[callee]);
				e.bind(after);
				break;
			}
			case OP_CALL_VVC: {
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = e.newLabel(), trap = e.newLabel(), iok = e.newLabel();
				e.ldrX(X9, X0, o.ipsend); e.cmpX(X4, X9); e.bcond(LO, iok);
				e.movn(W0, static_cast<uint16_t>(~IP_STACK_OVERFLOW)); e.b(exitLabel); e.bind(iok);
				loadSlot(e, W9, in.p0.i);
				matConst(e, W10, static_cast<Int>(FUNCTION_OFFSET)); e.sub(W9, W9, W10);
				matConst(e, W10, static_cast<Int>(functionCount));
				e.cmp(W9, W10); e.bcond(HS, trap);
				e.ldrX(X10, X0, o.funcentries); e.ldrXr(X9, X10, W9);
				e.adr(X10, after); e.strX(X10, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);						// push {after, dsp}
				if (window != 0) { addImmXBig(e, X1, X1, window * 4); }
				e.br(X9);
				e.bind(trap); e.movn(W0, static_cast<uint16_t>(~BAD_CALL)); e.b(exitLabel);
				e.bind(after);
				break;
			}
			case OP_CALL_NVC: {
				/*
					Re-entrancy safe, like the x64 backend: post-call state is reloaded from ctx, and the OK path
					continues through `ctx.nativeafter`, which `JitProcessor::pushCall` may retarget.
				*/
				const UInt ordinal = static_cast<UInt>(in.p0.i);
				const UInt window = static_cast<UInt>(in.p1.i);
				Label hot = e.newLabel(), retry = e.newLabel(), after = e.newLabel();
				e.bind(hot);																							// re-entry for blocking retry and resume
				if (window != 0) { addImmXBig(e, X1, X1, window * 4); }
				e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);
				e.adr(X9, after); e.strX(X9, X0, o.nativeafter);
				if (ordinal >= 4096) throw JitException("JIT: native ordinal beyond the arm64 ldr imm12 reach");		// a wrapped offset would silently call the wrong native
				e.ldrX(X9, X0, o.natives); e.ldrX(X9, X9, ordinal * 8);
				e.blr(X9);
				e.mov(W12, W0);																							// status, before x0 is overwritten with ctx
				e.addImmX(X0, X19, 0); reloadState(e, o);																// x1 is reloaded as the window
				e.cmpImm(W12, 0); e.bcond(NE, retry);
				e.ldrX(X9, X0, o.nativeafter); e.br(X9);																// `after`, or the last pushed callee's entry
				e.bind(retry);
				if (window != 0) {																						// publish the caller dsp: the hot re-issue
					subImmXBig(e, X1, X1, window * 4);																		// advances it again
					e.strX(X1, X0, o.dsp);
				}
				e.adr(X9, hot); e.strX(X9, X0, o.resume);
				e.mov(W0, W12); e.b(exitLabel);
				e.bind(after);
				e.strX(XZR, X0, o.nativeafter);																			// re-arm the "only inside a native call" guard
				if (window != 0) { subImmXBig(e, X1, X1, window * 4); }
				// w3 may be 0 after `resetTimeOut(0)`, so the next leader's fuel check suspends.
				break;
			}
			case OP_MOVE_VC: { const int d = cache.define(in.p0.i, GENERAL_REGISTER); matConst(e, static_cast<Reg>(d), in.p1.i); cache.endInstruction(); break; }
			case OP_MOVE_VV: { const int s = cache.read(in.p1.i, GENERAL_REGISTER); const int d = cache.define(in.p0.i, GENERAL_REGISTER); e.mov(static_cast<Reg>(d), static_cast<Reg>(s)); cache.endInstruction(); break; }
			// A constant address is assembler-validated and cannot touch the frame, so the cache needs no flush.
			case OP_PEEK_VC: {
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				if (in.p1.p >= MEMORY_OFFSET) { loadMemConst(e, static_cast<Reg>(d), GLOBALS_BASE, in.p1.p - MEMORY_OFFSET); }
				else { loadMemConst(e, static_cast<Reg>(d), X2, in.p1.p + readOffset); }
				cache.endInstruction();
				break;
			}
			case OP_POKE_CV: { const int r = cache.read(in.p1.i, GENERAL_REGISTER); storeMemConst(e, static_cast<Reg>(r), GLOBALS_BASE, in.p0.p - MEMORY_OFFSET); cache.endInstruction(); break; }
			case OP_POKE_CC: { const int r = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(r), in.p1.i); storeMemConst(e, static_cast<Reg>(r), GLOBALS_BASE, in.p0.p - MEMORY_OFFSET); cache.endInstruction(); break; }
			/*
				Skipping the flush is sound only when the constant is a data address (`isDataAddress`). `PEEK $x $p` and
				`POKE $p $x` also finalize to these forms, with a small offset constant and the pointer in the variable.
			*/
			case OP_PEEK_VCV: {
				const bool constAddrBase = isDataAddress(program, in.p1.p);
				ColdTrap trap; trap.label = e.newLabel(); trap.status = BAD_PEEK;
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				if (!constAddrBase) { cache.spillDirtyResident(); }
				const int addr = cache.scratch(GENERAL_REGISTER);
				matConst(e, static_cast<Reg>(addr), static_cast<Int>(in.p1.p + readOffset)); e.add(static_cast<Reg>(addr), static_cast<Reg>(addr), static_cast<Reg>(idx));
				const int lim = cache.scratch(GENERAL_REGISTER);
				e.ldrW(static_cast<Reg>(lim), X0, o.memsize); e.cmp(static_cast<Reg>(addr), static_cast<Reg>(lim));
				cache.captureDirtyLines(trap.dirty);
				e.bcond(HS, trap.label);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.ldrWx(static_cast<Reg>(d), X2, static_cast<Reg>(addr));
				cache.endInstruction();
				coldTraps.push_back(trap);
				break;
			}
			// A `REALM_NONFRAME` base cannot alias a cached local, so it skips the flush; any other base flushes.
			case OP_PEEK_VVV: {
				const std::map<Int, int>::const_iterator baseRealm = pointerRealm.find(in.p1.i);
				const bool nonFrame = (baseRealm != realmEnd && baseRealm->second == REALM_NONFRAME);
				ColdTrap trap; trap.label = e.newLabel(); trap.status = BAD_PEEK;
				const int base = cache.read(in.p1.i, GENERAL_REGISTER);
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				if (!nonFrame) { cache.spillDirtyResident(); }
				const int addr = cache.scratch(GENERAL_REGISTER);
				const int tmp = cache.scratch(GENERAL_REGISTER);
				e.add(static_cast<Reg>(addr), static_cast<Reg>(base), static_cast<Reg>(idx));
				matConst(e, static_cast<Reg>(tmp), static_cast<Int>(readOffset)); e.add(static_cast<Reg>(addr), static_cast<Reg>(addr), static_cast<Reg>(tmp));
				e.ldrW(static_cast<Reg>(tmp), X0, o.memsize); e.cmp(static_cast<Reg>(addr), static_cast<Reg>(tmp));
				cache.captureDirtyLines(trap.dirty);
				e.bcond(HS, trap.label);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.ldrWx(static_cast<Reg>(d), X2, static_cast<Reg>(addr));
				cache.endInstruction();
				coldTraps.push_back(trap);
				break;
			}
			case OP_POKE_CVV: case OP_POKE_CVC: {
				const bool constAddrBase = isDataAddress(program, in.p0.p);
				ColdTrap trap; trap.label = e.newLabel(); trap.status = BAD_POKE;
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_POKE_CVC) { val = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(val), in.p2.i); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				if (!constAddrBase) { cache.spillDirtyResident(); }														// may hit the frame: flush before, invalidate after
				const int addr = cache.scratch(GENERAL_REGISTER);
				matConst(e, static_cast<Reg>(addr), static_cast<Int>(in.p0.p - MEMORY_OFFSET)); e.add(static_cast<Reg>(addr), static_cast<Reg>(addr), static_cast<Reg>(idx));
				const int lim = cache.scratch(GENERAL_REGISTER);
				e.ldrW(static_cast<Reg>(lim), X0, o.rwmemsize); e.cmp(static_cast<Reg>(addr), static_cast<Reg>(lim));
				cache.captureDirtyLines(trap.dirty);
				e.bcond(HS, trap.label);
				e.strWx(static_cast<Reg>(val), GLOBALS_BASE, static_cast<Reg>(addr));
				cache.endInstruction();
				if (!constAddrBase) { cache.invalidateAll(); }
				coldTraps.push_back(trap);
				break;
			}
			case OP_POKE_VVV: case OP_POKE_VVC: {
				const std::map<Int, int>::const_iterator baseRealm = pointerRealm.find(in.p0.i);
				const bool nonFrame = (baseRealm != realmEnd && baseRealm->second == REALM_NONFRAME);
				ColdTrap trap; trap.label = e.newLabel(); trap.status = BAD_POKE;
				const int base = cache.read(in.p0.i, GENERAL_REGISTER);
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_POKE_VVC) { val = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(val), in.p2.i); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				if (!nonFrame) { cache.spillDirtyResident(); }
				const int addr = cache.scratch(GENERAL_REGISTER);
				const int tmp = cache.scratch(GENERAL_REGISTER);
				e.add(static_cast<Reg>(addr), static_cast<Reg>(base), static_cast<Reg>(idx));
				matConst(e, static_cast<Reg>(tmp), static_cast<Int>(MEMORY_OFFSET)); e.sub(static_cast<Reg>(addr), static_cast<Reg>(addr), static_cast<Reg>(tmp));
				e.ldrW(static_cast<Reg>(tmp), X0, o.rwmemsize); e.cmp(static_cast<Reg>(addr), static_cast<Reg>(tmp));
				cache.captureDirtyLines(trap.dirty);
				e.bcond(HS, trap.label);
				e.strWx(static_cast<Reg>(val), GLOBALS_BASE, static_cast<Reg>(addr));
				cache.endInstruction();
				if (!nonFrame) { cache.invalidateAll(); }
				coldTraps.push_back(trap);
				break;
			}
			case OP_GETL_VVV: {																							// bounded by the free frame span
				Label trap = e.newLabel(), cont = e.newLabel();
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				cache.spillDirtyResident();																				// indexes the frame: may alias a cached slot
				const int lim = cache.scratch(GENERAL_REGISTER);
				e.ldrX(static_cast<Reg>(lim), X0, o.dsend); e.sub(static_cast<Reg>(lim), static_cast<Reg>(lim), W1); e.lsrImm(static_cast<Reg>(lim), static_cast<Reg>(lim), 2);
				const int c = cache.scratch(GENERAL_REGISTER);
				matConst(e, static_cast<Reg>(c), in.p1.i); e.subs(static_cast<Reg>(lim), static_cast<Reg>(lim), static_cast<Reg>(c)); e.bcond(MI, trap); // limit = (end-dsp)/4 - C1, never negative
				e.cmp(static_cast<Reg>(idx), static_cast<Reg>(lim)); e.bcond(HS, trap);
				e.add(static_cast<Reg>(c), static_cast<Reg>(c), static_cast<Reg>(idx));
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.ldrWxs(static_cast<Reg>(d), X1, static_cast<Reg>(c));
				cache.endInstruction();
				e.b(cont); e.bind(trap); e.movn(W0, static_cast<uint16_t>(~BAD_PEEK)); e.b(exitLabel);
				e.bind(cont);
				break;
			}
			case OP_SETL_VVV: case OP_SETL_VVC: {
				Label trap = e.newLabel(), cont = e.newLabel();
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_SETL_VVC) { val = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(val), in.p2.i); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				cache.spillDirtyResident();
				const int lim = cache.scratch(GENERAL_REGISTER);
				e.ldrX(static_cast<Reg>(lim), X0, o.dsend); e.sub(static_cast<Reg>(lim), static_cast<Reg>(lim), W1); e.lsrImm(static_cast<Reg>(lim), static_cast<Reg>(lim), 2);
				const int c = cache.scratch(GENERAL_REGISTER);
				matConst(e, static_cast<Reg>(c), in.p0.i); e.subs(static_cast<Reg>(lim), static_cast<Reg>(lim), static_cast<Reg>(c)); e.bcond(MI, trap); // limit = (end-dsp)/4 - C0, never negative
				e.cmp(static_cast<Reg>(idx), static_cast<Reg>(lim)); e.bcond(HS, trap);
				e.add(static_cast<Reg>(c), static_cast<Reg>(c), static_cast<Reg>(idx));
				e.strWxs(static_cast<Reg>(val), X1, static_cast<Reg>(c));
				cache.endInstruction();
				cache.invalidateAll();
				e.b(cont); e.bind(trap); e.movn(W0, static_cast<uint16_t>(~BAD_POKE)); e.b(exitLabel);
				e.bind(cont);
				break;
			}
			case OP_COPY_VVC: case OP_COPY_VCC: case OP_COPY_CVC: case OP_COPY_CCC: {
				const bool destConst = (op == OP_COPY_CVC || op == OP_COPY_CCC);
				const bool srcConst = (op == OP_COPY_VCC || op == OP_COPY_CCC);
				Label trap = e.newLabel(), cont = e.newLabel(), lp = e.newLabel(), ldone = e.newLabel();
				// W9 indexes from GLOBALS_BASE, W10 from memoryBase
				if (destConst) { matConst(e, W9, in.p0.i - static_cast<Int>(MEMORY_OFFSET)); }
				else { loadSlot(e, W9, in.p0.i); matConst(e, W12, static_cast<Int>(MEMORY_OFFSET)); e.sub(W9, W9, W12); }
				if (srcConst) { matConst(e, W10, static_cast<Int>(in.p1.p + readOffset)); }
				else { loadSlot(e, W10, in.p1.i); matConst(e, W12, static_cast<Int>(readOffset)); e.add(W10, W10, W12); }
				matConst(e, W15, in.p2.i);
				e.ldrW(W14, X0, o.rwmemsize); e.cmp(W15, W14); e.bcond(HI, trap);
				e.sub(W14, W14, W15); e.cmp(W9, W14); e.bcond(HI, trap);												// subtract, never add: index + count can wrap
				e.ldrW(W14, X0, o.memsize); e.cmp(W15, W14); e.bcond(HI, trap);
				e.sub(W14, W14, W15); e.cmp(W10, W14); e.bcond(HI, trap);
				e.movz(W11, 0);
				e.bind(lp);
				e.cmp(W11, W15); e.bcond(HS, ldone);
				e.add(W12, W10, W11); e.ldrWx(W12, X2, W12);
				e.add(W14, W9, W11); e.strWx(W12, GLOBALS_BASE, W14);
				e.addImm(W11, W11, 1); e.b(lp);
				e.bind(ldone); e.b(cont);
				e.bind(trap); e.movn(W0, static_cast<uint16_t>(~ACCESS_VIOLATION)); e.b(exitLabel);
				e.bind(cont);
				break;
			}
			case OP_ADRL: {
				const int t = cache.scratch(GENERAL_REGISTER);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				e.sub(static_cast<Reg>(t), W1, W2);																		// the low 32 bits suffice within one buffer
				e.lsrImm(static_cast<Reg>(t), static_cast<Reg>(t), 2);
				matConst(e, static_cast<Reg>(d), static_cast<Int>(lowestAddress) + in.p1.i);
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
				const int t = cache.scratch(GENERAL_REGISTER);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);													// (v ^ mask) - mask, mask = v >> 31
				e.asrImm(static_cast<Reg>(t), static_cast<Reg>(s), 31);
				e.eor(static_cast<Reg>(d), static_cast<Reg>(s), static_cast<Reg>(t));
				e.sub(static_cast<Reg>(d), static_cast<Reg>(d), static_cast<Reg>(t));
				cache.endInstruction();
				break;
			}
			case OP_ABSF: { const int s = cache.read(in.p1.i, FLOAT_REGISTER); const int d = cache.define(in.p0.i, FLOAT_REGISTER); e.fabsS(static_cast<Reg>(d), static_cast<Reg>(s)); cache.endInstruction(); break; }
			case OP_FLOF: { const int s = cache.read(in.p1.i, FLOAT_REGISTER); const int d = cache.define(in.p0.i, FLOAT_REGISTER); e.frintmS(static_cast<Reg>(d), static_cast<Reg>(s)); cache.endInstruction(); break; }
			case OP_ADDF_VVV: emitBinaryF(e, cache, &Arm64Emitter::faddS, in, false, false); break;
			case OP_ADDF_VVC: emitBinaryF(e, cache, &Arm64Emitter::faddS, in, false, true); break;
			case OP_SUBF_VVV: emitBinaryF(e, cache, &Arm64Emitter::fsubS, in, false, false); break;
			case OP_SUBF_VVC: emitBinaryF(e, cache, &Arm64Emitter::fsubS, in, false, true); break;
			case OP_SUBF_VCV: emitBinaryF(e, cache, &Arm64Emitter::fsubS, in, true, false); break;
			case OP_MULF_VVV: emitBinaryF(e, cache, &Arm64Emitter::fmulS, in, false, false); break;
			case OP_MULF_VVC: emitBinaryF(e, cache, &Arm64Emitter::fmulS, in, false, true); break;
			case OP_DIVF_VVV: emitBinaryF(e, cache, &Arm64Emitter::fdivS, in, false, false); break;
			case OP_DIVF_VVC: emitBinaryF(e, cache, &Arm64Emitter::fdivS, in, false, true); break;
			case OP_DIVF_VCV: emitBinaryF(e, cache, &Arm64Emitter::fdivS, in, true, false); break;
			case OP_FTOI_VVC: {
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
			case OP_ITOF_VVC: {
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
			case OP_FORi_VVB: case OP_FORi_VCB: {
				const int r = cache.read(in.p0.i, GENERAL_REGISTER);
				e.addImm(static_cast<Reg>(r), static_cast<Reg>(r), 1);
				cache.define(in.p0.i, GENERAL_REGISTER);																// marks the incremented register dirty
				int lim;
				if (op == OP_FORi_VCB) { lim = cache.scratch(GENERAL_REGISTER); matConst(e, static_cast<Reg>(lim), in.p1.i); }
				else { lim = cache.read(in.p1.i, GENERAL_REGISTER); }
				e.cmp(static_cast<Reg>(r), static_cast<Reg>(lim));
				cache.endInstruction();
				emitConditionalEdge(e, cache, entryMaps, mainline, static_cast<UInt>(static_cast<Int>(j) + in.p2.i), LT, resident, coldEdges);
				break;
			}
			case OP_GOTO: {
				const UInt target = static_cast<UInt>(static_cast<Int>(j) + in.p0.i);
				reconcileOrBarrier(cache, entryMaps, target);
				e.b(mainline[target]);
				break;
			}
			case OP_SWCH: {																								// branch into a table of `b target`, index = min(unsigned(V0), C1)
				const std::vector<UInt> targets = switchTargets(code, j, memory);
				loadSlot(e, W9, in.p0.i);
				matConst(e, W10, in.p1.i);
				e.cmp(W9, W10);
				Label useVal = e.newLabel();
				e.bcond(LS, useVal);
				e.mov(W9, W10);
				e.bind(useVal);
				Label caseBase = e.newLabel();
				e.adr(X10, caseBase);
				e.lslImm(W11, W9, 2); e.addX(X10, X10, X11);															// the W write zero-extends into X11
				e.br(X10);
				e.bind(caseBase);
				for (size_t k = 0; k < targets.size(); ++k) { e.b(mainline[targets[k]]); }
				break;
			}
			case OP_LSSF_VVB: case OP_LSSF_VCB: case OP_LSSF_CVB: case OP_EQUF_VVB: case OP_EQUF_VCB:
			case OP_NLSF_VVB: case OP_NLSF_VCB: case OP_NLSF_CVB: case OP_NEQF_VVB: case OP_NEQF_VCB: {
				// MI and PL make NaN behave as in C++: a < b is false and !(a < b) is true.
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
				emitConditionalEdge(e, cache, entryMaps, mainline, static_cast<UInt>(static_cast<Int>(j) + in.p2.i), c, resident, coldEdges);
				break;
			}
			case OP_LSSI_VVB: case OP_LSSI_VCB: case OP_LSSI_CVB: case OP_EQUI_VVB: case OP_EQUI_VCB:
			case OP_NLSI_VVB: case OP_NLSI_VCB: case OP_NLSI_CVB: case OP_NEQI_VVB: case OP_NEQI_VCB: {
				Cond c = LT; bool c0const = false, c1const = false;
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
					default: throwUnlowerableOpcode(in.opcode);															// unreachable
				}
				const int a = loadIntOperand(e, cache, in.p0, c0const);
				const int b = loadIntOperand(e, cache, in.p1, c1const);
				e.cmp(static_cast<Reg>(a), static_cast<Reg>(b));
				cache.endInstruction();
				emitConditionalEdge(e, cache, entryMaps, mainline, static_cast<UInt>(static_cast<Int>(j) + in.p2.i), c, resident, coldEdges);
				break;
			}
			default: throwUnlowerableOpcode(op);																		// a backend bug: every finalized opcode needs a case
		}
	}

	// Cold section: trap arms, then loop-exit edge stubs, then suspend stubs.
	for (size_t k = 0; k < coldTraps.size(); ++k) {
		e.bind(coldTraps[k].label);
		spillResidencyMap(slotBackend, coldTraps[k].dirty);
		e.movn(W0, static_cast<uint16_t>(~coldTraps[k].status)); e.b(exitLabel);
	}

	for (size_t k = 0; k < coldEdges.size(); ++k) {
		e.bind(coldEdges[k].label);
		spillResidencyMap(slotBackend, coldEdges[k].dirty);
		e.b(mainline[coldEdges[k].target]);
	}

	/*
		Memory must match the interpreter at a suspend, so a leader with register-resident entry state spills it and
		resumes through a stub that refills it.
	*/
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		e.bind(suspendL[it->first]);
		const std::map<UInt, ResidencyMap>::const_iterator m = entryMaps.find(it->first);
		const bool resident = (m != entryMaps.end() && !m->second.entries.empty());
		if (resident) { spillResidencyMap(slotBackend, m->second); }
		writebackState(e, o);
		if (resident) {
			Label reload = e.newLabel();
			e.adr(X9, reload); e.strX(X9, X0, o.resume);
			e.movn(W0, static_cast<uint16_t>(~TIME_OUT)); e.b(exitLabel);
			e.bind(reload);
			fillResidencyMap(slotBackend, m->second);
			e.b(mainline[it->first]);
		} else {
			e.adr(X9, mainline[it->first]); e.strX(X9, X0, o.resume);
			e.movn(W0, static_cast<uint16_t>(~TIME_OUT)); e.b(exitLabel);
		}
	}
}

/*
	`int dispatch(JitProcessor* ctx)`: loads state from ctx and branches to RESUME; every exit returns its status in w0.
	Returns the entry's word offset.
*/
static size_t emitDispatcher(Arm64Emitter& e, const Offsets& o, Label exitLabel, UInt constantsSize) {
	const size_t entry = e.wordCount();
	e.subImmX(SP, SP, 32); e.strX(X19, SP, 0); e.strX(X30, SP, 8); e.strX(GLOBALS_BASE, SP, 16);						// AAPCS64: x19 and x20 are callee-saved
	e.addImmX(X19, X0, 0);																								// ctx survives inline native calls in x19
	reloadState(e, o);
	addImmXBig(e, GLOBALS_BASE, X2, constantsSize * 4);
	e.ldrX(X9, X0, o.resume); e.br(X9);
	e.bind(exitLabel);
	e.ldrX(X19, SP, 0); e.ldrX(X30, SP, 8); e.ldrX(GLOBALS_BASE, SP, 16); e.addImmX(SP, SP, 32); e.ret();
	return entry;
}

// Targets the static `JitProcessor::layout()` and never touches a processor instance.
void JitCompilerArm64::compile(const AssembledProgram& program, JitModule& out) {
	EmittedModule emitted;
	const Offsets o = JitProcessor::layout();
	Arm64Emitter e;
	std::vector<Label> entryLabels(program.functionCount);
	for (UInt k = 0; k < program.functionCount; ++k) { entryLabels[k] = e.newLabel(); }
	Label exitLabel = e.newLabel();																						// the one dispatcher exit
	for (UInt ord = 0; ord < program.functionCount; ++ord) {
		const UInt funcEnd = ((ord + 1 < program.functionCount) ? program.functionTable[ord + 1] : program.codeSize) - 1;
		lowerFunction(e, program, program.functionTable[ord], funcEnd, o, entryLabels, ord, exitLabel);
	}
	const size_t dispatchOffset = emitDispatcher(e, o, exitLabel, program.constsSize - 1);
	e.finalize();
	// Offsets are in words; the module wants bytes.
	const size_t words = e.wordCount();
	emitted.code.assign(e.code(), e.code() + words);
	emitted.entryByteOffsets.resize(program.functionCount);
	for (UInt ord = 0; ord < program.functionCount; ++ord) {
		emitted.entryByteOffsets[ord] = static_cast<size_t>(e.labelOffset(entryLabels[ord])) * 4;
	}
	emitted.dispatchByteOffset = dispatchOffset * 4;
	JitModule built(emitted);																							// makes the code executable; throws if the host refuses
	out.swap(built);
}

// Only on an arm64 host, so both backends can link together without a duplicate definition.
#if defined(__aarch64__) || defined(_M_ARM64)
void NativeJitCompiler::compile(const AssembledProgram& program, JitModule& out) {
	JitCompilerArm64().compile(program, out);
}
#endif

}																														// namespace GAZL
