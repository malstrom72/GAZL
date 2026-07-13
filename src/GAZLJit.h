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
	GAZLJit is the native (arm64) baseline JIT for the GAZL VM. It grows in this one pair of files (docs/JitEmitterHandoff.md):

	  - `Emitter` (namespace `GAZL`): a tiny AArch64 machine-code assembler — one method per instruction, one canonical
	    encoding form per operation — with no dependency on the interpreter (it only produces bytes; the Emitter-only
	    diff test links without `GAZL.cpp`). Verified against a clang-assembled oracle.
	  - the v1 lowering pass + `JitEngine` + native dispatcher: compiles a function's finalized
	    `Instruction[]` to native code and runs it. `JitEngine` is a `Processor` subclass (§5.1) that overrides the virtual
	    `run()`/`enterCall()`, so it is a polymorphic drop-in for the interpreter — the host loop is identical
	    (`enterCall(); do { resetTimeOut(N); } while (run() == TIME_OUT)`). This half depends on `GAZL.h`. Only these two
	    virtual overrides stay inline (they are tiny and must not pull the vtable into GAZLJit.o); every heavy body —
	    `lowerFunction`, `emitDispatcher`, `makeExecutable`, the emit helpers — lives in GAZLJit.cpp.

	This is arm64 only; the x64 emitter and the v2 register allocator (§5.7) are later steps.
*/

#ifndef GAZLJit_h
#define GAZLJit_h

#include <cstdint>
#include <cstddef>
#include <vector>
#include "GAZL.h"
#include "GAZLJitMem.h"			// makeExecutable() — platform-specific backend, architecture-neutral

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
constexpr Reg X3 = static_cast<Reg>(3),   X4 = static_cast<Reg>(4),   X9 = static_cast<Reg>(9);
constexpr Reg X10 = static_cast<Reg>(10), X11 = static_cast<Reg>(11), X12 = static_cast<Reg>(12);
constexpr Reg X19 = static_cast<Reg>(19), X20 = static_cast<Reg>(20), X21 = static_cast<Reg>(21);
constexpr Reg X30 = static_cast<Reg>(30), XZR = static_cast<Reg>(31), SP = static_cast<Reg>(31);

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
		void sdiv(Reg wd, Reg wn, Reg wm);						/// `sdiv wd, wn, wm` (signed; INT_MIN/-1 → INT_MIN, no trap)
		void msub(Reg wd, Reg wn, Reg wm, Reg wa);				/// `msub wd, wn, wm, wa` = wa - wn*wm (for modulo)
		void lslv(Reg wd, Reg wn, Reg wm);						/// `lsl wd, wn, wm` (register count, masked mod 32)
		void lsrv(Reg wd, Reg wn, Reg wm);						/// `lsr wd, wn, wm`
		void asrv(Reg wd, Reg wn, Reg wm);						/// `asr wd, wn, wm`
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
		void ldrWxs(Reg wt, Reg xn, Reg wm);					/// `ldr wt, [xn, wm, sxtw #2]` (signed index)
		void strWxs(Reg wt, Reg xn, Reg wm);					/// `str wt, [xn, wm, sxtw #2]`
		void ldurW(Reg wt, Reg xn, int simm9);					/// `ldur wt, [xn, #simm9]` (signed byte offset, -256..255)
		void sturW(Reg wt, Reg xn, int simm9);					/// `stur wt, [xn, #simm9]`

		// --- doubleword loads / stores (64-bit; for pointer-sized context fields) ---
		void ldrX(Reg xt, Reg xn, uint32_t byteOffset);			/// `ldr xt, [xn, #byteOffset]` (offset scaled by 8)
		void strX(Reg xt, Reg xn, uint32_t byteOffset);			/// `str xt, [xn, #byteOffset]`
		void ldrXr(Reg xt, Reg xn, Reg wm);						/// `ldr xt, [xn, wm, uxtw #3]` (8-byte-scaled table index)
		void adr(Reg xd, Label target);							/// `adr xd, target` (PC-relative address, ±1 MiB)

		// --- 64-bit address arithmetic / test (for the pinned dsp / ipsp pointers) ---
		void addImmX(Reg xd, Reg xn, uint32_t imm12);			/// `add xd, xn, #imm12` (64-bit)
		void subImmX(Reg xd, Reg xn, uint32_t imm12);			/// `sub xd, xn, #imm12` (64-bit)
		void cmpX(Reg xn, Reg xm);								/// `cmp xn, xm` (64-bit, for pointer bounds)
		void addX(Reg xd, Reg xn, Reg xm);						/// `add xd, xn, xm` (64-bit register add)
		void cbnzX(Reg xt, Label target);						/// `cbnz xt, target` (64-bit)

		// --- float scalar (single precision) ---
		void faddS(Reg sd, Reg sn, Reg sm);						/// `fadd sd, sn, sm`
		void fmulS(Reg sd, Reg sn, Reg sm);						/// `fmul sd, sn, sm`
		void fsubS(Reg sd, Reg sn, Reg sm);						/// `fsub sd, sn, sm`
		void fdivS(Reg sd, Reg sn, Reg sm);						/// `fdiv sd, sn, sm`
		void fcmpS(Reg sn, Reg sm);								/// `fcmp sn, sm` (sets NZCV for a float compare)
		void fabsS(Reg sd, Reg sn);								/// `fabs sd, sn` (single-precision absolute value; ABSf)
		void frintmS(Reg sd, Reg sn);							/// `frintm sd, sn` (round toward -inf = floorf; FLOf)
		void fcvtzs(Reg wd, Reg sn);							/// `fcvtzs wd, sn` (FTOI, round toward zero, saturating)
		void scvtf(Reg sd, Reg wn);								/// `scvtf sd, wn` (ITOF)
		void fmovSW(Reg sd, Reg wn);							/// `fmov sd, wn` (bit-copy int→float reg; float const load)
		void ldrS(Reg st, Reg xn, uint32_t byteOffset);			/// `ldr st, [xn, #byteOffset]`
		void strS(Reg st, Reg xn, uint32_t byteOffset);			/// `str st, [xn, #byteOffset]`
		void ldurS(Reg st, Reg xn, int simm9);					/// `ldur st, [xn, #simm9]` (signed offset; float frame slot)
		void sturS(Reg st, Reg xn, int simm9);					/// `stur st, [xn, #simm9]`
		void ldrSxs(Reg st, Reg xn, Reg wm);					/// `ldr st, [xn, wm, sxtw #2]` (far float slot, signed index)
		void strSxs(Reg st, Reg xn, Reg wm);					/// `str st, [xn, wm, sxtw #2]`

		// --- branches ---
		void b(Label target);									/// `b target`
		void bcond(Cond cond, Label target);					/// `b.<cond> target`
		void cbz(Reg wt, Label target);							/// `cbz wt, target`
		void cbnz(Reg wt, Label target);						/// `cbnz wt, target`
		void ret(Reg xn = X30);									/// `ret {xn}`
		void br(Reg xn);										/// `br xn` (branch to register)
		void blr(Reg xn);										/// `blr xn` (branch-with-link to register)

		// --- labels / fixups ---
		Label newLabel();										/// allocate an unbound label
		void bind(Label label);									/// position `label` at the current emit point
		void finalize();										/// patch all branch displacements to bound labels

		// --- buffer access ---
		const uint32_t* code() const { return words.empty() ? nullptr : &words[0]; }
		size_t wordCount() const { return words.size(); }		/// number of 32-bit instruction words emitted

	private:
		enum FixupKind { FIXUP_IMM26, FIXUP_IMM19, FIXUP_ADR };	// `b`→imm26; `b.cond`/`cbz`/`cbnz`→imm19; `adr`→imm21
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

// ============================================================================================================
// JIT lowering, engine, and native dispatcher (reopens namespace GAZL). Depends on GAZL.h.
// ============================================================================================================

namespace GAZL {

const int TRANSFER = 1;							// segment-to-segment transfer sentinel (no GAZL status is +1)
const int NATIVE_CALL = 2;						// "invoke a native, then continue" sentinel
const int BLOCK_RETRY = 5;						// a native returns this to suspend-and-retry (host policy; §5.4 blocking retry)

// GAZL finalized opcodes (the enum is internal to GAZL.cpp; base = FIRST_OPCODE_VALUE 0x2345, declaration order).
enum {
	OP_FUNC = 0x2345 + 0, OP_CALL_VVC = 0x2345 + 1, OP_CALL_CVC = 0x2345 + 2, OP_CALL_NVC = 0x2345 + 3,
	OP_RETU = 0x2345 + 4, OP_MOVE_VV = 0x2345 + 5, OP_MOVE_VC = 0x2345 + 6, OP_PEEK_VC = 0x2345 + 7,
	OP_POKE_CV = 0x2345 + 8, OP_POKE_CC = 0x2345 + 9, OP_PEEK_VVV = 0x2345 + 10, OP_PEEK_VCV = 0x2345 + 11,
	OP_POKE_VVV = 0x2345 + 12, OP_POKE_CVV = 0x2345 + 13, OP_POKE_VVC = 0x2345 + 14, OP_POKE_CVC = 0x2345 + 15,
	OP_GETL_VVV = 0x2345 + 16, OP_SETL_VVV = 0x2345 + 17, OP_SETL_VVC = 0x2345 + 18, OP_ADRL = 0x2345 + 19,
	OP_COPY_VVC = 0x2345 + 63, OP_COPY_VCC = 0x2345 + 64, OP_COPY_CVC = 0x2345 + 65, OP_COPY_CCC = 0x2345 + 66,
	OP_ABSI = 0x2345 + 20,
	OP_ADDI_VVV = 0x2345 + 21, OP_ADDI_VVC = 0x2345 + 22,
	OP_SUBI_VVV = 0x2345 + 23, OP_SUBI_VVC = 0x2345 + 24, OP_SUBI_VCV = 0x2345 + 25,
	OP_MULI_VVV = 0x2345 + 26, OP_MULI_VVC = 0x2345 + 27,
	OP_DIVI_VVV = 0x2345 + 28, OP_DIVI_VVC = 0x2345 + 29, OP_DIVI_VCV = 0x2345 + 30,
	OP_MODI_VVV = 0x2345 + 31, OP_MODI_VVC = 0x2345 + 32, OP_MODI_VCV = 0x2345 + 33,
	OP_ANDI_VVV = 0x2345 + 34, OP_ANDI_VVC = 0x2345 + 35,
	OP_IORI_VVV = 0x2345 + 36, OP_IORI_VVC = 0x2345 + 37,
	OP_XORI_VVV = 0x2345 + 38, OP_XORI_VVC = 0x2345 + 39,
	OP_SHLI_VVV = 0x2345 + 40, OP_SHLI_VVC = 0x2345 + 41, OP_SHLI_VCV = 0x2345 + 42,
	OP_SHRI_VVV = 0x2345 + 43, OP_SHRI_VVC = 0x2345 + 44, OP_SHRI_VCV = 0x2345 + 45,
	OP_SHRU_VVV = 0x2345 + 46, OP_SHRU_VVC = 0x2345 + 47, OP_SHRU_VCV = 0x2345 + 48,
	OP_ABSF = 0x2345 + 49, OP_FLOF = 0x2345 + 50,
	OP_ADDF_VVV = 0x2345 + 51, OP_ADDF_VVC = 0x2345 + 52,
	OP_SUBF_VVV = 0x2345 + 53, OP_SUBF_VVC = 0x2345 + 54, OP_SUBF_VCV = 0x2345 + 55,
	OP_MULF_VVV = 0x2345 + 56, OP_MULF_VVC = 0x2345 + 57,
	OP_DIVF_VVV = 0x2345 + 58, OP_DIVF_VVC = 0x2345 + 59, OP_DIVF_VCV = 0x2345 + 60,
	OP_FTOI_VVC = 0x2345 + 61, OP_ITOF_VVC = 0x2345 + 62,
	OP_FORi_VVB = 0x2345 + 67, OP_FORi_VCB = 0x2345 + 68,
	OP_LSSI_VVB = 0x2345 + 69, OP_LSSI_VCB = 0x2345 + 70, OP_LSSI_CVB = 0x2345 + 71,
	OP_EQUI_VVB = 0x2345 + 72, OP_EQUI_VCB = 0x2345 + 73,
	OP_NLSI_VVB = 0x2345 + 74, OP_NLSI_VCB = 0x2345 + 75, OP_NLSI_CVB = 0x2345 + 76,
	OP_NEQI_VVB = 0x2345 + 77, OP_NEQI_VCB = 0x2345 + 78,
	OP_LSSF_VVB = 0x2345 + 79, OP_LSSF_VCB = 0x2345 + 80, OP_LSSF_CVB = 0x2345 + 81,
	OP_EQUF_VVB = 0x2345 + 82, OP_EQUF_VCB = 0x2345 + 83,
	OP_NLSF_VVB = 0x2345 + 84, OP_NLSF_VCB = 0x2345 + 85, OP_NLSF_CVB = 0x2345 + 86,
	OP_NEQF_VVB = 0x2345 + 87, OP_NEQF_VCB = 0x2345 + 88,
	OP_GOTO = 0x2345 + 89, OP_SWCH = 0x2345 + 90
};

// makeExecutable() is declared in GAZLJitMem.h (platform backend), also in namespace GAZL.

// Byte offsets of the machine state a segment/dispatcher touches, within the (subclass) engine.
struct Offsets {
	uint32_t dsp, mb, fuel, ipsp, resume, saveddsp, natives, nativefn, funcentries, memsize, rwmemsize, dsend, ipsend,
		nativeafter;
};

/*
	The JIT engine: a `Processor` subclass sharing the base machine state (§5.1). Being a subclass, it reaches the
	protected state (dsp/ip/ipsp/clockCyclesLeft/memoryBase/natives/sizes) with no edit to src/GAZL.*, and adds the
	RESUME continuation + a few dispatch scratch fields.
*/
class JitEngine : public Processor {
	public:		Value* savedDsp;					// dsp saved across a native call (the C1 window is transient)
				void* nativeFn;						// resolved native fn pointer, blr'd by the native dispatcher
				void* nativeAfter;					// after-call continuation (dispatcher sets RESUME to it on native OK)
				void** funcEntries;					// ordinal -> JIT-compiled GAZL-function entry (indirect calls)
				void* jitDispatch;					// the native dispatcher trampoline (compiled once with the code)

		JitEngine(UInt codeSize, const Instruction* code, UInt fnCount, const UInt* fnTable, UInt memSize, Value* mem
					, UInt globalsSize, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* nat)
			: Processor(codeSize, code, fnCount, fnTable, memSize, mem, globalsSize, constsSize, ipStackSize, ipStack
				, nat, 0), savedDsp(0), nativeFn(0), nativeAfter(0), funcEntries(0), jitDispatch(0) { }

		// Bind the compiled artifacts (dispatcher trampoline + ordinal→entry table) produced by lowerProgram().
		void setCompiled(void* dispatchAddr, void** entries) { jitDispatch = dispatchAddr; funcEntries = entries; }

		// The memory image (globals/consts/data) — the lowering reads finalize-time constant tables from it (e.g. SWCH).
		const Value* memoryImage() const { return memoryBase; }

		Offsets offsets() const;			// gather the byte offsets of the machine-state fields (setup-time, see .cpp)

		// Polymorphic drop-in for the base Processor (§5.1). enterCall sets up the call stack (base logic) and seeds the
		// RESUME continuation with the callee's compiled entry; run() is one trip through the native dispatcher (mid-run
		// GAZL/native calls stay inside it), returning to the host only to suspend (TIME_OUT / BLOCK_RETRY) or finish.
		// The host loop is identical to the interpreter's: enterCall(); do { resetTimeOut(N); } while (run()==TIME_OUT).
		virtual Status enterCall(Pointer functionPointer) {
			const Status s = Processor::enterCall(functionPointer);
			if (s != OK) { return s; }
			resume = funcEntries[functionPointer - IP_OFFSET];
			return OK;
		}
		virtual Status run() {
			typedef int (*Disp)(JitEngine*);
			return static_cast<Status>(reinterpret_cast<Disp>(jitDispatch)(this));
		}
	private:	template<class T> uint32_t off(T* p) const {
					return static_cast<uint32_t>(reinterpret_cast<const char*>(p) - reinterpret_cast<const char*>(this));
				}
};

// --- lowering + dispatcher (heavy bodies live in GAZLJit.cpp) ---

/*
	Lower one function at `funcIndex` into `e` (appended). Emits an entry reload + FUNC prologue, a mainline per block,
	cold reload trampolines + §5.7.5 suspend stubs for loop heads, and the §5.4 call/return transfers. `entryLabels[ord]`
	are pre-created (for direct calls); `entryOffset[selfOrdinal]` is set to this function's native word offset. Returns
	false on an unsupported opcode.
*/
bool lowerFunction(Emitter& e, const Instruction* code, const Value* memory, UInt funcIndex, const Offsets& o,
		std::vector<Label>& entryLabels, std::vector<size_t>& entryOffset, UInt selfOrdinal, UInt functionCount);

// Emit the native dispatcher trampoline (§5.4 encoding (a)). `int dispatch(JitEngine* ctx)`: park CTX in a callee-saved
// reg, jump to RESUME, loop on TRANSFER (GAZL call/return — no host round-trip), make the one host call on NATIVE_CALL,
// and return to the host only to suspend (TIME_OUT) or finish. Returns the trampoline's word offset in the buffer.
size_t emitDispatcher(Emitter& e, const Offsets& o);

// Whole-program driver (GAZLJitCompile.cpp): lower every function, emit the dispatcher, publish an executable page, and
// bind it to `engine` via setCompiled(). Returns false if any function hits an opcode the backend can't lower yet (the
// caller should then fall back to the interpreter). arm64 only. If `outCodeWords` is non-null it receives the number of
// emitted 32-bit machine words on success (for --jit-stats: code_bytes = words * 4).
bool compile(JitEngine& engine, const Instruction* code, const UInt* functionTable, UInt functionCount,
		size_t* outCodeWords = 0);

} // namespace GAZL

#endif
