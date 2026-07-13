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

// GAZL finalized opcodes (the enum is internal to GAZL.cpp; base = FIRST_OPCODE_VALUE 0x2345, declaration order).
enum {
	OP_FUNC = 0x2345 + 0, OP_CALL_VVC = 0x2345 + 1, OP_CALL_CVC = 0x2345 + 2, OP_CALL_NVC = 0x2345 + 3,
	OP_RETU = 0x2345 + 4, OP_MOVE_VV = 0x2345 + 5, OP_MOVE_VC = 0x2345 + 6, OP_PEEK_VC = 0x2345 + 7,
	OP_POKE_CV = 0x2345 + 8, OP_PEEK_VCV = 0x2345 + 11, OP_POKE_CVV = 0x2345 + 13,
	OP_ADDI_VVV = 0x2345 + 21, OP_ADDI_VVC = 0x2345 + 22,
	OP_SUBI_VVV = 0x2345 + 23, OP_SUBI_VVC = 0x2345 + 24, OP_SUBI_VCV = 0x2345 + 25,
	OP_MULI_VVV = 0x2345 + 26, OP_MULI_VVC = 0x2345 + 27,
	OP_ANDI_VVV = 0x2345 + 34, OP_ANDI_VVC = 0x2345 + 35,
	OP_IORI_VVV = 0x2345 + 36, OP_IORI_VVC = 0x2345 + 37,
	OP_XORI_VVV = 0x2345 + 38, OP_XORI_VVC = 0x2345 + 39,
	OP_FORi_VVB = 0x2345 + 67, OP_FORi_VCB = 0x2345 + 68,
	OP_LSSI_VVB = 0x2345 + 69, OP_LSSI_VCB = 0x2345 + 70, OP_LSSI_CVB = 0x2345 + 71,
	OP_EQUI_VVB = 0x2345 + 72, OP_EQUI_VCB = 0x2345 + 73,
	OP_NLSI_VVB = 0x2345 + 74, OP_NLSI_VCB = 0x2345 + 75, OP_NLSI_CVB = 0x2345 + 76,
	OP_NEQI_VVB = 0x2345 + 77, OP_NEQI_VCB = 0x2345 + 78,
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
	uint32_t dsp, mb, fuel, ipsp, resume, saveddsp, natives, nativefn, funcentries, memsize, rwmemsize;
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
				void** funcEntries;					// ordinal -> JIT-compiled GAZL-function entry (indirect calls)

		JitEngine(UInt codeSize, const Instruction* code, UInt fnCount, const UInt* fnTable, UInt memSize, Value* mem
					, UInt globalsSize, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* nat)
			: Processor(codeSize, code, fnCount, fnTable, memSize, mem, globalsSize, constsSize, ipStackSize, ipStack
				, nat, 0), resume(0), savedDsp(0), nativeFn(0), funcEntries(0) { }

		Offsets offsets() const {
			Offsets o;
			o.dsp = off(&dsp); o.mb = off(&memoryBase); o.fuel = off(&clockCyclesLeft); o.ipsp = off(&ipsp);
			o.resume = off(&resume); o.saveddsp = off(&savedDsp); o.natives = off(&natives);
			o.nativefn = off(&nativeFn); o.funcentries = off(&funcEntries);
			o.memsize = off(&memorySize); o.rwmemsize = off(&rwMemorySize);
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
			} while (s == TIME_OUT);
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
inline void loadSlot(Emitter& e, Reg r, Int slot) { e.ldurW(r, X1, static_cast<int>(slot * 4)); }
inline void storeSlot(Emitter& e, Reg r, Int slot) { e.sturW(r, X1, static_cast<int>(slot * 4)); }
inline void matConst(Emitter& e, Reg r, Int v) { e.movImm32(r, static_cast<uint32_t>(v)); }
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
	for (std::set<UInt>::const_iterator it = targets.begin(); it != targets.end(); ++it) { mainline[*it] = e.newLabel(); }
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		reloadL[it->first] = e.newLabel(); suspendL[it->first] = e.newLabel();
	}

	// Function entry: reload pinned state, then FUNC prologue (dsp += localsSize).
	entryOffset[selfOrdinal] = e.wordCount();
	e.bind(entryLabels[selfOrdinal]);
	reloadState(e, o);
	const UInt localsSize = static_cast<UInt>(code[funcIndex].p0.i);
	if (localsSize != 0) { e.addImmX(X1, X1, localsSize * 4); }

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
			Label after = e.newLabel();
			e.adr(X9, after); e.strX(X9, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);	// push {cont, dsp}
			if (window != 0) { e.addImmX(X1, X1, window * 4); }	// dsp += arg window
			writebackState(e, o);
			e.adr(X9, entryLabels[callee]); e.strX(X9, X0, o.resume);	// RESUME = callee entry
			e.movz(W0, TRANSFER); e.ret();
			e.bind(after); reloadState(e, o);					// after the call returns: fresh segment → reload
		}
		else if (op == OP_CALL_VVC) {
			const UInt window = static_cast<UInt>(in.p1.i);		// ordinal from a slot at runtime → resolve + bounds-check
			Label after = e.newLabel(), trap = e.newLabel();
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
			Label after = e.newLabel();
			e.strX(X1, X0, o.saveddsp);							// stash original dsp (restored by the dispatcher after)
			if (window != 0) { e.addImmX(X1, X1, window * 4); }
			e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);	// publish window/fuel/ipsp
			e.adr(X9, after); e.strX(X9, X0, o.resume);			// RESUME = after-call (success)
			e.ldrX(X9, X0, o.natives); e.ldrX(X9, X9, ordinal * 8); e.strX(X9, X0, o.nativefn);	// resolve natives[ord]
			e.movz(W0, NATIVE_CALL); e.ret();					// hand the host call to the dispatcher
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
		else if (op == OP_ADDI_VVV) { emitBinary(e, &Emitter::add, in, false, false); }
		else if (op == OP_ADDI_VVC) { emitBinary(e, &Emitter::add, in, false, true); }
		else if (op == OP_SUBI_VVV) { emitBinary(e, &Emitter::sub, in, false, false); }
		else if (op == OP_SUBI_VVC) { emitBinary(e, &Emitter::sub, in, false, true); }
		else if (op == OP_SUBI_VCV) { emitBinary(e, &Emitter::sub, in, true, false); }
		else if (op == OP_MULI_VVV) { emitBinary(e, &Emitter::mul, in, false, false); }
		else if (op == OP_MULI_VVC) { emitBinary(e, &Emitter::mul, in, false, true); }
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
		else {													// conditional branches LSS/EQU/NLS/NEQ
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
	e.cmpImm(W0, 0); e.bcond(EQ, loop);					// native OK → continue at the after-call point
	e.bind(done);
	e.ldrX(X19, SP, 0); e.ldrX(X30, SP, 8); e.addImmX(SP, SP, 16); e.ret();
	return entry;
}

} // namespace GAZLJitLower

#endif
