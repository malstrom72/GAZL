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
#include <cstring>
#include <map>
#include <set>
#include <sys/mman.h>

#if defined(__APPLE__)
	#include <pthread.h>
	#include <libkern/OSCacheControl.h>
#endif

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

void Emitter::sdiv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC00C00u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::msub(Reg wd, Reg wn, Reg wm, Reg wa) {
	// `msub wd, wn, wm, wa` = wa - wn*wm. Modulo is `msub(rem, quotient, divisor, dividend)`.
	emit(0x1B008000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wa) << 10)
			| (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::lslv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC02000u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::lsrv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC02400u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
}

void Emitter::asrv(Reg wd, Reg wn, Reg wm) {
	emit(0x1AC02800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(wn) << 5) | wd);
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

void Emitter::ldrWxs(Reg wt, Reg xn, Reg wm) {
	// `ldr wt, [xn, wm, sxtw #2]`: option=sxtw(110), S=1 (scale by 4). Signed index (frame slot can be below dsp).
	emit(0xB860D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Emitter::strWxs(Reg wt, Reg xn, Reg wm) {
	emit(0xB820D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Emitter::ldurW(Reg wt, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);							// unscaled signed 9-bit byte offset
	emit(0xB8400000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | wt);
}

void Emitter::sturW(Reg wt, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xB8000000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | wt);
}

// --- doubleword loads / stores (64-bit) ---

void Emitter::ldrX(Reg xt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 7u) == 0 && (byteOffset >> 3) < 0x1000);	// unsigned offset, scaled by 8
	emit(0xF9400000u | ((byteOffset >> 3) << 10) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Emitter::strX(Reg xt, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 7u) == 0 && (byteOffset >> 3) < 0x1000);
	emit(0xF9000000u | ((byteOffset >> 3) << 10) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Emitter::ldrXr(Reg xt, Reg xn, Reg wm) {
	// `ldr xt, [xn, wm, uxtw #3]`: option=uxtw(010), S=1 (scale by 8 for a doubleword), 64-bit register offset.
	emit(0xF8605800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | xt);
}

void Emitter::adr(Reg xd, Label target) {
	branch(0x10000000u | xd, target.id, FIXUP_ADR);					// PC-relative; displacement patched by finalize()
}

// --- 64-bit address arithmetic / test ---

void Emitter::addImmX(Reg xd, Reg xn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0x91000000u | (imm12 << 10) | (static_cast<uint32_t>(xn) << 5) | xd);
}

void Emitter::subImmX(Reg xd, Reg xn, uint32_t imm12) {
	assert(imm12 < 0x1000);
	emit(0xD1000000u | (imm12 << 10) | (static_cast<uint32_t>(xn) << 5) | xd);
}

void Emitter::cbnzX(Reg xt, Label target) {
	branch(0xB5000000u | xt, target.id, FIXUP_IMM19);
}

void Emitter::cmpX(Reg xn, Reg xm) {
	emit(0xEB00001Fu | (static_cast<uint32_t>(xm) << 16) | (static_cast<uint32_t>(xn) << 5));	// subs xzr, xn, xm
}

void Emitter::addX(Reg xd, Reg xn, Reg xm) {
	emit(0x8B000000u | (static_cast<uint32_t>(xm) << 16) | (static_cast<uint32_t>(xn) << 5) | xd);
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

void Emitter::fdivS(Reg sd, Reg sn, Reg sm) {
	emit(0x1E201800u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5) | sd);
}

void Emitter::fcmpS(Reg sn, Reg sm) {
	emit(0x1E202000u | (static_cast<uint32_t>(sm) << 16) | (static_cast<uint32_t>(sn) << 5));
}

void Emitter::fcvtzs(Reg wd, Reg sn) {
	emit(0x1E380000u | (static_cast<uint32_t>(sn) << 5) | wd);		// FTOI, round toward zero (saturating)
}

void Emitter::scvtf(Reg sd, Reg wn) {
	emit(0x1E220000u | (static_cast<uint32_t>(wn) << 5) | sd);		// ITOF
}

void Emitter::fmovSW(Reg sd, Reg wn) {
	emit(0x1E270000u | (static_cast<uint32_t>(wn) << 5) | sd);		// bit-copy Wn -> Sd (no conversion)
}

void Emitter::ldrS(Reg st, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xBD400000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Emitter::strS(Reg st, Reg xn, uint32_t byteOffset) {
	assert((byteOffset & 3u) == 0 && (byteOffset >> 2) < 0x1000);
	emit(0xBD000000u | ((byteOffset >> 2) << 10) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Emitter::ldurS(Reg st, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xBC400000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Emitter::sturS(Reg st, Reg xn, int simm9) {
	assert(simm9 >= -256 && simm9 <= 255);
	emit(0xBC000000u | ((static_cast<uint32_t>(simm9) & 0x1FFu) << 12) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Emitter::ldrSxs(Reg st, Reg xn, Reg wm) {
	emit(0xBC60D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | st);
}

void Emitter::strSxs(Reg st, Reg xn, Reg wm) {
	emit(0xBC20D800u | (static_cast<uint32_t>(wm) << 16) | (static_cast<uint32_t>(xn) << 5) | st);
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

void Emitter::br(Reg xn) {
	emit(0xD61F0000u | (static_cast<uint32_t>(xn) << 5));
}

void Emitter::blr(Reg xn) {
	emit(0xD63F0000u | (static_cast<uint32_t>(xn) << 5));
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
		} else if (f.kind == FIXUP_IMM19) {
			words[f.site] |= ((static_cast<uint32_t>(disp) & 0x0007FFFFu) << 5);
		} else {														// FIXUP_ADR: 21-bit byte displacement, split lo/hi
			const uint32_t immBytes = static_cast<uint32_t>(disp * 4) & 0x001FFFFFu;
			words[f.site] |= ((immBytes & 0x3u) << 29) | (((immBytes >> 2) & 0x0007FFFFu) << 5);
		}
	}
	fixups.clear();
}

} // namespace GAZL


// ============================================================================================================
// JIT lowering, engine, and native dispatcher (declarations in GAZLJit.h). Depends on GAZL.h; the Emitter above
// does not — so the emit helpers here are file-local (`static`) and only lowerFunction/emitDispatcher/makeExecutable
// (and the JitEngine methods) are external. None of this references a Processor symbol, so GAZLJit.o still links
// into the Emitter-only diff test without GAZL.cpp.
// ============================================================================================================

namespace GAZLJitLower {

using namespace GAZL;

// --- W^X executable memory (spike A1 rung-1 strategy; see docs/JitSpikeA1-Results.md) ---

void* makeExecutable(const uint32_t* words, size_t wordCount) {
	const size_t bytes = wordCount * sizeof(uint32_t);
#if defined(__APPLE__)
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	if (p == MAP_FAILED) { return nullptr; }
	const bool toggle = (pthread_jit_write_protect_supported_np() != 0);
	if (toggle) { pthread_jit_write_protect_np(0); }
	std::memcpy(p, words, bytes);
	if (toggle) { pthread_jit_write_protect_np(1); }
	sys_icache_invalidate(p, bytes);
#else
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) { return nullptr; }
	std::memcpy(p, words, bytes);
	if (::mprotect(p, bytes, PROT_READ | PROT_EXEC) != 0) { ::munmap(p, bytes); return nullptr; }
	__builtin___clear_cache(reinterpret_cast<char*>(p), reinterpret_cast<char*>(p) + bytes);
#endif
	return p;
}

// The JitEngine's field-offset gatherer (setup-time; non-virtual, so defining it here pulls no vtable into GAZLJit.o).
Offsets JitEngine::offsets() const {
	Offsets o;
	o.dsp = off(&dsp); o.mb = off(&memoryBase); o.fuel = off(&clockCyclesLeft); o.ipsp = off(&ipsp);
	o.resume = off(&resume); o.saveddsp = off(&savedDsp); o.natives = off(&natives);
	o.nativefn = off(&nativeFn); o.funcentries = off(&funcEntries);
	o.memsize = off(&memorySize); o.rwmemsize = off(&rwMemorySize); o.dsend = off(&dataStackEnd);
	o.ipsend = off(&ipStackEnd); o.nativeafter = off(&nativeAfter);
	return o;
}

// --- emit helpers (x0=ctx, x1=dsp, x2=membase, w3=fuel, x4=ipsp; scratch x9..x12) ---

static void reloadState(Emitter& e, const Offsets& o) {
	e.ldrX(X1, X0, o.dsp); e.ldrX(X2, X0, o.mb); e.ldrW(W3, X0, o.fuel); e.ldrX(X4, X0, o.ipsp);
}
static void writebackState(Emitter& e, const Offsets& o) {
	e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);
}
static void matConst(Emitter& e, Reg r, Int v) { e.movImm32(r, static_cast<uint32_t>(v)); }
// Frame slots are Value-indices off dsp (x1). ldur/stur reach ±64 words; far slots (big frames / LOCA arrays) fall back
// to a register-offset load (index in W13 — kept distinct from the W9..W12 operand scratches). See task #23.
static bool slotNear(Int slot) { return slot >= -64 && slot <= 63; }
static void loadSlot(Emitter& e, Reg r, Int slot) {
	if (slotNear(slot)) { e.ldurW(r, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.ldrWxs(r, X1, W13); }
}
static void storeSlot(Emitter& e, Reg r, Int slot) {
	if (slotNear(slot)) { e.sturW(r, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.strWxs(r, X1, W13); }
}
static void loadSlotF(Emitter& e, Reg s, Int slot) {
	if (slotNear(slot)) { e.ldurS(s, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.ldrSxs(s, X1, W13); }
}
static void storeSlotF(Emitter& e, Reg s, Int slot) {
	if (slotNear(slot)) { e.sturS(s, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.strSxs(s, X1, W13); }
}
static void loadOp(Emitter& e, Reg r, const Value& p, bool isConst) {
	if (isConst) { matConst(e, r, p.i); } else { loadSlot(e, r, p.i); }
}
// `dst = s1 <op> s2`, where s2 is a const (imm-form) or slot; s1 is always a slot for VVV/VVC, a const for VCV.
static void emitBinary(Emitter& e, void (Emitter::*op)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	loadOp(e, W10, in.p1, s1Const);
	loadOp(e, W11, in.p2, s2Const);
	(e.*op)(W9, W10, W11);
	storeSlot(e, W9, in.p0.i);
}

// Load a float operand into S-reg `s` (using `wtmp` for a const): a slot via ldur, or a float constant via fmov s,w.
static void loadOpF(Emitter& e, Reg s, Reg wtmp, const Value& p, bool isConst) {
	if (isConst) { matConst(e, wtmp, p.i); e.fmovSW(s, wtmp); }
	else { loadSlotF(e, s, p.i); }
}
// `dst = s1 <fop> s2` on float slots/consts.
static void emitBinaryF(Emitter& e, void (Emitter::*fop)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	loadOpF(e, S0, W9, in.p1, s1Const);
	loadOpF(e, S1, W10, in.p2, s2Const);
	(e.*fop)(S2, S0, S1);
	storeSlotF(e, S2, in.p0.i);
}

// Emit a shift `dst = value <shift> count`. kind: 0=lsl, 1=asr (SHRi), 2=lsr (SHRu). form: 0=VVV, 1=VVC, 2=VCV.
// value is p1 (a slot for VVV/VVC, a const for VCV); count is p2 (a const for VVC, else a slot, masked mod 32 by HW).
static void emitShift(Emitter& e, int kind, const Instruction& in, int form) {
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
static void emitDivMod(Emitter& e, bool rem, const Instruction& in, int form) {
	loadOp(e, W10, in.p1, form == 2);					// dividend (const for VCV)
	loadOp(e, W11, in.p2, form == 1);					// divisor (const for VVC)
	if (form != 1) {									// variable divisor → guard
		Label ok = e.newLabel();
		e.cbnz(W11, ok);
		e.movn(W0, 6); e.ret();							// ~6 = -7 = DIVISION_BY_ZERO (terminal)
		e.bind(ok);
	}
	e.sdiv(W9, W10, W11);								// quotient (INT_MIN/-1 → INT_MIN, matches §6.1)
	if (rem) { e.msub(W9, W9, W11, W10); }				// remainder = dividend - quotient*divisor
	storeSlot(e, W9, in.p0.i);
}

// Compute a branch instruction's target (absolute index). Returns false if `j` is not a branch.
static bool branchTarget(const Instruction* code, UInt j, UInt& target) {
	const Int op = code[j].opcode;
	switch (op) {
		case OP_GOTO: target = static_cast<UInt>(static_cast<Int>(j) + code[j].p0.i); return true;
		case OP_FORi_VVB: case OP_FORi_VCB:
		case OP_LSSI_VVB: case OP_LSSI_VCB: case OP_LSSI_CVB:
		case OP_EQUI_VVB: case OP_EQUI_VCB:
		case OP_NLSI_VVB: case OP_NLSI_VCB: case OP_NLSI_CVB:
		case OP_NEQI_VVB: case OP_NEQI_VCB:
		case OP_LSSF_VVB: case OP_LSSF_VCB: case OP_LSSF_CVB:
		case OP_EQUF_VVB: case OP_EQUF_VCB:
		case OP_NLSF_VVB: case OP_NLSF_VCB: case OP_NLSF_CVB:
		case OP_NEQF_VVB: case OP_NEQF_VCB:
			target = static_cast<UInt>(static_cast<Int>(j) + code[j].p2.i); return true;
		default: return false;
	}
}

/*
	Lower one function at `funcIndex` into `e` (appended). Emits an entry reload + FUNC prologue, a mainline per block,
	cold reload trampolines + §5.7.5 suspend stubs for loop heads, and the §5.4 call/return transfers. `entryLabels[ord]`
	are pre-created (for direct calls); `entryOffset[selfOrdinal]` is set to this function's native word offset. Returns
	false on an unsupported opcode.
*/
bool lowerFunction(Emitter& e, const Instruction* code, UInt funcIndex, const Offsets& o,
		std::vector<Label>& entryLabels, std::vector<size_t>& entryOffset, UInt selfOrdinal, UInt functionCount) {
	UInt retIndex = funcIndex;
	while (code[retIndex].opcode != OP_RETU) { ++retIndex; }

	// Pass 1 — analysis: branch targets + back-edge loop heads (with block weights).
	std::set<UInt> targets;
	std::map<UInt, UInt> loopWeight;
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		UInt tgt;
		if (branchTarget(code, j, tgt)) {
			targets.insert(tgt);
			if (tgt <= j) { loopWeight[tgt] = j - tgt + 1; }		// back-edge → loop head needs a fuel-check safepoint
		}
	}
	std::map<UInt, Label> mainline, reloadL, suspendL;
	std::vector<std::pair<Label, Label> > nativeReloads;	// {call-site reload prologue, hot re-entry} per CALL_NVC (blocking retry)
	for (std::set<UInt>::const_iterator it = targets.begin(); it != targets.end(); ++it) { mainline[*it] = e.newLabel(); }
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		reloadL[it->first] = e.newLabel(); suspendL[it->first] = e.newLabel();
	}

	// Function entry: reload pinned state, then FUNC prologue (dsp += localsSize).
	entryOffset[selfOrdinal] = e.wordCount();
	e.bind(entryLabels[selfOrdinal]);
	reloadState(e, o);
	const UInt localsSize = static_cast<UInt>(code[funcIndex].p0.i);
	if (localsSize != 0) {							// dsp += localsSize (in bytes); register add if beyond the imm12 range
		if (localsSize * 4 < 0x1000) { e.addImmX(X1, X1, localsSize * 4); }
		else { matConst(e, W9, static_cast<Int>(localsSize * 4)); e.addX(X1, X1, X9); }
	}
	{												// FUNC stack-overflow: if (dsp + paramsSize > dataStackEnd) DATA_STACK_OVERFLOW
		const UInt paramsSize = static_cast<UInt>(code[funcIndex].p1.i);
		Label sok = e.newLabel();
		e.ldrX(X9, X0, o.dsend); e.addImmX(X10, X1, paramsSize * 4); e.cmpX(X10, X9);
		e.bcond(LS, sok); e.movn(W0, 4); e.ret();	// > end → ~4 = -5 = DATA_STACK_OVERFLOW
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
			e.ldrX(X9, X4, 0); e.ldrX(X1, X4, 8);				// cont ; caller dsp (or 0 = native marker)
			e.cbnzX(X1, notNative);
			e.subImmX(X4, X4, 16); e.ldrX(X1, X4, 8);			// native return: pop again for true dsp
			writebackState(e, o);
			e.movz(W0, 0); e.ret();								// OK — terminal (return to host)
			e.bind(notNative);
			writebackState(e, o);
			e.strX(X9, X0, o.resume); e.movz(W0, TRANSFER); e.ret();	// GAZL return: RESUME = cont, transfer
			break;
		}
		case OP_CALL_CVC: {
			const UInt callee = in.p0.p - IP_OFFSET;			// ordinal known at compile time → direct
			const UInt window = static_cast<UInt>(in.p1.i);
			Label after = e.newLabel(), iok = e.newLabel();
			e.ldrX(X9, X0, o.ipsend); e.cmpX(X4, X9); e.bcond(LO, iok);	// ipsp >= ipStackEnd → IP_STACK_OVERFLOW
			e.movn(W0, 5); e.ret(); e.bind(iok);				// ~5 = -6
			e.adr(X9, after); e.strX(X9, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);	// push {cont, dsp}
			if (window != 0) { e.addImmX(X1, X1, window * 4); }	// dsp += arg window
			writebackState(e, o);
			e.adr(X9, entryLabels[callee]); e.strX(X9, X0, o.resume);	// RESUME = callee entry
			e.movz(W0, TRANSFER); e.ret();
			e.bind(after); reloadState(e, o);					// after the call returns: fresh segment → reload
			break;
		}
		case OP_CALL_VVC: {
			const UInt window = static_cast<UInt>(in.p1.i);		// ordinal from a slot at runtime → resolve + bounds-check
			Label after = e.newLabel(), trap = e.newLabel(), iok = e.newLabel();
			e.ldrX(X9, X0, o.ipsend); e.cmpX(X4, X9); e.bcond(LO, iok);	// ipsp >= ipStackEnd → IP_STACK_OVERFLOW
			e.movn(W0, 5); e.ret(); e.bind(iok);
			loadSlot(e, W9, in.p0.i);							// fn pointer = IP_OFFSET + ordinal
			matConst(e, W10, static_cast<Int>(IP_OFFSET)); e.sub(W9, W9, W10);	// ordinal
			matConst(e, W10, static_cast<Int>(functionCount));
			e.cmp(W9, W10); e.bcond(HS, trap);					// ordinal >= functionCount → BAD_CALL
			e.ldrX(X10, X0, o.funcentries); e.ldrXr(X9, X10, W9);	// entry = funcEntries[ordinal]
			e.adr(X10, after); e.strX(X10, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);
			if (window != 0) { e.addImmX(X1, X1, window * 4); }
			writebackState(e, o);
			e.strX(X9, X0, o.resume); e.movz(W0, TRANSFER); e.ret();
			e.bind(trap); e.movn(W0, 3); e.ret();				// ~3 = -4 = BAD_CALL (terminal)
			e.bind(after); reloadState(e, o);
			break;
		}
		case OP_CALL_NVC: {
			const UInt ordinal = static_cast<UInt>(in.p0.i);	// native ordinal (C0)
			const UInt window = static_cast<UInt>(in.p1.i);		// param-window offset (C1)
			Label after = e.newLabel(), hot = e.newLabel(), callReload = e.newLabel();
			e.bind(hot);										// hot re-entry (also fall-through target); dsp/fuel/ipsp live
			e.strX(X1, X0, o.saveddsp);							// stash original dsp (restored by the dispatcher after)
			if (window != 0) { e.addImmX(X1, X1, window * 4); }
			e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);	// publish window/fuel/ipsp
			e.adr(X9, after); e.strX(X9, X0, o.nativeafter);	// stash success continuation (dispatcher uses it on OK)
			e.adr(X9, callReload); e.strX(X9, X0, o.resume);	// RESUME = call site (nonzero native → re-issue: blocking retry)
			e.ldrX(X9, X0, o.natives); e.ldrX(X9, X9, ordinal * 8); e.strX(X9, X0, o.nativefn);	// resolve natives[ord]
			e.movz(W0, NATIVE_CALL); e.ret();					// hand the host call to the dispatcher
			nativeReloads.push_back(std::make_pair(callReload, hot));
			e.bind(after); reloadState(e, o);
			break;
		}
		case OP_MOVE_VC: matConst(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i); break;
		case OP_MOVE_VV: loadSlot(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i); break;
		case OP_PEEK_VC: e.ldrW(W9, X2, static_cast<uint32_t>((in.p1.p - MEMORY_OFFSET) * 4)); storeSlot(e, W9, in.p0.i); break;
		case OP_POKE_CV: loadSlot(e, W9, in.p1.i); e.strW(W9, X2, static_cast<uint32_t>((in.p0.p - MEMORY_OFFSET) * 4)); break;
		case OP_PEEK_VCV: {
			Label trap = e.newLabel(), cont = e.newLabel();
			matConst(e, W9, static_cast<Int>(in.p1.p - MEMORY_OFFSET)); loadSlot(e, W10, in.p2.i); e.add(W9, W9, W10);
			e.ldrW(W10, X0, o.memsize); e.cmp(W9, W10); e.bcond(HS, trap);
			e.ldrWx(W11, X2, W9); storeSlot(e, W11, in.p0.i);
			e.b(cont); e.bind(trap); e.movn(W0, 1); e.ret();	// ~1 = -2 = BAD_PEEK
			e.bind(cont);
			break;
		}
		case OP_POKE_CVV: {
			Label trap = e.newLabel(), cont = e.newLabel();
			matConst(e, W9, static_cast<Int>(in.p0.p - MEMORY_OFFSET)); loadSlot(e, W10, in.p1.i); e.add(W9, W9, W10);
			e.ldrW(W10, X0, o.rwmemsize); e.cmp(W9, W10); e.bcond(HS, trap);
			loadSlot(e, W11, in.p2.i); e.strWx(W11, X2, W9);
			e.b(cont); e.bind(trap); e.movn(W0, 2); e.ret();	// ~2 = -3 = BAD_POKE
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
			e.b(cont); e.bind(trap); e.movn(W0, 1); e.ret();
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
			e.b(cont); e.bind(trap); e.movn(W0, 2); e.ret();
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
			e.bind(trap); e.movn(W0, 7); e.ret();				// ~7 = -8 = ACCESS_VIOLATION
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
			e.b(cont); e.bind(trap); e.movn(W0, 1); e.ret();	// BAD_PEEK
			e.bind(cont);
			break;
		}
		case OP_POKE_VVV: {
			Label trap = e.newLabel(), cont = e.newLabel();
			loadSlot(e, W9, in.p0.i); loadSlot(e, W10, in.p1.i); e.add(W9, W9, W10);	// base + index
			matConst(e, W10, static_cast<Int>(MEMORY_OFFSET)); e.sub(W9, W9, W10);
			e.ldrW(W10, X0, o.rwmemsize); e.cmp(W9, W10); e.bcond(HS, trap);
			loadSlot(e, W11, in.p2.i); e.strWx(W11, X2, W9);
			e.b(cont); e.bind(trap); e.movn(W0, 2); e.ret();	// BAD_POKE
			e.bind(cont);
			break;
		}
		case OP_ADDI_VVV: emitBinary(e, &Emitter::add, in, false, false); break;
		case OP_ADDI_VVC: emitBinary(e, &Emitter::add, in, false, true); break;
		case OP_SUBI_VVV: emitBinary(e, &Emitter::sub, in, false, false); break;
		case OP_SUBI_VVC: emitBinary(e, &Emitter::sub, in, false, true); break;
		case OP_SUBI_VCV: emitBinary(e, &Emitter::sub, in, true, false); break;
		case OP_MULI_VVV: emitBinary(e, &Emitter::mul, in, false, false); break;
		case OP_MULI_VVC: emitBinary(e, &Emitter::mul, in, false, true); break;
		case OP_DIVI_VVV: emitDivMod(e, false, in, 0); break;
		case OP_DIVI_VVC: emitDivMod(e, false, in, 1); break;
		case OP_DIVI_VCV: emitDivMod(e, false, in, 2); break;
		case OP_MODI_VVV: emitDivMod(e, true, in, 0); break;
		case OP_MODI_VVC: emitDivMod(e, true, in, 1); break;
		case OP_MODI_VCV: emitDivMod(e, true, in, 2); break;
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
		case OP_ADDF_VVV: emitBinaryF(e, &Emitter::faddS, in, false, false); break;
		case OP_ADDF_VVC: emitBinaryF(e, &Emitter::faddS, in, false, true); break;
		case OP_SUBF_VVV: emitBinaryF(e, &Emitter::fsubS, in, false, false); break;
		case OP_SUBF_VVC: emitBinaryF(e, &Emitter::fsubS, in, false, true); break;
		case OP_SUBF_VCV: emitBinaryF(e, &Emitter::fsubS, in, true, false); break;
		case OP_MULF_VVV: emitBinaryF(e, &Emitter::fmulS, in, false, false); break;
		case OP_MULF_VVC: emitBinaryF(e, &Emitter::fmulS, in, false, true); break;
		case OP_DIVF_VVV: emitBinaryF(e, &Emitter::fdivS, in, false, false); break;
		case OP_DIVF_VVC: emitBinaryF(e, &Emitter::fdivS, in, false, true); break;
		case OP_DIVF_VCV: emitBinaryF(e, &Emitter::fdivS, in, true, false); break;
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
		case OP_ANDI_VVV: emitBinary(e, &Emitter::and_, in, false, false); break;
		case OP_ANDI_VVC: emitBinary(e, &Emitter::and_, in, false, true); break;
		case OP_IORI_VVV: emitBinary(e, &Emitter::orr, in, false, false); break;
		case OP_IORI_VVC: emitBinary(e, &Emitter::orr, in, false, true); break;
		case OP_XORI_VVV: emitBinary(e, &Emitter::eor, in, false, false); break;
		case OP_XORI_VVC: emitBinary(e, &Emitter::eor, in, false, true); break;
		case OP_FORi_VVB: case OP_FORi_VCB: {
			loadSlot(e, W10, in.p0.i); e.addImm(W10, W10, 1); storeSlot(e, W10, in.p0.i);
			loadOp(e, W11, in.p1, op == OP_FORi_VCB);
			e.cmp(W10, W11);
			e.bcond(LT, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
			break;
		}
		case OP_GOTO: e.b(mainline[static_cast<UInt>(static_cast<Int>(j) + in.p0.i)]); break;
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
		default: return false;									// unsupported opcode
		}
	}

	// Cold section: loop-head reload trampolines + §5.7.5 suspend stubs (each names its own continuation via adr).
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		const UInt head = it->first;
		e.bind(reloadL[head]); reloadState(e, o); e.b(mainline[head]);	// resume entry: reload, then hot mainline
		e.bind(suspendL[head]);
		writebackState(e, o);
		e.adr(X9, reloadL[head]); e.strX(X9, X0, o.resume);				// RESUME = this head's reload prologue
		e.movn(W0, 0); e.ret();											// TIME_OUT
	}
	// Cold native-call retry trampolines: a re-dispatch (RESUME=call site) reloads then re-enters the CALL_NVC hot code.
	for (size_t k = 0; k < nativeReloads.size(); ++k) {
		e.bind(nativeReloads[k].first); reloadState(e, o); e.b(nativeReloads[k].second);
	}
	return true;
}

// Emit the native dispatcher trampoline (§5.4 encoding (a)). `int dispatch(JitEngine* ctx)`: park CTX in a callee-saved
// reg, jump to RESUME, loop on TRANSFER (GAZL call/return — no host round-trip), make the one host call on NATIVE_CALL,
// and return to the host only to suspend (TIME_OUT) or finish. Returns the trampoline's word offset in the buffer.
size_t emitDispatcher(Emitter& e, const Offsets& o) {
	const size_t entry = e.wordCount();
	Label loop = e.newLabel(), done = e.newLabel();
	e.subImmX(SP, SP, 16); e.strX(X19, SP, 0); e.strX(X30, SP, 8);	// save CTX (x19) + return addr (x30)
	e.addImmX(X19, X0, 0);								// x19 = ctx (callee-saved → survives the host call)
	e.bind(loop);
	e.ldrX(X9, X19, o.resume);
	e.addImmX(X0, X19, 0); e.blr(X9);					// run the segment (arg0 = ctx); w0 = status
	e.cmpImm(W0, TRANSFER); e.bcond(EQ, loop);			// TRANSFER: thread next segment — stays in JIT
	e.cmpImm(W0, NATIVE_CALL); e.bcond(NE, done);		// TIME_OUT / OK / trap → return to host
	e.ldrX(X9, X19, o.nativefn);						// NATIVE_CALL: the one real host call
	e.addImmX(X0, X19, 0); e.blr(X9);
	e.ldrX(X10, X19, o.saveddsp); e.strX(X10, X19, o.dsp);	// restore the transient window dsp
	e.cmpImm(W0, 0); e.bcond(NE, done);					// nonzero native = suspend: return status; RESUME stays = call site (retry)
	e.ldrX(X10, X19, o.nativeafter); e.strX(X10, X19, o.resume);	// native OK: RESUME = after-call, continue
	e.b(loop);
	e.bind(done);
	e.ldrX(X19, SP, 0); e.ldrX(X30, SP, 8); e.addImmX(SP, SP, 16); e.ret();
	return entry;
}

} // namespace GAZLJitLower
