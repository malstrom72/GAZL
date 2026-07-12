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

/*
	GAZLJit is the native (arm64) baseline JIT for the GAZL VM. This first module is the `Emitter`: a tiny AArch64
	machine-code assembler that appends one 32-bit instruction word per call into a growable buffer. It has no dependency
	on the interpreter (`GAZL.h`/`GAZL.cpp`) — it only produces bytes. The dispatcher, register allocator and the rest of
	the JIT (see docs/JitCompilerResearch.md) grow into this same pair of files later.

	The design is deliberately minimal (see docs/JitEmitterHandoff.md): one canonical encoding form per operation (imm12
	/ register forms; 32-bit constants materialized with `movz`+`movk`), so the encoder stays uniform and easy to verify
	against a clang-assembled oracle. Only the instruction subset the first kernel needs is covered.

	This is arm64 only; the x64 emitter is a later step.
*/

#ifndef GAZLJit_h
#define GAZLJit_h

#include <cstdint>
#include <cstddef>
#include <vector>

namespace GAZL {

/*
	AArch64 register operand. The integer file (`Wn`/`Xn`) and the FP/SIMD scalar file (`Sn`) share the same 5-bit
	encoding slot, so `X<n>` and `S<n>` are just readable aliases for the same number as `W<n>`; the emitter method
	picks the width. Number 31 means `WZR`/`XZR` in data-processing slots and `SP` in a base-register slot (never used
	here). See docs/JitEmitterHandoff.md.
*/
enum Reg {
	W0 = 0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15,
	W16, W17, W18, W19, W20, W21, W22, W23, W24, W25, W26, W27, W28, W29, W30, WZR = 31
};

constexpr Reg X0 = static_cast<Reg>(0),   X1 = static_cast<Reg>(1),   X2 = static_cast<Reg>(2);
constexpr Reg X3 = static_cast<Reg>(3),   X4 = static_cast<Reg>(4),   X19 = static_cast<Reg>(19);
constexpr Reg X20 = static_cast<Reg>(20), X21 = static_cast<Reg>(21), X30 = static_cast<Reg>(30);
constexpr Reg XZR = static_cast<Reg>(31), SP = static_cast<Reg>(31);

constexpr Reg S0 = static_cast<Reg>(0), S1 = static_cast<Reg>(1), S2 = static_cast<Reg>(2), S3 = static_cast<Reg>(3);

/*
	AArch64 condition codes (the 4-bit field of `B.cond`). Only the ones the first kernel and its bounds/fuel checks use
	are named here.
*/
enum Cond {
	EQ = 0, NE = 1, HS = 2, LO = 3, MI = 4, PL = 5, VS = 6, VC = 7,
	HI = 8, LS = 9, GE = 10, LT = 11, GT = 12, LE = 13, AL = 14
};

/*
	A branch target. Created with `Emitter::newLabel()`, positioned with `Emitter::bind()`, and referenced by branch
	methods. Forward references are recorded as fixups while emitting and patched by `Emitter::finalize()`.
*/
struct Label {
	int id;
};

/*
	Appends AArch64 machine code, one instruction word per method call, into a `uint32_t` buffer. Register + condition
	operands select fields that are OR-ed into a per-op base opcode. Labels and a forward-reference fixup pass provide
	PC-relative branches; call `finalize()` once (after every `bind()`) to patch branch displacements before reading the
	`code()` bytes.
*/
class Emitter {
	public:
		Emitter() {}

		// --- moves / constant materialization ---
		void movz(Reg wd, uint16_t imm16, unsigned shift = 0);	/// `movz wd, #imm16 [, lsl #shift]` (shift ∈ {0,16})
		void movk(Reg wd, uint16_t imm16, unsigned shift = 0);	/// `movk wd, #imm16 [, lsl #shift]`
		void movn(Reg wd, uint16_t imm16, unsigned shift = 0);	/// `movn wd, #imm16 [, lsl #shift]`
		void mov(Reg wd, Reg wm);								/// `mov wd, wm` (register move; `orr wd, wzr, wm`)
		void movImm32(Reg wd, uint32_t imm);					/// materialize a full 32-bit constant via `movz`+`movk`

		// --- integer arithmetic / logic (32-bit) ---
		void add(Reg wd, Reg wn, Reg wm);						/// `add wd, wn, wm`
		void addImm(Reg wd, Reg wn, uint32_t imm12);			/// `add wd, wn, #imm12`
		void sub(Reg wd, Reg wn, Reg wm);						/// `sub wd, wn, wm`
		void subImm(Reg wd, Reg wn, uint32_t imm12);			/// `sub wd, wn, #imm12`
		void subs(Reg wd, Reg wn, Reg wm);						/// `subs wd, wn, wm` (sets flags)
		void subsImm(Reg wd, Reg wn, uint32_t imm12);			/// `subs wd, wn, #imm12` (sets flags)
		void cmp(Reg wn, Reg wm);								/// `cmp wn, wm` (`subs wzr, wn, wm`)
		void cmpImm(Reg wn, uint32_t imm12);					/// `cmp wn, #imm12` (`subs wzr, wn, #imm12`)
		void mul(Reg wd, Reg wn, Reg wm);						/// `mul wd, wn, wm` (`madd wd, wn, wm, wzr`)
		void and_(Reg wd, Reg wn, Reg wm);						/// `and wd, wn, wm`
		void orr(Reg wd, Reg wn, Reg wm);						/// `orr wd, wn, wm`
		void eor(Reg wd, Reg wn, Reg wm);						/// `eor wd, wn, wm`
		void lslImm(Reg wd, Reg wn, unsigned shift);			/// `lsl wd, wn, #shift`
		void lsrImm(Reg wd, Reg wn, unsigned shift);			/// `lsr wd, wn, #shift`
		void asrImm(Reg wd, Reg wn, unsigned shift);			/// `asr wd, wn, #shift`

		// --- word loads / stores (32-bit) ---
		void ldrW(Reg wt, Reg xn, uint32_t byteOffset);			/// `ldr wt, [xn, #byteOffset]` (offset scaled by 4)
		void strW(Reg wt, Reg xn, uint32_t byteOffset);			/// `str wt, [xn, #byteOffset]`
		void ldrWx(Reg wt, Reg xn, Reg wm);						/// `ldr wt, [xn, wm, uxtw #2]`
		void strWx(Reg wt, Reg xn, Reg wm);						/// `str wt, [xn, wm, uxtw #2]`

		// --- float scalar (single precision) ---
		void faddS(Reg sd, Reg sn, Reg sm);						/// `fadd sd, sn, sm`
		void fmulS(Reg sd, Reg sn, Reg sm);						/// `fmul sd, sn, sm`
		void fsubS(Reg sd, Reg sn, Reg sm);						/// `fsub sd, sn, sm`
		void fcvtzs(Reg wd, Reg sn);							/// `fcvtzs wd, sn` (FTOI, round toward zero)
		void scvtf(Reg sd, Reg wn);								/// `scvtf sd, wn` (ITOF)
		void ldrS(Reg st, Reg xn, uint32_t byteOffset);			/// `ldr st, [xn, #byteOffset]`
		void strS(Reg st, Reg xn, uint32_t byteOffset);			/// `str st, [xn, #byteOffset]`

		// --- branches ---
		void b(Label target);									/// `b target`
		void bcond(Cond cond, Label target);					/// `b.<cond> target`
		void cbz(Reg wt, Label target);							/// `cbz wt, target`
		void cbnz(Reg wt, Label target);						/// `cbnz wt, target`
		void ret(Reg xn = X30);									/// `ret {xn}`

		// --- labels / fixups ---
		Label newLabel();										/// allocate an unbound label
		void bind(Label label);									/// position `label` at the current emit point
		void finalize();										/// patch all branch displacements to bound labels

		// --- buffer access ---
		const uint32_t* code() const { return words.empty() ? nullptr : &words[0]; }
		size_t wordCount() const { return words.size(); }		/// number of 32-bit instruction words emitted

	private:
		enum FixupKind { FIXUP_IMM26, FIXUP_IMM19 };			// `b` uses imm26; `b.cond`/`cbz`/`cbnz` use imm19
		struct Fixup {
			size_t site;										// index (in words) of the branch instruction to patch
			int labelId;										// which label it targets
			FixupKind kind;
		};

		void emit(uint32_t word) { words.push_back(word); }
		void branch(uint32_t base, int labelId, FixupKind kind);

		std::vector<uint32_t> words;
		std::vector<ptrdiff_t> labelTargets;					// per-label bound word index, or -1 while unbound
		std::vector<Fixup> fixups;
};

} // namespace GAZL

#endif
