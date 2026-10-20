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
#include "GAZLJit.h"
#include "GAZLJitMem.h"

#include <cstddef>
#include <cstring>
#include <map>
#include <vector>
#include "assert.h"																										// local-overridable, see GAZL.h

namespace GAZL {

void X64Emitter::d32(uint32_t v) {
	b(static_cast<uint8_t>(v)); b(static_cast<uint8_t>(v >> 8));
	b(static_cast<uint8_t>(v >> 16)); b(static_cast<uint8_t>(v >> 24));
}
void X64Emitter::d64(uint64_t v) {
	d32(static_cast<uint32_t>(v)); d32(static_cast<uint32_t>(v >> 32));
}

// `reg` is the ModRM.reg field (0 for opcode-embedded forms); `base` is the rm, SIB base or opcode-embedded register.
void X64Emitter::rex(bool w, int reg, int base) {
	const uint8_t r = static_cast<uint8_t>(0x40 | (w ? 8 : 0) | ((reg >= 8) ? 4 : 0) | ((base >= 8) ? 1 : 0));
	if (r != 0x40) { b(r); }
}

void X64Emitter::modrmReg(int reg, int rm) {
	b(static_cast<uint8_t>(0xC0 | ((reg & 7) << 3) | (rm & 7)));
}

/*
	RSP/R12 as base need a SIB byte (rm 4 escapes to SIB). RBP/R13 (rm 5) cannot use mod 00, which means RIP-relative,
	so a zero displacement takes the disp8 form.
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

void X64Emitter::aluImm(int ext, Reg rd, uint32_t imm) {
	rex(false, 0, rd);
	b(0x81);
	b(static_cast<uint8_t>(0xC0 | (ext << 3) | (rd & 7)));
	d32(imm);
}

void X64Emitter::movImm(Reg rd, uint32_t imm) { rex(false, 0, rd); b(static_cast<uint8_t>(0xB8 | (rd & 7))); d32(imm); }
void X64Emitter::movImm64(Reg rd, uint64_t imm) { rex(true, 0, rd); b(static_cast<uint8_t>(0xB8 | (rd & 7))); d64(imm); }
void X64Emitter::mov(Reg rd, Reg rs) { rex(false, rs, rd); b(0x89); modrmReg(rs, rd); }
void X64Emitter::movQ(Reg rd, Reg rs) { rex(true, rs, rd); b(0x89); modrmReg(rs, rd); }

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

void X64Emitter::neg(Reg rd) { rex(false, 0, rd); b(0xF7); b(static_cast<uint8_t>(0xC0 | (3 << 3) | (rd & 7))); }
void X64Emitter::cdq() { b(0x99); }
void X64Emitter::idiv(Reg rs) { rex(false, 0, rs); b(0xF7); b(static_cast<uint8_t>(0xC0 | (7 << 3) | (rs & 7))); }
void X64Emitter::shlCl(Reg rd) { rex(false, 0, rd); b(0xD3); b(static_cast<uint8_t>(0xC0 | (4 << 3) | (rd & 7))); }
void X64Emitter::shrCl(Reg rd) { rex(false, 0, rd); b(0xD3); b(static_cast<uint8_t>(0xC0 | (5 << 3) | (rd & 7))); }
void X64Emitter::sarCl(Reg rd) { rex(false, 0, rd); b(0xD3); b(static_cast<uint8_t>(0xC0 | (7 << 3) | (rd & 7))); }
void X64Emitter::shlImm(Reg rd, uint8_t n) { rex(false, 0, rd); b(0xC1); b(static_cast<uint8_t>(0xC0 | (4 << 3) | (rd & 7))); b(n); }
void X64Emitter::shrImm(Reg rd, uint8_t n) { rex(false, 0, rd); b(0xC1); b(static_cast<uint8_t>(0xC0 | (5 << 3) | (rd & 7))); b(n); }
void X64Emitter::sarImm(Reg rd, uint8_t n) { rex(false, 0, rd); b(0xC1); b(static_cast<uint8_t>(0xC0 | (7 << 3) | (rd & 7))); b(n); }

void X64Emitter::load(Reg rd, Reg base, int32_t disp) { rex(false, rd, base); b(0x8B); memOperand(rd, base, disp); }
void X64Emitter::store(Reg base, int32_t disp, Reg rs) { rex(false, rs, base); b(0x89); memOperand(rs, base, disp); }
void X64Emitter::loadQ(Reg rd, Reg base, int32_t disp) { rex(true, rd, base); b(0x8B); memOperand(rd, base, disp); }
void X64Emitter::storeQ(Reg base, int32_t disp, Reg rs) { rex(true, rs, base); b(0x89); memOperand(rs, base, disp); }
void X64Emitter::loadIdx(Reg rd, Reg base, Reg index, int32_t disp) { rex3(false, rd, index, base); b(0x8B); memOperandIdx(rd, base, index, disp); }
void X64Emitter::storeIdx(Reg base, Reg index, int32_t disp, Reg rs) { rex3(false, rs, index, base); b(0x89); memOperandIdx(rs, base, index, disp); }
void X64Emitter::subQ(Reg rd, Reg rs) { rex(true, rs, rd); b(0x29); modrmReg(rs, rd); }

// The mandatory prefix (F3 or 66, 0 for none) must precede REX.
void X64Emitter::sseRR(uint8_t prefix, uint8_t opcode, int reg, int rm) {
	if (prefix != 0) { b(prefix); }
	rex(false, reg, rm);
	b(0x0F); b(opcode);
	modrmReg(reg, rm);
}
void X64Emitter::movssLoad(Reg xd, Reg base, int32_t disp) { b(0xF3); rex(false, xd, base); b(0x0F); b(0x10); memOperand(xd, base, disp); }
void X64Emitter::movssStore(Reg base, int32_t disp, Reg xs) { b(0xF3); rex(false, xs, base); b(0x0F); b(0x11); memOperand(xs, base, disp); }
void X64Emitter::movssReg(Reg xd, Reg xs) { sseRR(0x00, 0x28, xd, xs); }												// movaps: a full-register write, free of the merge false dependency of movss
void X64Emitter::addss(Reg xd, Reg xs) { sseRR(0xF3, 0x58, xd, xs); }
void X64Emitter::subss(Reg xd, Reg xs) { sseRR(0xF3, 0x5C, xd, xs); }
void X64Emitter::mulss(Reg xd, Reg xs) { sseRR(0xF3, 0x59, xd, xs); }
void X64Emitter::divss(Reg xd, Reg xs) { sseRR(0xF3, 0x5E, xd, xs); }
void X64Emitter::ucomiss(Reg xa, Reg xb) { sseRR(0x00, 0x2E, xa, xb); }
void X64Emitter::cvttss2si(Reg rd, Reg xs) { sseRR(0xF3, 0x2C, rd, xs); }
void X64Emitter::xorps(Reg xd, Reg xs) { sseRR(0x00, 0x57, xd, xs); }
void X64Emitter::andps(Reg xd, Reg xs) { sseRR(0x00, 0x54, xd, xs); }
void X64Emitter::cvtsi2ss(Reg xd, Reg rs) { sseRR(0xF3, 0x2A, xd, rs); }
void X64Emitter::movdToXmm(Reg xd, Reg rs) { sseRR(0x66, 0x6E, xd, rs); }
void X64Emitter::movdFromXmm(Reg rd, Reg xs) { sseRR(0x66, 0x7E, xs, rd); }												// ModRM.reg is the xmm source
void X64Emitter::roundss(Reg xd, Reg xs, uint8_t mode) { b(0x66); rex(false, xd, xs); b(0x0F); b(0x3A); b(0x0A); modrmReg(xd, xs); b(mode); }
void X64Emitter::movssRip(Reg xd, Label literal) { b(0xF3); rex(false, xd, 0); b(0x0F); b(0x10); b(static_cast<uint8_t>(((xd & 7) << 3) | 5)); relBranch(literal.id); }

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

// The pool goes after all code, so it never sits in an execution path.
Label X64Emitter::floatLiteral(uint32_t bits) {
	const std::map<uint32_t, int>::const_iterator it = literalIndex.find(bits);
	if (it != literalIndex.end()) { return literals[it->second].second; }
	const Label label = newLabel();
	literalIndex[bits] = static_cast<int>(literals.size());
	literals.push_back(std::make_pair(bits, label));
	return label;
}

void X64Emitter::emitLiteralPool() {
	if (literals.empty()) { return; }
	while ((bytes.size() & 3) != 0) { b(0x90); }
	for (size_t i = 0; i < literals.size(); ++i) {
		bind(literals[i].second);
		d32(literals[i].first);
	}
}

void X64Emitter::alignTo(size_t boundary) {
	assert(boundary != 0 && (boundary & (boundary - 1)) == 0);
	while ((bytes.size() & (boundary - 1)) != 0) { b(0x90); }															// unreachable: every function ends in a jmp
}

void X64Emitter::finalize() {
	for (size_t i = 0; i < fixups.size(); ++i) {
		const Fixup& f = fixups[i];
		const ptrdiff_t target = labelTargets[f.labelId];
		assert(target >= 0 && "fixup to an unbound label (missing bind() or emitLiteralPool())");
		const int32_t rel = static_cast<int32_t>(target - static_cast<ptrdiff_t>(f.site + 4));
		const uint32_t u = static_cast<uint32_t>(rel);
		bytes[f.site + 0] = static_cast<uint8_t>(u);
		bytes[f.site + 1] = static_cast<uint8_t>(u >> 8);
		bytes[f.site + 2] = static_cast<uint8_t>(u >> 16);
		bytes[f.site + 3] = static_cast<uint8_t>(u >> 24);
	}
}


/*
	Lowering and the native dispatcher. The dispatcher's frame is the only host frame: GAZL calls and returns branch
	directly and keep their return addresses on the ipStack, and a `^native` is called inline, so a fuel timeout can
	suspend and resume anywhere, even inside nested GAZL calls. See design/jit/JitCompilerResearch.md section 5.4.
*/

// Pins are callee-saved and scratch caller-saved on both SysV and Win64. `dataStackEnd` is loaded from ctx on demand.
static const Reg DSP = RBX, MEMORY_BASE = R14, FUEL = R13, IP_STACK_PTR = R15, CONTEXT = R12;
static const Reg SCRATCH_A = RCX, SCRATCH_B = RDX;																		// A doubles as the shift count CL
static const Reg FLOAT_0 = static_cast<Reg>(0), FLOAT_1 = static_cast<Reg>(1);											// xmm0 / xmm1

/*
	`ARG_0` is the one argument register that differs by ABI. `CALL_FRAME` is what the dispatcher reserves for a
	segment's inline native call. Win64 also makes rsi/rdi callee-saved, which matters only around `rep movsd`.
*/
#if defined(_WIN32)
static const Reg ARG_0 = RCX;
static const uint32_t CALL_FRAME = 40u;																					// 32-byte shadow space + 8-byte align pad
#else
static const Reg ARG_0 = RDI;
static const uint32_t CALL_FRAME = 8u;																					// 16-byte align pad only
#endif

// `reloadState` needs ctx already in `CONTEXT`; `enterSegment` takes it from `ARG_0`.
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
	Register cache pools: caller-saved on both SysV and Win64, and disjoint from the pins and from the rax/rcx/rdx that
	idiv, shifts and `rep movsd` use, so the cache never needs `evict()`.
*/
static const int X64_GENERAL_POOL[] = { R8, R9, R10, R11 };
static const int X64_FLOAT_POOL[] = { 2, 3, 4, 5 };																		// xmm2-xmm5

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
	public:		virtual bool bridgeWritesHome() const { return true; }							// see RegisterCacheBackend
	public:		virtual void emitCrossMove(int dstRegister, RegisterClass dstClass, int srcRegister) {
					const Reg d = static_cast<Reg>(dstRegister), s = static_cast<Reg>(srcRegister);
					if (dstClass == FLOAT_REGISTER) { e.movdToXmm(d, s); } else { e.movdFromXmm(d, s); }
				}
	private:	X64Emitter& e;
};

typedef BasicColdTrap<Label> ColdTrap;
typedef BasicColdEdge<Label> ColdEdge;

static Label resolveConditionalEdge(X64Emitter& emitter, RegisterCache& cache, std::map<UInt, ResidencyMap>& entryMaps
		, std::map<UInt, Label>& labels, UInt target, bool resident, std::vector<ColdEdge>& coldEdges) {
	ColdEdge edge;
	if (!planConditionalEdge(cache, entryMaps, target, resident, edge.dirty)) { return labels[target]; }
	edge.label = emitter.newLabel(); edge.target = target;
	coldEdges.push_back(edge);
	return edge.label;
}

/*
	Slides the TAIL argument window down onto this frame's base and rewinds dsp to it. The destination is below the
	source, so an ascending copy is overlap-safe. Must not touch `SCRATCH_B`, which holds the indirect target.
*/
static void emitTailWindow(X64Emitter& emitter, UInt window, UInt frame) {
	const int32_t down = 0 - static_cast<int32_t>(frame * 4u);
	if (window <= 8) {
		for (UInt k = 0; k < window; ++k) {
			emitter.load(RAX, DSP, static_cast<int32_t>(k * 4u));
			emitter.store(DSP, static_cast<int32_t>(k * 4u) + down, RAX);
		}
	} else {
	#if defined(_WIN32)
		emitter.push(RSI); emitter.push(RDI);																			// callee-saved on Win64
	#endif
		emitter.movQ(RDI, DSP); emitter.addImmQ(RDI, static_cast<uint32_t>(down));
		emitter.movQ(RSI, DSP);
		emitter.movImm(SCRATCH_A, window);
		emitter.cld(); emitter.repMovsd();
	#if defined(_WIN32)
		emitter.pop(RDI); emitter.pop(RSI);
	#endif
	}
	if (frame != 0) { emitter.addImmQ(DSP, static_cast<uint32_t>(down)); }
}

// `d = a <op> b` on the two-operand ISA; goes through a scratch when `d` aliases `b`.
static void emitTwoOperand(X64Emitter& emitter, RegisterCache& cache, BinaryOp copy, BinaryOp op
		, RegisterClass registerClass, int d, int a, int b) {
	if (d != b) {
		if (d != a) {
			(emitter.*copy)(static_cast<Reg>(d), static_cast<Reg>(a));
		}
		(emitter.*op)(static_cast<Reg>(d), static_cast<Reg>(b));
	} else {
		const int t = cache.scratch(registerClass);
		(emitter.*copy)(static_cast<Reg>(t), static_cast<Reg>(a));
		(emitter.*op)(static_cast<Reg>(t), static_cast<Reg>(b));
		(emitter.*copy)(static_cast<Reg>(d), static_cast<Reg>(t));
	}
}

static int loadIntOperand(X64Emitter& emitter, RegisterCache& cache, const Value& p, bool isConst) {
	if (!isConst) { return cache.read(p.i, GENERAL_REGISTER); }
	const int r = cache.scratch(GENERAL_REGISTER);
	emitter.movImm(static_cast<Reg>(r), static_cast<uint32_t>(p.i));
	return r;
}

static void emitBinary(X64Emitter& emitter, RegisterCache& cache, BinaryOp op, const Instruction& instruction, bool source1Const, bool source2Const) {
	const int a = loadIntOperand(emitter, cache, instruction.p1, source1Const);
	const int b = loadIntOperand(emitter, cache, instruction.p2, source2Const);
	const int d = cache.define(instruction.p0.i, GENERAL_REGISTER);
	emitTwoOperand(emitter, cache, &X64Emitter::mov, op, GENERAL_REGISTER, d, a, b);
	cache.endInstruction();
}

/*
	Divisor -1 is special-cased (quotient -a, remainder 0) because x86 idiv faults on INT_MIN / -1. A constant zero
	divisor is rejected at assembly, so only a variable divisor gets the zero check.
*/
static void emitDivMod(X64Emitter& emitter, RegisterCache& cache, const Instruction& instruction, bool rem, bool source1Const, bool source2Const, std::vector<ColdTrap>& coldTraps) {
	if (source1Const) { emitter.movImm(RAX, static_cast<uint32_t>(instruction.p1.i)); }
	else { const int a = cache.read(instruction.p1.i, GENERAL_REGISTER); emitter.mov(RAX, static_cast<Reg>(a)); }
	if (source2Const) { emitter.movImm(RCX, static_cast<uint32_t>(instruction.p2.i)); }
	else { const int bb = cache.read(instruction.p2.i, GENERAL_REGISTER); emitter.mov(RCX, static_cast<Reg>(bb)); }
	if (!source2Const) {
		ColdTrap trap; trap.label = emitter.newLabel(); trap.status = DIVISION_BY_ZERO;
		emitter.cmpImm(RCX, 0);
		cache.captureDirtyLines(trap.dirty);																			// the trap exit must leave memory interpreter-identical
		emitter.jcc(CC_E, trap.label);
		coldTraps.push_back(trap);
	}
	emitter.cmpImm(RCX, 0xFFFFFFFFu);
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

// `kind`: 0 = shl, 1 = shr, 2 = sar.
static void emitShift(X64Emitter& emitter, RegisterCache& cache, const Instruction& instruction, int kind, bool source1Const, bool source2Const) {
	int v;
	if (source1Const) { v = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(v), static_cast<uint32_t>(instruction.p1.i)); }
	else { v = cache.read(instruction.p1.i, GENERAL_REGISTER); }
	int count = -1;
	if (!source2Const) {
		count = cache.read(instruction.p2.i, GENERAL_REGISTER);
		emitter.mov(RCX, static_cast<Reg>(count));																		// before seeding dst, which may alias the count
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

static void emitBranch(X64Emitter& emitter, RegisterCache& cache, Cond condition, const Instruction& instruction, UInt instructionIndex,
		bool operand0Const, bool operand1Const, std::map<UInt, Label>& labels, std::map<UInt, ResidencyMap>& entryMaps,
		bool resident, std::vector<ColdEdge>& coldEdges) {
	const int a = loadIntOperand(emitter, cache, instruction.p0, operand0Const);
	const int b = loadIntOperand(emitter, cache, instruction.p1, operand1Const);
	emitter.cmp(static_cast<Reg>(a), static_cast<Reg>(b));
	cache.endInstruction();
	const UInt target = static_cast<UInt>(static_cast<Int>(instructionIndex) + instruction.p2.i);
	emitter.jcc(condition, resolveConditionalEdge(emitter, cache, entryMaps, labels, target, resident, coldEdges));
}

static int loadFloatOperandCached(X64Emitter& emitter, RegisterCache& cache, const Value& operand, bool isConst) {
	if (!isConst) { return cache.read(operand.i, FLOAT_REGISTER); }
	const int x = cache.scratch(FLOAT_REGISTER);
	emitter.movssRip(static_cast<Reg>(x), emitter.floatLiteral(static_cast<uint32_t>(operand.i)));
	return x;
}
#ifdef GAZL_CANONICAL_NAN
// Forces any NaN in `d` to 0x7FC00000, matching the interpreter's `canonicalNaN()`.
static void emitCanonicalizeNaN(X64Emitter& emitter, int d) {
	Label notNaN = emitter.newLabel();
	emitter.ucomiss(static_cast<Reg>(d), static_cast<Reg>(d));		// PF set iff d is NaN
	emitter.jcc(CC_NP, notNaN);
	emitter.movssRip(static_cast<Reg>(d), emitter.floatLiteral(0x7FC00000u));
	emitter.bind(notNaN);
}
#define EMIT_CANON_NAN(emitter, d) emitCanonicalizeNaN((emitter), (d))
#else
#define EMIT_CANON_NAN(emitter, d) ((void)0)
#endif

static void emitBinaryFloat(X64Emitter& emitter, RegisterCache& cache, BinaryOp fop, const Instruction& instruction, bool source1Const, bool source2Const) {
	const int a = loadFloatOperandCached(emitter, cache, instruction.p1, source1Const);
	const int b = loadFloatOperandCached(emitter, cache, instruction.p2, source2Const);
	const int d = cache.define(instruction.p0.i, FLOAT_REGISTER);
	emitTwoOperand(emitter, cache, &X64Emitter::movssReg, fop, FLOAT_REGISTER, d, a, b);
	EMIT_CANON_NAN(emitter, d);
	cache.endInstruction();
}

// `kind`: 0 = <, 1 = !(<), 2 = ==, 3 = !=, each with C++ NaN semantics.
static void emitBranchFloat(X64Emitter& emitter, RegisterCache& cache, int kind, const Instruction& instruction, UInt instructionIndex,
		bool operand0Const, bool operand1Const, std::map<UInt, Label>& labels, std::map<UInt, ResidencyMap>& entryMaps,
		bool resident, std::vector<ColdEdge>& coldEdges) {
	const int a = loadFloatOperandCached(emitter, cache, instruction.p0, operand0Const);
	const int b = loadFloatOperandCached(emitter, cache, instruction.p1, operand1Const);
	const Reg xa = static_cast<Reg>(a), xb = static_cast<Reg>(b);
	const UInt targetIndex = static_cast<UInt>(static_cast<Int>(instructionIndex) + instruction.p2.i);
	if (kind == 0) { emitter.ucomiss(xb, xa); }																			// a < b as b > a, which is false when unordered
	else { emitter.ucomiss(xa, xb); }
	cache.endInstruction();
	Label target = resolveConditionalEdge(emitter, cache, entryMaps, labels, targetIndex, resident, coldEdges);			// may emit only movs, which leave EFLAGS intact
	if (kind == 0) { emitter.jcc(CC_A, target); }
	else if (kind == 1) { emitter.jcc(CC_P, target); emitter.jcc(CC_AE, target); }					// ucomiss sets CF on NaN, so CC_AE alone would miss unordered
	else if (kind == 2) { Label unordered = emitter.newLabel(); emitter.jcc(CC_P, unordered); emitter.jcc(CC_E, target); emitter.bind(unordered); }
	else { emitter.jcc(CC_P, target); emitter.jcc(CC_NE, target); }
}

/*
	Appends the function at `funcStart` to `emitter`. Pins arrive live from the dispatcher, and every terminal path
	jumps to `epilogue` with its Status in eax. Throws on an opcode the backend does not cover.
*/
void JitCompilerX64::lowerFunction(X64Emitter& emitter, const AssembledProgram& program, UInt funcStart, UInt funcEnd
		, const Offsets& offsets, const std::vector<Label>& entryLabels, Label epilogue) {
	/*
		`MEMORY_BASE` holds `memoryBase`, the lowest constant, so a pointer plus `readOffset` is its word index from
		there. A write is range-checked as `pointer - MEMORY_OFFSET` against `rwMemorySize` and stored `constantsSize`
		words further on (`rwDisplacement`), as in the interpreter. `memory`, for the analyses, is the word at
		`MEMORY_OFFSET`.
	*/
	const Instruction* const code = program.code;
	const UInt functionCount = program.sizes.functionCount;
	const UInt constantsSize = program.sizes.constsSize - 1;
	const Value* const memory = program.memory + constantsSize;
	const UInt readOffset = constantsSize - MEMORY_OFFSET;
	const int32_t rwDisplacement = static_cast<int32_t>(constantsSize * 4);
	const Pointer lowestAddress = MEMORY_OFFSET - constantsSize;
	const UInt endIndex = funcEnd;																						// the next FUNC, not the first RETU: a function may have several RETUs or end in a GOTO

	// Every block leader is a fuel safepoint and a resume point, with a mainline label and a suspend stub.
	std::map<UInt, UInt> loopWeight;
	jitFuelSafepoints(code, funcStart, endIndex, memory, loopWeight);
	std::map<UInt, Label> labels, suspendL;
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		labels[it->first] = emitter.newLabel(); suspendL[it->first] = emitter.newLabel();
	}

	const UInt localsSize = static_cast<UInt>(code[funcStart].p0.i);
	if (localsSize >= MAX_JIT_FRAME_WORDS || static_cast<UInt>(code[funcStart].p1.i) >= MAX_JIT_FRAME_WORDS) {
		throw JitException("JIT: frame too large for a 32-bit byte offset");								// the host runs it interpreted
	}
	if (localsSize != 0) { emitter.addImmQ(DSP, localsSize * 4u); }
	{
		const UInt paramsSize = static_cast<UInt>(code[funcStart].p1.i);
		Label stackOk = emitter.newLabel();
		emitter.movQ(RAX, DSP); if (paramsSize != 0) { emitter.addImmQ(RAX, paramsSize * 4u); }
		emitter.loadQ(SCRATCH_A, CONTEXT, offsets.dsend); emitter.cmpQ(RAX, SCRATCH_A); emitter.jcc(CC_BE, stackOk);
		emitter.movImm(RAX, static_cast<uint32_t>(DATA_STACK_OVERFLOW)); emitter.jmp(epilogue);
		emitter.bind(stackOk);
	}

	X64SlotBackend slotBackend(emitter);
	const RegisterPool registerPool = { X64_GENERAL_POOL, sizeof(X64_GENERAL_POOL) / sizeof(X64_GENERAL_POOL[0])
			, X64_FLOAT_POOL, sizeof(X64_FLOAT_POOL) / sizeof(X64_FLOAT_POOL[0]) };
	RegisterCache cache(registerPool, slotBackend);
	UseSchedule useSchedule;
	buildUseSchedule(code, funcStart, endIndex, useSchedule);
	cache.setUseSchedule(&useSchedule);
	std::map<Int, int> pointerRealm;
	buildPointerRealms(program, funcStart, endIndex, pointerRealm);
	const std::map<Int, int>::const_iterator realmEnd = pointerRealm.end();
	std::map<UInt, UInt> loopExtent;
	jitResidencyLeaders(code, funcStart, endIndex, memory, loopExtent);
	LiveSets liveIn;
	if (!loopExtent.empty()) {																							// only resident loop headers read it
		liveIn.build(code, funcStart, endIndex, memory);
	}
	std::map<UInt, ResidencyMap> entryMaps;
	std::vector<ColdTrap> coldTraps;																					// these two are emitted after the mainline
	std::vector<ColdEdge> coldEdges;
	UInt residentEnd = 0;
	bool resident = false;																								// inside a register-resident loop body

	for (UInt j = funcStart; j <= endIndex; ++j) {
		cache.setInstructionIndex(j);
		if (resident && j > residentEnd) { resident = false; }
		std::map<UInt, Label>::iterator labelIt = labels.find(j);
		if (labelIt != labels.end()) {
			establishLeader(cache, code, j, loopExtent, loopWeight, liveIn, entryMaps, resident, residentEnd);
			emitter.bind(labelIt->second);
		}
		std::map<UInt, UInt>::iterator weightIt = loopWeight.find(j);													// charge the block; suspend once fuel goes negative
		if (weightIt != loopWeight.end()) { emitter.subImm(FUEL, weightIt->second); emitter.jcc(CC_S, suspendL[j]); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		if (!isCacheLowered(op)) { cache.barrier(); }
		switch (op) {
			case OP_FUNC: break;
			case OP_RETU: {																								// pops {continuation, dsp}; dsp 0 marks a native or top-level caller
				Label notNative = emitter.newLabel();
				emitter.addImmQ(IP_STACK_PTR, 0u - 16u);
				emitter.loadQ(RAX, IP_STACK_PTR, 0);
				emitter.loadQ(DSP, IP_STACK_PTR, 8);
				emitter.movImm(SCRATCH_A, 0); emitter.cmpQ(DSP, SCRATCH_A); emitter.jcc(CC_NE, notNative);
				emitter.addImmQ(IP_STACK_PTR, 0u - 16u); emitter.loadQ(DSP, IP_STACK_PTR, 8);							// pop again for the true dsp
				emitter.storeQ(CONTEXT, offsets.dsp, DSP); emitter.storeQ(CONTEXT, offsets.ipsp, IP_STACK_PTR);			// published: a blocking native's caller
				emitter.store(CONTEXT, offsets.fuel, FUEL);																// re-reads these after its nested run()
				emitter.movImm(RAX, 0); emitter.jmp(epilogue);
				emitter.bind(notNative);
				emitter.jmpReg(RAX);
				break;
			}

			case OP_TAIL_CC: {																							// the ordinal is validated at assembly
				emitTailWindow(emitter, static_cast<UInt>(in.p1.i), static_cast<UInt>(in.p2.i));
				emitter.jmp(entryLabels[in.p0.t - FUNCTION_OFFSET]);
				break;
			}
			case OP_TAIL_VC: {
				Label trap = emitter.newLabel();
				emitter.load(SCRATCH_A, DSP, in.p0.i * 4); emitter.subImm(SCRATCH_A, FUNCTION_OFFSET);					// read before the slide overwrites the window
				emitter.cmpImm(SCRATCH_A, functionCount); emitter.jcc(CC_AE, trap);
				emitter.loadQ(RAX, CONTEXT, offsets.funcentries); emitter.shlImm(SCRATCH_A, 3); emitter.addQ(RAX, SCRATCH_A);
				emitter.loadQ(SCRATCH_B, RAX, 0);																		// survives the slide
				emitTailWindow(emitter, static_cast<UInt>(in.p1.i), static_cast<UInt>(in.p2.i));
				emitter.jmpReg(SCRATCH_B);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_CALL)); emitter.jmp(epilogue);
				break;
			}
			case OP_CALL_CVC: {																							// pushes {after, dsp} and branches into the callee
				const UInt callee = in.p0.t - FUNCTION_OFFSET;
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = emitter.newLabel(), ipOk = emitter.newLabel();
				emitter.loadQ(RAX, CONTEXT, offsets.ipsend); emitter.cmpQ(IP_STACK_PTR, RAX); emitter.jcc(CC_B, ipOk);
				emitter.movImm(RAX, static_cast<uint32_t>(IP_STACK_OVERFLOW)); emitter.jmp(epilogue); emitter.bind(ipOk);
				emitter.leaRip(RAX, after); emitter.storeQ(IP_STACK_PTR, 0, RAX); emitter.storeQ(IP_STACK_PTR, 8, DSP); emitter.addImmQ(IP_STACK_PTR, 16);
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }
				emitter.jmp(entryLabels[callee]);
				emitter.bind(after);																					// the callee's RETU lands here with pins live
				break;
			}
			case OP_CALL_VVC: {
				const UInt window = static_cast<UInt>(in.p1.i);
				Label after = emitter.newLabel(), ipOk = emitter.newLabel(), trap = emitter.newLabel();
				emitter.loadQ(RAX, CONTEXT, offsets.ipsend); emitter.cmpQ(IP_STACK_PTR, RAX); emitter.jcc(CC_B, ipOk);
				emitter.movImm(RAX, static_cast<uint32_t>(IP_STACK_OVERFLOW)); emitter.jmp(epilogue); emitter.bind(ipOk);
				emitter.load(RCX, DSP, in.p0.i * 4); emitter.subImm(RCX, FUNCTION_OFFSET);
				emitter.cmpImm(RCX, functionCount); emitter.jcc(CC_AE, trap);
				emitter.loadQ(RAX, CONTEXT, offsets.funcentries); emitter.shlImm(RCX, 3); emitter.addQ(RAX, RCX); emitter.loadQ(RDX, RAX, 0); // rdx = funcEntries[ordinal]
				emitter.leaRip(RAX, after); emitter.storeQ(IP_STACK_PTR, 0, RAX); emitter.storeQ(IP_STACK_PTR, 8, DSP); emitter.addImmQ(IP_STACK_PTR, 16);
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }
				emitter.jmpReg(RDX);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_CALL)); emitter.jmp(epilogue);
				emitter.bind(after);
				break;
			}
			case OP_CALL_NVC: {																							// calls the native inline, without a dispatcher round trip
				/*
					Everything reloaded after the call comes from ctx fields that a nested run() inside the native
					leaves stable. The OK path continues through `ctx.nativeafter`, preset to `after`;
					`JitProcessor::pushCall` retargets it at a pushed callee, whose RETU chains back to `after`. `after`
					rewinds dsp from the window to the caller frame, so pushed frames must store the window. RESUME is
					set only on a nonzero status, so a nested run() cannot clobber it.
				*/
				const UInt ordinal = static_cast<UInt>(in.p0.i);
				const UInt window = static_cast<UInt>(in.p1.i);
				Label hot = emitter.newLabel(), retry = emitter.newLabel(), after = emitter.newLabel();
				emitter.bind(hot);																						// re-entry on a blocking retry or resume
				if (window != 0) { emitter.addImmQ(DSP, window * 4u); }
				emitter.storeQ(CONTEXT, offsets.dsp, DSP); emitter.store(CONTEXT, offsets.fuel, FUEL); emitter.storeQ(CONTEXT, offsets.ipsp, IP_STACK_PTR);
				emitter.leaRip(RAX, after); emitter.storeQ(CONTEXT, offsets.nativeafter, RAX);
				emitter.loadQ(RAX, CONTEXT, offsets.natives); emitter.loadQ(RAX, RAX, static_cast<int32_t>(ordinal * 8));
				emitter.movQ(ARG_0, CONTEXT); emitter.callReg(RAX);														// pins are callee-saved, so they survive
				emitter.load(FUEL, CONTEXT, offsets.fuel);																// the native may have reset the fuel
				emitter.loadQ(IP_STACK_PTR, CONTEXT, offsets.ipsp);														// adopts frames pushed by pushCall
				emitter.loadQ(DSP, CONTEXT, offsets.dsp);
				emitter.cmpImm(RAX, 0); emitter.jcc(CC_NE, retry);
				emitter.loadQ(RAX, CONTEXT, offsets.nativeafter); emitter.jmpReg(RAX);
				emitter.bind(retry);
				if (window != 0) {																						// publish the caller dsp in both register and ctx:
					emitter.addImmQ(DSP, 0u - window * 4u);																// re-entry at `hot` advances it again
					emitter.storeQ(CONTEXT, offsets.dsp, DSP);
				}
				emitter.leaRip(SCRATCH_A, hot); emitter.storeQ(CONTEXT, offsets.resume, SCRATCH_A);						// not rax, which holds the status
				emitter.jmp(epilogue);
				emitter.bind(after);
				emitter.movImm(RAX, 0); emitter.storeQ(CONTEXT, offsets.nativeafter, RAX);								// re-arms the "only inside a native call" guard
				if (window != 0) { emitter.addImmQ(DSP, 0u - window * 4u); }
				break;
			}

			case OP_MOVE_VV: { const int s = cache.read(in.p1.i, GENERAL_REGISTER); const int d = cache.define(in.p0.i, GENERAL_REGISTER); if (d != s) { emitter.mov(static_cast<Reg>(d), static_cast<Reg>(s)); } cache.endInstruction(); break; }
			case OP_MOVE_VC: { const int d = cache.define(in.p0.i, GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(d), static_cast<uint32_t>(in.p1.i)); cache.endInstruction(); break; }

			// Constant addresses are validated at assembly and cannot reach the frame, so the cache needs no flush.
			case OP_PEEK_VC: { const int d = cache.define(in.p0.i, GENERAL_REGISTER); emitter.load(static_cast<Reg>(d), MEMORY_BASE, static_cast<int32_t>((in.p1.p + readOffset) * 4)); cache.endInstruction(); break; }
			case OP_POKE_CV: { const int r = cache.read(in.p1.i, GENERAL_REGISTER); emitter.store(MEMORY_BASE, static_cast<int32_t>((in.p0.p + readOffset) * 4), static_cast<Reg>(r)); cache.endInstruction(); break; }
			case OP_POKE_CC: { const int r = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(r), static_cast<uint32_t>(in.p1.i)); emitter.store(MEMORY_BASE, static_cast<int32_t>((in.p0.p + readOffset) * 4), static_cast<Reg>(r)); cache.endInstruction(); break; }

			/*
				Skipping the flush is sound only when the constant is a real globals or constants address. The
				pointer-deref idiom `PEEK $x $p` also finalizes to this form, with a small offset as the constant and
				the pointer in the variable, so it must flush like `POKE_VVV`.
			*/
			case OP_PEEK_VCV: {
				const int32_t base = static_cast<int32_t>(in.p1.p + readOffset);
				const bool constAddrBase = isDataAddress(program, in.p1.p);
				ColdTrap trap; trap.label = emitter.newLabel(); trap.status = BAD_PEEK;
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				if (!constAddrBase) { cache.spillDirtyResident(); }
				emitter.mov(RAX, static_cast<Reg>(idx)); emitter.addImm(RAX, static_cast<uint32_t>(base));
				emitter.load(SCRATCH_B, CONTEXT, offsets.memsize); emitter.cmp(RAX, SCRATCH_B);
				cache.captureDirtyLines(trap.dirty);
				emitter.jcc(CC_AE, trap.label);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				emitter.loadIdx(static_cast<Reg>(d), MEMORY_BASE, RAX, 0);								// index by the checked sum: `idx` alone zero-extends, so a 32-bit wrap would escape the bound
				cache.endInstruction();
				coldTraps.push_back(trap);
				break;
			}
			case OP_POKE_CVV: case OP_POKE_CVC: {
				const int32_t base = static_cast<int32_t>(in.p0.p - MEMORY_OFFSET);
				const bool constAddrBase = isDataAddress(program, in.p0.p);
				ColdTrap trap; trap.label = emitter.newLabel(); trap.status = BAD_POKE;
				const int idx = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_POKE_CVC) { val = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(val), static_cast<uint32_t>(in.p2.i)); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				if (!constAddrBase) { cache.spillDirtyResident(); }
				emitter.mov(RAX, static_cast<Reg>(idx)); emitter.addImm(RAX, static_cast<uint32_t>(base));
				emitter.load(SCRATCH_B, CONTEXT, offsets.rwmemsize); emitter.cmp(RAX, SCRATCH_B);
				cache.captureDirtyLines(trap.dirty);
				emitter.jcc(CC_AE, trap.label);
				emitter.storeIdx(MEMORY_BASE, RAX, rwDisplacement, static_cast<Reg>(val));				// index by the checked sum, as in OP_PEEK_VCV
				cache.endInstruction();
				if (!constAddrBase) { cache.invalidateAll(); }
				coldTraps.push_back(trap);
				break;
			}

			// A `REALM_NONFRAME` base cannot alias a cached local, so the flush is skipped; the trap arm still spills.
			case OP_PEEK_VVV: {
				const std::map<Int, int>::const_iterator baseRealm = pointerRealm.find(in.p1.i);
				const bool nonFrame = (baseRealm != realmEnd && baseRealm->second == REALM_NONFRAME);
				ColdTrap trap; trap.label = emitter.newLabel(); trap.status = BAD_PEEK;
				const int bp = cache.read(in.p1.i, GENERAL_REGISTER);
				const int ip = cache.read(in.p2.i, GENERAL_REGISTER);
				if (!nonFrame) { cache.spillDirtyResident(); }
				emitter.mov(RAX, static_cast<Reg>(bp)); emitter.add(RAX, static_cast<Reg>(ip)); emitter.addImm(RAX, readOffset);
				emitter.load(SCRATCH_B, CONTEXT, offsets.memsize); emitter.cmp(RAX, SCRATCH_B);
				cache.captureDirtyLines(trap.dirty);
				emitter.jcc(CC_AE, trap.label);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				emitter.loadIdx(static_cast<Reg>(d), MEMORY_BASE, RAX, 0);
				cache.endInstruction();
				coldTraps.push_back(trap);
				break;
			}
			case OP_POKE_VVV: case OP_POKE_VVC: {
				const std::map<Int, int>::const_iterator baseRealm = pointerRealm.find(in.p0.i);
				const bool nonFrame = (baseRealm != realmEnd && baseRealm->second == REALM_NONFRAME);
				ColdTrap trap; trap.label = emitter.newLabel(); trap.status = BAD_POKE;
				const int bp = cache.read(in.p0.i, GENERAL_REGISTER);
				const int ip = cache.read(in.p1.i, GENERAL_REGISTER);
				int val;
				if (op == OP_POKE_VVC) { val = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(val), static_cast<uint32_t>(in.p2.i)); }
				else { val = cache.read(in.p2.i, GENERAL_REGISTER); }
				if (!nonFrame) { cache.spillDirtyResident(); }
				emitter.mov(RAX, static_cast<Reg>(bp)); emitter.add(RAX, static_cast<Reg>(ip)); emitter.subImm(RAX, MEMORY_OFFSET);
				emitter.load(SCRATCH_B, CONTEXT, offsets.rwmemsize); emitter.cmp(RAX, SCRATCH_B);
				cache.captureDirtyLines(trap.dirty);
				emitter.jcc(CC_AE, trap.label);
				emitter.storeIdx(MEMORY_BASE, RAX, rwDisplacement, static_cast<Reg>(val));
				cache.endInstruction();
				if (!nonFrame) { cache.invalidateAll(); }
				coldTraps.push_back(trap);
				break;
			}

			// The bound is the free span from dsp to `dataStackEnd`, less the constant base.
			case OP_GETL_VVV: {
				const int32_t frameBase = static_cast<int32_t>(in.p1.i);
				Label trap = emitter.newLabel(), cont = emitter.newLabel();
				const int idx = cache.read(in.p2.i, GENERAL_REGISTER);
				cache.spillDirtyResident();
				emitter.loadQ(RAX, CONTEXT, offsets.dsend); emitter.subQ(RAX, DSP); emitter.shrImm(RAX, 2);
				emitter.subImm(RAX, static_cast<uint32_t>(frameBase)); emitter.jcc(CC_S, trap);
				emitter.cmp(static_cast<Reg>(idx), RAX); emitter.jcc(CC_AE, trap);
				const int d = cache.define(in.p0.i, GENERAL_REGISTER);
				emitter.loadIdx(static_cast<Reg>(d), DSP, static_cast<Reg>(idx), frameBase * 4);
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
				emitter.subImm(RAX, static_cast<uint32_t>(frameBase)); emitter.jcc(CC_S, trap);
				emitter.cmp(static_cast<Reg>(idx), RAX); emitter.jcc(CC_AE, trap);
				emitter.storeIdx(DSP, static_cast<Reg>(idx), frameBase * 4, static_cast<Reg>(val));
				cache.endInstruction();
				cache.invalidateAll();
				emitter.jmp(cont);
				emitter.bind(trap); emitter.movImm(RAX, static_cast<uint32_t>(BAD_POKE)); emitter.jmp(epilogue);
				emitter.bind(cont);
				break;
			}

			/*
				Both ranges are checked like the interpreter, before the Win64 push so a trap needs no unwind. Each
				check compares the index against size minus count, since index plus count could wrap.
			*/
			case OP_COPY_VVC: case OP_COPY_VCC: case OP_COPY_CVC: case OP_COPY_CCC: {
				const bool destConst = (op == OP_COPY_CVC || op == OP_COPY_CCC);
				const bool srcConst = (op == OP_COPY_VCC || op == OP_COPY_CCC);
				Label copyTrap = emitter.newLabel(), copyCont = emitter.newLabel();
				if (destConst) { emitter.movImm(RAX, static_cast<uint32_t>(in.p0.p - MEMORY_OFFSET)); }
				else { emitter.load(RAX, DSP, in.p0.i * 4); emitter.subImm(RAX, MEMORY_OFFSET); }
				emitter.load(SCRATCH_B, CONTEXT, offsets.rwmemsize); emitter.cmpImm(SCRATCH_B, static_cast<uint32_t>(in.p2.i)); emitter.jcc(CC_B, copyTrap);
					emitter.subImm(SCRATCH_B, static_cast<uint32_t>(in.p2.i)); emitter.cmp(RAX, SCRATCH_B); emitter.jcc(CC_A, copyTrap);
				if (srcConst) { emitter.movImm(RAX, static_cast<uint32_t>(in.p1.p + readOffset)); }
				else { emitter.load(RAX, DSP, in.p1.i * 4); emitter.addImm(RAX, readOffset); }
				emitter.load(SCRATCH_B, CONTEXT, offsets.memsize); emitter.cmpImm(SCRATCH_B, static_cast<uint32_t>(in.p2.i)); emitter.jcc(CC_B, copyTrap);
					emitter.subImm(SCRATCH_B, static_cast<uint32_t>(in.p2.i)); emitter.cmp(RAX, SCRATCH_B); emitter.jcc(CC_A, copyTrap);
			#if defined(_WIN32)
				emitter.push(RSI); emitter.push(RDI);																	// callee-saved on Win64
			#endif
				if (destConst) { emitter.movQ(RDI, MEMORY_BASE); emitter.addImmQ(RDI, static_cast<uint32_t>((in.p0.p + readOffset) * 4)); }
				else { emitter.load(RAX, DSP, in.p0.i * 4); emitter.addImm(RAX, readOffset); emitter.shlImm(RAX, 2); emitter.movQ(RDI, MEMORY_BASE); emitter.addQ(RDI, RAX); }
				if (srcConst) { emitter.movQ(RSI, MEMORY_BASE); emitter.addImmQ(RSI, static_cast<uint32_t>((in.p1.p + readOffset) * 4)); }
				else { emitter.load(RAX, DSP, in.p1.i * 4); emitter.addImm(RAX, readOffset); emitter.shlImm(RAX, 2); emitter.movQ(RSI, MEMORY_BASE); emitter.addQ(RSI, RAX); }
				emitter.movImm(RCX, static_cast<uint32_t>(in.p2.i));
				emitter.cld(); emitter.repMovsd();
			#if defined(_WIN32)
				emitter.pop(RDI); emitter.pop(RSI);
			#endif
				emitter.jmp(copyCont);
				emitter.bind(copyTrap); emitter.movImm(RAX, static_cast<uint32_t>(ACCESS_VIOLATION)); emitter.jmp(epilogue);
				emitter.bind(copyCont);
				break;
			}

			case OP_ADRL: {																								// pointer = lowestAddress + dsp word index + slot
				emitter.movQ(RAX, DSP); emitter.subQ(RAX, MEMORY_BASE); emitter.shrImm(RAX, 2);
				emitter.addImm(RAX, static_cast<uint32_t>(in.p1.i));
				emitter.addImm(RAX, lowestAddress);
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

			case OP_DIVI_VVV: emitDivMod(emitter, cache, in, false, false, false, coldTraps); break;
			case OP_DIVI_VVC: emitDivMod(emitter, cache, in, false, false, true, coldTraps); break;
			case OP_DIVI_VCV: emitDivMod(emitter, cache, in, false, true, false, coldTraps); break;
			case OP_MODI_VVV: emitDivMod(emitter, cache, in, true, false, false, coldTraps); break;
			case OP_MODI_VVC: emitDivMod(emitter, cache, in, true, false, true, coldTraps); break;
			case OP_MODI_VCV: emitDivMod(emitter, cache, in, true, true, false, coldTraps); break;
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

			case OP_FORi_VVB: case OP_FORi_VCB: {
				const int r = cache.read(in.p0.i, GENERAL_REGISTER);
				emitter.addImm(static_cast<Reg>(r), 1);
				cache.define(in.p0.i, GENERAL_REGISTER);																// marks the incremented counter dirty
				int lim;
				if (op == OP_FORi_VCB) { lim = cache.scratch(GENERAL_REGISTER); emitter.movImm(static_cast<Reg>(lim), static_cast<uint32_t>(in.p1.i)); }
				else { lim = cache.read(in.p1.i, GENERAL_REGISTER); }
				emitter.cmp(static_cast<Reg>(r), static_cast<Reg>(lim));
				cache.endInstruction();
				emitter.jcc(CC_L, resolveConditionalEdge(emitter, cache, entryMaps, labels, static_cast<UInt>(static_cast<Int>(j) + in.p2.i), resident, coldEdges));
				break;
			}

			case OP_ADDF_VVV: emitBinaryFloat(emitter, cache, &X64Emitter::addss, in, false, false); break;
			case OP_ADDF_VVC: emitBinaryFloat(emitter, cache, &X64Emitter::addss, in, false, true); break;
			case OP_SUBF_VVV: emitBinaryFloat(emitter, cache, &X64Emitter::subss, in, false, false); break;
			case OP_SUBF_VVC: emitBinaryFloat(emitter, cache, &X64Emitter::subss, in, false, true); break;
			case OP_SUBF_VCV: emitBinaryFloat(emitter, cache, &X64Emitter::subss, in, true, false); break;
			case OP_MULF_VVV: emitBinaryFloat(emitter, cache, &X64Emitter::mulss, in, false, false); break;
			case OP_MULF_VVC: emitBinaryFloat(emitter, cache, &X64Emitter::mulss, in, false, true); break;
			case OP_DIVF_VVV: emitBinaryFloat(emitter, cache, &X64Emitter::divss, in, false, false); break;
			case OP_DIVF_VVC: emitBinaryFloat(emitter, cache, &X64Emitter::divss, in, false, true); break;
			case OP_DIVF_VCV: emitBinaryFloat(emitter, cache, &X64Emitter::divss, in, true, false); break;
			case OP_FTOI_VVC: {																							// (int)(src * scale), saturating like the interpreter's ftoi()
				{ const int sx = cache.read(in.p1.i, FLOAT_REGISTER); emitter.movssReg(FLOAT_0, static_cast<Reg>(sx)); }
				emitter.movssRip(FLOAT_1, emitter.floatLiteral(static_cast<uint32_t>(in.p2.i))); emitter.mulss(FLOAT_0, FLOAT_1);
				emitter.cvttss2si(SCRATCH_A, FLOAT_0);																	// 0x80000000 on overflow, inf or NaN
				emitter.cmpImm(SCRATCH_A, 0x80000000u);
				Label ftoiDone = emitter.newLabel();
				emitter.jcc(CC_NE, ftoiDone);
				emitter.ucomiss(FLOAT_0, FLOAT_0);
				Label ftoiNotNan = emitter.newLabel();
				emitter.jcc(CC_NP, ftoiNotNan);
				emitter.movImm(SCRATCH_A, 0); emitter.jmp(ftoiDone);													// NaN -> 0
				emitter.bind(ftoiNotNan);
				emitter.movdFromXmm(SCRATCH_B, FLOAT_0); emitter.cmpImm(SCRATCH_B, 0);
				Label ftoiNegative = emitter.newLabel();
				emitter.jcc(CC_L, ftoiNegative);																		// negative keeps INT_MIN
				emitter.movImm(SCRATCH_A, 0x7FFFFFFFu);
				emitter.bind(ftoiNegative); emitter.bind(ftoiDone);
				{ const int d = cache.define(in.p0.i, GENERAL_REGISTER); emitter.mov(static_cast<Reg>(d), SCRATCH_A); }
				cache.endInstruction();
				break;
			}
			case OP_ITOF_VVC: {																							// (float)src * scale
				const int src = cache.read(in.p1.i, GENERAL_REGISTER);													// before define: p0 may alias p1
				const int d = cache.define(in.p0.i, FLOAT_REGISTER);
				emitter.xorps(static_cast<Reg>(d), static_cast<Reg>(d));												// cvtsi2ss merges into d: zero it to break the false dependency
				emitter.cvtsi2ss(static_cast<Reg>(d), static_cast<Reg>(src));
				emitter.movssRip(FLOAT_1, emitter.floatLiteral(static_cast<uint32_t>(in.p2.i))); emitter.mulss(static_cast<Reg>(d), FLOAT_1);
				cache.endInstruction();
				break;
			}
			case OP_ABSF: {																								// same bits as fabsf: NaN payload kept
				const int s = cache.read(in.p1.i, FLOAT_REGISTER);
				const int d = cache.define(in.p0.i, FLOAT_REGISTER);
				if (d != s) { emitter.movssReg(static_cast<Reg>(d), static_cast<Reg>(s)); }
				emitter.movssRip(FLOAT_1, emitter.floatLiteral(0x7FFFFFFFu));
				emitter.andps(static_cast<Reg>(d), FLOAT_1);
				cache.endInstruction();
				break;
			}
			case OP_FLOF: {
				const int s = cache.read(in.p1.i, FLOAT_REGISTER); const int d = cache.define(in.p0.i, FLOAT_REGISTER);
				if (d != s) { emitter.xorps(static_cast<Reg>(d), static_cast<Reg>(d)); }								// roundss merges into d: zero it to break the false dependency
				emitter.roundss(static_cast<Reg>(d), static_cast<Reg>(s), 1); cache.endInstruction(); break;
			}

			case OP_LSSF_VVB: emitBranchFloat(emitter, cache, 0, in, j, false, false, labels, entryMaps, resident, coldEdges); break;
			case OP_LSSF_VCB: emitBranchFloat(emitter, cache, 0, in, j, false, true, labels, entryMaps, resident, coldEdges); break;
			case OP_LSSF_CVB: emitBranchFloat(emitter, cache, 0, in, j, true, false, labels, entryMaps, resident, coldEdges); break;
			case OP_EQUF_VVB: emitBranchFloat(emitter, cache, 2, in, j, false, false, labels, entryMaps, resident, coldEdges); break;
			case OP_EQUF_VCB: emitBranchFloat(emitter, cache, 2, in, j, false, true, labels, entryMaps, resident, coldEdges); break;
			case OP_NLSF_VVB: emitBranchFloat(emitter, cache, 1, in, j, false, false, labels, entryMaps, resident, coldEdges); break;
			case OP_NLSF_VCB: emitBranchFloat(emitter, cache, 1, in, j, false, true, labels, entryMaps, resident, coldEdges); break;
			case OP_NLSF_CVB: emitBranchFloat(emitter, cache, 1, in, j, true, false, labels, entryMaps, resident, coldEdges); break;
			case OP_NEQF_VVB: emitBranchFloat(emitter, cache, 3, in, j, false, false, labels, entryMaps, resident, coldEdges); break;
			case OP_NEQF_VCB: emitBranchFloat(emitter, cache, 3, in, j, false, true, labels, entryMaps, resident, coldEdges); break;

			case OP_GOTO: {
				const UInt target = static_cast<UInt>(static_cast<Int>(j) + in.p0.i);
				reconcileOrBarrier(cache, entryMaps, target);
				emitter.jmp(labels[target]);
				break;
			}

			case OP_SWCH: {																								// index = min(unsigned V0, C1) into a table of rel32 jmps
				const std::vector<UInt> targets = switchTargets(code, j, memory);
				emitter.load(SCRATCH_A, DSP, in.p0.i * 4);
				emitter.cmpImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i));
				Label keep = emitter.newLabel();
				emitter.jcc(CC_BE, keep);
				emitter.movImm(SCRATCH_A, static_cast<uint32_t>(in.p1.i));
				emitter.bind(keep);
				emitter.mov(SCRATCH_B, SCRATCH_A); emitter.shlImm(SCRATCH_B, 2); emitter.add(SCRATCH_B, SCRATCH_A);		// index * 5: each table jmp is 5 bytes
				Label tableBase = emitter.newLabel();
				emitter.leaRip(RAX, tableBase); emitter.addQ(RAX, SCRATCH_B); emitter.jmpReg(RAX);
				emitter.bind(tableBase);
				for (size_t k = 0; k < targets.size(); ++k) { emitter.jmp(labels[targets[k]]); }
				break;
			}

			case OP_LSSI_VVB: emitBranch(emitter, cache, CC_L, in, j, false, false, labels, entryMaps, resident, coldEdges); break;
			case OP_LSSI_VCB: emitBranch(emitter, cache, CC_L, in, j, false, true, labels, entryMaps, resident, coldEdges); break;
			case OP_LSSI_CVB: emitBranch(emitter, cache, CC_L, in, j, true, false, labels, entryMaps, resident, coldEdges); break;
			case OP_EQUI_VVB: emitBranch(emitter, cache, CC_E, in, j, false, false, labels, entryMaps, resident, coldEdges); break;
			case OP_EQUI_VCB: emitBranch(emitter, cache, CC_E, in, j, false, true, labels, entryMaps, resident, coldEdges); break;
			case OP_NLSI_VVB: emitBranch(emitter, cache, CC_GE, in, j, false, false, labels, entryMaps, resident, coldEdges); break;
			case OP_NLSI_VCB: emitBranch(emitter, cache, CC_GE, in, j, false, true, labels, entryMaps, resident, coldEdges); break;
			case OP_NLSI_CVB: emitBranch(emitter, cache, CC_GE, in, j, true, false, labels, entryMaps, resident, coldEdges); break;
			case OP_NEQI_VVB: emitBranch(emitter, cache, CC_NE, in, j, false, false, labels, entryMaps, resident, coldEdges); break;
			case OP_NEQI_VCB: emitBranch(emitter, cache, CC_NE, in, j, false, true, labels, entryMaps, resident, coldEdges); break;

			default: throwUnlowerableOpcode(op);																		// a backend bug, never routine
		}
	}
	// Cold section: trap arms and loop-exit edge stubs spill their dirty snapshot first.
	for (size_t k = 0; k < coldTraps.size(); ++k) {
		emitter.bind(coldTraps[k].label);
		spillResidencyMap(slotBackend, coldTraps[k].dirty);
		emitter.movImm(RAX, static_cast<uint32_t>(coldTraps[k].status)); emitter.jmp(epilogue);
	}

	for (size_t k = 0; k < coldEdges.size(); ++k) {
		emitter.bind(coldEdges[k].label);
		spillResidencyMap(slotBackend, coldEdges[k].dirty);
		emitter.jmp(labels[coldEdges[k].target]);
	}

	/*
		Suspend stubs, one per block leader. Memory must be interpreter-identical at a suspend, so a leader with
		register-resident entry state spills it and resumes through a stub that refills it, since the dispatcher
		reloads only the pins.
	*/
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		const UInt head = it->first;
		emitter.bind(suspendL[head]);
		const std::map<UInt, ResidencyMap>::const_iterator m = entryMaps.find(head);
		const bool resident = (m != entryMaps.end() && !m->second.entries.empty());
		if (resident) { spillResidencyMap(slotBackend, m->second); }
		writebackState(emitter, offsets);
		if (resident) {
			Label reload = emitter.newLabel();
			emitter.leaRip(RAX, reload); emitter.storeQ(CONTEXT, offsets.resume, RAX);
			emitter.movImm(RAX, static_cast<uint32_t>(TIME_OUT)); emitter.jmp(epilogue);
			emitter.bind(reload);
			fillResidencyMap(slotBackend, m->second);
			emitter.jmp(labels[head]);
		} else {
			emitter.leaRip(RAX, labels[head]); emitter.storeQ(CONTEXT, offsets.resume, RAX);
			emitter.movImm(RAX, static_cast<uint32_t>(TIME_OUT)); emitter.jmp(epilogue);
		}
	}
}

/*
	Emits `int dispatch(JitProcessor* ctx)`, which owns the only native frame: it saves the pins, reserves
	`CALL_FRAME`, loads the pins from ctx and jumps to RESUME. Segments come back to `epilogue` only to suspend, finish
	or trap.
*/
static size_t emitDispatcher(X64Emitter& emitter, const Offsets& offsets, Label epilogue) {
	const size_t entry = emitter.size();
	emitter.push(DSP); emitter.push(RBP); emitter.push(CONTEXT); emitter.push(FUEL); emitter.push(MEMORY_BASE); emitter.push(IP_STACK_PTR); // rbp only keeps the push count even for alignment
	emitter.addImmQ(RSP, 0u - CALL_FRAME);
	enterSegment(emitter, offsets);
	emitter.loadQ(RAX, CONTEXT, offsets.resume); emitter.jmpReg(RAX);
	emitter.bind(epilogue);																								// Status in eax
	emitter.addImmQ(RSP, CALL_FRAME);
	emitter.pop(IP_STACK_PTR); emitter.pop(MEMORY_BASE); emitter.pop(FUEL); emitter.pop(CONTEXT); emitter.pop(RBP); emitter.pop(DSP);
	emitter.ret();
	return entry;
}

void JitCompilerX64::compile(const AssembledProgram& program, JitModule& out) {
	EmittedModule emitted;
	const Offsets offsets = JitProcessor::layout();
	X64Emitter emitter;
	std::vector<Label> entryLabels(program.sizes.functionCount);
	for (UInt k = 0; k < program.sizes.functionCount; ++k) { entryLabels[k] = emitter.newLabel(); }
	Label epilogue = emitter.newLabel();

	for (UInt ordinal = 0; ordinal < program.sizes.functionCount; ++ordinal) {
		emitter.alignTo(16);																							// so a code-layout shift stays within one function
		emitter.bind(entryLabels[ordinal]);
		const UInt funcEnd = ((ordinal + 1 < program.sizes.functionCount) ? program.functionTable[ordinal + 1]
				: program.sizes.codeSize) - 1;
		lowerFunction(emitter, program, program.functionTable[ordinal], funcEnd, offsets, entryLabels, epilogue);
	}
	const size_t dispatcherOffset = emitDispatcher(emitter, offsets, epilogue);
	emitter.emitLiteralPool();
	emitter.finalize();

	// The module holds 32-bit words, so the last partial word is zero-padded.
	const size_t byteCount = emitter.size();
	emitted.code.assign((byteCount + 3) / 4, 0);
	if (byteCount != 0) { std::memcpy(&emitted.code[0], emitter.code(), byteCount); }
	emitted.entryByteOffsets.resize(program.sizes.functionCount);
	for (UInt ordinal = 0; ordinal < program.sizes.functionCount; ++ordinal) {
		emitted.entryByteOffsets[ordinal] = static_cast<size_t>(emitter.labelOffset(entryLabels[ordinal]));
	}
	emitted.dispatchByteOffset = dispatcherOffset;
	JitModule built(emitted);																							// throws JitException if the host denies executable memory
	out.swap(built);
}

// Only on an x86-64 host, so both backends can link together without a duplicate definition.
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
void NativeJitCompiler::compile(const AssembledProgram& program, JitModule& out) {
	JitCompilerX64().compile(program, out);
}
#endif

}																														// namespace GAZL
