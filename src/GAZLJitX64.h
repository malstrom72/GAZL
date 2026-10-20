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

#ifndef GAZLJitX64_h
#define GAZLJitX64_h

#include <stdint.h>
#include <vector>
#include <map>
#include <utility>
#include <cstddef>
#include "GAZLJit.h"

namespace GAZL {

/*
	x86-64 general-purpose register by 4-bit hardware encoding; the emitter method picks the 32- or 64-bit view.
	`R8`..`R15` need a REX prefix. As a memory base, `RSP`/`R12` force a SIB byte and `RBP`/`R13` a displacement.
*/
enum Reg {
	RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
	R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

// x86 condition code: the low nibble of the Jcc opcode.
enum Cond {
	CC_E = 0x4, CC_NE = 0x5, CC_L = 0xC, CC_GE = 0xD, CC_LE = 0xE, CC_G = 0xF,
	CC_B = 0x2, CC_AE = 0x3, CC_BE = 0x6, CC_A = 0x7, CC_P = 0xA, CC_NP = 0xB,	// P/NP: parity, set when a float compare is unordered
	CC_S = 0x8																	// sign: the fuel-timeout check
};

// A branch target; `id` is -1 until `X64Emitter::newLabel()` allocates it.
struct Label {
	int id;
	Label() : id(-1) {}
};

/*
	Appends x86-64 machine code into a byte buffer. Register-register forms are two-operand (`dst OP= src`). Branches
	emit a rel32 placeholder that `finalize()` patches once every label is bound. Ops are 32-bit unless named `*Q`
	(64-bit, for pointers).
*/
class X64Emitter {
	public:
		X64Emitter() {}

		void movImm(Reg rd, uint32_t imm);						// zero-extends to 64 bits
		void movImm64(Reg rd, uint64_t imm);
		void mov(Reg rd, Reg rs);
		void movQ(Reg rd, Reg rs);

		void add(Reg rd, Reg rs);
		void sub(Reg rd, Reg rs);
		void imul(Reg rd, Reg rs);
		void and_(Reg rd, Reg rs);
		void or_(Reg rd, Reg rs);
		void xor_(Reg rd, Reg rs);
		void cmp(Reg ra, Reg rb);
		void cmpQ(Reg ra, Reg rb);
		void addImm(Reg rd, uint32_t imm);
		void subImm(Reg rd, uint32_t imm);
		void cmpImm(Reg ra, uint32_t imm);
		void addQ(Reg rd, Reg rs);
		void addImmQ(Reg rd, uint32_t imm);
		void neg(Reg rd);
		void cdq();												// sign-extends eax into edx before `idiv`
		void idiv(Reg rs);										// edx:eax / rs: quotient in eax, remainder in edx
		void shlCl(Reg rd);
		void shrCl(Reg rd);										// logical
		void sarCl(Reg rd);										// arithmetic
		void shlImm(Reg rd, uint8_t n);
		void shrImm(Reg rd, uint8_t n);
		void sarImm(Reg rd, uint8_t n);

		void load(Reg rd, Reg base, int32_t disp);				// `mov rd(32), [base + disp]`
		void store(Reg base, int32_t disp, Reg rs);				// `mov [base + disp], rs(32)`
		void loadQ(Reg rd, Reg base, int32_t disp);				// `mov rd(64), [base + disp]`
		void storeQ(Reg base, int32_t disp, Reg rs);			// `mov [base + disp], rs(64)`
		void loadIdx(Reg rd, Reg base, Reg index, int32_t disp);	// `mov rd(32), [base + index*4 + disp]`
		void storeIdx(Reg base, Reg index, int32_t disp, Reg rs);	// `mov [base + index*4 + disp], rs(32)`
		void subQ(Reg rd, Reg rs);

		// SSE scalar floats: XMM operands reuse the `Reg` encodings 0..15 (a separate register file).
		void movssLoad(Reg xd, Reg base, int32_t disp);
		void movssStore(Reg base, int32_t disp, Reg xs);
		void movssReg(Reg xd, Reg xs);							// register copy, encoded as `movaps`
		void addss(Reg xd, Reg xs);
		void subss(Reg xd, Reg xs);
		void mulss(Reg xd, Reg xs);
		void divss(Reg xd, Reg xs);
		void ucomiss(Reg xa, Reg xb);
		void cvttss2si(Reg rd, Reg xs);							// float to int, truncating
		void xorps(Reg xd, Reg xs);								// with xd == xs, zeroes xd and breaks merge false dependencies
		void andps(Reg xd, Reg xs);
		void cvtsi2ss(Reg xd, Reg rs);
		void movdToXmm(Reg xd, Reg rs);							// bit copy
		void movdFromXmm(Reg rd, Reg xs);						// bit copy
		void roundss(Reg xd, Reg xs, uint8_t mode);				// SSE4.1; mode 1 = floor
		void movssRip(Reg xd, Label literal);					// `movss xd, [rip + literal]`: pooled constant load

		void push(Reg r);
		void pop(Reg r);
		void nop();
		void cld();
		void repMovsd();										// copies ecx dwords from [rsi] to [rdi]; clobbers rcx, rsi, rdi
		void ret();
		void jmp(Label target);
		void jcc(Cond cc, Label target);
		void callRel(Label target);
		void callReg(Reg r);
		void leaRip(Reg rd, Label target);						// `lea rd, [rip + target]` (64-bit)
		void jmpReg(Reg r);

		Label newLabel();										// unbound until `bind()`
		void bind(Label label);									// binds `label` to the current emit point
		void finalize();										// patches every rel32; all labels must be bound
		void alignTo(size_t boundary);							// nop-pads to a power-of-2 boundary

		Label floatLiteral(uint32_t bits);						// pooled and deduplicated by bit pattern
		void emitLiteralPool();									// once, after all code and before `finalize()`

		const uint8_t* code() const { return bytes.empty() ? 0 : &bytes[0]; }
		size_t size() const { return bytes.size(); }
		ptrdiff_t labelOffset(Label l) const { return labelTargets[l.id]; }	// byte offset; `l` must be bound

	private:
		struct Fixup {
			size_t site;										// byte offset of the rel32 field
			int labelId;
		};

		void b(uint8_t v) { bytes.push_back(v); }
		void d32(uint32_t v);									// little-endian
		void d64(uint64_t v);
		void rex(bool w, int reg, int base);					// emitted only when needed
		void modrmReg(int reg, int rm);
		void memOperand(int reg, Reg base, int32_t disp);
		void memOperandIdx(int reg, Reg base, Reg index, int32_t disp);
		void rex3(bool w, int reg, int index, int base);		// `rex()` plus REX.X for an index register
		void sseRR(uint8_t prefix, uint8_t opcode, int reg, int rm);
		void aluImm(int ext, Reg rd, uint32_t imm);				// 81 /ext id
		void relBranch(int labelId);

		std::vector<uint8_t> bytes;
		std::vector<ptrdiff_t> labelTargets;					// -1 while unbound
		std::vector<Fixup> fixups;
		std::vector< std::pair<uint32_t, Label> > literals;		// in first-use order
		std::map<uint32_t, int> literalIndex;					// bits -> index into `literals`
};

// The x86-64 JIT backend, nameable on any host; `NativeJitCompiler` uses it on an x86-64 host.
class JitCompilerX64 : public JitCompiler {
	public:		virtual void compile(const AssembledProgram& program, JitModule& out);
	private:	static void lowerFunction(X64Emitter& emitter, const AssembledProgram& program, UInt funcStart
						, UInt funcEnd, const Offsets& offsets, const std::vector<Label>& entryLabels, Label epilogue);
};

} // namespace GAZL

#endif
