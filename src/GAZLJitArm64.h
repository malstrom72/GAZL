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
	The arm64 JIT backend: an AArch64 encoder (`Arm64Emitter`, one method per instruction, one encoding form per
	operation) and `JitCompilerArm64`, whose lowering pass lives in GAZLJitArm64.cpp. The arch-neutral JIT API is in
	GAZLJit.h. See design/jit/JitEmitterHandoff.md.
*/

#ifndef GAZLJitArm64_h
#define GAZLJitArm64_h

#include <stdint.h>
#include <cstddef>
#include <vector>
#include "GAZL.h"
#include "GAZLJit.h"

namespace GAZL {

/*
	AArch64 register number; `X<n>` and `S<n>` alias `W<n>` and the emitter method picks the width. Number 31 is
	`WZR`/`XZR` in data-processing slots but `SP` in a base-register slot.
*/
enum Reg {
	W0 = 0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15,
	W16, W17, W18, W19, W20, W21, W22, W23, W24, W25, W26, W27, W28, W29, W30, WZR = 31
};

const Reg X0 = static_cast<Reg>(0),   X1 = static_cast<Reg>(1),   X2 = static_cast<Reg>(2);
const Reg X3 = static_cast<Reg>(3),   X4 = static_cast<Reg>(4),   X9 = static_cast<Reg>(9);
const Reg X10 = static_cast<Reg>(10), X11 = static_cast<Reg>(11), X12 = static_cast<Reg>(12);
const Reg X19 = static_cast<Reg>(19), X20 = static_cast<Reg>(20), X21 = static_cast<Reg>(21);
const Reg X30 = static_cast<Reg>(30), XZR = static_cast<Reg>(31), SP = static_cast<Reg>(31);

const Reg S0 = static_cast<Reg>(0), S1 = static_cast<Reg>(1), S2 = static_cast<Reg>(2), S3 = static_cast<Reg>(3);
const Reg S22 = static_cast<Reg>(22);															// lets the emitter oracle test register fields above 15

enum Cond {																						// AArch64 condition codes (the 4-bit field of `b.cond`)
	EQ = 0, NE = 1, HS = 2, LO = 3, MI = 4, PL = 5, VS = 6, VC = 7,
	HI = 8, LS = 9, GE = 10, LT = 11, GT = 12, LE = 13, AL = 14
};

struct Label {																					// branch target from `Arm64Emitter::newLabel()`
	int id;
};

/*
	Appends one AArch64 instruction word per method call. Call `finalize()` once, after every `bind()`, to patch branch
	displacements before reading `code()`.
*/
class Arm64Emitter {
	public:
		Arm64Emitter() {}

		// --- moves / constant materialization ---
		void movz(Reg wd, uint16_t imm16, unsigned shift = 0);	// `movz wd, #imm16 [, lsl #shift]` (shift 0 or 16)
		void movk(Reg wd, uint16_t imm16, unsigned shift = 0);	// `movk wd, #imm16 [, lsl #shift]`
		void movn(Reg wd, uint16_t imm16, unsigned shift = 0);	// `movn wd, #imm16 [, lsl #shift]`
		void mov(Reg wd, Reg wm);								// `mov wd, wm` (register move; `orr wd, wzr, wm`)
		void movImm32(Reg wd, uint32_t imm);					// materialize a full 32-bit constant via `movz`+`movk`

		// --- integer arithmetic / logic (32-bit) ---
		void add(Reg wd, Reg wn, Reg wm);						// `add wd, wn, wm`
		void addImm(Reg wd, Reg wn, uint32_t imm12);			// `add wd, wn, #imm12`
		void sub(Reg wd, Reg wn, Reg wm);						// `sub wd, wn, wm`
		void subImm(Reg wd, Reg wn, uint32_t imm12);			// `sub wd, wn, #imm12`
		void subs(Reg wd, Reg wn, Reg wm);						// `subs wd, wn, wm` (sets flags)
		void subsImm(Reg wd, Reg wn, uint32_t imm12);			// `subs wd, wn, #imm12` (sets flags)
		void cmp(Reg wn, Reg wm);								// `cmp wn, wm` (`subs wzr, wn, wm`)
		void cmpImm(Reg wn, uint32_t imm12);					// `cmp wn, #imm12` (`subs wzr, wn, #imm12`)
		void mul(Reg wd, Reg wn, Reg wm);						// `mul wd, wn, wm` (`madd wd, wn, wm, wzr`)
		void sdiv(Reg wd, Reg wn, Reg wm);						// `sdiv wd, wn, wm` (INT_MIN / -1 gives INT_MIN, no trap)
		void msub(Reg wd, Reg wn, Reg wm, Reg wa);				// `msub wd, wn, wm, wa` = wa - wn*wm (for modulo)
		void lslv(Reg wd, Reg wn, Reg wm);						// `lsl wd, wn, wm` (register count, masked mod 32)
		void lsrv(Reg wd, Reg wn, Reg wm);						// `lsr wd, wn, wm`
		void asrv(Reg wd, Reg wn, Reg wm);						// `asr wd, wn, wm`
		void and_(Reg wd, Reg wn, Reg wm);						// `and wd, wn, wm`
		void orr(Reg wd, Reg wn, Reg wm);						// `orr wd, wn, wm`
		void eor(Reg wd, Reg wn, Reg wm);						// `eor wd, wn, wm`
		void lslImm(Reg wd, Reg wn, unsigned shift);			// `lsl wd, wn, #shift`
		void lsrImm(Reg wd, Reg wn, unsigned shift);			// `lsr wd, wn, #shift`
		void asrImm(Reg wd, Reg wn, unsigned shift);			// `asr wd, wn, #shift`

		// --- word loads / stores (32-bit) ---
		void ldrW(Reg wt, Reg xn, uint32_t byteOffset);			// `ldr wt, [xn, #byteOffset]` (offset scaled by 4)
		void strW(Reg wt, Reg xn, uint32_t byteOffset);			// `str wt, [xn, #byteOffset]`
		void ldrWx(Reg wt, Reg xn, Reg wm);						// `ldr wt, [xn, wm, uxtw #2]`
		void strWx(Reg wt, Reg xn, Reg wm);						// `str wt, [xn, wm, uxtw #2]`
		void ldrWxs(Reg wt, Reg xn, Reg wm);					// `ldr wt, [xn, wm, sxtw #2]` (signed index)
		void strWxs(Reg wt, Reg xn, Reg wm);					// `str wt, [xn, wm, sxtw #2]`
		void ldurW(Reg wt, Reg xn, int simm9);					// `ldur wt, [xn, #simm9]` (signed byte offset, -256..255)
		void sturW(Reg wt, Reg xn, int simm9);					// `stur wt, [xn, #simm9]`

		// --- doubleword loads / stores (64-bit; for pointer-sized context fields) ---
		void ldrX(Reg xt, Reg xn, uint32_t byteOffset);			// `ldr xt, [xn, #byteOffset]` (offset scaled by 8)
		void strX(Reg xt, Reg xn, uint32_t byteOffset);			// `str xt, [xn, #byteOffset]`
		void ldrXr(Reg xt, Reg xn, Reg wm);						// `ldr xt, [xn, wm, uxtw #3]` (8-byte-scaled table index)
		void adr(Reg xd, Label target);							// `adr xd, target` (PC-relative, within 1 MiB)

		// --- 64-bit address arithmetic / test (for the pinned dsp / ipsp pointers) ---
		void addImmX(Reg xd, Reg xn, uint32_t imm12);			// `add xd, xn, #imm12` (64-bit)
		void subImmX(Reg xd, Reg xn, uint32_t imm12);			// `sub xd, xn, #imm12` (64-bit)
		void cmpX(Reg xn, Reg xm);								// `cmp xn, xm` (64-bit, for pointer bounds)
		void addX(Reg xd, Reg xn, Reg xm);						// `add xd, xn, xm` (64-bit register add)
		void subX(Reg xd, Reg xn, Reg xm);						// `sub xd, xn, xm` (64-bit register sub)
		void cbnzX(Reg xt, Label target);						// `cbnz xt, target` (64-bit)

		// --- float scalar (single precision) ---
		void faddS(Reg sd, Reg sn, Reg sm);						// `fadd sd, sn, sm`
		void fmulS(Reg sd, Reg sn, Reg sm);						// `fmul sd, sn, sm`
		void fsubS(Reg sd, Reg sn, Reg sm);						// `fsub sd, sn, sm`
		void fdivS(Reg sd, Reg sn, Reg sm);						// `fdiv sd, sn, sm`
		void fcmpS(Reg sn, Reg sm);								// `fcmp sn, sm` (sets NZCV for a float compare)
		void fabsS(Reg sd, Reg sn);								// `fabs sd, sn`
		void frintmS(Reg sd, Reg sn);							// `frintm sd, sn` (round toward -inf, i.e. floorf)
		void fcvtzs(Reg wd, Reg sn);							// `fcvtzs wd, sn` (round toward zero, saturating)
		void scvtf(Reg sd, Reg wn);								// `scvtf sd, wn`
		void fmovSW(Reg sd, Reg wn);							// `fmov sd, wn` (bit copy)
		void fmovWS(Reg wd, Reg sn);							// `fmov wd, sn` (bit copy)
		void ldrS(Reg st, Reg xn, uint32_t byteOffset);			// `ldr st, [xn, #byteOffset]`
		void strS(Reg st, Reg xn, uint32_t byteOffset);			// `str st, [xn, #byteOffset]`
		void ldurS(Reg st, Reg xn, int simm9);					// `ldur st, [xn, #simm9]` (signed offset; float frame slot)
		void sturS(Reg st, Reg xn, int simm9);					// `stur st, [xn, #simm9]`
		void ldrSxs(Reg st, Reg xn, Reg wm);					// `ldr st, [xn, wm, sxtw #2]` (far float slot, signed index)
		void strSxs(Reg st, Reg xn, Reg wm);					// `str st, [xn, wm, sxtw #2]`

		// --- branches ---
		void b(Label target);									// `b target`
		void bcond(Cond cond, Label target);					// `b.<cond> target`
		void cbz(Reg wt, Label target);							// `cbz wt, target`
		void cbnz(Reg wt, Label target);						// `cbnz wt, target`
		void ret(Reg xn = X30);									// `ret {xn}`
		void br(Reg xn);										// `br xn` (branch to register)
		void blr(Reg xn);										// `blr xn` (branch-with-link to register)

		// --- labels / fixups ---
		Label newLabel();										// allocate an unbound label
		void bind(Label label);									// position `label` at the current emit point
		void finalize();										// patch all branch displacements to bound labels

		// --- buffer access ---
		const uint32_t* code() const { return words.empty() ? 0 : &words[0]; }
		size_t wordCount() const { return words.size(); }		// number of 32-bit instruction words emitted
		ptrdiff_t labelOffset(Label l) const { return labelTargets[l.id]; }	// bound word index (after bind); for entry tables

	private:
		enum FixupKind { FIXUP_IMM26, FIXUP_IMM19, FIXUP_ADR };	// imm26 for `b`; imm19 for `b.cond`, `cbz`, `cbnz`; imm21 for `adr`
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

/*
	The arm64 JIT backend, created by `NativeJitCompiler` on an arm64 host. Declared here so codegen tests can name it.
*/
class JitCompilerArm64 : public JitCompiler {
	public:		virtual void compile(const AssembledProgram& program, JitModule& out);
	private:	static void lowerFunction(Arm64Emitter& e, const AssembledProgram& program, UInt funcIndex, UInt funcEnd
						, const Offsets& o, std::vector<Label>& entryLabels, UInt selfOrdinal, Label exitLabel);
};

} // namespace GAZL

#endif
