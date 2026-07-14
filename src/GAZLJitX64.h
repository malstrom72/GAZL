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
#include <cstddef>

namespace GAZL {

/*
	x86-64 general-purpose register, by 4-bit hardware encoding. The 32-bit view (`eax`..`r15d`) and the 64-bit view
	(`rax`..`r15`) share the encoding; the emitter method (or a REX.W bit) picks the width. Numbers 8..15 (`r8`..`r15`)
	need a REX prefix. `RSP`/`R12` as a memory base force a SIB byte; `RBP`/`R13` force a displacement — the memory
	operand encoder handles both. SysV AMD64 (Darwin/Linux): int args RDI,RSI,RDX,RCX,R8,R9; return RAX; callee-saved
	RBX,RBP,R12-R15.
*/
enum Reg {
	RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
	R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

/*
	x86 condition code (the low nibble of the Jcc / SETcc opcode). Signed compares use L/GE/LE/G; equality E/NE; unsigned
	B/AE/BE/A. Chosen to mirror the interpreter's signed integer branches.
*/
enum Cond {
	CC_E = 0x4, CC_NE = 0x5, CC_L = 0xC, CC_GE = 0xD, CC_LE = 0xE, CC_G = 0xF,
	CC_B = 0x2, CC_AE = 0x3, CC_BE = 0x6, CC_A = 0x7, CC_P = 0xA, CC_NP = 0xB	// P/NP = parity (unordered) for float compares
};

// A branch target; `id` indexes the emitter's label table (-1 = invalid).
struct Label {
	int id;
	Label() : id(-1) {}
};

/*
	Appends x86-64 machine code into a byte buffer. Unlike the fixed-width AArch64 emitter, x64 instructions are
	variable length (REX? + opcode + ModRM + SIB? + disp? + imm?), so this works in bytes. Register-register forms use
	the two-operand ISA (`dst OP= src`). Branches to `Label`s emit a rel32 placeholder and are patched by `finalize()`
	after every `bind()`. 32-bit ops (default) match the interpreter's Int slots; `*Q` variants add REX.W for the
	pointer-sized pinned registers (dsp, memory base). See GAZLJitArm64.h for the sibling backend.
*/
class X64Emitter {
	public:
		X64Emitter() {}

		// --- moves / constants ---
		void movImm(Reg rd, uint32_t imm);						// `mov rd(32), imm32` (zero-extends to 64)
		void movImm64(Reg rd, uint64_t imm);					// `mov rd(64), imm64` (REX.W B8+rd io)
		void mov(Reg rd, Reg rs);								// `mov rd, rs` (32-bit)
		void movQ(Reg rd, Reg rs);								// `mov rd, rs` (64-bit)

		// --- integer arithmetic / logic (32-bit unless noted) ---
		void add(Reg rd, Reg rs);								// `add rd, rs`
		void sub(Reg rd, Reg rs);								// `sub rd, rs`
		void imul(Reg rd, Reg rs);								// `imul rd, rs` (0F AF /r)
		void and_(Reg rd, Reg rs);								// `and rd, rs`
		void or_(Reg rd, Reg rs);								// `or rd, rs`
		void xor_(Reg rd, Reg rs);								// `xor rd, rs`
		void cmp(Reg ra, Reg rb);								// `cmp ra, rb` (sets flags)
		void addImm(Reg rd, uint32_t imm);						// `add rd, imm32` (81 /0)
		void subImm(Reg rd, uint32_t imm);						// `sub rd, imm32` (81 /5)
		void cmpImm(Reg ra, uint32_t imm);						// `cmp ra, imm32` (81 /7)
		void addQ(Reg rd, Reg rs);								// `add rd, rs` (64-bit; pointer arithmetic)
		void addImmQ(Reg rd, uint32_t imm);						// `add rd, imm32` (64-bit; advances the dsp pointer)
		void neg(Reg rd);										// `neg rd` (F7 /3)
		void cdq();												// `cdq` (99): sign-extend eax into edx before idiv
		void idiv(Reg rs);										// `idiv rs` (F7 /7): edx:eax / rs -> eax quot, edx rem
		void shlCl(Reg rd);										// `shl rd, cl`
		void shrCl(Reg rd);										// `shr rd, cl` (logical)
		void sarCl(Reg rd);										// `sar rd, cl` (arithmetic)
		void shlImm(Reg rd, uint8_t n);							// `shl rd, imm8`
		void shrImm(Reg rd, uint8_t n);							// `shr rd, imm8`
		void sarImm(Reg rd, uint8_t n);							// `sar rd, imm8`

		// --- loads / stores off a base register + displacement ---
		void load(Reg rd, Reg base, int32_t disp);				// `mov rd(32), [base + disp]`
		void store(Reg base, int32_t disp, Reg rs);				// `mov [base + disp], rs(32)`
		void loadQ(Reg rd, Reg base, int32_t disp);				// `mov rd(64), [base + disp]`
		void storeQ(Reg base, int32_t disp, Reg rs);			// `mov [base + disp], rs(64)`
		void loadIdx(Reg rd, Reg base, Reg index, int32_t disp);	// `mov rd(32), [base + index*4 + disp]` (SIB, scale 4)
		void storeIdx(Reg base, Reg index, int32_t disp, Reg rs);	// `mov [base + index*4 + disp], rs(32)`
		void subQ(Reg rd, Reg rs);								// `sub rd, rs` (64-bit; pointer difference for ADRL)

		// --- SSE scalar single-precision floats (XMM operands reuse the Reg 0..15 encodings, a separate file from GP) ---
		void movssLoad(Reg xd, Reg base, int32_t disp);			// `movss xd, [base + disp]` (F3 0F 10)
		void movssStore(Reg base, int32_t disp, Reg xs);		// `movss [base + disp], xs` (F3 0F 11)
		void addss(Reg xd, Reg xs);								// `addss xd, xs`
		void subss(Reg xd, Reg xs);								// `subss xd, xs`
		void mulss(Reg xd, Reg xs);								// `mulss xd, xs`
		void divss(Reg xd, Reg xs);								// `divss xd, xs`
		void ucomiss(Reg xa, Reg xb);							// `ucomiss xa, xb` (sets EFLAGS for a float compare)
		void cvttss2si(Reg rd, Reg xs);							// `cvttss2si rd, xs` (float -> int, truncate; FTOI)
		void cvtsi2ss(Reg xd, Reg rs);							// `cvtsi2ss xd, rs` (int -> float; ITOF)
		void movdToXmm(Reg xd, Reg rs);							// `movd xd, rs` (bit-copy int -> xmm; float const load)
		void roundss(Reg xd, Reg xs, uint8_t mode);				// `roundss xd, xs, imm8` (SSE4.1; mode 1 = floor; FLOF)

		// --- stack / control ---
		void push(Reg r);										// `push r` (50+rd, REX.B for r8-15)
		void pop(Reg r);										// `pop r`  (58+rd)
		void nop();												// `nop` (0x90)
		void cld();												// `cld` (FC): clear direction flag (forward string ops)
		void repMovsd();										// `rep movsd` (F3 A5): copy ecx dwords [rsi]->[rdi] (COPY)
		void ret();												// `ret` (C3)
		void jmp(Label target);									// `jmp rel32` (E9)
		void jcc(Cond cc, Label target);						// `j<cc> rel32` (0F 8x)
		void callRel(Label target);								// `call rel32` (E8) — GAZL->GAZL direct call
		void callReg(Reg r);									// `call r` (FF /2) — indirect / native via a materialized pointer

		// --- labels / fixups ---
		Label newLabel();										// allocate an unbound label
		void bind(Label label);									// position `label` at the current emit point
		void finalize();										// patch all rel32 branch displacements to bound labels

		// --- buffer access ---
		const uint8_t* code() const { return bytes.empty() ? 0 : &bytes[0]; }
		size_t size() const { return bytes.size(); }			// emitted byte count

	private:
		struct Fixup {
			size_t site;										// byte offset of the rel32 field to patch
			int labelId;
		};

		void b(uint8_t v) { bytes.push_back(v); }
		void d32(uint32_t v);									// little-endian 32-bit
		void d64(uint64_t v);
		void rex(bool w, int reg, int base);					// emit REX iff needed (W, or reg/base extended)
		void modrmReg(int reg, int rm);							// mod=11 register-direct
		void memOperand(int reg, Reg base, int32_t disp);		// ModRM (+SIB +disp) for [base + disp]
		void memOperandIdx(int reg, Reg base, Reg index, int32_t disp);	// ModRM+SIB for [base + index*4 + disp]
		void rex3(bool w, int reg, int index, int base);		// REX with an index field (REX.X) too
		void sseRR(uint8_t prefix, uint8_t opcode, int reg, int rm);	// [prefix] REX? 0F opcode  register-direct SSE form
		void aluImm(int ext, Reg rd, uint32_t imm);				// 81 /ext id  (ext selects add/sub/cmp/...)
		void relBranch(int labelId);							// emit a rel32 placeholder + record a fixup

		std::vector<uint8_t> bytes;
		std::vector<ptrdiff_t> labelTargets;					// per-label bound byte offset, or -1 while unbound
		std::vector<Fixup> fixups;
};

} // namespace GAZL

#endif
