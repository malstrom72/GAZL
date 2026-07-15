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
	    `run()`/`enterCall()`, so it is a polymorphic drop-in for the interpreter - the host loop is identical
	    (`enterCall(); do { resetTimeOut(N); } while (run() == TIME_OUT)`). This depends on `GAZL.h`. Every body -
	    the two virtual overrides, `JitProcessor::layout`, and (per backend) `lowerFunction`, `emitDispatcher`,
	    `JitCompiler::compile` - lives in a .cpp.

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
#include "GAZLJitMem.h"			// makeExecutable() - platform-specific backend, architecture-neutral

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

// Byte offsets of the machine state a segment/dispatcher touches, within the (subclass) engine.
struct Offsets {
	uint32_t dsp, mb, fuel, ipsp, resume, saveddsp, natives, nativefn, funcentries, memsize, rwmemsize, dsend, ipsend,
		nativeafter;
};

/*
	Thrown when the JIT itself fails at runtime - e.g. the host refuses to make a code page executable even though
	jitAvailable() reported the capability (a broken invariant / resource exhaustion, not an assembler error). A
	GAZL::Exception subclass, so a host that already catches GAZL::Exception catches it too; it carries a free-form
	message instead of an AssemblerError.
*/
class JitException : public Exception {
	public:		JitException(const std::string& message) : failureMessage(message) { }
	public:		virtual ~JitException() throw() { }
	public:		virtual const char* what() const throw() { return failureMessage.c_str(); }
	private:	std::string failureMessage;
};

/*
	A backend's raw output, before it is made executable: the emitted machine-code words, each function ordinal's entry as
	a byte offset into them, and the dispatcher's byte offset. A plain value (its vectors own themselves); JitCompiler
	turns it into an executable JitModule.
*/
struct EmittedModule {
	std::vector<uint32_t> code;
	std::vector<size_t> entryByteOffsets;		// per function ordinal
	size_t dispatchByteOffset;
	EmittedModule() : dispatchByteOffset(0) { }
};

/*
	The compiled artifact - the JIT's analogue of the interpreter's {code[], functionTable[]}: an executable page, its
	dispatcher entry, and the ordinal→entry table the machine code indexes. Immutable and shareable, so one module can
	back many JitProcessors, on many threads (§5.6).

	RAII value type. A default-constructed module is empty (isCompiled() == false) and owns nothing. JitCompiler::compile
	builds a filled module and hands it over via swap() - there is no half-built state and nothing sets its fields from
	outside. It owns a unique executable page, so it is non-copyable; transfer ownership with swap(). It must outlive every
	JitProcessor bound to it.
*/
class JitModule {
	public:
		JitModule() : ownedPage(0), ownedWords(0), dispatch(0) { }		// empty - owns nothing
		/*
			Make an emitted module's code executable and take ownership of the page (acquisition == initialization).
			Throws GAZL::JitException if the host refuses executable memory (jitAvailable() gates this).
		*/
		explicit JitModule(const EmittedModule& emitted);
		~JitModule();													// frees the page (a no-op when empty)
		void swap(JitModule& other);									// O(1) - exchange ownership

		bool isCompiled() const { return dispatch != 0; }				// holds a runnable artifact (a value-state query)
		size_t codeWords() const { return ownedWords; }					// emitted 32-bit words (for --jit-stats)
		void* dispatchEntry() const { return dispatch; }					// native dispatcher entry (JitProcessor binds this)
		void* const* entryTable() const { return entries.empty() ? 0 : &entries[0]; }	// ordinal -> entry; the machine code indexes it

	private:
		void* ownedPage;					// executable page owned here (0 = empty)
		size_t ownedWords;					// page size in words, for freeExecutable
		std::vector<void*> entries;			// ordinal -> entry (page + byte offset)
		void* dispatch;						// native dispatcher entry (page + byte offset)

		JitModule(const JitModule&);					// non-copyable - owns a unique page; transfer via swap
		JitModule& operator=(const JitModule&);
};

inline void swap(JitModule& a, JitModule& b) { a.swap(b); }				// ADL swap

/*
	The JIT engine - mirrors `Processor`: a `Processor` subclass over the shared machine state (§5.1), constructed FROM a
	JitModule (as `Processor` is from `code`/`functionTable`) plus the same run state. It overrides the virtual
	run()/enterCall(), so it is a polymorphic drop-in - the host loop is identical to the interpreter's.
*/
class JitProcessor : public Processor {
	public:
		/*
			Two constructors, mirroring the base Processor's two: the higher-level one (data stack = the whole span
			between globals and constants) and the lower-level one (explicit rwMemorySize / dataStackOffset /
			dataStackSize, for running several engines over one shared code image - e.g. a JitProcessor per thread, each
			with its own data + ip stack; the compiled code is immutable after publish, so it is safe to share). Both add
			the JitModule up front and delegate to the matching Processor constructor.
		*/
		JitProcessor(const JitModule& module, const AssembledProgram& program, UInt ipStackSize
					, CallStackEntry* ipStack, NativeFunc const* natives, void* userData = 0)
			: Processor(program, ipStackSize, ipStack, natives, userData) { bindModule(module); }

		JitProcessor(const JitModule& module, const AssembledProgram& program, UInt rwMemorySize
					, UInt dataStackOffset, UInt dataStackSize, UInt ipStackSize, CallStackEntry* ipStack
					, NativeFunc const* natives, void* userData = 0)
			: Processor(program, rwMemorySize, dataStackOffset, dataStackSize, ipStackSize, ipStack, natives
				, userData) { bindModule(module); }

		/*
			The field ABI JitCompiler bakes into the machine code (byte offsets of dsp/memoryBase/... in a JitProcessor).
			Static: the layout is instance-independent (single inheritance, fixed struct), so no engine is needed (see .cpp).
		*/
		static Offsets layout();

		/*
			Polymorphic drop-in for the base Processor (§5.1). enterCall seeds the RESUME continuation with the callee's
			compiled entry; run() is one trip through the native dispatcher (mid-run GAZL/native calls stay inside it).
			Host loop, identical to the interpreter's: enterCall(); do { resetTimeOut(N); } while (run()==TIME_OUT).
			Both are defined in GAZLJit.cpp.
		*/
		virtual Status enterCall(Pointer functionPointer);
		virtual Status run();

	private:
		/*
			Bind the compiled module + zero the native-call scratch. Shared by both constructors (C++03 has no delegation).
			Precondition: the module holds compiled code (an empty module has no dispatcher to run). Defined in GAZLJit.cpp.
		*/
		void bindModule(const JitModule& module);

		Value* savedDsp;					// dsp saved across a native call (the C1 window is transient)
		void* nativeFn;						// resolved native fn pointer, blr'd by the native dispatcher
		void* nativeAfter;					// after-call continuation (dispatcher sets RESUME to it on native OK)
		void* const* funcEntries;			// ordinal -> native entry (bound from the JitModule)
		void* jitDispatch;					// the native dispatcher trampoline (bound from the JitModule)
};

/*
	The JIT compiler - the JIT's counterpart of Assembler. Abstract base with one backend subclass per target
	(JitCompilerArm64 / JitCompilerX64), each supplying emit(); the per-instruction lowering pass and dispatcher emitter
	are file-static inside each backend .cpp. It reads an AssembledProgram (Instruction[] + functionTable + the const memory image,
	read only for SWCH jump tables) - never a processor - and holds no program itself, so one compiler compiles many.
	Obtain the host's backend via NativeJitCompiler (below); a build links only the backend(s) it includes.
*/
class JitCompiler {
	public:		virtual ~JitCompiler() { }

				/*
					Compile `program` into `out` (transferred in via swap); on return `out` is isCompiled(). A backend
					covers every finalized opcode, so lowering a valid finalized program always succeeds - the only
					failures are exceptional and both throw GAZL::JitException (leaving `out` unchanged): the host refusing
					executable memory (call jitAvailable() first to avoid it), or a finalized opcode left unlowered (a bug).
				*/
				virtual void compile(const AssembledProgram& program, JitModule& out) = 0;

	protected:	/*
					Arch-neutral lowering helpers for the backend subclasses (defined in GAZLJit.cpp).
					Fuel safepoints for one function: the basic-block leaders (function entry, branch/SWCH targets, the
					instruction after any branch/GOTO/SWCH/CALL, with long straight runs split so none exceeds the internal
					fuel-check granularity), filled into `weight` as leader -> charge; each backend emits a fuel check
					charging `weight` at each leader, so the JIT spends fuel at ~the interpreter's 1/instruction rate (§5.5).
				*/
				static void jitFuelSafepoints(const Instruction* code, UInt funcStart, UInt endIndex
						, const Value* memory, std::map<UInt, UInt>& weight);

				/*
					A backend hit a finalized opcode it does not cover - a programmer error (every backend must lower all 91),
					not a runtime condition. asserts (loud in debug) and, because asserts vanish in release, also throws
					GAZL::JitException so a release build degrades to the interpreter rather than emitting wrong code. Never
					returns; the backends' switch defaults call it.
				*/
				static void throwUnlowerableOpcode(Int opcode);
};

/*
	The host-native JIT compiler: a JitCompiler whose compile() lowers with the backend matching the host arch
	(JitCompilerArm64 / JitCompilerX64). Construct one wherever you need to compile; it holds no state, so instances (and
	threads) are independent. compile() is defined in whichever backend .cpp the host arch selects, so both backends can
	still link together; if the host-matching backend was not built it is absent (name the concrete backend directly).
*/
class NativeJitCompiler : public JitCompiler {
	public:		virtual void compile(const AssembledProgram& program, JitModule& out);	// lowers with the host backend
};

/*
	------------------------------------------------------------------------------------------------------------------
	v2 register allocator - internal to the JIT backends, not client API (docs/JitCompilerResearch.md §5.7). Declared at
	namespace scope (not nested in JitCompiler) because the concrete RegisterCacheBackend is supplied by code that is not
	a JitCompiler subclass - each backend's file-static fill/spill helper and the GAZLJitLowerTest mock - which a
	protected nesting would lock out. Implemented in GAZLJit.cpp.
	------------------------------------------------------------------------------------------------------------------
*/

/*
	Which physical register file a value lives in. Chosen per definition, because GAZL transients are typeless (a `%`
	slot legitimately holds ptr, int, and float in successive defs); locals carry their declared type. The memory image
	is class-agnostic (a spill is a raw word store), so class matters only for picking the register and the op.
*/
enum RegisterClass {
	GENERAL_REGISTER,		// integer / pointer (Wn on arm64, e-/r- on x64)
	FLOAT_REGISTER			// single-precision  (Sn on arm64, xmm on x64)
};

/*
	The physical registers the cache may allocate, per class: the host registers NOT pinned by the §5.3 segment ABI
	(dsp / memoryBase / fuel / ipsp / ctx) and not otherwise reserved. Values are the backend's own register encodings
	(GAZL::Reg) widened to int and opaque to the cache; list them best-first if the arch has a preference. Each backend
	owns one static pool. Everything is flushed at calls, so caller-saved registers are fair game within a block.
	Intended: arm64 general X5-X17/X19-X28 (~22), float S0-S31 (32); x64 general RAX RCX RDX RSI RDI R8-R11 (9), float
	XMM0-XMM15 (16).
*/
struct RegisterPool {
	const int* generalRegisters;
	size_t generalCount;
	const int* floatRegisters;
	size_t floatCount;
};

/*
	The one arch-specific service the cache needs: emit a load/store between a physical register and a frame slot's
	memory home (a Value index off dsp, exactly as the opcode operands carry it). These wrap the existing per-backend
	loadSlot/storeSlot helpers (which already handle near vs far slots). The cache never emits an instruction itself; it
	only decides which register holds what and calls these to fill/spill.
*/
class RegisterCacheBackend {
	public:		virtual void emitFill(int physicalRegister, Int slot, RegisterClass registerClass) = 0;	// reg <- home(slot)
	public:		virtual void emitSpill(Int slot, int physicalRegister, RegisterClass registerClass) = 0;	// home(slot) <- reg
	public:		virtual ~RegisterCacheBackend() { }
};

/*
	v2.0 floating register cache (§5.7.1). One instance per function lowering; the per-opcode switch routes every operand
	through read()/define()/scratch() instead of naming a fixed scratch register, and calls the coherence events at the
	situations the §5.7.1 table names. Lines are floating: filled on first read, dirty-on-write with NO store at the def,
	LRU-evicted under pressure (spilled if dirty), flushed + cleared at every basic-block boundary. Correctness never
	rests on the aliasing spec - memory is made current at every pointer op, boundary, and call - so any GAZL the
	interpreter accepts is handled (v2.0 needs no §1.1 rule at all).

	Two-operand ISAs (x64): the cache hands out independent registers; turning `dst = s1 OP s2` into `mov dst,s1; OP
	dst,s2` stays in the x64 switch. Fixed-register ops (x64 idiv -> EDX:EAX, shifts -> CL, rep movsd -> RSI/RDI/RCX)
	call evict() on the specific registers first; on arm64 evict() finds nothing to do.

	Per-instruction protocol: read()/define()/scratch() each pin their register for the current instruction so a later
	operand in the SAME instruction cannot evict it (up to 3 operands + scratch); call endInstruction() to release the
	pins (and drop the transient scratch lines).
*/
class RegisterCache {
	public:		RegisterCache(const RegisterPool& pool, RegisterCacheBackend& backend);

	// --- per-instruction operand access (each pins its register until endInstruction) ---
	public:		int read(Int slot, RegisterClass registerClass);	// slot's value in a register, filling on a miss
	public:		int define(Int slot, RegisterClass registerClass);	// a register to write slot's new value into (dirty, no store)
	public:		int scratch(RegisterClass registerClass);			// a register holding no live value (address math, boxed constants)
	public:		void endInstruction();								// release this instruction's pins; drop its scratch lines

	// --- coherence events (§5.7.1 event table) ---
	public:		void enterBlock();				// at a block leader: the map starts empty (canonical-memory join)
	public:		void spillDirtyResident();		// store dirty lines, KEEP them resident: before a pointer READ, before a back-edge fuel check
	public:		void invalidateAll();			// spill dirty + drop every mapping: AFTER a pointer WRITE
	public:		void barrier();					// spill dirty + drop every mapping: a branch, a fall-through into a leader, a CALL, a RETU

	// --- x64 fixed-register ops (finds nothing to do on arm64) ---
	public:		void evict(int physicalRegister);	// free a specific physical register (spill if dirty, then drop its line)

	// --- queries ---
	public:		bool isResident(Int slot) const;	// is this slot currently held in a register?

	private:	static const size_t POOL_CAPACITY = 32;	// ceiling for either file (arm64 float uses all 32)
	private:	struct Line {
					Int slot;						// the frame slot this register holds (meaningless when scratchTemp)
					RegisterClass registerClass;	// which file (parallels the pool array this Line sits in)
					bool occupied;					// this pool entry currently holds a value
					bool dirty;						// register copy has diverged from the home (needs a spill to evict)
					bool pinned;					// pinned for the current instruction (cannot be evicted this cycle)
					bool scratchTemp;				// a transient with no home: never spilled, dropped at endInstruction
					uint32_t lastUse;				// LRU stamp (monotonic; smallest = eviction victim)
				};

	private:	Line* linesOf(RegisterClass registerClass, size_t& count);
	private:	const int* registersOf(RegisterClass registerClass) const;
	private:	int acquire(RegisterClass registerClass);	// a pool index ready to (re)assign, evicting the LRU unpinned line if full
	private:	void spillLine(RegisterClass registerClass, int index);	// store it if dirty + has a home, then mark clean
	private:	void flushAndClear();						// spill all dirty, then drop every mapping
	private:	void assertNoDirty() const;					// debug contract check for enterBlock

	private:	const RegisterPool& registerPool;
	private:	RegisterCacheBackend& cacheBackend;
	private:	Line generalLines[POOL_CAPACITY];			// parallels registerPool.generalRegisters[0..generalCount)
	private:	Line floatLines[POOL_CAPACITY];				// parallels registerPool.floatRegisters[0..floatCount)
	private:	uint32_t useClock;							// increments on every access; stamps Line::lastUse for LRU
};

} // namespace GAZL

#endif
