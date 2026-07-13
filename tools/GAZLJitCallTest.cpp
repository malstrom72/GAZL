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
	GAZL -> GAZL calls under the §5.4 dispatcher / segment model — "GAZL calls are never host calls." One dispatcher
	owns the single native frame; compiled functions are SEGMENTS it threads between via the RESUME continuation. No
	nested host frames, so the whole call chain stays suspendable/resumable at any depth (§5.4). Traps and returns are
	the same unified shape (§5.7.5).

	Mechanics, mirroring the interpreter's dsp arithmetic exactly (so the JIT needs no understanding of the arg-window
	semantics, only faithful replication):
	  - pinned dsp / membase / fuel / ipsp live in the `Processor`; each segment reloads them at entry and writes the
	    mutable ones back at a transfer. Frame slots are signed Value-indices off dsp (ldur/stur).
	  - CALL_CVC: push {native return continuation, dsp} to the ipStack, `dsp += window`, set RESUME = callee entry
	    (direct — the ordinal is a compile-time constant), return TRANSFER to the dispatcher.
	  - FUNC: `dsp += localsSize`.  RETU: pop {cont, dsp}; the dsp==0 native-return marker ends the chain (OK), else set
	    RESUME = cont and TRANSFER.
	  - fuel timeout at a loop head suspends via a cold reload prologue (RESUME target); the hot back-edge skips it.

	The dispatcher loop is `while ((s = RESUME(ctx)) is TRANSFER or TIME_OUT) ...` — one field, one jump (§5.7.5), done
	in C++ for this prototype (the design's is native `ldr RESUME; br`). Validated on a kernel where main() calls a
	helper sq() in a loop: the JIT's whole memory image is byte-identical to the interpreter, including runs forced to
	suspend/resume repeatedly across the loop. src/GAZL.* is compiled READ-ONLY. AArch64 only.

	Deferred (noted): indirect calls (CALL_VVC), native calls (CALL_NVC), ipStack-overflow/trap checks, and suspends
	taken *inside* a callee (this kernel's only safepoint is main's loop head). Exits non-zero on any mismatch.
*/

#include "GAZLJit.h"
#include "GAZL.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sys/mman.h>

#if defined(__APPLE__)
	#include <pthread.h>
	#include <libkern/OSCacheControl.h>
#endif

using namespace GAZL;

static int failures = 0;
static const int TRANSFER = 1;					// segment-to-segment transfer sentinel (no GAZL status is +1)
static const int NATIVE_CALL = 2;				// "invoke a native, then continue" sentinel

static long gNativeCallCount = 0;				// proves the host native actually ran (bumped each call)

// A host native, called exactly as the interpreter calls it (Processor* + accessParams, §5.4). Computes r = x*x with
// the OUT/return at params[0] and the arg at params[1] (the same %0/%1 slots the GAZL sq used).
static Status nativeSquare(Processor* p) {
	Value* q = p->accessParams(2);
	if (q == 0) { return DATA_STACK_OVERFLOW; }
	q[0].i = q[1].i * q[1].i;
	++gNativeCallCount;
	return OK;
}
static NativeFunc const gNativeTable[] = { nativeSquare };
static const char* const gNativeNames[] = { "nsq" };

static void* makeExecutable(const uint32_t* words, size_t wordCount) {
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

enum {
	OP_FUNC = 0x2345 + 0, OP_CALL_VVC = 0x2345 + 1, OP_CALL_CVC = 0x2345 + 2, OP_CALL_NVC = 0x2345 + 3,
	OP_RETU = 0x2345 + 4, OP_MOVE_VV = 0x2345 + 5,
	OP_MOVE_VC = 0x2345 + 6, OP_PEEK_VC = 0x2345 + 7, OP_POKE_CV = 0x2345 + 8,
	OP_PEEK_VCV = 0x2345 + 11, OP_POKE_CVV = 0x2345 + 13,
	OP_ADDI_VVV = 0x2345 + 21, OP_ADDI_VVC = 0x2345 + 22, OP_SUBI_VVV = 0x2345 + 23, OP_SUBI_VVC = 0x2345 + 24,
	OP_MULI_VVV = 0x2345 + 26, OP_MULI_VVC = 0x2345 + 27, OP_FORi_VVB = 0x2345 + 67, OP_FORi_VCB = 0x2345 + 68
};

// Byte offsets of the pinned state within the (subclass) engine.
struct Offsets { uint32_t dsp, mb, fuel, ipsp, resume, saveddsp, nativeord, natives, nativefn, funcentries;
		uint32_t memsize, rwmemsize; };

/*
	JIT engine subclass: shares the base `Processor` machine state and adds the RESUME continuation field.
*/
class JitEngine : public Processor {
	public:		void* resume;
				Value* savedDsp;					// dsp saved across a native call (the C1 window is transient)
				int nativeOrd;						// (unused now the dispatcher is native) which native to invoke
				void* nativeFn;						// resolved native fn pointer, blr'd by the native dispatcher
				void** funcEntries;				// ordinal -> JIT-compiled GAZL-function entry (indirect calls, CALL_VVC)

		JitEngine(UInt codeSize, const Instruction* code, UInt fnCount, const UInt* fnTable, UInt memSize, Value* mem
					, UInt globalsSize, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* nat)
			: Processor(codeSize, code, fnCount, fnTable, memSize, mem, globalsSize, constsSize, ipStackSize, ipStack
				, nat, 0), resume(0), savedDsp(0), nativeOrd(0), nativeFn(0), funcEntries(0) { }

		Offsets offsets() const {
			Offsets o;
			o.dsp = off(&dsp); o.mb = off(&memoryBase); o.fuel = off(&clockCyclesLeft); o.ipsp = off(&ipsp);
			o.resume = off(&resume); o.saveddsp = off(&savedDsp); o.nativeord = off(&nativeOrd);
			o.natives = off(&natives); o.nativefn = off(&nativeFn); o.funcentries = off(&funcEntries);
			o.memsize = off(&memorySize); o.rwmemsize = off(&rwMemorySize);
			return o;
		}

		// Drive the NATIVE dispatcher trampoline (§5.4 encoding (a)). Mid-run GAZL calls/returns and native calls stay
		// entirely inside the trampoline — control returns to C++ only to suspend (TIME_OUT, re-granted here) or finish.
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

// --- machine buffers ---
namespace {
	const int CODE_SIZE = 64 * 1024, DATA_SIZE = 64 * 1024, FUNCTION_TABLE_SIZE = 1024, CALL_STACK_SIZE = 256;
	Instruction gCode[CODE_SIZE];
	Value gMemory[DATA_SIZE];
	UInt gFunctionTable[FUNCTION_TABLE_SIZE];
	CallStackEntry gCallStack[CALL_STACK_SIZE];
	UInt gGlobalsSize = 0, gConstsSize = 0, gFunctionCount = 0;
	std::vector<Value> gCleanImage;
}

// --- emit helpers (x0=ctx, x1=dsp, x2=membase, w3=fuel, x4=ipsp; scratch x9..x12) ---
static void reloadState(Emitter& e, const Offsets& o) {
	e.ldrX(X1, X0, o.dsp); e.ldrX(X2, X0, o.mb); e.ldrW(W3, X0, o.fuel); e.ldrX(X4, X0, o.ipsp);
}
static void writebackState(Emitter& e, const Offsets& o) {
	e.strX(X1, X0, o.dsp); e.strW(W3, X0, o.fuel); e.strX(X4, X0, o.ipsp);
}
static void loadSlot(Emitter& e, Reg r, Int slot) { e.ldurW(r, X1, static_cast<int>(slot * 4)); }
static void storeSlot(Emitter& e, Reg r, Int slot) { e.sturW(r, X1, static_cast<int>(slot * 4)); }
static void matConst(Emitter& e, Reg r, Int v) { e.movImm32(r, static_cast<uint32_t>(v)); }
static void binOp(Emitter& e, void (Emitter::*op)(Reg, Reg, Reg), const Instruction& in, bool c2) {
	loadSlot(e, W10, in.p1.i);
	if (c2) { matConst(e, W11, in.p2.i); } else { loadSlot(e, W11, in.p2.i); }
	(e.*op)(W9, W10, W11);
	storeSlot(e, W9, in.p0.i);
}

// Lower one function into `e` (appended). Emits an entry reload + FUNC prologue, a mainline for each block, cold reload
// trampolines + suspend stubs for loop heads, and CALL/RETU transfers. `entryLabels[ord]` are pre-created.
static bool lowerFunction(Emitter& e, const Instruction* code, UInt funcIndex, const Offsets& o,
		std::vector<Label>& entryLabels, std::vector<size_t>& entryOffset, UInt selfOrdinal) {
	UInt retIndex = funcIndex;
	while (code[retIndex].opcode != OP_RETU) { ++retIndex; }

	// Analysis: branch targets + back-edge loop heads (with block weights).
	std::set<UInt> targets;
	std::map<UInt, UInt> loopWeight;
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		const Int op = code[j].opcode;
		if (op == OP_FORi_VVB || op == OP_FORi_VCB) {
			const UInt tgt = static_cast<UInt>(static_cast<Int>(j) + code[j].p2.i);
			targets.insert(tgt);
			if (tgt <= j) { loopWeight[tgt] = j - tgt + 1; }
		}
	}
	std::map<UInt, Label> mainline, reloadL, suspendL, afterCall;
	for (std::set<UInt>::const_iterator it = targets.begin(); it != targets.end(); ++it) { mainline[*it] = e.newLabel(); }
	for (std::map<UInt, UInt>::const_iterator it = loopWeight.begin(); it != loopWeight.end(); ++it) {
		reloadL[it->first] = e.newLabel(); suspendL[it->first] = e.newLabel();
	}

	// Function entry: reload pinned state, then FUNC prologue (dsp += localsSize).
	entryOffset[selfOrdinal] = e.wordCount();		// native word index of this entry (bound next)
	e.bind(entryLabels[selfOrdinal]);
	reloadState(e, o);
	const UInt localsSize = static_cast<UInt>(code[funcIndex].p0.i);
	if (localsSize != 0) { e.addImmX(X1, X1, localsSize * 4); }

	for (UInt j = funcIndex; j <= retIndex; ++j) {
		if (mainline.count(j)) { e.bind(mainline[j]); }
		if (loopWeight.count(j)) { e.subsImm(W3, W3, loopWeight[j]); e.bcond(MI, suspendL[j]); }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		if (op == OP_FUNC) { continue; }
		else if (op == OP_RETU) {
			Label notNative = e.newLabel();
			e.subImmX(X4, X4, 16);						// ipsp-- ; pop {cont, dsp}
			e.ldrX(X9, X4, 0);							// cont
			e.ldrX(X1, X4, 8);							// caller dsp (or 0 = native marker)
			e.cbnzX(X1, notNative);
			e.subImmX(X4, X4, 16);						// native return: pop again for true dsp
			e.ldrX(X1, X4, 8);
			writebackState(e, o);
			e.movz(W0, 0); e.ret();						// OK — terminal
			e.bind(notNative);
			writebackState(e, o);
			e.strX(X9, X0, o.resume);					// RESUME = return continuation
			e.movz(W0, TRANSFER); e.ret();
		}
		else if (op == OP_CALL_CVC) {
			const UInt callee = in.p0.p - IP_OFFSET;
			const UInt window = static_cast<UInt>(in.p1.i);
			Label after = e.newLabel(); afterCall[j] = after;
			e.adr(X9, after);							// push {return continuation, dsp}
			e.strX(X9, X4, 0);
			e.strX(X1, X4, 8);
			e.addImmX(X4, X4, 16);						// ipsp++
			if (window != 0) { e.addImmX(X1, X1, window * 4); }	// dsp += arg window
			writebackState(e, o);
			e.adr(X9, entryLabels[callee]);				// RESUME = callee entry (direct)
			e.strX(X9, X0, o.resume);
			e.movz(W0, TRANSFER); e.ret();
			e.bind(after);								// after the call returns: fresh segment → reload
			reloadState(e, o);
		}
		else if (op == OP_CALL_VVC) {
			// Indirect call: read the fn pointer from a slot, unbias to an ordinal, bounds-check (BAD_CALL trap), then
			// index the ordinal->native-entry table — the JIT's functionTable maps ordinal to a native entry (§5.4).
			const Int slot = in.p0.i;
			const UInt window = static_cast<UInt>(in.p1.i);
			Label after = e.newLabel(), trap = e.newLabel();
			loadSlot(e, W9, slot);						// w9 = fn pointer (IP_OFFSET + ordinal)
			matConst(e, W10, static_cast<Int>(IP_OFFSET));
			e.sub(W9, W9, W10);							// w9 = ordinal
			matConst(e, W10, static_cast<Int>(gFunctionCount));
			e.cmp(W9, W10);
			e.bcond(HS, trap);							// ordinal >= functionCount → BAD_CALL (unsigned compare)
			e.ldrX(X10, X0, o.funcentries);			// table base
			e.ldrXr(X9, X10, W9);						// entry = funcEntries[ordinal]
			e.adr(X10, after);							// push {return continuation, dsp}
			e.strX(X10, X4, 0); e.strX(X1, X4, 8); e.addImmX(X4, X4, 16);
			if (window != 0) { e.addImmX(X1, X1, window * 4); }
			writebackState(e, o);
			e.strX(X9, X0, o.resume);					// RESUME = resolved callee entry (dynamic)
			e.movz(W0, TRANSFER); e.ret();
			e.bind(trap);
			e.movn(W0, 3); e.ret();						// ~3 = -4 = BAD_CALL (terminal)
			e.bind(after);
			reloadState(e, o);
		}
		else if (op == OP_CALL_NVC) {
			const UInt ordinal = static_cast<UInt>(in.p0.i);	// C0 = native ordinal
			const UInt window = static_cast<UInt>(in.p1.i);		// C1 = param-window offset
			Label after = e.newLabel();
			e.strX(X1, X0, o.saveddsp);					// stash original dsp (restored by the dispatcher after)
			if (window != 0) { e.addImmX(X1, X1, window * 4); }
			e.strX(X1, X0, o.dsp);						// publish param window for accessParams()
			e.strW(W3, X0, o.fuel);						// publish fuel/ipsp so the native can use the API / reenter
			e.strX(X4, X0, o.ipsp);
			e.adr(X9, after); e.strX(X9, X0, o.resume);	// RESUME = after-call (success continuation)
			e.ldrX(X9, X0, o.natives);					// resolve the native fn pointer for the dispatcher to blr:
			e.ldrX(X9, X9, ordinal * 8);				//   natives[ordinal]
			e.strX(X9, X0, o.nativefn);
			e.movz(W0, NATIVE_CALL); e.ret();			// hand the host call to the dispatcher
			e.bind(after);								// on success: fresh segment → reload (fuel may have changed)
			reloadState(e, o);
		}
		else if (op == OP_MOVE_VC) { matConst(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i); }
		else if (op == OP_MOVE_VV) { loadSlot(e, W9, in.p1.i); storeSlot(e, W9, in.p0.i); }
		else if (op == OP_PEEK_VC) { e.ldrW(W9, X2, static_cast<uint32_t>((in.p1.p - MEMORY_OFFSET) * 4)); storeSlot(e, W9, in.p0.i); }
		else if (op == OP_POKE_CV) { loadSlot(e, W9, in.p1.i); e.strW(W9, X2, static_cast<uint32_t>((in.p0.p - MEMORY_OFFSET) * 4)); }
		else if (op == OP_PEEK_VCV) {
			// PEEK dst, &base, index: ui = (base - MEMORY_OFFSET) + index; if (ui < memorySize) dst = membase[ui]; else BAD_PEEK.
			Label trap = e.newLabel(), cont = e.newLabel();
			matConst(e, W9, static_cast<Int>(in.p1.p - MEMORY_OFFSET));	// base word offset (constant)
			loadSlot(e, W10, in.p2.i); e.add(W9, W9, W10);				// ui = base + index
			e.ldrW(W10, X0, o.memsize); e.cmp(W9, W10); e.bcond(HS, trap);	// bounds check (read: whole memory)
			e.ldrWx(W11, X2, W9); storeSlot(e, W11, in.p0.i);			// dst = membase[ui]
			e.b(cont);
			e.bind(trap); e.movn(W0, 1); e.ret();						// ~1 = -2 = BAD_PEEK (terminal)
			e.bind(cont);
		}
		else if (op == OP_POKE_CVV) {
			// POKE &base, index, value: ui = (base - MEMORY_OFFSET) + index; if (ui < rwMemorySize) membase[ui] = value; else BAD_POKE.
			Label trap = e.newLabel(), cont = e.newLabel();
			matConst(e, W9, static_cast<Int>(in.p0.p - MEMORY_OFFSET));	// base word offset (constant)
			loadSlot(e, W10, in.p1.i); e.add(W9, W9, W10);				// ui = base + index
			e.ldrW(W10, X0, o.rwmemsize); e.cmp(W9, W10); e.bcond(HS, trap);	// bounds check (write: RW region only)
			loadSlot(e, W11, in.p2.i); e.strWx(W11, X2, W9);			// membase[ui] = value
			e.b(cont);
			e.bind(trap); e.movn(W0, 2); e.ret();						// ~2 = -3 = BAD_POKE (terminal)
			e.bind(cont);
		}
		else if (op == OP_ADDI_VVV) { binOp(e, &Emitter::add, in, false); }
		else if (op == OP_ADDI_VVC) { binOp(e, &Emitter::add, in, true); }
		else if (op == OP_SUBI_VVV) { binOp(e, &Emitter::sub, in, false); }
		else if (op == OP_SUBI_VVC) { binOp(e, &Emitter::sub, in, true); }
		else if (op == OP_MULI_VVV) { binOp(e, &Emitter::mul, in, false); }
		else if (op == OP_MULI_VVC) { binOp(e, &Emitter::mul, in, true); }
		else if (op == OP_FORi_VVB || op == OP_FORi_VCB) {
			loadSlot(e, W10, in.p0.i); e.addImm(W10, W10, 1); storeSlot(e, W10, in.p0.i);
			if (op == OP_FORi_VCB) { matConst(e, W11, in.p1.i); } else { loadSlot(e, W11, in.p1.i); }
			e.cmp(W10, W11);
			e.bcond(LT, mainline[static_cast<UInt>(static_cast<Int>(j) + in.p2.i)]);
		}
		else { return false; }							// unsupported opcode
	}

	// Cold section: loop-head reload trampolines + suspend stubs.
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

// Emit the NATIVE dispatcher trampoline (§5.4 encoding (a)). `int dispatch(JitEngine* ctx)`: park CTX in a callee-saved
// reg, jump to RESUME, loop on TRANSFER (GAZL call/return — no C++ round-trip), make the one host call on NATIVE_CALL,
// and return to C++ only to suspend (TIME_OUT) or finish. Returns the trampoline's word offset in the buffer.
static size_t emitDispatcher(Emitter& e, const Offsets& o) {
	const size_t entry = e.wordCount();
	Label loop = e.newLabel(), done = e.newLabel();
	e.subImmX(SP, SP, 16); e.strX(X19, SP, 0); e.strX(X30, SP, 8);	// save CTX (x19) + return addr (x30)
	e.addImmX(X19, X0, 0);								// x19 = ctx (callee-saved → survives the host call)
	e.bind(loop);
	e.ldrX(X9, X19, o.resume);							// x9 = RESUME continuation
	e.addImmX(X0, X19, 0); e.blr(X9);					// run the segment (arg0 = ctx); w0 = status
	e.cmpImm(W0, TRANSFER); e.bcond(EQ, loop);			// TRANSFER: thread next segment — stays in JIT, no C++
	e.cmpImm(W0, NATIVE_CALL); e.bcond(NE, done);		// TIME_OUT / OK / trap → return to C++
	e.ldrX(X9, X19, o.nativefn);						// NATIVE_CALL: the one real host call
	e.addImmX(X0, X19, 0); e.blr(X9);					//   natives[ord](ctx); w0 = native status
	e.ldrX(X10, X19, o.saveddsp); e.strX(X10, X19, o.dsp);	// restore the transient window dsp
	e.cmpImm(W0, 0); e.bcond(EQ, loop);					// native OK → continue at the after-call point
	e.bind(done);										// (nonzero native status falls through → terminal)
	e.ldrX(X19, SP, 0); e.ldrX(X30, SP, 8); e.addImmX(SP, SP, 16); e.ret();
	return entry;
}

// --- harness ---
static bool assemble(const char* source, Symbols& globals) {
	for (int i = 0; i < static_cast<int>(sizeof(gNativeTable) / sizeof(*gNativeTable)); ++i) {
		globals.registerNative(gNativeNames[i], i);
	}
	UInt codeSize = 0, gs = 0, cs = 0, fc = 0;
	try {
		Assembler assem(CODE_SIZE, gCode, FUNCTION_TABLE_SIZE, gFunctionTable, DATA_SIZE, gMemory, globals);
		assem.newUnit("callKernel");
		std::string src(source); size_t pos = 0;
		while (pos < src.size()) {
			const size_t nl = src.find('\n', pos);
			assem.feed(src.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos).c_str());
			if (nl == std::string::npos) { break; }
			pos = nl + 1;
		}
		assem.finalize(codeSize, gs, cs, fc);
	} catch (const Exception& e) {
		std::printf("  ASSEMBLE FAILED: %s (%s)\n", ASSEMBLER_ERROR_TEXTS[e.error], e.detail.c_str()); return false;
	}
	gGlobalsSize = gs; gConstsSize = cs; gFunctionCount = fc;
	gCleanImage.assign(gMemory, gMemory + DATA_SIZE);
	return true;
}

static void restoreClean() { std::memcpy(gMemory, gCleanImage.data(), DATA_SIZE * sizeof(Value)); }

static std::vector<Value> runInterpreter(Pointer mainPtr, Pointer gInPtr, int n, Status& outStatus) {
	restoreClean();
	Processor p(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
			CALL_STACK_SIZE, gCallStack, gNativeTable, nullptr);
	p.accessMemory(gInPtr, 1)->i = n;
	Status s = p.enterCall(mainPtr);
	do { p.resetTimeOut(0x7FFFFFFF); s = p.run(); } while (s == TIME_OUT);	// a trap (BAD_POKE, ...) is a valid outcome
	outStatus = s;
	return std::vector<Value>(gMemory, gMemory + DATA_SIZE);
}

static bool imagesEqual(const std::vector<Value>& a, const std::vector<Value>& b, int& firstDiff) {
	for (size_t i = 0; i < a.size(); ++i) { if (a[i].i != b[i].i) { firstDiff = static_cast<int>(i); return false; } }
	firstDiff = -1; return true;
}

// Kernel A: main() calls a GAZL helper sq(i) in a loop (CALL_CVC/RETU per iteration).
static const char* const KERNEL_GAZL =
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"sq:   FUNC\n$r:   OUTi\n$x:   INPi\n      MULi $r $x $x\n      RETU\n"
	"main: FUNC\n      PARA *3\n$n: LOCi\n$acc: LOCi\n$i: LOCi\n$t: LOCi\n"
	"      PEEK $n &gIn\n      MOVi $acc #0\n      MOVi $i #0\n"
	".l:   MOVi %1 $i\n      CALL &sq %0 *2\n      MOVi $t %0\n      ADDi $acc $acc $t\n      FORi $i $n @.l\n"
	"      POKE &gOut $acc\n      RETU\n";

// Kernel C: main() calls sq through a function POINTER local (CALL_VVC — ordinal resolved at runtime).
static const char* const KERNEL_INDIRECT =
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"sq:   FUNC\n$r:   OUTi\n$x:   INPi\n      MULi $r $x $x\n      RETU\n"
	"main: FUNC\n      PARA *3\n$fp: LOCp\n$n: LOCi\n$acc: LOCi\n$i: LOCi\n$t: LOCi\n"
	"      MOVp $fp &sq\n      PEEK $n &gIn\n      MOVi $acc #0\n      MOVi $i #0\n"
	".l:   MOVi %1 $i\n      CALL $fp %0 *2\n      MOVi $t %0\n      ADDi $acc $acc $t\n      FORi $i $n @.l\n"
	"      POKE &gOut $acc\n      RETU\n";

// Kernel B: identical to A, but sq is the HOST native `nsq` (CALL_NVC per iteration — a real C++ call).
static const char* const KERNEL_NATIVE =
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"main: FUNC\n      PARA *2\n$n: LOCi\n$acc: LOCi\n$i: LOCi\n$t: LOCi\n"
	"      PEEK $n &gIn\n      MOVi $acc #0\n      MOVi $i #0\n"
	".l:   MOVi %1 $i\n      CALL ^nsq %0 *2\n      MOVi $t %0\n      ADDi $acc $acc $t\n      FORi $i $n @.l\n"
	"      POKE &gOut $acc\n      RETU\n";

// Assemble + lower every function, then check the JIT against the interpreter at full and tiny fuel.
static void runKernel(const char* name, const char* source, const int* inputs, size_t nInputs) {
	std::printf("Kernel \"%s\":\n", name);
	Symbols globals;
	if (!assemble(source, globals)) { ++failures; return; }
	const Pointer mainPtr = globals.findFunction("main");
	UInt sz = 0;
	const Pointer gInPtr = globals.findGlobal("gIn", sz), gOutPtr = globals.findGlobal("gOut", sz);
	if (mainPtr == NULL_POINTER || gInPtr == NULL_POINTER || gOutPtr == NULL_POINTER) {
		std::printf("  symbol lookup failed\n"); ++failures; return;
	}
	const UInt mainOrd = mainPtr - IP_OFFSET;

	JitEngine probe(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize, gConstsSize,
			CALL_STACK_SIZE, gCallStack, gNativeTable);
	const Offsets o = probe.offsets();

	Emitter e;
	std::vector<Label> entryLabels(gFunctionCount);
	std::vector<size_t> entryOffset(gFunctionCount, 0);
	for (UInt k = 0; k < gFunctionCount; ++k) { entryLabels[k] = e.newLabel(); }
	bool ok = true;
	for (UInt ord = 0; ord < gFunctionCount; ++ord) {			// lower every function into one buffer
		ok = ok && lowerFunction(e, gCode, gFunctionTable[ord], o, entryLabels, entryOffset, ord);
	}
	if (!ok) { std::printf("  lowering failed (unsupported opcode)\n"); ++failures; return; }
	const size_t dispatchOffset = emitDispatcher(e, o);			// native dispatcher into the same buffer
	e.finalize();
	void* code = makeExecutable(e.code(), e.wordCount());
	if (code == nullptr) { std::printf("  W^X alloc failed\n"); ++failures; return; }
	void* const dispatchAddr = reinterpret_cast<char*>(code) + dispatchOffset * 4;
	std::vector<void*> funcEntries(gFunctionCount);			// ordinal -> JIT-compiled GAZL-function entry address
	for (UInt ord = 0; ord < gFunctionCount; ++ord) {
		funcEntries[ord] = reinterpret_cast<char*>(code) + entryOffset[ord] * 4;
	}
	std::printf("  lowered %zu native words for %u function(s); native dispatcher @ word %zu\n",
			e.wordCount(), gFunctionCount, dispatchOffset);

	for (size_t k = 0; k < nInputs; ++k) {
		const int n = inputs[k];
		Status wantStatus = OK;
		const std::vector<Value> want = runInterpreter(mainPtr, gInPtr, n, wantStatus);

		for (int pass = 0; pass < 2; ++pass) {
			const int fuel = (pass == 0) ? 0x7FFFFFFF : 6;
			restoreClean();
			JitEngine eng(CODE_SIZE, gCode, gFunctionCount, gFunctionTable, DATA_SIZE, gMemory, gGlobalsSize,
					gConstsSize, CALL_STACK_SIZE, gCallStack, gNativeTable);
			eng.funcEntries = funcEntries.data();
			eng.accessMemory(gInPtr, 1)->i = n;
			eng.enterCall(mainPtr);							// sets up ipStack (native-return marker) + dsp
			eng.resetTimeOut(fuel);
			void* mainEntry = reinterpret_cast<char*>(code) + entryOffset[mainOrd] * 4;
			gNativeCallCount = 0;
			int suspends = 0;
			const Status s = eng.run(dispatchAddr, mainEntry, fuel, suspends);
			std::vector<Value> got(gMemory, gMemory + DATA_SIZE);
			int diff = -1;
			const bool good = (s == wantStatus) && imagesEqual(want, got, diff);	// JIT must match status AND memory
			std::printf("  n=%-8d %-11s status=%-3d hostcalls=%-5ld suspends=%-5d gOut=%-12d %s\n", n,
					pass == 0 ? "[fullfuel]" : "[tinyfuel]", s, gNativeCallCount, suspends,
					gMemory[gOutPtr - MEMORY_OFFSET].i, good ? "OK" : "MISMATCH");
			if (!good) { std::printf("    want status=%d, diff word=%d\n", wantStatus, diff); ++failures; }
		}
	}
	::munmap(code, e.wordCount() * sizeof(uint32_t));
	std::printf("\n");
}

// Kernel D: checked indexed memory — buf[n] = 12345; read it back (POKE_CVV / PEEK_VCV, bounds-checked). An out-of-
// range index traps BAD_POKE — a Status, no signal — which must match the interpreter.
static const char* const KERNEL_MEMORY =
	"gIn:  GLOB *1\n       DATi #0\n" "gOut: GLOB *1\n       DATi #0\n"
	"buf:  GLOB *8\n       DATi #0\n       DATi #0\n       DATi #0\n       DATi #0\n"
	"       DATi #0\n       DATi #0\n       DATi #0\n       DATi #0\n"
	"main: FUNC\n      PARA *1\n$n: LOCi\n$x: LOCi\n"
	"      PEEK $n &gIn\n      MOVi $x #12345\n      POKE &buf $n $x\n      PEEK $x &buf $n\n"
	"      POKE &gOut $x\n      RETU\n";

int main() {
	std::printf("GAZLJit (§5.4): direct/indirect/native calls, native dispatcher, checked memory + traps (arm64)\n\n");
	const int counts[] = { 0, 1, 2, 5, 10, 100, 1000 };
	const int indices[] = { 0, 3, 7, 100, 2000000 };			// last is out of range → BAD_POKE trap
	runKernel("gazl-call sq()  [CALL_CVC]", KERNEL_GAZL, counts, sizeof(counts) / sizeof(*counts));
	runKernel("indirect &sq()  [CALL_VVC]", KERNEL_INDIRECT, counts, sizeof(counts) / sizeof(*counts));
	runKernel("native nsq()    [CALL_NVC]", KERNEL_NATIVE, counts, sizeof(counts) / sizeof(*counts));
	runKernel("checked buf[n]  [PEEK/POKE + trap]", KERNEL_MEMORY, indices, sizeof(indices) / sizeof(*indices));
	std::printf("%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures, failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
