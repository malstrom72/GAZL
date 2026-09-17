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
	(design/jit/JitEmitterHandoff.md):

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
#include <set>
#include "GAZL.h"
#include "GAZLOpcodes.h"			// the ONE opcode enum, shared with the assembler/interpreter
#include "GAZLJitMem.h"			// makeExecutable() - platform-specific backend, architecture-neutral

namespace GAZL {

const int TRANSFER = 1;							// segment-to-segment transfer sentinel (no GAZL status is +1)
const int NATIVE_CALL = 2;						// "invoke a native, then continue" sentinel
const int BLOCK_RETRY = 5;						// a native returns this to suspend-and-retry (host policy; §5.4 blocking retry)

/*
	Finalized opcodes. These are ALIASES for the one enum in GAZLOpcodes.h, never a copy of it: every name
	below is checked by the compiler against a real enumerator, and every VALUE comes from the engine this
	JIT is compiled against. The hand-copied mirror this replaced spelled the values out as `0x2345 + N`,
	which is correct for exactly one numbering - insert an opcode mid-enum and each one lowers as its
	neighbour, with no compile error and no exception. See the two rules at the top of GAZLOpcodes.h.
*/
enum {
	OP_FUNC = FUNC_CC_, OP_CALL_VVC = CALL_VVC, OP_CALL_CVC = CALL_CVC, OP_CALL_NVC = CALL_NVC, OP_RETU = RETU_C__,
	OP_TAIL_CC = TAIL_CC_, OP_TAIL_VC = TAIL_VC_,									// GAZL 2 tail calls (absent from a v1 stream, but always numbered)
	OP_MOVE_VV = MOVE_VV_, OP_MOVE_VC = MOVE_VC_, OP_PEEK_VC = PEEK_VC_, OP_POKE_CV = POKE_CV_,
	OP_POKE_CC = POKE_CC_, OP_PEEK_VVV = PEEK_VVV, OP_PEEK_VCV = PEEK_VCV, OP_POKE_VVV = POKE_VVV,
	OP_POKE_CVV = POKE_CVV, OP_POKE_VVC = POKE_VVC, OP_POKE_CVC = POKE_CVC, OP_GETL_VVV = GETL_VVV,
	OP_SETL_VVV = SETL_VVV, OP_SETL_VVC = SETL_VVC, OP_ADRL = ADRL_VV_, OP_ABSI = ABSI_VV_, OP_ADDI_VVV = ADDI_VVV,
	OP_ADDI_VVC = ADDI_VVC, OP_SUBI_VVV = SUBI_VVV, OP_SUBI_VVC = SUBI_VVC, OP_SUBI_VCV = SUBI_VCV,
	OP_MULI_VVV = MULI_VVV, OP_MULI_VVC = MULI_VVC, OP_DIVI_VVV = DIVI_VVV, OP_DIVI_VVC = DIVI_VVC,
	OP_DIVI_VCV = DIVI_VCV, OP_MODI_VVV = MODI_VVV, OP_MODI_VVC = MODI_VVC, OP_MODI_VCV = MODI_VCV,
	OP_ANDI_VVV = ANDI_VVV, OP_ANDI_VVC = ANDI_VVC, OP_IORI_VVV = IORI_VVV, OP_IORI_VVC = IORI_VVC,
	OP_XORI_VVV = XORI_VVV, OP_XORI_VVC = XORI_VVC, OP_SHLI_VVV = SHLI_VVV, OP_SHLI_VVC = SHLI_VVC,
	OP_SHLI_VCV = SHLI_VCV, OP_SHRI_VVV = SHRI_VVV, OP_SHRI_VVC = SHRI_VVC, OP_SHRI_VCV = SHRI_VCV,
	OP_SHRU_VVV = SHRU_VVV, OP_SHRU_VVC = SHRU_VVC, OP_SHRU_VCV = SHRU_VCV, OP_ABSF = ABSF_VV_, OP_FLOF = FLOF_VV_,
	OP_ADDF_VVV = ADDF_VVV, OP_ADDF_VVC = ADDF_VVC, OP_SUBF_VVV = SUBF_VVV, OP_SUBF_VVC = SUBF_VVC,
	OP_SUBF_VCV = SUBF_VCV, OP_MULF_VVV = MULF_VVV, OP_MULF_VVC = MULF_VVC, OP_DIVF_VVV = DIVF_VVV,
	OP_DIVF_VVC = DIVF_VVC, OP_DIVF_VCV = DIVF_VCV, OP_FTOI_VVC = FTOI_VVC, OP_ITOF_VVC = ITOF_VVC,
	OP_COPY_VVC = COPY_VVC, OP_COPY_VCC = COPY_VCC, OP_COPY_CVC = COPY_CVC, OP_COPY_CCC = COPY_CCC,
	OP_FORi_VVB = FORi_VVB, OP_FORi_VCB = FORi_VCB, OP_LSSI_VVB = LSSI_VVB, OP_LSSI_VCB = LSSI_VCB,
	OP_LSSI_CVB = LSSI_CVB, OP_EQUI_VVB = EQUI_VVB, OP_EQUI_VCB = EQUI_VCB, OP_NLSI_VVB = NLSI_VVB,
	OP_NLSI_VCB = NLSI_VCB, OP_NLSI_CVB = NLSI_CVB, OP_NEQI_VVB = NEQI_VVB, OP_NEQI_VCB = NEQI_VCB,
	OP_LSSF_VVB = LSSF_VVB, OP_LSSF_VCB = LSSF_VCB, OP_LSSF_CVB = LSSF_CVB, OP_EQUF_VVB = EQUF_VVB,
	OP_EQUF_VCB = EQUF_VCB, OP_NLSF_VVB = NLSF_VVB, OP_NLSF_VCB = NLSF_VCB, OP_NLSF_CVB = NLSF_CVB,
	OP_NEQF_VVB = NEQF_VVB, OP_NEQF_VCB = NEQF_VCB, OP_GOTO = GOTO_B__, OP_SWCH = SWCH_VCC
};

/*
	Every finalized opcode must be lowered by BOTH backends, so gaining one has to break the BUILD rather
	than the generated code. If this fires: add a `case` for the new opcode to GAZLJitX64.cpp and
	GAZLJitArm64.cpp (and to isCacheLowered / operandRoles if it touches frame slots), then update the count.
*/
static_assert(FINALIZED_OPCODE_COUNT == 93, "a finalized opcode was added or removed - both JIT backends need a case for it");

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
	uint32_t dsp, mb, fuel, ipsp, resume, natives, funcentries, memsize, rwmemsize, dsend, ipsend,
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
		virtual Value* pushCall(Pointer functionPointer);	// native->GAZL forward over compiled continuations: links ctx.nativeAfter into a plain frame and retargets it at the pushed callee's entry (LIFO chains; see GAZLJit.cpp). Same contract as Processor::pushCall.

	private:
		/*
			Bind the compiled module + zero the native-call scratch. Shared by both constructors (C++03 has no delegation).
			Precondition: the module holds compiled code (an empty module has no dispatcher to run). Defined in GAZLJit.cpp.
		*/
		void bindModule(const JitModule& module);

		void* nativeAfter;					// the redirectable OK continuation of the ACTIVE native call: preset to the call site's `after` label, retargeted by pushCall, zeroed by `after` (doubles as the "inside a native call" guard); isolated across nested run() by JitProcessor::run
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
					v2.2 residency qualification: the loop headers whose every in-edge re-establishes a fixed entry state -
					the natural fall-through plus backward GOTO / conditional branches, which reconcile to the captured map.
					A leader targeted by any FORWARD branch or by SWCH is excluded (those edges arrive with an empty cache).
					Fills `loopExtent` with header -> index of its LAST back-edge (the loop body span, for slot-set pruning).
					At a qualified header the backend captures instead of clearing; at a backward branch it reconciles.
				*/
				static void jitResidencyLeaders(const Instruction* code, UInt funcStart, UInt endIndex
						, const Value* memory, std::map<UInt, UInt>& loopExtent);

				/*
					A backend hit a finalized opcode it does not cover - a programmer error (every backend must lower every one),
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
	v2 register allocator - internal to the JIT backends (design/jit/JitCompilerResearch.md §5.7), implemented in GAZLJit.cpp.
	At namespace scope, not nested in JitCompiler, so non-subclass code (each backend's fill/spill helper, the
	GAZLJitLowerTest mock) can implement RegisterCacheBackend.
*/

// Which register file a value is in; chosen per def, since GAZL transients are typeless. A spill is a class-agnostic word store.
enum RegisterClass {
	GENERAL_REGISTER,
	FLOAT_REGISTER
};

/*
	The registers the cache may allocate, per class: everything not pinned by the §5.3 segment ABI. Values are the
	backend's own Reg encodings widened to int. Each backend owns one pool; caller-saved is fine (all flushed at calls).
*/
struct RegisterPool {
	const int* generalRegisters;
	size_t generalCount;
	const int* floatRegisters;
	size_t floatCount;
};

// The cache's one arch-specific service: fill/spill a register from/to a slot's frame home (the backends' loadSlot/storeSlot).
class RegisterCacheBackend {
	public:		virtual void emitFill(int physicalRegister, Int slot, RegisterClass registerClass) = 0;
				/*
					Move a slot's value straight from one register file to the other, replacing the FILL of a
					spill-then-fill pair. GAZL's PEEK/POKE are UNTYPED word moves, so a float that arrives from memory
					lands in the general file and every float op on it then wanted the other file; `a slot lives in one
					file at a time` (evictOtherClass) made that a store plus a load, with the store-to-load latency
					landing inside the dependency chain. A Value is a 32-bit word and this is a bit copy, so the classes
					are interchangeable here: movd on x64, fmov on arm64.
				*/
	public:		virtual void emitCrossMove(int dstRegister, RegisterClass dstClass, int srcRegister) = 0;
	public:		virtual void emitSpill(Int slot, int physicalRegister, RegisterClass registerClass) = 0;
				/*
					Whether the SPILL half of that pair should stay, written from the source register before the move so
					the bridged line arrives clean.

					This is a genuine per-backend split, not a tuning knob anyone should collapse. The store happens
					either way - eagerly here, or later wherever the dirty line is spilled - and the ONLY difference is
					where it sits and which domain it issues in. The two backends measured OPPOSITE signs on the same
					kernels, so one rule cannot serve both (min_ms, --bench=10 --warmup=3, JIT against JIT):

					                       spectralnorm            sor
					  x64 (Zen 4 7950X)    eager 89.35            eager 68.10
					                       deferred 90.39 (+1.2%) deferred 68.02 (-0.1%)
					  arm64 (Apple Si)     eager 53.22 (+6.9%)    eager 95.24 (+5.1%)
					                       deferred 49.79         deferred 90.64

					READ EACH ROW ON ITS OWN. The two rows come from different machines and different sessions, so
					only the WITHIN-ROW comparison is meaningful - nothing follows from x64's sor being faster than
					arm64's, or the reverse. Absolute numbers drift: two fresh builds of ONE arm64 commit, hours apart the same
					day, measured sor at 90.6 ms and 83.7 ms. And best-of-N min is noisier than it looks on a loaded machine -
					byte-identical arm64 code measured 3.4% apart across two builds of one kernel, drifting upward
					through the run. Treat anything under about 3% there as nothing unless the PER-ROUND ranges
					separate, which is how both decisions above were actually settled (eager 53.22-54.56 against
					deferred 49.79-50.95 on spectralnorm; 95.24-95.93 against 90.64-91.74 on sor).

					x64 wants it eager: the deferred store lands on the block's back edge AND changes domain, an
					integer `mov [home], r10d` becoming an FP `movss [home], xmm3`, competing with the mulss/addss the
					loop is already issuing. arm64 wants it deferred: grouping the store with the other tail stores in
					the float domain beats an eager integer store sitting mid-loop between the load and the fmov. No
					microarchitectural explanation is offered for the arm64 side - it is measured, and the obvious
					hypothesis (that the store-to-load pair at one address was the expensive part) was tested and
					refuted, since writing the home eagerly removes that pair and still loses.

					Static counts do not predict this. In sor the eager form has FEWER stores in total than the
					deferred one (62 against 64) and is still 5% slower on arm64, so where the stores execute matters
					more than how many exist. Re-measure rather than reason if this is ever revisited.
				*/
	public:		virtual bool bridgeWritesHome() const = 0;
	public:		virtual ~RegisterCacheBackend() { }
};

// slot -> ascending instruction indices where the slot is READ (the JIT builds one per function; see setUseSchedule).
typedef std::map<Int, std::vector<UInt> > UseSchedule;

/*
	A snapshot of which slots are register-resident (v2.2 cross-block residency): the fixed entry state of a loop header.
	Captured at the header's fall-through entry (pruned to the slots the loop actually reads), reconciled to at every
	back-edge, spilled by the header's suspend stub and refilled by its resume trampoline. `expectDirty` marks slots the
	loop WRITES: they are modeled dirty from iteration one (the redefined value really is dirty when the header is
	reached again, and a clean model would let an eviction skip the store and lose it). Slots the loop only reads are
	kept register==home (canonicalized by one store at capture if needed), so hazard flushes inside the loop never
	re-store them.
*/
struct ResidencyMap {
	struct Entry {
		Int slot;
		RegisterClass registerClass;
		int poolIndex;								// index into the RegisterPool's class array
		int physicalRegister;						// the register itself, for backend stubs (suspend spill / resume fill)
		bool expectDirty;							// the loop writes this slot: model dirty at the header
	};
	std::vector<Entry> entries;
};

// Opcodes whose operands route through the cache; everything else barriers the cache and lowers as v1 (§5.7).
bool isCacheLowered(Int op);

// SWCH `instructionIndex`'s jump-table targets: absolute instruction indices, in table order.
std::vector<UInt> switchTargets(const Instruction* code, UInt instructionIndex, const Value* memory);

// Scan code[from..to] and record every slot READ per instruction (uses GAZL::operandRoles) into `schedule` (Belady input).
void buildUseSchedule(const Instruction* code, UInt from, UInt to, UseSchedule& schedule);

// Scan a loop body code[from..to] once for the slots it reads / writes (residency pruning + expectDirty; see
// ResidencyMap) and for the same slots split by REGISTER CLASS, mirroring the backends' lowering choices (float
// arithmetic / compares / ABSF / FLOF and the float halves of FTOI/ITOF use FLOAT_REGISTER; everything else, incl
// MOVE, uses GENERAL). The class sets feed the multi-block residency pressure gate: a per-class overflow of
// capture()'s keepMax thrashes the map.
void buildLoopSets(const Instruction* code, UInt from, UInt to, std::set<Int>& readSlots, std::set<Int>& writtenSlots
		, std::set<Int>& generalSlots, std::set<Int>& floatSlots);

// A leader's residency map = the loop's fixed bindings FILTERED to the slots LIVE-IN at that leader (v2.2 varying maps:
// dead bindings free their registers for body temps; same slot -> same register everywhere, so edges never need moves).
void filterResidencyMap(const ResidencyMap& full, const std::set<Int>& liveIn, ResidencyMap& out);

/*
	Pointer-realm stamp (§1.1, v2.3a): the coarse realm of the pointer VALUE a slot holds, w.r.t. THIS frame's cached
	slots. NONFRAME = provably not this frame (a received parameter pointer, or a globals/constants symbol address) -> a
	`PEEK`/`POKE` through it cannot alias a cached local, so its flush is skipped. MYFRAME = an ADRL-of-local pointer (or
	arithmetic on one) -> may alias, must flush. UNKNOWN = loaded from memory, a call result, or a join of the two -> must
	flush (conservative, = v2.0). BOTTOM = no pointer definition reached the slot. Sound on all GAZL 1.0 (realms are a
	language guarantee); the fuzzer's pointer-passing / cross-realm arms police it.
*/
enum PointerRealm { REALM_BOTTOM = 0, REALM_NONFRAME = 1, REALM_MYFRAME = 2, REALM_UNKNOWN = 3 };

// Stamp each slot's pointer realm over code[from..to] (one function): live-in pointer slots (params) = NONFRAME, ADRL =
// MYFRAME, propagated through MOVp/ADDp/SUBp, joined to a fixed point. Absent key => REALM_BOTTOM (treat as must-flush).
void buildPointerRealms(const Instruction* code, UInt from, UInt to, std::map<Int, int>& realm);

/*
	Per-instruction live-in sets over code[from..to] (one function): slot s is live-in at j iff some path from j reads s
	before writing it. Standard backward dataflow over the successor graph (fall-through + branch/SWCH targets from the
	same successor model jitFuelSafepoints uses); gen = slots read, kill = slots written (a FORi counter is both). Foundation
	for v2.2-full cross-block residency: a leader's entry-residency candidates are its live-in slots. `memory` supplies the
	SWCH jump table. liveIn[j] holds the set for instruction j.
*/
void buildLiveIn(const Instruction* code, UInt from, UInt to, const Value* memory, std::map<UInt, std::set<Int> >& liveIn);

/*
	v2.0 floating register cache (§5.7.1): a per-function write-back cache of frame slots. The opcode switch routes
	operands through read/define/scratch and calls the coherence events below; correctness never rests on the aliasing
	spec because memory is made current at every pointer op, block boundary, and call.

	Eviction is v2.0.5 block-local Belady when a use schedule is supplied (evict the resident line whose next read is
	furthest, dead lines first), else LRU. Belady only picks WHICH line to spill, so an imprecise schedule stays correct.
*/
class RegisterCache {
	public:		RegisterCache(const RegisterPool& pool, RegisterCacheBackend& backend);

	// Belady inputs (optional): the per-function next-read schedule, and the scan position advanced per instruction.
	public:		void setUseSchedule(const UseSchedule* schedule) { useSchedule = schedule; }
	public:		void setInstructionIndex(UInt index) { instructionIndex = index; }

	// read/define/scratch pin their register for the current instruction; endInstruction releases the pins.
	public:		int read(Int slot, RegisterClass registerClass);		// fills on a miss
	public:		int define(Int slot, RegisterClass registerClass);		// dirty, no store at the def
	public:		int scratch(RegisterClass registerClass);				// no home; dropped at endInstruction
	public:		void endInstruction();

	public:		void enterBlock();				// leader: map starts empty
	public:		void spillDirtyResident();		// before a pointer READ / back-edge: flush dirty, keep resident
	public:		void captureDirtyLines(ResidencyMap& map) const;	// snapshot for a terminal trap arm; no model change
	public:		void invalidateAll();			// after a pointer WRITE: flush dirty + drop all
	public:		void barrier();					// branch / fall-through to leader / CALL / RETU: flush dirty + drop all

	public:		void evict(int physicalRegister);	// x64 fixed-register ops (idiv/shift/rep); no-op on arm64
	public:		bool isResident(Int slot) const;

	// v2.2 loop-header residency: establish the header's entry state (varying maps: wanted = read-in-loop, live-in at
	// the header, single-class; resident wanted lines are kept + dirtiness-canonicalized, absent ones PRELOADED into
	// free lines up to keepMax), later re-establish it at every in-edge.
	public:		void capture(ResidencyMap& map, const std::set<Int>& wantedGeneral, const std::set<Int>& wantedFloat, const std::set<Int>& writtenInLoop);
	public:		void residencyCapacity(size_t& generalMax, size_t& floatMax) const;	// max entries capture() keeps per class; the multi-block pressure gate
	public:		void reconcileTo(const ResidencyMap& map);		// at a back-edge: spill/drop strays, fill missing; empty when equal

	private:	static const size_t POOL_CAPACITY = 32;
	private:	struct Line {
					Int slot;
					RegisterClass registerClass;
					bool occupied;
					bool dirty;						// register copy diverged from the home
					bool pinned;					// operand of the current instruction; not evictable this cycle
					bool scratchTemp;				// no home: never spilled, dropped at endInstruction
					uint32_t lastUse;				// LRU stamp
					uint32_t nextUse;				// Belady: instruction index of this slot's next READ (UINT32_MAX = none)
				};

	private:	Line* linesOf(RegisterClass registerClass, size_t& count);
	private:	const int* registersOf(RegisterClass registerClass) const;
	private:	int acquire(RegisterClass registerClass);	// a pool index ready to (re)assign, evicting a line (Belady or LRU) if full
	private:	uint32_t nextReadAfter(Int slot) const;		// next scheduled read of `slot` strictly after instructionIndex
	private:	void evictOtherClass(Int slot, RegisterClass wantedClass, bool spillFirst);	// a slot lives in one file at a time
	private:	void spillLine(RegisterClass registerClass, int index);	// store it if dirty + has a home, then mark clean
	private:	void flushAndClear();						// spill all dirty, then drop every mapping
	private:	void assertNoDirty() const;					// debug contract check for enterBlock

	private:	const RegisterPool& registerPool;
	private:	RegisterCacheBackend& cacheBackend;
	private:	Line generalLines[POOL_CAPACITY];			// parallels registerPool.generalRegisters[0..generalCount)
	private:	Line floatLines[POOL_CAPACITY];				// parallels registerPool.floatRegisters[0..floatCount)
	private:	uint32_t useClock;							// increments on every access; stamps Line::lastUse for LRU
	private:	const UseSchedule* useSchedule;				// Belady next-read lists (0 = fall back to LRU eviction)
	private:	UInt instructionIndex;						// current scan position, for nextReadAfter
};

// A branch edge's cache handoff: a qualified target re-establishes its fixed entry state, anything else gets the v2.0 flush.
inline void reconcileOrBarrier(RegisterCache& cache, std::map<UInt, ResidencyMap>& entryMaps, UInt target) {
	std::map<UInt, ResidencyMap>::iterator it = entryMaps.find(target);
	if (it != entryMaps.end()) { cache.reconcileTo(it->second); } else { cache.barrier(); }
}

/*
	Leader entry, shared by both backends at every block leader before its label is bound. A FRESH loop header (not
	already an interior leader of an enclosing resident loop) captures its residency map - wanted bindings, per-class
	pressure gate, the filtered maps of its interior leaders - and marks [header, extent] resident. Any other leader, or
	a gated header, reconciles to its entry map if it has one, else barriers. `j` is the leader's instruction index.
*/
void establishLeader(RegisterCache& cache, const Instruction* code, UInt j, const std::map<UInt, UInt>& loopExtent
		, const std::map<UInt, UInt>& loopWeight, std::map<UInt, std::set<Int> >& liveIn
		, std::map<UInt, ResidencyMap>& entryMaps, bool& resident, UInt& residentEnd);

/*
	A conditional branch's cache transition, shared by both backends. An in-loop edge (the target has an entry map)
	reconciles inline - loads/stores leave the flags, so this may sit between compare and branch. A loop exit from a
	resident map captures the dirty lines into `dirty` and returns true: the caller branches to a ColdEdge stub that
	spills on the TAKEN path only. Anything else barriers. False means: branch straight to the target's label.
*/
bool planConditionalEdge(RegisterCache& cache, std::map<UInt, ResidencyMap>& entryMaps, UInt target, bool resident
		, ResidencyMap& dirty);

} // namespace GAZL

#endif
