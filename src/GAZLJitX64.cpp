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
#include "GAZLJit.h"																									// arch-neutral opcode enum + Offsets / JitModule / JitProcessor / JitCompiler (+ GAZL.h)
#include "GAZLJitMem.h"																									// makeExecutable() - platform-specific backend, architecture-neutral

#include <cstddef>
#include <cstring>																										// std::memcpy - pack the emitted byte stream into 32-bit words
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
	if (needSIB) { b(static_cast<uint8_t>((0 << 6) | (4 << 3) | rm)); }													// scale 1, no index, base = rm
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
	b(static_cast<uint8_t>((2 << 6) | ((index & 7) << 3) | bb));														// scale 4
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
void X64Emitter::movssReg(Reg xd, Reg xs) { sseRR(0x00, 0x28, xd, xs); }												// movaps xd, xs (xmm register copy; 0F 28 /r - full-register write: no merge false-dep, move-eliminated)
void X64Emitter::addss(Reg xd, Reg xs) { sseRR(0xF3, 0x58, xd, xs); }
void X64Emitter::subss(Reg xd, Reg xs) { sseRR(0xF3, 0x5C, xd, xs); }
void X64Emitter::mulss(Reg xd, Reg xs) { sseRR(0xF3, 0x59, xd, xs); }
void X64Emitter::divss(Reg xd, Reg xs) { sseRR(0xF3, 0x5E, xd, xs); }
void X64Emitter::ucomiss(Reg xa, Reg xb) { sseRR(0x00, 0x2E, xa, xb); }
void X64Emitter::cvttss2si(Reg rd, Reg xs) { sseRR(0xF3, 0x2C, rd, xs); }
void X64Emitter::xorps(Reg xd, Reg xs) { sseRR(0x00, 0x57, xd, xs); }													// xorps xd, xs (0F 57 /r); xd == xs is the recognized zero idiom, breaking dependencies on xd
void X64Emitter::cvtsi2ss(Reg xd, Reg rs) { sseRR(0xF3, 0x2A, xd, rs); }
void X64Emitter::movdToXmm(Reg xd, Reg rs) { sseRR(0x66, 0x6E, xd, rs); }
void X64Emitter::movdFromXmm(Reg rd, Reg xs) { sseRR(0x66, 0x7E, xs, rd); }												// 66 0F 7E /r: ModRM.reg = xmm source
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
	d32(0);																												// rel32 placeholder, patched by finalize()
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
static const Reg SCRATCH_A = RCX, SCRATCH_B = RDX;																		// general scratch (A also serves as the shift-count CL)
static const Reg FLOAT_0 = static_cast<Reg>(0), FLOAT_1 = static_cast<Reg>(1);											// xmm0 / xmm1 (a separate register file from GP)

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
static const uint32_t CALL_FRAME = 40u;																					// 32-byte shadow space + 8-byte align pad
#else
static const Reg ARG_0 = RDI;
static const uint32_t CALL_FRAME = 8u;																					// just the 16-align pad; SysV has no shadow space
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

typedef void (X64Emitter::*BinaryOp)(Reg, Reg);

/*
	x64 register pool + fill/spill backend (§5.7). Registers caller-saved on BOTH SysV and Win64 (no prologue save),
	outside the §5.3 pins and the RAX/RCX/RDX scratch idiv/shift/rep use - so the cache never collides with those and
	evict() is never needed here. slot -> [DSP + slot*4] (32-bit disp, no near/far split).
*/
static const int X64_GENERAL_POOL[] = { R8, R9, R10, R11 };
static const int X64_FLOAT_POOL[] = { 2, 3, 4, 5 };																		// xmm2-xmm5 (caller-saved on both ABIs; xmm0/xmm1 stay fixed scratch)

class X64SlotBackend : public RegisterCacheBackend {
	public:		X64SlotBackend(X64Emitter& emitter) : e(emitter) { }
	public:		virtual void emitFill(int physicalRegister, Int slot, RegisterClass registerClass) {
					const Reg r = static_cast<Reg>(physicalRegister);
					if (registerClass == GENERAL_REGISTER) { e.load(r, DSP, static_cast<int32_t>(slot) * 4); }
					else { e.movssLoad(r, DSP, static_cast<int32_t>(slot) * 4); }
				}
	public:		virtual void emitSpill(Int slot, int physicalRegister, RegisterClass registerClass) {
					const Reg r = static_cast<Reg>(physicalRegister);
					if (registerClass == GENERAL_REGISTER) { e.store(DSP, static_cast<int32_t>(slot) * 4, r); }
					else { e.movssStore(DSP, static_cast<int32_t>(slot) * 4, r); }
				}
	private:	X64Emitter& e;
};

// Store every entry of a captured dirty snapshot to its home (terminal trap arms; see RegisterCache::captureDirtyLines).
static void emitDirtyStores(X64Emitter& e, const ResidencyMap& map) {
	for (size_t k = 0; k < map.entries.size(); ++k) {
		const ResidencyMap::Entry& entry = map.entries[k];
		const Reg r = static_cast<Reg>(entry.physicalRegister);
		if (entry.registerClass == GENERAL_REGISTER) { e.store(DSP, static_cast<int32_t>(entry.slot) * 4, r); }
		else { e.movssStore(DSP, static_cast<int32_t>(entry.slot) * 4, r); }
	}
}

// A checked memory op's terminal trap, deferred to the function's cold section so the hot path stays branch-and-continue.
struct ColdTrap {
	Label label;
	ResidencyMap dirty;								// the captureDirtyLines snapshot to store before exiting
	Status status;									// BAD_PEEK / BAD_POKE
};

// Opcodes whose operands route through the cache; everything else barriers the cache and lowers as v1 (§5.7).
static bool cacheLowered(Int op) {
	switch (op) {
		case OP_MOVE_VV: case OP_MOVE_VC:
		case OP_ADDI_VVV: case OP_ADDI_VVC:
		case OP_SUBI_VVV: case OP_SUBI_VVC: case OP_SUBI_VCV:
		case OP_MULI_VVV: case OP_MULI_VVC:
		case OP_ANDI_VVV: case OP_ANDI_VVC:
		case OP_IORI_VVV: case OP_IORI_VVC:
		case OP_XORI_VVV: case OP_XORI_VVC:
		case OP_ABSI: case OP_ABSF: case OP_FLOF:
		case OP_ADDF_VVV: case OP_ADDF_VVC:
		case OP_SUBF_VVV: case OP_SUBF_VVC: case OP_SUBF_VCV:
		case OP_MULF_VVV: case OP_MULF_VVC:
		case OP_DIVF_VVV: case OP_DIVF_VVC: case OP_DIVF_VCV:
		case OP_FTOI_VVC: case OP_ITOF_VVC:
		case OP_SHLI_VVV: case OP_SHLI_VVC: case OP_SHLI_VCV:
		case OP_SHRI_VVV: case OP_SHRI_VVC: case OP_SHRI_VCV:
		case OP_SHRU_VVV: case OP_SHRU_VVC: case OP_SHRU_VCV:
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

// destination = source1 <op> source2 through the cache. Two-operand ISA: seed dst with s1, then `op dst, s2` in place.
static void emitBinary(X64Emitter& emitter, RegisterCache& cache, BinaryOp op, const Instruction& instruction, bool source1Const, bool source2Const) {
	int a;
	if (source1Const) { a = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(a), static_cast<uint32_t>(instruction.p1.i)); }
	else { a = cache.read(instruction.p1.i, GENERAL_REGISTER); }
	int b;
	if (source2Const) { b = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(b), static_cast<uint32_t>(instruction.p2.i)); }
	else { b = cache.read(instruction.p2.i, GENERAL_REGISTER); }
	const int d = cache.define(instruction.p0.i, GENERAL_REGISTER);
	if (d != b) {																										// normal: dst holds s1, then op dst, s2
		if (d != a) { emitter.mov(static_cast<Reg>(d), static_cast<Reg>(a)); }
		(emitter.*op)(static_cast<Reg>(d), static_cast<Reg>(b));
	} else {																											// dst aliases s2 (p0 == p2): route through a temp so `op` does not read a clobbered s2
		const int t = cache.scratch(GENERAL_REGISTER);
		emitter.mov(static_cast<Reg>(t), static_cast<Reg>(a));
		(emitter.*op)(static_cast<Reg>(t), static_cast<Reg>(b));
		emitter.mov(static_cast<Reg>(d), static_cast<Reg>(t));
	}
	cache.endInstruction();
}

/*
	Signed division (rem=false) / modulo (rem=true). Dividend p1 -> eax, divisor p2 -> ecx. Guards match the interpreter:
	a runtime zero divisor traps DIVISION_BY_ZERO; divisor == -1 is special-cased (div -> -a, mod -> 0) to dodge the x86
	#DE on INT_MIN / -1. Result is left in eax before the store. (A const-zero divisor is an assemble-time error, so the
	zero guard is only for a variable divisor, matching arm64.)
*/
static void emitDivMod(X64Emitter& emitter, RegisterCache& cache, const Instruction& instruction, bool rem, bool source1Const, bool source2Const, Label epilogue) {
	if (source1Const) { emitter.movImm(RAX, static_cast<uint32_t>(instruction.p1.i)); }
	else { const int a = cache.read(instruction.p1.i, GENERAL_REGISTER); emitter.mov(RAX, static_cast<Reg>(a)); }		// dividend -> eax
	if (source2Const) { emitter.movImm(RCX, static_cast<uint32_t>(instruction.p2.i)); }
	else { const int bb = cache.read(instruction.p2.i, GENERAL_REGISTER); emitter.mov(RCX, static_cast<Reg>(bb)); }		// divisor -> ecx
	if (!source2Const) {
		cache.spillDirtyResident();																						// on the MAIN path: the terminal trap needs memory interpreter-current
		emitter.cmpImm(RCX, 0);
		Label nonZero = emitter.newLabel();
		emitter.jcc(CC_NE, nonZero);
		emitter.movImm(RAX, static_cast<uint32_t>(DIVISION_BY_ZERO)); emitter.jmp(epilogue);
		emitter.bind(nonZero);
	}
	emitter.cmpImm(RCX, 0xFFFFFFFFu);																					// divisor == -1 ?
	Label notMinusOne = emitter.newLabel(), done = emitter.newLabel();
	emitter.jcc(CC_NE, notMinusOne);
	if (rem) { emitter.movImm(RAX, 0); } else { emitter.neg(RAX); }
	emitter.jmp(done);
	emitter.bind(notMinusOne);
	emitter.cdq(); emitter.idiv(RCX);
	if (rem) { emitter.mov(RAX, RDX); }
	emitter.bind(done);
	const int d = cache.define(instruction.p0.i, GENERAL_REGISTER);
	emitter.mov(static_cast<Reg>(d), RAX);
	cache.endInstruction();
}

// Shift through the cache: value p1 -> dst (a pool reg), count p2 (const -> imm8, else a slot -> CL). kind: 0=shl, 1=shr, 2=sar.
static void emitShift(X64Emitter& emitter, RegisterCache& cache, const Instruction& instruction, int kind, bool source1Const, bool source2Const) {
	int v;
	if (source1Const) { v = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(v), static_cast<uint32_t>(instruction.p1.i)); }
	else { v = cache.read(instruction.p1.i, GENERAL_REGISTER); }
	int count = -1;
	if (!source2Const) {
		count = cache.read(instruction.p2.i, GENERAL_REGISTER);
		emitter.mov(RCX, static_cast<Reg>(count));																		// capture the count into CL BEFORE seeding dst (dst may alias the count slot)
	}
	const int d = cache.define(instruction.p0.i, GENERAL_REGISTER);
	if (d != v) { emitter.mov(static_cast<Reg>(d), static_cast<Reg>(v)); }
	if (source2Const) {
		const uint8_t n = static_cast<uint8_t>(instruction.p2.i & 31);
		if (kind == 0) { emitter.shlImm(static_cast<Reg>(d), n); } else if (kind == 1) { emitter.shrImm(static_cast<Reg>(d), n); } else { emitter.sarImm(static_cast<Reg>(d), n); }
	} else {
		if (kind == 0) { emitter.shlCl(static_cast<Reg>(d)); } else if (kind == 1) { emitter.shrCl(static_cast<Reg>(d)); } else { emitter.sarCl(static_cast<Reg>(d)); }
	}
	cache.endInstruction();
}

// if (a <condition> b) goto target - a = p0, b = p1, in the const modes named by the opcode; target = this index + p2.
static void emitBranch(X64Emitter& emitter, RegisterCache& cache, Cond condition, const Instruction& instruction, UInt instructionIndex,
		bool operand0Const, bool operand1Const, std::map<UInt, Label>& labels, std::map<UInt, ResidencyMap>& entryMaps) {
	int a;
	if (operand0Const) { a = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(a), static_cast<uint32_t>(instruction.p0.i)); }
	else { a = cache.read(instruction.p0.i, GENERAL_REGISTER); }
	int b;
	if (operand1Const) { b = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(b), static_cast<uint32_t>(instruction.p1.i)); }
	else { b = cache.read(instruction.p1.i, GENERAL_REGISTER); }
	emitter.cmp(static_cast<Reg>(a), static_cast<Reg>(b));
	cache.endInstruction();
	const UInt target = static_cast<UInt>(static_cast<Int>(instructionIndex) + instruction.p2.i);
	reconcileOrBarrier(cache, entryMaps, target);																		// block ends here (mov loads/stores leave EFLAGS)
	emitter.jcc(condition, labels[target]);
}

// Load a float operand into a cache register: a float slot (read), or a constant materialized via a GP scratch + movd.
static int loadFloatOperandCached(X64Emitter& emitter, RegisterCache& cache, const Value& operand, bool isConst) {
	if (!isConst) { return cache.read(operand.i, FLOAT_REGISTER); }
	const int bits = cache.scratch(GENERAL_REGISTER);
	const int x = cache.scratch(FLOAT_REGISTER);
	emitter.movImm(static_cast<Reg>(bits), static_cast<uint32_t>(operand.i));
	emitter.movdToXmm(static_cast<Reg>(x), static_cast<Reg>(bits));
	return x;
}
// DIVf with a runtime divisor: match the interpreter's zero-divisor trap (GAZL.cpp CHECK_FLOAT_DIV_BY_ZERO). Test the
// divisor's bits: (bits << 1) == 0 iff the value is +0.0 or -0.0 (matching `== 0.0f`; NaN/inf fall through). VVC (const
// divisor) is assemble-time-checked, so it stays on emitBinaryFloat.
static void emitDivFChecked(X64Emitter& emitter, RegisterCache& cache, const Instruction& instruction, bool source1Const, Label epilogue) {
	const int a = loadFloatOperandCached(emitter, cache, instruction.p1, source1Const);
	const int b = loadFloatOperandCached(emitter, cache, instruction.p2, false);
	cache.spillDirtyResident();																							// main-path flush before the terminal trap
	emitter.movdFromXmm(SCRATCH_B, static_cast<Reg>(b));
	emitter.add(SCRATCH_B, SCRATCH_B);																					// <<1: ZF set iff divisor is +-0.0
	Label ok = emitter.newLabel();
	emitter.jcc(CC_NE, ok);
	emitter.movImm(RAX, static_cast<uint32_t>(DIVISION_BY_ZERO)); emitter.jmp(epilogue);
	emitter.bind(ok);
	const int d = cache.define(instruction.p0.i, FLOAT_REGISTER);
	if (d != b) {
		if (d != a) { emitter.movssReg(static_cast<Reg>(d), static_cast<Reg>(a)); }
		emitter.divss(static_cast<Reg>(d), static_cast<Reg>(b));
	} else {
		const int t = cache.scratch(FLOAT_REGISTER);
		emitter.movssReg(static_cast<Reg>(t), static_cast<Reg>(a));
		emitter.divss(static_cast<Reg>(t), static_cast<Reg>(b));
		emitter.movssReg(static_cast<Reg>(d), static_cast<Reg>(t));
	}
	cache.endInstruction();
}

// destination = source1 <fop> source2 through the cache (two-operand: seed dst with s1, then fop dst, s2).
static void emitBinaryFloat(X64Emitter& emitter, RegisterCache& cache, BinaryOp fop, const Instruction& instruction, bool source1Const, bool source2Const) {
	const int a = loadFloatOperandCached(emitter, cache, instruction.p1, source1Const);
	const int b = loadFloatOperandCached(emitter, cache, instruction.p2, source2Const);
	const int d = cache.define(instruction.p0.i, FLOAT_REGISTER);
	if (d != b) {
		if (d != a) { emitter.movssReg(static_cast<Reg>(d), static_cast<Reg>(a)); }
		(emitter.*fop)(static_cast<Reg>(d), static_cast<Reg>(b));
	} else {
		const int t = cache.scratch(FLOAT_REGISTER);
		emitter.movssReg(static_cast<Reg>(t), static_cast<Reg>(a));
		(emitter.*fop)(static_cast<Reg>(t), static_cast<Reg>(b));
		emitter.movssReg(static_cast<Reg>(d), static_cast<Reg>(t));
	}
	cache.endInstruction();
}

// Float compare-branch, NaN-correct versus C++: kind 0 = <, 1 = >=, 2 = ==, 3 = !=. a = p0, b = p1, target = index + p2.
static void emitBranchFloat(X64Emitter& emitter, RegisterCache& cache, int kind, const Instruction& instruction, UInt instructionIndex,
		bool operand0Const, bool operand1Const, std::map<UInt, Label>& labels, std::map<UInt, ResidencyMap>& entryMaps) {
	const int a = loadFloatOperandCached(emitter, cache, instruction.p0, operand0Const);
	const int b = loadFloatOperandCached(emitter, cache, instruction.p1, operand1Const);
	const Reg xa = static_cast<Reg>(a), xb = static_cast<Reg>(b);
	const UInt targetIndex = static_cast<UInt>(static_cast<Int>(instructionIndex) + instruction.p2.i);
	Label target = labels[targetIndex];
	if (kind == 0) { emitter.ucomiss(xb, xa); }																			// b > a ordered == (a < b)
	else { emitter.ucomiss(xa, xb); }
	cache.endInstruction();
	reconcileOrBarrier(cache, entryMaps, targetIndex);																	// block ends here (mov loads/stores leave EFLAGS)
	if (kind == 0) { emitter.jcc(CC_A, target); }
	else if (kind == 1) { emitter.jcc(CC_AE, target); }																	// a >= b ordered
	else if (kind == 2) { Label unordered = emitter.newLabel(); emitter.jcc(CC_P, unordered); emitter.jcc(CC_E, target); emitter.bind(unordered); }
	else { emitter.jcc(CC_P, target); emitter.jcc(CC_NE, target); }														// unordered or not-equal
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
	if (localsSize != 0) { emitter.addImmQ(DSP, localsSize * 4u); }														// dsp += frame
	{
		const UInt paramsSize = static_cast<UInt>(code[funcStart].p1.i);
		Label stackOk = emitter.newLabel();
		emitter.movQ(RAX, DSP); if (paramsSize != 0) { emitter.addImmQ(RAX, paramsSize * 4u); }
		emitter.loadQ(SCRATCH_A, CONTEXT, offsets.dsend); emitter.cmpQ(RAX, SCRATCH_A); emitter.jcc(CC_BE, stackOk);
		emitter.movImm(RAX, static_cast<uint32_t>(DATA_STACK_OVERFLOW)); emitter.jmp(epilogue);							// dsp + params > dsend
		emitter.bind(stackOk);
	}

	X64SlotBackend slotBackend(emitter);
	const RegisterPool registerPool = { X64_GENERAL_POOL, sizeof(X64_GENERAL_POOL) / sizeof(X64_GENERAL_POOL[0])
			, X64_FLOAT_POOL, sizeof(X64_FLOAT_POOL) / sizeof(X64_FLOAT_POOL[0]) };
	RegisterCache cache(registerPool, slotBackend);
	UseSchedule useSchedule;
	buildUseSchedule(code, funcStart, endIndex, useSchedule);															// Belady next-read lists (§5.7 v2.0.5)
	cache.setUseSchedule(&useSchedule);
	std::map<UInt, UInt> loopExtent;
	jitResidencyLeaders(code, funcStart, endIndex, memory, loopExtent);													// v2.2: loop headers whose entry state stays register-resident
	std::map<UInt, ResidencyMap> entryMaps;
	std::vector<ColdTrap> coldTraps;																					// checked-op trap arms, emitted after the mainline

	// Pass 2 - emit.
	for (UInt j = funcStart; j <= endIndex; ++j) {
		cache.setInstructionIndex(j);
		std::map<UInt, Label>::iterator labelIt = labels.find(j);
		if (labelIt != labels.end()) {
			std::map<UInt, UInt>::const_iterator loopIt = loopExtent.find(j);
			if (loopIt != loopExtent.end()) {																			// loop header: the fall-through state (pruned) becomes the fixed entry state
				std::set<Int> readSlots, writtenSlots;
				buildLoopSlotSets(code, j, loopIt->second, readSlots, writtenSlots);
				cache.capture(entryMaps[j], readSlots, writtenSlots);
			} else { cache.barrier(); }																					// any other leader: starts empty as in v2.0
			emitter.bind(labelIt->second);
		}
		std::map<UInt, UInt>::iterator weightIt = loopWeight.find(j);													// loop head: charge the block, suspend on timeout (§5.5)
		if (weightIt != loopWeight.end()) { emitter.subImm(FUEL, weightIt->second); emitter.jcc(CC_S, suspendL[j]); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		if (!cacheLowered(op)) { cache.barrier(); }																		// uncached opcode: lower it as v1 over an empty cache
		switch (op) {
			case OP_FUNC: break;
			case OP_RETU: {																								// pop the ipStack; tail-branch back to the caller, or OK at the native/top marker
				Label notNative = emitter.newLabel();
				emitter.addImmQ(IP_STACK_PTR, 0u - 16u);																// ipsp -= 16 : pop {cont, dsp}
				emitter.loadQ(RAX, IP_STACK_PTR, 0);																	// cont (caller continuation address)
				emitter.loadQ(DSP, IP_STACK_PTR, 8);																	// caller dsp (0 = native/top marker)
				emitter.movImm(SCRATCH_A, 0); emitter.cmpQ(DSP, SCRATCH_A); emitter.jcc(CC_NE, notNative);
				emitter.addImmQ(IP_STACK_PTR, 0u - 16u); emitter.loadQ(DSP, IP_STACK_PTR, 8);							// native return: pop again for the true dsp
				emitter.storeQ(CONTEXT, offsets.dsp, DSP); emitter.storeQ(CONTEXT, offsets.ipsp, IP_STACK_PTR);			// publish (the interpreter's ret: write-back): a BLOCKING
				emitter.store(CONTEXT, offsets.fuel, FUEL);																// native's caller re-reads these after its nested run()
				emitter.movImm(RAX, 0); emitter.jmp(epilogue);															// OK - terminal (return to host); pins stay live in regs
				emitter.bind(notNative);
				emitter.jmpReg(RAX);																					// GAZL return: jump straight into the caller's continuation
				break;
			}

			case OP_CALL_CVC: {																							// direct GAZL call: push {after, dsp}, tail-branch into the callee entry
				const UInt callee = in.p0.p - IP_OFFSET;
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = emitter.newLabel(), ipOk = emitter.newLabel();
				emitter.loadQ(RAX, CONTEXT, offsets.ipsend); emitter.cmpQ(IP_STACK_PTR, RAX); emitter.jcc(CC_B, ipOk);
				emitter.movImm(RAX, static_cast<uint32_t>(IP_STACK_OVERFLOW)); emitter.jmp(epilogue); emitter.bind(ipOk);
				emitter.leaRip(RAX, after); emitter.storeQ(IP_STACK_PTR, 0, RAX); emitter.storeQ(IP_STACK_PTR, 8, DSP); emitter.addImmQ(IP_STACK_PTR, 16);
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }													// dsp += arg window
				emitter.jmp(entryLabels[callee]);																		// direct tail-branch into the callee (state stays live)
				emitter.bind(after);																					// callee's RETU lands here; pins already live
				break;
			}
			case OP_CALL_VVC: {																							// indirect: slot -> ordinal -> ctx.funcEntries[ordinal], tail-branch
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = emitter.newLabel(), ipOk = emitter.newLabel(), trap = emitter.newLabel();
				emitter.loadQ(RAX, CONTEXT, offsets.ipsend); emitter.cmpQ(IP_STACK_PTR, RAX); emitter.jcc(CC_B, ipOk);
				emitter.movImm(RAX, static_cast<uint32_t>(IP_STACK_OVERFLOW)); emitter.jmp(epilogue); emitter.bind(ipOk);
				emitter.load(RCX, DSP, in.p0.i * 4); emitter.subImm(RCX, IP_OFFSET);									// ordinal
				emitter.cmpImm(RCX, functionCount); emitter.jcc(CC_AE, trap);											// >= functionCount -> BAD_CALL
				emitter.loadQ(RAX, CONTEXT, offsets.funcentries); emitter.shlImm(RCX, 3); emitter.addQ(RAX, RCX); emitter.loadQ(RDX, RAX, 0); // rdx = funcEntries[ordinal]
				emitter.leaRip(RAX, after); emitter.storeQ(IP_STACK_PTR, 0, RAX); emitter.storeQ(IP_STACK_PTR, 8, DSP); emitter.addImmQ(IP_STACK_PTR, 16);
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }
				emitter.jmpReg(RDX);																					// indirect tail-branch into the callee segment
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_CALL)); emitter.jmp(epilogue);
				emitter.bind(after);
				break;
			}
			case OP_CALL_NVC: {																							// native: publish window/fuel/ipsp, call the native inline (no dispatcher round-trip)
				/*
					Re-entrancy-safe shape (see TODO(jit-native-reentrancy) resolution): every post-call value comes
					from ctx fields that a nested enterCall()+run() episode inside the native leaves stable - ctx.dsp
					is the published window and the sentinel discipline restores exactly it; ctx.ipsp reflects any
					frames a pushCall() added (a no-op reload otherwise). The OK path continues INDIRECTLY through
					ctx.nativeafter, preset to `after`: JitProcessor::pushCall retargets it at the pushed callee's
					entry, whose RETU chains back to `after` through the pushed frames - so `^native` behaves exactly
					like `&function`. `after` re-arms the not-in-a-native guard and normalizes dsp to the caller
					frame, which is why pushed frames store the WINDOW uniformly. RESUME = hot is set on the nonzero
					path only (same retry semantics, but nested episodes can no longer clobber it pre-call).
				*/
				const UInt ordinal = static_cast<UInt>(in.p0.i);
				const UInt window = static_cast<UInt>(in.p1.i);
				Label hot = emitter.newLabel(), retry = emitter.newLabel(), after = emitter.newLabel();
				emitter.bind(hot);																						// hot re-entry (blocking-retry / suspend re-issue target)
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }
				emitter.storeQ(CONTEXT, offsets.dsp, DSP); emitter.store(CONTEXT, offsets.fuel, FUEL); emitter.storeQ(CONTEXT, offsets.ipsp, IP_STACK_PTR);
				emitter.leaRip(RAX, after); emitter.storeQ(CONTEXT, offsets.nativeafter, RAX);							// redirectable OK continuation (pushCall retargets)
				emitter.loadQ(RAX, CONTEXT, offsets.natives); emitter.loadQ(RAX, RAX, static_cast<int32_t>(ordinal * 8)); // rax = natives[ordinal]
				emitter.movQ(ARG_0, CONTEXT); emitter.callReg(RAX);														// native(ctx); eax = status (pins are callee-saved -> preserved)
				emitter.load(FUEL, CONTEXT, offsets.fuel);																// native may resetTimeOut(0): refresh the fuel pin from ctx
				emitter.loadQ(IP_STACK_PTR, CONTEXT, offsets.ipsp);														// adopt frames pushed by pushCall (no-op otherwise)
				emitter.loadQ(DSP, CONTEXT, offsets.dsp);																// the window - stable across nested episodes (sentinel discipline)
				emitter.cmpImm(RAX, 0); emitter.jcc(CC_NE, retry);
				emitter.loadQ(RAX, CONTEXT, offsets.nativeafter); emitter.jmpReg(RAX);									// OK: `after`, or the last-pushed callee's entry
				emitter.bind(retry);
				if (window != 0) {																						// publish the CALLER dsp (register AND ctx): the hot
					emitter.addImmQ(DSP, 0u - window * 4u);																// re-issue reloads ctx.dsp and advances it again
					emitter.storeQ(CONTEXT, offsets.dsp, DSP);
				}
				emitter.leaRip(SCRATCH_A, hot); emitter.storeQ(CONTEXT, offsets.resume, SCRATCH_A);						// RESUME = call site (rax still holds the status!)
				emitter.jmp(epilogue);																					// nonzero (retry / suspend / trap): return to host, eax = status
				emitter.bind(after);
				emitter.movImm(RAX, 0); emitter.storeQ(CONTEXT, offsets.nativeafter, RAX);								// re-arm the "only inside a native call" guard
				if (window != 0) { emitter.addImmQ(DSP, 0u - window * 4u); }											// normalize to the caller frame base
				break;
			}

			case OP_MOVE_VV: { const int s = cache.read(in.p1.i, GENERAL_REGISTER); const int d = cache.define(in.p0.i, GENERAL_REGISTER); if (d != s) { emitter.mov(static_cast<Reg>(d), static_cast<Reg>(s)); } cache.endInstruction(); break; }
			case OP_MOVE_VC: { const int d = cache.define(in.p0.i, GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(d), static_cast<uint32_t>(in.p1.i)); cache.endInstruction(); break; }

			// constant-address global access: assembler-validated, cannot touch the frame, so no cache coherence is needed.
			case OP_PEEK_VC: { const int d = cache.define(in.p0.i, GENERAL_REGISTER); emitter.load(static_cast<Reg>(d), MEMORY_BASE, static_cast<int32_t>((in.p1.p - MEMORY_OFFSET) * 4)); cache.endInstruction(); break; }
			case OP_POKE_CV: { const int r = cache.read(in.p1.i, GENERAL_REGISTER); emitter.store(MEMORY_BASE, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), static_cast<Reg>(r)); cache.endInstruction(); break; }
			case OP_POKE_CC: { const int r = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(r), static_cast<uint32_t>(in.p1.i)); emitter.store(MEMORY_BASE, static_cast<int32_t>((in.p0.p - MEMORY_OFFSET) * 4), static_cast<Reg>(r)); cache.endInstruction(); break; }

			// var-indexed global memory (pointer coherence): flush dirty before a read; flush + invalidate around a write.
			case OP_PEEK_VCV: {																							// dst = memory[C1 + index]; globals/constants-realm read
				const int32_t base = static_cast<int32_t>(in.p1.p - MEMORY_OFFSET);
				ColdTrap trap; trap.label = emitter.newLabel(); trap.status = BAD_PEEK;
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);													// no flush: a const-base access cannot reach the frame (§1.1 realms)
				emitter.mov(RAX, static_cast<Reg>(idx)); emitter.addImm(RAX, static_cast<uint32_t>(base));				// word index = base + index
				emitter.load(SCRATCH_B, CONTEXT, offsets.memsize); emitter.cmp(RAX, SCRATCH_B);
				cache.captureDirtyLines(trap.dirty);																	// the trap exit must leave memory interpreter-identical
				emitter.jcc(CC_AE, trap.label);																			// trap arm is cold, after the mainline
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				emitter.loadIdx(static_cast<Reg>(d), MEMORY_BASE, static_cast<Reg>(idx), base * 4);
				cache.endInstruction();
				coldTraps.push_back(trap);
				break;
			}
			case OP_POKE_CVV: case OP_POKE_CVC: {																		// memory[C0 + index] = value; globals-realm write
				const int32_t base = static_cast<int32_t>(in.p0.p - MEMORY_OFFSET);
				ColdTrap trap; trap.label = emitter.newLabel(); trap.status = BAD_POKE;
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_POKE_CVC) { val = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(val), static_cast<uint32_t>(in.p2.i)); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				emitter.mov(RAX, static_cast<Reg>(idx)); emitter.addImm(RAX, static_cast<uint32_t>(base));				// no flush: a const-base access cannot reach the frame (§1.1 realms)
				emitter.load(SCRATCH_B, CONTEXT, offsets.rwmemsize); emitter.cmp(RAX, SCRATCH_B);
				cache.captureDirtyLines(trap.dirty);																	// the trap exit must leave memory interpreter-identical
				emitter.jcc(CC_AE, trap.label);																			// trap arm is cold, after the mainline
				emitter.storeIdx(MEMORY_BASE, static_cast<Reg>(idx), base * 4, static_cast<Reg>(val));
				cache.endInstruction();
				coldTraps.push_back(trap);
				break;
			}

			case OP_PEEK_VVV: {																							// base(var) + index(var) = a GAZL pointer; wordIndex = base+index-MEMORY_OFFSET
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				const int bp = cache.read(in.p1.i, GENERAL_REGISTER);
				const int ip = cache.read(in.p2.i, GENERAL_REGISTER);
				cache.spillDirtyResident();
				emitter.mov(RAX, static_cast<Reg>(bp)); emitter.add(RAX, static_cast<Reg>(ip)); emitter.subImm(RAX, MEMORY_OFFSET);
				emitter.load(SCRATCH_B, CONTEXT, offsets.memsize); emitter.cmp(RAX, SCRATCH_B); emitter.jcc(CC_AE, trap);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				emitter.loadIdx(static_cast<Reg>(d), MEMORY_BASE, RAX, 0);
				cache.endInstruction();
				emitter.jmp(cont); emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_PEEK)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}
			case OP_POKE_VVV: case OP_POKE_VVC: {
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				const int bp = cache.read(in.p0.i, GENERAL_REGISTER);
				const int ip = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_POKE_VVC) { val = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(val), static_cast<uint32_t>(in.p2.i)); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				cache.spillDirtyResident();
				emitter.mov(RAX, static_cast<Reg>(bp)); emitter.add(RAX, static_cast<Reg>(ip)); emitter.subImm(RAX, MEMORY_OFFSET);
				emitter.load(SCRATCH_B, CONTEXT, offsets.rwmemsize); emitter.cmp(RAX, SCRATCH_B); emitter.jcc(CC_AE, trap);
				emitter.storeIdx(MEMORY_BASE, RAX, 0, static_cast<Reg>(val));
				cache.endInstruction();
				cache.invalidateAll();
				emitter.jmp(cont); emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_POKE)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}

			// local array (frame): bound is the free frame span (dataStackEnd - dsp)/4 - C. Frame access -> pointer coherence.
			case OP_GETL_VVV: {
				const int32_t frameBase = static_cast<int32_t>(in.p1.i);
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				cache.spillDirtyResident();
				emitter.loadQ(RAX, CONTEXT, offsets.dsend); emitter.subQ(RAX, DSP); emitter.shrImm(RAX, 2);				// (dataStackEnd - dsp) in words
				emitter.subImm(RAX, static_cast<uint32_t>(frameBase));													// limit = words - C
				emitter.cmp(static_cast<Reg>(idx), RAX); emitter.jcc(CC_AE, trap);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				emitter.loadIdx(static_cast<Reg>(d), DSP, static_cast<Reg>(idx), frameBase * 4);						// [dsp + index*4 + C*4]
				cache.endInstruction();
				emitter.jmp(cont);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_PEEK)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}
			case OP_SETL_VVV: case OP_SETL_VVC: {
				const int32_t frameBase = static_cast<int32_t>(in.p0.i);
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_SETL_VVC) { val = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(val), static_cast<uint32_t>(in.p2.i)); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				cache.spillDirtyResident();
				emitter.loadQ(RAX, CONTEXT, offsets.dsend); emitter.subQ(RAX, DSP); emitter.shrImm(RAX, 2);
				emitter.subImm(RAX, static_cast<uint32_t>(frameBase));													// limit = words - C
				emitter.cmp(static_cast<Reg>(idx), RAX); emitter.jcc(CC_AE, trap);
				emitter.storeIdx(DSP, static_cast<Reg>(idx), frameBase * 4, static_cast<Reg>(val));						// [dsp + index*4 + C*4] = value
				cache.endInstruction();
				cache.invalidateAll();
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
				emitter.push(RSI); emitter.push(RDI);																	// Win64: rsi/rdi are callee-saved; rep movsd clobbers them
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
			case OP_ADRL: {
				emitter.movQ(RAX, DSP); emitter.subQ(RAX, MEMORY_BASE); emitter.shrImm(RAX, 2);							// (dsp - memoryBase) / 4 = dsp word offset
				emitter.addImm(RAX, static_cast<uint32_t>(in.p1.i));													// + slot
				emitter.addImm(RAX, MEMORY_OFFSET);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				emitter.mov(static_cast<Reg>(d), RAX);
				cache.endInstruction();
				break;
			}

			case OP_ADDI_VVV: emitBinary(emitter, cache, &X64Emitter::add, in, false, false); break;
			case OP_ADDI_VVC: emitBinary(emitter, cache, &X64Emitter::add, in, false, true); break;
			case OP_SUBI_VVV: emitBinary(emitter, cache, &X64Emitter::sub, in, false, false); break;
			case OP_SUBI_VVC: emitBinary(emitter, cache, &X64Emitter::sub, in, false, true); break;
			case OP_SUBI_VCV: emitBinary(emitter, cache, &X64Emitter::sub, in, true, false); break;
			case OP_MULI_VVV: emitBinary(emitter, cache, &X64Emitter::imul, in, false, false); break;
			case OP_MULI_VVC: emitBinary(emitter, cache, &X64Emitter::imul, in, false, true); break;
			case OP_ANDI_VVV: emitBinary(emitter, cache, &X64Emitter::and_, in, false, false); break;
			case OP_ANDI_VVC: emitBinary(emitter, cache, &X64Emitter::and_, in, false, true); break;
			case OP_IORI_VVV: emitBinary(emitter, cache, &X64Emitter::or_, in, false, false); break;
			case OP_IORI_VVC: emitBinary(emitter, cache, &X64Emitter::or_, in, false, true); break;
			case OP_XORI_VVV: emitBinary(emitter, cache, &X64Emitter::xor_, in, false, false); break;
			case OP_XORI_VVC: emitBinary(emitter, cache, &X64Emitter::xor_, in, false, true); break;

			case OP_DIVI_VVV: emitDivMod(emitter, cache, in, false, false, false, epilogue); break;
			case OP_DIVI_VVC: emitDivMod(emitter, cache, in, false, false, true, epilogue); break;
			case OP_DIVI_VCV: emitDivMod(emitter, cache, in, false, true, false, epilogue); break;
			case OP_MODI_VVV: emitDivMod(emitter, cache, in, true, false, false, epilogue); break;
			case OP_MODI_VVC: emitDivMod(emitter, cache, in, true, false, true, epilogue); break;
			case OP_MODI_VCV: emitDivMod(emitter, cache, in, true, true, false, epilogue); break;
			case OP_SHLI_VVV: emitShift(emitter, cache, in, 0, false, false); break;
			case OP_SHLI_VVC: emitShift(emitter, cache, in, 0, false, true); break;
			case OP_SHLI_VCV: emitShift(emitter, cache, in, 0, true, false); break;
			case OP_SHRI_VVV: emitShift(emitter, cache, in, 2, false, false); break;
			case OP_SHRI_VVC: emitShift(emitter, cache, in, 2, false, true); break;
			case OP_SHRI_VCV: emitShift(emitter, cache, in, 2, true, false); break;
			case OP_SHRU_VVV: emitShift(emitter, cache, in, 1, false, false); break;
			case OP_SHRU_VVC: emitShift(emitter, cache, in, 1, false, true); break;
			case OP_SHRU_VCV: emitShift(emitter, cache, in, 1, true, false); break;
			case OP_ABSI: {																								// |x| = (x ^ (x >> 31)) - (x >> 31)
				const int s = cache.read(in.p1.i, GENERAL_REGISTER);
				const int mask = cache.scratch(GENERAL_REGISTER);
				emitter.mov(static_cast<Reg>(mask), static_cast<Reg>(s)); emitter.sarImm(static_cast<Reg>(mask), 31);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				if (d != s) { emitter.mov(static_cast<Reg>(d), static_cast<Reg>(s)); }
				emitter.xor_(static_cast<Reg>(d), static_cast<Reg>(mask)); emitter.sub(static_cast<Reg>(d), static_cast<Reg>(mask));
				cache.endInstruction();
				break;
			}

			case OP_FORi_VVB: case OP_FORi_VCB: {																		// ++counter; if (counter < limit) branch
				const int r = cache.read(in.p0.i, GENERAL_REGISTER);
				emitter.addImm(static_cast<Reg>(r), 1);
				cache.define(in.p0.i, GENERAL_REGISTER);																// counter now dirty (barrier will spill the increment)
				int lim;
				if (op == OP_FORi_VCB) { lim = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(lim), static_cast<uint32_t>(in.p1.i)); }
				else { lim = cache.read(in.p1.i, GENERAL_REGISTER); }
				emitter.cmp(static_cast<Reg>(r), static_cast<Reg>(lim));
				cache.endInstruction();
				reconcileOrBarrier(cache, entryMaps, static_cast<UInt>(static_cast<Int>(j) + in.p2.i));					// block ends here (mov loads/stores leave EFLAGS)
				emitter.jcc(CC_L, labels[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
				break;
			}

			case OP_ADDF_VVV: emitBinaryFloat(emitter, cache, &X64Emitter::addss, in, false, false); break;
			case OP_ADDF_VVC: emitBinaryFloat(emitter, cache, &X64Emitter::addss, in, false, true); break;
			case OP_SUBF_VVV: emitBinaryFloat(emitter, cache, &X64Emitter::subss, in, false, false); break;
			case OP_SUBF_VVC: emitBinaryFloat(emitter, cache, &X64Emitter::subss, in, false, true); break;
			case OP_SUBF_VCV: emitBinaryFloat(emitter, cache, &X64Emitter::subss, in, true, false); break;
			case OP_MULF_VVV: emitBinaryFloat(emitter, cache, &X64Emitter::mulss, in, false, false); break;
			case OP_MULF_VVC: emitBinaryFloat(emitter, cache, &X64Emitter::mulss, in, false, true); break;
			case OP_DIVF_VVV: emitDivFChecked(emitter, cache, in, false, epilogue); break;
			case OP_DIVF_VVC: emitBinaryFloat(emitter, cache, &X64Emitter::divss, in, false, true); break;				// const divisor: assemble-checked
			case OP_DIVF_VCV: emitDivFChecked(emitter, cache, in, true, epilogue); break;
			/*
				FTOI / ITOF carry a scale constant (p2): FTOI = (int)(src * scale) with the interpreter's saturation;
				ITOF = (float)src * scale.
			*/
			case OP_FTOI_VVC: {
				{ const int sx = cache.read(in.p1.i, FLOAT_REGISTER); emitter.movssReg(FLOAT_0, static_cast<Reg>(sx)); } // operand via cache
				emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p2.i)); emitter.movdToXmm(FLOAT_1, SCRATCH_A); emitter.mulss(FLOAT_0, FLOAT_1); // * scale
				emitter.cvttss2si(SCRATCH_A, FLOAT_0);																	// x86 yields 0x80000000 for overflow / inf / NaN
				emitter.cmpImm(SCRATCH_A, 0x80000000u);
				Label ftoiDone = emitter.newLabel();
				emitter.jcc(CC_NE, ftoiDone);																			// in range -> done; else saturate like the interpreter's ftoi()
				emitter.ucomiss(FLOAT_0, FLOAT_0);																		// NaN sets PF
				Label ftoiNotNan = emitter.newLabel();
				emitter.jcc(CC_NP, ftoiNotNan);
				emitter.movImm(SCRATCH_A, 0); emitter.jmp(ftoiDone);													// NaN -> 0
				emitter.bind(ftoiNotNan);
				emitter.movdFromXmm(SCRATCH_B, FLOAT_0); emitter.cmpImm(SCRATCH_B, 0);									// sign bit: signed >= 0 means positive / +inf
				Label ftoiNegative = emitter.newLabel();
				emitter.jcc(CC_L, ftoiNegative);																		// negative / -inf -> keep INT_MIN
				emitter.movImm(SCRATCH_A, 0x7FFFFFFFu);																	// positive / +inf -> INT_MAX
				emitter.bind(ftoiNegative); emitter.bind(ftoiDone);
				{ const int d = cache.define(in.p0.i, GENERAL_REGISTER); emitter.mov(static_cast<Reg>(d), SCRATCH_A); }	// result via cache
				cache.endInstruction();
				break;
			}
			case OP_ITOF_VVC: {
				const int src = cache.read(in.p1.i, GENERAL_REGISTER);													// read before define: p0 may alias p1 (in-place itof)
				const int d = cache.define(in.p0.i, FLOAT_REGISTER);
				emitter.xorps(static_cast<Reg>(d), static_cast<Reg>(d));												// cvtsi2ss merges into d: zero it first or the conversion false-depends on d's last writer
				emitter.cvtsi2ss(static_cast<Reg>(d), static_cast<Reg>(src));											// convert straight into d (no FLOAT_0 round trip / copy)
				emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p2.i)); emitter.movdToXmm(FLOAT_1, SCRATCH_A); emitter.mulss(static_cast<Reg>(d), FLOAT_1);
				cache.endInstruction();
				break;
			}
			case OP_ABSF: {																								// clear the float word's sign bit (bitwise, in a GP register)
				const int s = cache.read(in.p1.i, GENERAL_REGISTER);
				const int m = cache.scratch(GENERAL_REGISTER);
				emitter.movImm(static_cast<Reg>(m), 0x7FFFFFFFu);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				if (d != s) { emitter.mov(static_cast<Reg>(d), static_cast<Reg>(s)); }
				emitter.and_(static_cast<Reg>(d), static_cast<Reg>(m));
				cache.endInstruction();
				break;
			}
			case OP_FLOF: {
				const int s = cache.read(in.p1.i, FLOAT_REGISTER); const int d = cache.define(in.p0.i, FLOAT_REGISTER);
				if (d != s) { emitter.xorps(static_cast<Reg>(d), static_cast<Reg>(d)); }								// roundss merges into d: zero a distinct d first or it false-depends on d's stale value
				emitter.roundss(static_cast<Reg>(d), static_cast<Reg>(s), 1); cache.endInstruction(); break;
			}

			case OP_LSSF_VVB: emitBranchFloat(emitter, cache, 0, in, j, false, false, labels, entryMaps); break;
			case OP_LSSF_VCB: emitBranchFloat(emitter, cache, 0, in, j, false, true, labels, entryMaps); break;
			case OP_LSSF_CVB: emitBranchFloat(emitter, cache, 0, in, j, true, false, labels, entryMaps); break;
			case OP_EQUF_VVB: emitBranchFloat(emitter, cache, 2, in, j, false, false, labels, entryMaps); break;
			case OP_EQUF_VCB: emitBranchFloat(emitter, cache, 2, in, j, false, true, labels, entryMaps); break;
			case OP_NLSF_VVB: emitBranchFloat(emitter, cache, 1, in, j, false, false, labels, entryMaps); break;
			case OP_NLSF_VCB: emitBranchFloat(emitter, cache, 1, in, j, false, true, labels, entryMaps); break;
			case OP_NLSF_CVB: emitBranchFloat(emitter, cache, 1, in, j, true, false, labels, entryMaps); break;
			case OP_NEQF_VVB: emitBranchFloat(emitter, cache, 3, in, j, false, false, labels, entryMaps); break;
			case OP_NEQF_VCB: emitBranchFloat(emitter, cache, 3, in, j, false, true, labels, entryMaps); break;

			case OP_GOTO: {
				const UInt target = static_cast<UInt>(static_cast<Int>(j) + in.p0.i);
				reconcileOrBarrier(cache, entryMaps, target);
				emitter.jmp(labels[target]);
				break;
			}

			case OP_SWCH: {																								// index = min(unsigned(V0), C1); jump into a table of `jmp case`
				const UInt size = static_cast<UInt>(in.p1.i) + 1;
				const UInt table = static_cast<UInt>(in.p2.p - MEMORY_OFFSET);
				emitter.load(SCRATCH_A, DSP, in.p0.i * 4);
				emitter.cmpImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i));
				Label keep = emitter.newLabel();
				emitter.jcc(CC_BE, keep);																				// (unsigned) val <= C1 -> keep; else clamp
				emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i));
				emitter.bind(keep);
				emitter.mov(SCRATCH_B, SCRATCH_A); emitter.shlImm(SCRATCH_B, 2); emitter.add(SCRATCH_B, SCRATCH_A);		// index * 5 (each table jmp is 5 bytes)
				Label tableBase = emitter.newLabel();
				emitter.leaRip(RAX, tableBase); emitter.addQ(RAX, SCRATCH_B); emitter.jmpReg(RAX);
				emitter.bind(tableBase);
				for (UInt k = 0; k < size; ++k) {
					const UInt target = static_cast<UInt>(static_cast<Int>(j) + memory[table + k].i);
					emitter.jmp(labels[target]);
				}
				break;
			}

			case OP_LSSI_VVB: emitBranch(emitter, cache, CC_L, in, j, false, false, labels, entryMaps); break;
			case OP_LSSI_VCB: emitBranch(emitter, cache, CC_L, in, j, false, true, labels, entryMaps); break;
			case OP_LSSI_CVB: emitBranch(emitter, cache, CC_L, in, j, true, false, labels, entryMaps); break;
			case OP_EQUI_VVB: emitBranch(emitter, cache, CC_E, in, j, false, false, labels, entryMaps); break;
			case OP_EQUI_VCB: emitBranch(emitter, cache, CC_E, in, j, false, true, labels, entryMaps); break;
			case OP_NLSI_VVB: emitBranch(emitter, cache, CC_GE, in, j, false, false, labels, entryMaps); break;
			case OP_NLSI_VCB: emitBranch(emitter, cache, CC_GE, in, j, false, true, labels, entryMaps); break;
			case OP_NLSI_CVB: emitBranch(emitter, cache, CC_GE, in, j, true, false, labels, entryMaps); break;
			case OP_NEQI_VVB: emitBranch(emitter, cache, CC_NE, in, j, false, false, labels, entryMaps); break;
			case OP_NEQI_VCB: emitBranch(emitter, cache, CC_NE, in, j, false, true, labels, entryMaps); break;

			default: throwUnlowerableOpcode(op);																		// a finalized opcode the backend must cover (a bug, never routine)
		}
	}
	// Cold section: checked-op trap arms (see ColdTrap) - store the dirty snapshot, set the status, exit.
	for (size_t k = 0; k < coldTraps.size(); ++k) {
		emitter.bind(coldTraps[k].label);
		emitDirtyStores(emitter, coldTraps[k].dirty);
		emitter.movImm(RAX, static_cast<uint32_t>(coldTraps[k].status)); emitter.jmp(epilogue);
	}

	/*
		Cold section: one suspend stub per block leader - writeback the pins to ctx and return TIME_OUT to the host. A
		leader with register-resident entry state (v2.2) first spills its ResidencyMap - memory must be interpreter-
		identical at a suspend - and parks RESUME at a reload stub that refills the map before re-entering the mainline;
		an empty map points RESUME straight at the mainline head as before (the dispatcher reloads only the pins).
		Unreachable by fall-through - each preceding block ends in jmp. Mirrors the arm64 cold section.
	*/
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		const UInt head = it->first;
		emitter.bind(suspendL[head]);
		const std::map<UInt, ResidencyMap>::const_iterator m = entryMaps.find(head);
		const bool resident = (m != entryMaps.end() && !m->second.entries.empty());
		if (resident) {
			for (size_t k = 0; k < m->second.entries.size(); ++k) {
				const ResidencyMap::Entry& entry = m->second.entries[k];
				if (!entry.expectDirty) { continue; }																	// read-only in the loop: register==home already
				const Reg r = static_cast<Reg>(entry.physicalRegister);
				if (entry.registerClass == GENERAL_REGISTER) { emitter.store(DSP, static_cast<int32_t>(entry.slot) * 4, r); }
				else { emitter.movssStore(DSP, static_cast<int32_t>(entry.slot) * 4, r); }
			}
		}
		writebackState(emitter, offsets);
		if (resident) {
			Label reload = emitter.newLabel();
			emitter.leaRip(RAX, reload); emitter.storeQ(CONTEXT, offsets.resume, RAX);									// RESUME = reload stub (below)
			emitter.movImm(RAX, static_cast<uint32_t>(TIME_OUT)); emitter.jmp(epilogue);
			emitter.bind(reload);
			for (size_t k = 0; k < m->second.entries.size(); ++k) {
				const ResidencyMap::Entry& entry = m->second.entries[k];
				const Reg r = static_cast<Reg>(entry.physicalRegister);
				if (entry.registerClass == GENERAL_REGISTER) { emitter.load(r, DSP, static_cast<int32_t>(entry.slot) * 4); }
				else { emitter.movssLoad(r, DSP, static_cast<int32_t>(entry.slot) * 4); }
			}
			emitter.jmp(labels[head]);
		} else {
			emitter.leaRip(RAX, labels[head]); emitter.storeQ(CONTEXT, offsets.resume, RAX);							// RESUME = this block's mainline head
			emitter.movImm(RAX, static_cast<uint32_t>(TIME_OUT)); emitter.jmp(epilogue);
		}
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
	emitter.push(DSP); emitter.push(RBP); emitter.push(CONTEXT); emitter.push(FUEL); emitter.push(MEMORY_BASE); emitter.push(IP_STACK_PTR); // 6 pushes (even -> keeps rsp alignment)
	emitter.addImmQ(RSP, 0u - CALL_FRAME);																				// dispatcher's call frame (16-align pad + Win64 shadow)
	enterSegment(emitter, offsets);																						// ctx = arg0; reload the pins from ctx
	emitter.loadQ(RAX, CONTEXT, offsets.resume); emitter.jmpReg(RAX);													// tail-jump into RESUME (the hot entry / resume point)
	emitter.bind(epilogue);																								// a segment jumps here with its Status in eax
	emitter.addImmQ(RSP, CALL_FRAME);
	emitter.pop(IP_STACK_PTR); emitter.pop(MEMORY_BASE); emitter.pop(FUEL); emitter.pop(CONTEXT); emitter.pop(RBP); emitter.pop(DSP);
	emitter.ret();																										// eax = Status
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
	const Offsets offsets = JitProcessor::layout();																		// the run-state ABI, obtained without an engine
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
	JitModule built(emitted);																							// makes the code executable (throws JitException on host denial)
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

}																														// namespace GAZL
