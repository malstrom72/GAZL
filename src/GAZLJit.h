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
	GAZLJit is the native baseline JIT for the GAZL VM. This header holds only the arch-neutral public API
	(docs/JitEmitterHandoff.md):

	  - the finalized-opcode enum + `Offsets`, and `JitModule` / `JitProcessor` / `JitCompiler`: the compiled artifact,
	    the engine, and the compiler driver. `JitProcessor` is a `Processor` subclass (§5.1) that overrides the virtual
	    `run()`/`enterCall()`, so it is a polymorphic drop-in for the interpreter — the host loop is identical
	    (`enterCall(); do { resetTimeOut(N); } while (run() == TIME_OUT)`). This depends on `GAZL.h`. Only these two
	    virtual overrides stay inline (they are tiny and must not pull the vtable into the .o); every heavy body —
	    `JitProcessor::layout` and (per backend) `lowerFunction`, `emitDispatcher`, `JitCompiler::compile` — lives in a
	    .cpp.

	The arm64 backend lives beside this in GAZLJitArm64.h / GAZLJitArm64.cpp: the `Arm64Emitter` assembler plus the v1
	lowering pass + native dispatcher that drive it. The x64 emitter and the v2 register allocator (§5.7) are later
	steps.
*/

#ifndef GAZLJit_h
#define GAZLJit_h

#include <stdint.h>
#include <cstddef>
#include <vector>
#include <map>
#include "GAZL.h"
#include "GAZLJitMem.h"			// makeExecutable() — platform-specific backend, architecture-neutral

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

/*
	Runtime probe: will this host actually let us run JIT-compiled code? Two layers, both in GAZLJit.cpp: makeExecutable()
	must succeed (catches macOS' missing allow-jit entitlement and Windows' Arbitrary Code Guard), then a trivial emitted
	stub must run under a fault guard and return its sentinel (catches "the syscalls succeeded but executing the page
	faults"). The result is cached and the call is safe before any compile; the interpreter is always the fallback. Only
	meaningful in a GAZL_JIT build (that is where GAZLJit.cpp is linked).
*/
bool jitAvailable();

/*
	Shared, arch-neutral pass-1 primitive used by both JIT backends (defined in GAZLJit.cpp): if instruction
	`instructionIndex` is a conditional/unconditional branch, report the target instruction index and return true. SWCH
	jump-table targets are not covered here — each backend reads those from the const-memory table itself.
*/
bool jitBranchTarget(const Instruction* code, UInt instructionIndex, UInt& target);

/*
	Fuel safepoints for one function (arch-neutral, defined in GAZLJit.cpp): the basic-block leaders — function entry,
	branch/SWCH targets, and the instruction after any branch/GOTO/SWCH/CALL — with long straight runs split so no block
	exceeds maxBlockWeight. Fills `weight` with leaderIndex -> charge (instruction span to the next safepoint). Both
	backends emit a fuel check charging `weight` at each leader, so the JIT consumes fuel at ~the interpreter's
	1/instruction rate (fuel-rate fidelity) and worst-case time-to-suspend is bounded by maxBlockWeight (§5.5).
*/
const UInt MAX_BLOCK_WEIGHT = 64;				// fuel-check granularity: the host must grant at least this per resume
void jitFuelSafepoints(const Instruction* code, UInt funcStart, UInt endIndex, const Value* memory
		, UInt maxBlockWeight, std::map<UInt, UInt>& weight);

// Byte offsets of the machine state a segment/dispatcher touches, within the (subclass) engine.
struct Offsets {
	uint32_t dsp, mb, fuel, ipsp, resume, saveddsp, natives, nativefn, funcentries, memsize, rwmemsize, dsend, ipsend,
		nativeafter;
};

/*
	The compiled artifact — the JIT's analogue of the interpreter's {code[], functionTable[]}: an executable page's
	dispatcher entry plus the ordinal→native-entry table. Immutable and shareable, so one module can back many
	JitProcessors, on many threads (§5.6). Produced by JitCompiler (which fills it), consumed by JitProcessor's ctor.

	RAII owner: a filled module owns its executable page + entry table and frees them in its destructor, so it must
	outlive every JitProcessor bound to it, and it is non-copyable (two owners would double-free). An empty module
	(default-constructed, or a failed compile) owns nothing — ok() is false and the destructor frees nothing.
*/
class JitModule {
	public:
		JitModule()
			: dispatch(0)
			, nativeEntries(0)
			, codeWordCount(0)
			, ownedPage(0)
			, ownedWords(0) { }
		~JitModule() { if (ownedPage != 0) { freeExecutable(ownedPage, ownedWords); delete[] nativeEntries; } }

		bool ok() const { return dispatch != 0; }			// false if a function used an opcode the backend can't lower
		size_t codeWords() const { return codeWordCount; }	// emitted 32-bit words (for --jit-stats)

	private:
		friend class JitCompiler;			// fills the module
		friend class JitProcessor;			// binds dispatch + nativeEntries

		void* dispatch;						// native dispatcher trampoline entry
		void** nativeEntries;				// ordinal -> native function entry (enterCall / indirect CALL_VVC)
		size_t codeWordCount;				// emitted 32-bit words
		void* ownedPage;					// executable page owned here (0 = empty module; frees nothing)
		size_t ownedWords;					// page size in words, for freeExecutable

		JitModule(const JitModule&);					// non-copyable — it owns an executable page
		JitModule& operator=(const JitModule&);
};

/*
	The JIT engine — mirrors `Processor`: a `Processor` subclass over the shared machine state (§5.1), constructed FROM a
	JitModule (as `Processor` is from `code`/`functionTable`) plus the same run state. It overrides the virtual
	run()/enterCall(), so it is a polymorphic drop-in — the host loop is identical to the interpreter's.
*/
class JitProcessor : public Processor {
	private:
		Value* savedDsp;					// dsp saved across a native call (the C1 window is transient)
		void* nativeFn;						// resolved native fn pointer, blr'd by the native dispatcher
		void* nativeAfter;					// after-call continuation (dispatcher sets RESUME to it on native OK)
		void** funcEntries;					// ordinal -> native entry (bound from the JitModule)
		void* jitDispatch;					// the native dispatcher trampoline (bound from the JitModule)

		// Bind the compiled module + zero the native-call scratch. Shared by both constructors (C++03 has no delegation).
		void bindModule(const JitModule& module) {
			savedDsp = 0; nativeFn = 0; nativeAfter = 0;
			funcEntries = module.nativeEntries; jitDispatch = module.dispatch;
		}

	public:
		/*
			Two constructors, mirroring the base Processor's two: the higher-level one (data stack = the whole span
			between globals and constants) and the lower-level one (explicit rwMemorySize / dataStackOffset /
			dataStackSize, for running several engines over one shared code image — e.g. a JitProcessor per thread, each
			with its own data + ip stack; the compiled code is immutable after publish, so it is safe to share). Both add
			the JitModule up front and forward an optional userData through to the Processor.
		*/
		JitProcessor(const JitModule& module, UInt codeSize, const Instruction* code, UInt functionCount
					, const UInt* functionTable, UInt memorySize, Value* memory, UInt globalsSize, UInt constsSize
					, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* natives, void* userData = 0)
			: Processor(codeSize, code, functionCount, functionTable, memorySize, memory, globalsSize, constsSize
				, ipStackSize, ipStack, natives, userData) { bindModule(module); }

		JitProcessor(const JitModule& module, UInt codeSize, const Instruction* code, UInt functionCount
					, const UInt* functionTable, UInt memorySize, Value* memory, UInt rwMemorySize, UInt dataStackOffset
					, UInt dataStackSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* natives
					, void* userData = 0)
			: Processor(codeSize, code, functionCount, functionTable, memorySize, memory, rwMemorySize, dataStackOffset
				, dataStackSize, ipStackSize, ipStack, natives, userData) { bindModule(module); }

		/*
			The field ABI JitCompiler bakes into the machine code (byte offsets of dsp/memoryBase/... in a JitProcessor).
			Static: the layout is instance-independent (single inheritance, fixed struct), so no engine is needed (see .cpp).
		*/
		static Offsets layout();

		/*
			Polymorphic drop-in for the base Processor (§5.1). enterCall seeds the RESUME continuation with the callee's
			compiled entry; run() is one trip through the native dispatcher (mid-run GAZL/native calls stay inside it).
			Host loop, identical to the interpreter's: enterCall(); do { resetTimeOut(N); } while (run()==TIME_OUT).
		*/
		virtual Status enterCall(Pointer functionPointer) {
			const Status s = Processor::enterCall(functionPointer);
			if (s != OK) { return s; }
			resume = funcEntries[functionPointer - IP_OFFSET];
			return OK;
		}
		virtual Status run() {
			typedef int (*Disp)(JitProcessor*);
			return static_cast<Status>(reinterpret_cast<Disp>(jitDispatch)(this));
		}
};

/*
	The lowering pass (lowerFunction) and dispatcher emitter (emitDispatcher) are internal to GAZLJit.cpp (file-static),
	used only by JitCompiler::compile below.
*/

/*
	The JIT compiler — mirrors `Assembler`: lowers a whole finalized program to native code and fills a JitModule. It
	takes the program (the `Instruction[]` + functionTable + the const memory image, read only for SWCH jump tables) —
	never a processor. Bind the result by constructing a JitProcessor from it. The out module has ok()==false if any
	function hits an opcode the backend can't lower yet (caller falls back to the interpreter). arm64 only.
*/
class JitCompiler {
	public:		void compile(const Instruction* code, UInt functionCount, const UInt* functionTable
						, const Value* memory, JitModule& out);		// fills `out`; check out.ok() (mirrors Assembler::finalize's out-params)

	// Shared publish step (defined in GAZLJit.cpp): makeExecutable the emitted words, then fill `out`'s page + native
	// entry table + dispatch entry from byte offsets. Both backends call this after emitting; leaves `out` empty (so
	// ok() stays false) if the page can't be made executable. Offsets are in bytes (arm64 converts its word offsets).
	protected:	static void publishModule(JitModule& out, const uint32_t* codeWords, size_t wordCount
						, const size_t* entryByteOffsets, UInt functionCount, size_t dispatchByteOffset);
};

} // namespace GAZL

#endif
