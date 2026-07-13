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
	The consolidated GAZL -> arm64 v1 lowering pass, JIT engine, and native dispatcher — one copy, shared by the test
	harnesses (previously duplicated across the lower/resume/call tests). It compiles a function's finalized
	`Instruction[]` to native code that a `JitEngine` (a `Processor` subclass — §5.1) runs over the shared VM state,
	byte-identical to the interpreter. src/GAZL.* stays READ-ONLY; only the Emitter (src/GAZLJit.*) is depended on for
	encoding. AArch64 only.

	What it covers (docs/JitCompilerResearch.md §5.2–5.8): v1 memory-resident locals (operands are signed Value-indices
	off a pinned dsp, ldur/stur); per-loop fuel-check safepoints with §5.7.5 RESUME continuations; the §5.4 dispatcher /
	segment model for calls (direct CALL_CVC, indirect CALL_VVC, native CALL_NVC) via an ipStack of native return
	continuations; and bounds-checked indexed memory (PEEK_VCV/POKE_CVV) that traps as a Status. The dispatcher is a
	native trampoline (encoding (a)): mid-run, GAZL calls/returns and native calls stay in JIT-land; control returns to
	the host only to suspend (TIME_OUT) or finish.

	Register roles inside a segment: x0=ctx, x1=dsp, x2=membase, w3=fuel, x4=ipsp; scratch x9..x12.
*/

#ifndef GAZLJitLower_h
#define GAZLJitLower_h

#include "GAZLJit.h"
#include "GAZL.h"

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <map>
#include <set>
#include <sys/mman.h>

#if defined(__APPLE__)
	#include <pthread.h>
	#include <libkern/OSCacheControl.h>
#endif

namespace GAZLJitLower {

using namespace GAZL;

const int TRANSFER = 1;							// segment-to-segment transfer sentinel (no GAZL status is +1)
const int NATIVE_CALL = 2;						// "invoke a native, then continue" sentinel
const int BLOCK_RETRY = 5;						// a native returns this to suspend-and-retry (host policy; §5.4 blocking retry)

// GAZL finalized opcodes (the enum is internal to GAZL.cpp; base = FIRST_OPCODE_VALUE 0x2345, declaration order).
enum {
	OP_FUNC = 0x2345 + 0, OP_CALL_VVC = 0x2345 + 1, OP_CALL_CVC = 0x2345 + 2, OP_CALL_NVC = 0x2345 + 3,
	OP_RETU = 0x2345 + 4, OP_MOVE_VV = 0x2345 + 5, OP_MOVE_VC = 0x2345 + 6, OP_PEEK_VC = 0x2345 + 7,
	OP_POKE_CV = 0x2345 + 8, OP_PEEK_VVV = 0x2345 + 10, OP_PEEK_VCV = 0x2345 + 11,
	OP_POKE_VVV = 0x2345 + 12, OP_POKE_CVV = 0x2345 + 13,
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
	OP_GOTO = 0x2345 + 89
};

// --- W^X executable memory (spike A1 rung-1 strategy; see docs/JitSpikeA1-Results.md) ---

inline void* makeExecutable(const uint32_t* words, size_t wordCount) {
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
	public:		void* resume;						// §5.7.5 native continuation: where the next dispatch jumps
				Value* savedDsp;					// dsp saved across a native call (the C1 window is transient)
				void* nativeFn;						// resolved native fn pointer, blr'd by the native dispatcher
				void* nativeAfter;					// after-call continuation (dispatcher sets RESUME to it on native OK)
				void** funcEntries;					// ordinal -> JIT-compiled GAZL-function entry (indirect calls)

		JitEngine(UInt codeSize, const Instruction* code, UInt fnCount, const UInt* fnTable, UInt memSize, Value* mem
					, UInt globalsSize, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* nat)
			: Processor(codeSize, code, fnCount, fnTable, memSize, mem, globalsSize, constsSize, ipStackSize, ipStack
				, nat, 0), resume(0), savedDsp(0), nativeFn(0), nativeAfter(0), funcEntries(0) { }

		Offsets offsets() const {
			Offsets o;
			o.dsp = off(&dsp); o.mb = off(&memoryBase); o.fuel = off(&clockCyclesLeft); o.ipsp = off(&ipsp);
			o.resume = off(&resume); o.saveddsp = off(&savedDsp); o.natives = off(&natives);
			o.nativefn = off(&nativeFn); o.funcentries = off(&funcEntries);
			o.memsize = off(&memorySize); o.rwmemsize = off(&rwMemorySize); o.dsend = off(&dataStackEnd);
			o.ipsend = off(&ipStackEnd); o.nativeafter = off(&nativeAfter);
			return o;
		}

		// Drive the native dispatcher trampoline. Mid-run GAZL calls/returns and native calls stay inside it; control
		// returns here only to suspend (TIME_OUT, re-granted) or finish. `segEntry` seeds RESUME (a fresh call == a resume).
		Status run(void* dispatchEntry, void* segEntry, int perCallFuel, int& suspends) {
			typedef int (*Disp)(JitEngine*);
			resume = segEntry;
			suspends = 0;
			Status s;
			do {
				s = static_cast<Status>(reinterpret_cast<Disp>(dispatchEntry)(this));
				if (s == TIME_OUT) { ++suspends; resetTimeOut(perCallFuel); }
				// BLOCK_RETRY: the native suspended; re-dispatch (RESUME = the call site) to re-issue it (§5.4).
			} while (s == TIME_OUT || s == BLOCK_RETRY);
			return s;
		}
	private:	template<class T> uint32_t off(T* p) const {
					return static_cast<uint32_t>(reinterpret_cast<const char*>(p) - reinterpret_cast<const char*>(this));
				}
};

// --- emit helpers (x0=ctx, x1=dsp, x2=membase, w3=fuel, x4=ipsp; scratch x9..x12) ---

inline void reloadState(Emitter& e, const Offsets& o) {
	e.ldrX(X1, X0, o.dsp); e.ldrX(X2, X0, o.mb); e.ldrW(W3, X0, o.fuel); e.ldrX(X4, X0, o.ipsp);
}
inline void writebackState(Emitter& e, const Offsets& o) {
	e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);
}
inline void matConst(Emitter& e, Reg r, Int v) { e.movImm32(r, static_cast<uint32_t>(v)); }
// Frame slots are Value-indices off dsp (x1). ldur/stur reach ±64 words; far slots (big frames / LOCA arrays) fall back
// to a register-offset load (index in W13 — kept distinct from the W9..W12 operand scratches). See task #23.
inline bool slotNear(Int slot) { return slot >= -64 && slot <= 63; }
inline void loadSlot(Emitter& e, Reg r, Int slot) {
	if (slotNear(slot)) { e.ldurW(r, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.ldrWxs(r, X1, W13); }
}
inline void storeSlot(Emitter& e, Reg r, Int slot) {
	if (slotNear(slot)) { e.sturW(r, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.strWxs(r, X1, W13); }
}
inline void loadSlotF(Emitter& e, Reg s, Int slot) {
	if (slotNear(slot)) { e.ldurS(s, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.ldrSxs(s, X1, W13); }
}
inline void storeSlotF(Emitter& e, Reg s, Int slot) {
	if (slotNear(slot)) { e.sturS(s, X1, static_cast<int>(slot * 4)); }
	else { matConst(e, W13, slot); e.strSxs(s, X1, W13); }
}
inline void loadOp(Emitter& e, Reg r, const Value& p, bool isConst) {
	if (isConst) { matConst(e, r, p.i); } else { loadSlot(e, r, p.i); }
}
// `dst = s1 <op> s2`, where s2 is a const (imm-form) or slot; s1 is always a slot for VVV/VVC, a const for VCV.
inline void emitBinary(Emitter& e, void (Emitter::*op)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	loadOp(e, W10, in.p1, s1Const);
	loadOp(e, W11, in.p2, s2Const);
	(e.*op)(W9, W10, W11);
	storeSlot(e, W9, in.p0.i);
}

// Load a float operand into S-reg `s` (using `wtmp` for a const): a slot via ldur, or a float constant via fmov s,w.
inline void loadOpF(Emitter& e, Reg s, Reg wtmp, const Value& p, bool isConst) {
	if (isConst) { matConst(e, wtmp, p.i); e.fmovSW(s, wtmp); }
	else { loadSlotF(e, s, p.i); }
}
// `dst = s1 <fop> s2` on float slots/consts.
inline void emitBinaryF(Emitter& e, void (Emitter::*fop)(Reg, Reg, Reg), const Instruction& in, bool s1Const, bool s2Const) {
	loadOpF(e, S0, W9, in.p1, s1Const);
	loadOpF(e, S1, W10, in.p2, s2Const);
	(e.*fop)(S2, S0, S1);
	storeSlotF(e, S2, in.p0.i);
}

// Emit a shift `dst = value <shift> count`. kind: 0=lsl, 1=asr (SHRi), 2=lsr (SHRu). form: 0=VVV, 1=VVC, 2=VCV.
// value is p1 (a slot for VVV/VVC, a const for VCV); count is p2 (a const for VVC, else a slot, masked mod 32 by HW).
inline void emitShift(Emitter& e, int kind, const Instruction& in, int form) {
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
inline void emitDivMod(Emitter& e, bool rem, const Instruction& in, int form) {
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
inline bool branchTarget(const Instruction* code, UInt j, UInt& target) {
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
inline bool lowerFunction(Emitter& e, const Instruction* code, UInt funcIndex, const Offsets& o,
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
		if (op == OP_FUNC) { continue; }							// prologue stack/fuel check omitted for the prototype
		else if (op == OP_RETU) {
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
		}
		else if (op == OP_CALL_CVC) {
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
		}
		else if (op == OP_CALL_VVC) {
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
		}
		else if (op == OP_CALL_NVC) {
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
		}
		else if (op == OP_MOVE_VC) { matConst(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i); }
		else if (op == OP_MOVE_VV) { loadSlot(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i); }
		else if (op == OP_PEEK_VC) { e.ldrW(W9, X2, static_cast<uint32_t>((in.p1.p - MEMORY_OFFSET) * 4)); storeSlot(e, W9, in.p0.i); }
		else if (op == OP_POKE_CV) { loadSlot(e, W9, in.p1.i); e.strW(W9, X2, static_cast<uint32_t>((in.p0.p - MEMORY_OFFSET) * 4)); }
		else if (op == OP_PEEK_VCV) {							// checked read: ui=(base-OFF)+index; ui<memorySize else BAD_PEEK
			Label trap = e.newLabel(), cont = e.newLabel();
			matConst(e, W9, static_cast<Int>(in.p1.p - MEMORY_OFFSET)); loadSlot(e, W10, in.p2.i); e.add(W9, W9, W10);
			e.ldrW(W10, X0, o.memsize); e.cmp(W9, W10); e.bcond(HS, trap);
			e.ldrWx(W11, X2, W9); storeSlot(e, W11, in.p0.i);
			e.b(cont); e.bind(trap); e.movn(W0, 1); e.ret();	// ~1 = -2 = BAD_PEEK
			e.bind(cont);
		}
		else if (op == OP_POKE_CVV) {							// checked write: ui=(base-OFF)+index; ui<rwMemorySize else BAD_POKE
			Label trap = e.newLabel(), cont = e.newLabel();
			matConst(e, W9, static_cast<Int>(in.p0.p - MEMORY_OFFSET)); loadSlot(e, W10, in.p1.i); e.add(W9, W9, W10);
			e.ldrW(W10, X0, o.rwmemsize); e.cmp(W9, W10); e.bcond(HS, trap);
			loadSlot(e, W11, in.p2.i); e.strWx(W11, X2, W9);
			e.b(cont); e.bind(trap); e.movn(W0, 2); e.ret();	// ~2 = -3 = BAD_POKE
			e.bind(cont);
		}
		else if (op == OP_GETL_VVV) {							// checked stack-local read: dst = (dsp+C1)[index], index < stackLeft
			Label trap = e.newLabel(), cont = e.newLabel();
			e.ldrX(X9, X0, o.dsend); e.sub(W9, W9, W1); e.lsrImm(W9, W9, 2);	// (dataStackEnd - dsp) in Value units
			matConst(e, W10, in.p1.i); e.sub(W9, W9, W10);					// limit = that - C1
			loadSlot(e, W10, in.p2.i); e.cmp(W10, W9); e.bcond(HS, trap);	// index >= limit → BAD_PEEK
			matConst(e, W11, in.p1.i); e.add(W11, W11, W10);				// C1 + index (signed offset off dsp)
			e.ldrWxs(W11, X1, W11); storeSlot(e, W11, in.p0.i);
			e.b(cont); e.bind(trap); e.movn(W0, 1); e.ret();
			e.bind(cont);
		}
		else if (op == OP_SETL_VVV || op == OP_SETL_VVC) {		// checked stack-local write: (dsp+C0)[index] = value
			Label trap = e.newLabel(), cont = e.newLabel();
			e.ldrX(X9, X0, o.dsend); e.sub(W9, W9, W1); e.lsrImm(W9, W9, 2);
			matConst(e, W10, in.p0.i); e.sub(W9, W9, W10);					// limit = (end-dsp) - C0
			loadSlot(e, W10, in.p1.i); e.cmp(W10, W9); e.bcond(HS, trap);	// index >= limit → BAD_POKE
			matConst(e, W11, in.p0.i); e.add(W11, W11, W10);				// C0 + index
			if (op == OP_SETL_VVC) { matConst(e, W12, in.p2.i); } else { loadSlot(e, W12, in.p2.i); }
			e.strWxs(W12, X1, W11);
			e.b(cont); e.bind(trap); e.movn(W0, 2); e.ret();
			e.bind(cont);
		}
		else if (op == OP_COPY_VVC || op == OP_COPY_VCC || op == OP_COPY_CVC || op == OP_COPY_CCC) {
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
		}
		else if (op == OP_ADRL) {								// V0 = &dsp[C1] as a MEMORY_OFFSET-biased pointer
			e.sub(W9, W1, W2);									// (dsp - memoryBase) in bytes (low 32 valid within buffer)
			e.lsrImm(W9, W9, 2);								//   -> Value units
			matConst(e, W10, static_cast<Int>(MEMORY_OFFSET) + in.p1.i); e.add(W9, W9, W10);
			storeSlot(e, W9, in.p0.i);
		}
		else if (op == OP_PEEK_VVV) {							// checked read through a runtime pointer: dst = mem[base+index-OFF]
			Label trap = e.newLabel(), cont = e.newLabel();
			loadSlot(e, W9, in.p1.i); loadSlot(e, W10, in.p2.i); e.add(W9, W9, W10);	// base + index
			matConst(e, W10, static_cast<Int>(MEMORY_OFFSET)); e.sub(W9, W9, W10);	// - MEMORY_OFFSET
			e.ldrW(W10, X0, o.memsize); e.cmp(W9, W10); e.bcond(HS, trap);
			e.ldrWx(W11, X2, W9); storeSlot(e, W11, in.p0.i);
			e.b(cont); e.bind(trap); e.movn(W0, 1); e.ret();	// BAD_PEEK
			e.bind(cont);
		}
		else if (op == OP_POKE_VVV) {							// checked write through a runtime pointer: mem[base+index-OFF] = value
			Label trap = e.newLabel(), cont = e.newLabel();
			loadSlot(e, W9, in.p0.i); loadSlot(e, W10, in.p1.i); e.add(W9, W9, W10);	// base + index
			matConst(e, W10, static_cast<Int>(MEMORY_OFFSET)); e.sub(W9, W9, W10);
			e.ldrW(W10, X0, o.rwmemsize); e.cmp(W9, W10); e.bcond(HS, trap);
			loadSlot(e, W11, in.p2.i); e.strWx(W11, X2, W9);
			e.b(cont); e.bind(trap); e.movn(W0, 2); e.ret();	// BAD_POKE
			e.bind(cont);
		}
		else if (op == OP_ADDI_VVV) { emitBinary(e, &Emitter::add, in, false, false); }
		else if (op == OP_ADDI_VVC) { emitBinary(e, &Emitter::add, in, false, true); }
		else if (op == OP_SUBI_VVV) { emitBinary(e, &Emitter::sub, in, false, false); }
		else if (op == OP_SUBI_VVC) { emitBinary(e, &Emitter::sub, in, false, true); }
		else if (op == OP_SUBI_VCV) { emitBinary(e, &Emitter::sub, in, true, false); }
		else if (op == OP_MULI_VVV) { emitBinary(e, &Emitter::mul, in, false, false); }
		else if (op == OP_MULI_VVC) { emitBinary(e, &Emitter::mul, in, false, true); }
		else if (op == OP_DIVI_VVV) { emitDivMod(e, false, in, 0); }
		else if (op == OP_DIVI_VVC) { emitDivMod(e, false, in, 1); }
		else if (op == OP_DIVI_VCV) { emitDivMod(e, false, in, 2); }
		else if (op == OP_MODI_VVV) { emitDivMod(e, true, in, 0); }
		else if (op == OP_MODI_VVC) { emitDivMod(e, true, in, 1); }
		else if (op == OP_MODI_VCV) { emitDivMod(e, true, in, 2); }
		else if (op == OP_SHLI_VVV) { emitShift(e, 0, in, 0); }
		else if (op == OP_SHLI_VVC) { emitShift(e, 0, in, 1); }
		else if (op == OP_SHLI_VCV) { emitShift(e, 0, in, 2); }
		else if (op == OP_SHRI_VVV) { emitShift(e, 1, in, 0); }
		else if (op == OP_SHRI_VVC) { emitShift(e, 1, in, 1); }
		else if (op == OP_SHRI_VCV) { emitShift(e, 1, in, 2); }
		else if (op == OP_SHRU_VVV) { emitShift(e, 2, in, 0); }
		else if (op == OP_SHRU_VVC) { emitShift(e, 2, in, 1); }
		else if (op == OP_SHRU_VCV) { emitShift(e, 2, in, 2); }
		else if (op == OP_ABSI) {						// dst = |src| (branchless: mask = src>>31; (src^mask)-mask)
			loadSlot(e, W10, in.p1.i); e.asrImm(W11, W10, 31); e.eor(W9, W10, W11); e.sub(W9, W9, W11);
			storeSlot(e, W9, in.p0.i);
		}
		else if (op == OP_ADDF_VVV) { emitBinaryF(e, &Emitter::faddS, in, false, false); }
		else if (op == OP_ADDF_VVC) { emitBinaryF(e, &Emitter::faddS, in, false, true); }
		else if (op == OP_SUBF_VVV) { emitBinaryF(e, &Emitter::fsubS, in, false, false); }
		else if (op == OP_SUBF_VVC) { emitBinaryF(e, &Emitter::fsubS, in, false, true); }
		else if (op == OP_SUBF_VCV) { emitBinaryF(e, &Emitter::fsubS, in, true, false); }
		else if (op == OP_MULF_VVV) { emitBinaryF(e, &Emitter::fmulS, in, false, false); }
		else if (op == OP_MULF_VVC) { emitBinaryF(e, &Emitter::fmulS, in, false, true); }
		else if (op == OP_DIVF_VVV) { emitBinaryF(e, &Emitter::fdivS, in, false, false); }
		else if (op == OP_DIVF_VVC) { emitBinaryF(e, &Emitter::fdivS, in, false, true); }
		else if (op == OP_DIVF_VCV) { emitBinaryF(e, &Emitter::fdivS, in, true, false); }
		else if (op == OP_FTOI_VVC) {					// dst_int = fcvtzs(src_float * scale)  (saturating, toward zero)
			loadSlotF(e, S0, in.p1.i); matConst(e, W9, in.p2.i); e.fmovSW(S1, W9);
			e.fmulS(S0, S0, S1); e.fcvtzs(W9, S0); storeSlot(e, W9, in.p0.i);
		}
		else if (op == OP_ITOF_VVC) {					// dst_float = (float)src_int * scale
			loadSlot(e, W9, in.p1.i); e.scvtf(S0, W9); matConst(e, W10, in.p2.i); e.fmovSW(S1, W10);
			e.fmulS(S0, S0, S1); storeSlotF(e, S0, in.p0.i);
		}
		else if (op == OP_ANDI_VVV) { emitBinary(e, &Emitter::and_, in, false, false); }
		else if (op == OP_ANDI_VVC) { emitBinary(e, &Emitter::and_, in, false, true); }
		else if (op == OP_IORI_VVV) { emitBinary(e, &Emitter::orr, in, false, false); }
		else if (op == OP_IORI_VVC) { emitBinary(e, &Emitter::orr, in, false, true); }
		else if (op == OP_XORI_VVV) { emitBinary(e, &Emitter::eor, in, false, false); }
		else if (op == OP_XORI_VVC) { emitBinary(e, &Emitter::eor, in, false, true); }
		else if (op == OP_FORi_VVB || op == OP_FORi_VCB) {		// ++i ; if i < n goto head
			loadSlot(e, W10, in.p0.i); e.addImm(W10, W10, 1); storeSlot(e, W10, in.p0.i);
			loadOp(e, W11, in.p1, op == OP_FORi_VCB);
			e.cmp(W10, W11);
			e.bcond(LT, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
		}
		else if (op == OP_GOTO) { e.b(mainline[static_cast<UInt>(static_cast<Int>(j) + in.p0.i)]); }
		else if (op == OP_LSSF_VVB || op == OP_LSSF_VCB || op == OP_LSSF_CVB || op == OP_EQUF_VVB || op == OP_EQUF_VCB
				|| op == OP_NLSF_VVB || op == OP_NLSF_VCB || op == OP_NLSF_CVB || op == OP_NEQF_VVB || op == OP_NEQF_VCB) {
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
		}
		else {													// integer conditional branches LSS/EQU/NLS/NEQ
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
inline size_t emitDispatcher(Emitter& e, const Offsets& o) {
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

#endif
