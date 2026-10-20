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
	Arch-neutral public API of the native baseline JIT (design/jit/JitEmitterHandoff.md). `JitProcessor` is a drop-in
	`Processor` subclass run with the interpreter's host loop. The backends live in GAZLJitArm64 and GAZLJitX64, and
	`RegisterCache` below is their shared register allocator.
*/

#ifndef GAZLJit_h
#define GAZLJit_h

#include <stdint.h>
#include <cstddef>
#include <vector>
#include <map>
#include <set>
#include "GAZL.h"
#include "GAZLOpcodes.h"			// the opcode enum shared with the assembler and interpreter
#include "GAZLJitMem.h"			// executable memory, one implementation per OS

namespace GAZL {

const int BLOCK_RETRY = 5;						// a native returns this to suspend and be retried (host policy)

/*
	Finalized opcodes as aliases of the GAZLOpcodes.h enum, never a copy, so every value follows the engine this JIT is
	compiled against. See the two rules at the top of GAZLOpcodes.h.
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
	Both backends must lower every finalized opcode, so a new one breaks the build here. Add its `case` to
	GAZLJitX64.cpp and GAZLJitArm64.cpp (if it touches frame slots, also to isCacheLowered, jitResidencySafe's
	exclusions, operandRoles and buildLoopSets), then update the count.
*/
static_assert(FINALIZED_OPCODE_COUNT == 93, "a finalized opcode was added or removed - both JIT backends need a case for it");

/*
	Whether this host can run JIT-compiled code; when false, use the interpreter. The first call probes (it maps a page
	and runs a stub under temporary fault handlers) and caches the result, so make it from one thread before compiling.
*/
bool jitAvailable();

// Byte offsets of the machine state a segment/dispatcher touches, within the (subclass) engine.
struct Offsets {
	uint32_t dsp, mb, fuel, ipsp, resume, natives, funcentries, memsize, rwmemsize, dsend, ipsend,
		nativeafter;
};

// Thrown when the JIT fails at run time, such as the host refusing executable memory despite jitAvailable().
class JitException : public Exception {
	public:		JitException(const std::string& message) : failureMessage(message) { }
	public:		virtual ~JitException() throw() { }
	public:		virtual const char* what() const throw() { return failureMessage.c_str(); }
	private:	std::string failureMessage;
};

const UInt MAX_JIT_FRAME_WORDS = 0x10000000u;	// compile() refuses locals or params at or above this, so frame byte offsets fit 32 bits

// A backend's raw output before it is made executable; the byte offsets index `code`.
struct EmittedModule {
	std::vector<uint32_t> code;
	std::vector<size_t> entryByteOffsets;		// per function ordinal
	size_t dispatchByteOffset;
	EmittedModule() : dispatchByteOffset(0) { }
};

/*
	The compiled artifact: an executable page, its dispatcher entry and the ordinal to entry table. Immutable once
	built, so one module can back many JitProcessors on many threads, and it must outlive every one bound to it. It owns
	the page, so it is non-copyable; JitCompiler::compile hands it over with swap().
*/
class JitModule {
	public:
		JitModule() : ownedPage(0), ownedWords(0), dispatch(0) { }		// empty, owns nothing
		explicit JitModule(const EmittedModule& emitted);				// throws JitException if the host refuses executable memory
		~JitModule();
		void swap(JitModule& other);

		bool isCompiled() const { return dispatch != 0; }
		size_t codeWords() const { return ownedWords; }					// emitted 32-bit words
		void* dispatchEntry() const { return dispatch; }
		void* const* entryTable() const { return entries.empty() ? 0 : &entries[0]; }	// ordinal -> entry

	private:
		void* ownedPage;					// 0 when empty
		size_t ownedWords;					// page size in words, for freeExecutable
		std::vector<void*> entries;			// ordinal -> entry (page + byte offset)
		void* dispatch;						// native dispatcher entry (page + byte offset)

		JitModule(const JitModule&);					// non-copyable: owns the page
		JitModule& operator=(const JitModule&);
};

inline void swap(JitModule& a, JitModule& b) { a.swap(b); }				// ADL swap

// The JIT engine: a `Processor` subclass bound to a JitModule, driven by the same host loop as the interpreter.
class JitProcessor : public Processor {
	public:
		// The two `AssembledProgram` constructors of `Processor`, plus the compiled `module`, which threads may share.
		JitProcessor(const JitModule& module, const AssembledProgram& program, UInt ipStackSize
					, CallStackEntry* ipStack, NativeFunc const* natives, void* userData = 0)
			: Processor(program, ipStackSize, ipStack, natives, userData) { bindModule(module); }

		JitProcessor(const JitModule& module, const AssembledProgram& program, UInt rwMemorySize
					, UInt dataStackOffset, UInt dataStackSize, UInt ipStackSize, CallStackEntry* ipStack
					, NativeFunc const* natives, void* userData = 0)
			: Processor(program, rwMemorySize, dataStackOffset, dataStackSize, ipStackSize, ipStack, natives
				, userData) { bindModule(module); }

		static Offsets layout();					// the field byte offsets JitCompiler bakes into the machine code

		virtual Status enterCall(Target target);	// seeds the resume continuation with the callee's compiled entry
		virtual Status run();						// one trip through the native dispatcher; GAZL and native calls stay inside it
		virtual Value* pushCall(Target target);	// same contract as Processor::pushCall; retargets `nativeAfter` at the callee's entry

	private:
		void bindModule(const JitModule& module);	// shared by both constructors; `module` must be compiled

		void* nativeAfter;					// OK continuation of the active native call (0 when none): preset to its `after` label, retargeted by pushCall; run() isolates it across nesting
		void* const* funcEntries;			// ordinal -> native entry (bound from the JitModule)
		void* jitDispatch;					// the native dispatcher trampoline (bound from the JitModule)
};

// Compiles an AssembledProgram to a JitModule; one backend subclass per target. Holds no program, so one compiles many.
class JitCompiler {
	public:		virtual ~JitCompiler() { }

				virtual void compile(const AssembledProgram& program, JitModule& out) = 0;	// fills `out` via swap; throws JitException, leaving `out` unchanged, if the host refuses executable memory

	protected:	static void jitFuelSafepoints(const Instruction* code, UInt funcStart, UInt endIndex
						, const Value* memory, std::map<UInt, UInt>& weight);	// fills `weight` with leader -> fuel charge, about one per instruction like the interpreter

				static void jitResidencyLeaders(const Instruction* code, UInt funcStart, UInt endIndex
						, const Value* memory, std::map<UInt, UInt>& loopExtent);	// fills `loopExtent` with header -> last back-edge, for headers no forward branch or SWCH targets

				static void throwUnlowerableOpcode(Int opcode);	// asserts, then throws JitException so a release build falls back to the interpreter
};

// The stateless JitCompiler for the host architecture; it is absent if that backend is not built.
class NativeJitCompiler : public JitCompiler {
	public:		virtual void compile(const AssembledProgram& program, JitModule& out);
};

// Register file of a cached value, chosen per definition since GAZL values are untyped.
enum RegisterClass {
	GENERAL_REGISTER,
	FLOAT_REGISTER
};

/*
	The registers the cache may allocate per class (backend `Reg` encodings): everything the segment ABI does not pin.
	Caller-saved registers are fine, since the cache is flushed at every call.
*/
struct RegisterPool {
	const int* generalRegisters;
	size_t generalCount;
	const int* floatRegisters;
	size_t floatCount;
};

// The cache's one arch-specific service: fill or spill a register from or to a slot's frame home.
class RegisterCacheBackend {
	public:		virtual void emitFill(int physicalRegister, Int slot, RegisterClass registerClass) = 0;
	public:		virtual void emitCrossMove(int dstRegister, RegisterClass dstClass, int srcRegister) = 0;	// bit-copies a slot's value to the other register file, replacing a spill-then-fill pair
	public:		virtual void emitSpill(Int slot, int physicalRegister, RegisterClass registerClass) = 0;
	public:		virtual bool bridgeWritesHome() const = 0;	// whether a cross move also stores the source to the home first; set per backend by measurement (design/jit/JitArm64TailHandoff.md), so re-measure rather than unify
	public:		virtual ~RegisterCacheBackend() { }
};

// slot -> ascending instruction indices where the slot is read, one per function.
typedef std::map<Int, std::vector<UInt> > UseSchedule;

/*
	Which slots are register-resident at a loop header: captured on entry, reconciled to at every back-edge, spilled by
	the suspend stub and refilled on resume. `expectDirty` marks the slots the loop writes, modeled dirty from the first
	iteration so an eviction never skips their store; slots it only reads are kept equal to their home.
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

/*
	Cold-section records per backend `Label` type. A ColdTrap is a checked op's trap, kept off the hot path. A ColdEdge
	is a loop exit from a resident body: its stub stores the dirty lines on the taken path only, then enters the exit
	leader, which assumes an empty cache.
*/
template <class LabelType> struct BasicColdTrap {
	LabelType label;
	ResidencyMap dirty;								// the captureDirtyLines snapshot to store before exiting
	Status status;									// BAD_PEEK / BAD_POKE / DIVISION_BY_ZERO
};
template <class LabelType> struct BasicColdEdge {
	LabelType label;
	ResidencyMap dirty;								// dirty state at the branch point, stored only if the exit is taken
	UInt target;									// the exit leader (mainline label key)
};

// Whether `op` routes its operands through the cache; any other opcode barriers it.
bool isCacheLowered(Int op);

// SWCH jump-table targets as absolute instruction indices, in table order; `memory` is the word at `MEMORY_OFFSET`.
std::vector<UInt> switchTargets(const Instruction* code, UInt instructionIndex, const Value* memory);

// Fills `schedule` with the slots read by each instruction in code[from..to], the input to Belady eviction.
void buildUseSchedule(const Instruction* code, UInt from, UInt to, UseSchedule& schedule);

/*
	Collects the slots a loop body code[from..to] reads and writes, and the same slots split by register class. The
	class split must match the backends' lowering; it feeds the residency pressure gate.
*/
void buildLoopSets(const Instruction* code, UInt from, UInt to, std::set<Int>& readSlots, std::set<Int>& writtenSlots
		, std::set<Int>& generalSlots, std::set<Int>& floatSlots);

/*
	Per-instruction live-in slots of one function code[from..to]: slot `s` is live-in at `j` if some path from `j` reads
	`s` before writing it. Queries take the absolute instruction index.
*/
class LiveSets {
	public:		LiveSets() : firstIndex(0) { }
	public:		bool isLive(UInt at, Int slot) const;
	public:		bool empty(UInt at) const;
	public:		void build(const Instruction* code, UInt from, UInt to, const Value* memory);
	private:	size_t rowWords() const { return (slots.size() + 31) / 32; }
	private:	const UInt* row(UInt at) const { return &bits[(at - firstIndex) * rowWords()]; }
	private:	int slotIndex(Int slot) const;											// -1 when the function never touches it
	private:	std::vector<Int> slots;													// sorted, distinct: the row's bit order
	private:	std::vector<UInt> bits;													// rowWords() words per instruction
	private:	UInt firstIndex;														// `from`: row 0 is this instruction
};

// `full` restricted to the slots live-in at `at`; a slot keeps one register throughout, so edges never need moves.
void filterResidencyMap(const ResidencyMap& full, const LiveSets& liveIn, UInt at, ResidencyMap& out);

/*
	Realm of the pointer a slot holds, relative to this frame's cached slots; sound because pointer realms are a GAZL
	language guarantee. A `PEEK`/`POKE` through NONFRAME (a parameter or data address) cannot alias a cached local and
	skips the flush; MYFRAME (from ADRL) and UNKNOWN must flush; BOTTOM means no pointer definition reached the slot.
*/
enum PointerRealm { REALM_BOTTOM = 0, REALM_NONFRAME = 1, REALM_MYFRAME = 2, REALM_UNKNOWN = 3 };

// Fills `realm` for one function, joined to a fixed point; an absent key is REALM_BOTTOM (treat as must-flush).
void buildPointerRealms(const AssembledProgram& program, UInt from, UInt to, std::map<Int, int>& realm);

// True if `pointer` is a constant or global of `program`, so it can never reach a frame.
bool isDataAddress(const AssembledProgram& program, Pointer pointer);

/*
	Per-function write-back cache of frame slots in registers (design/jit/JitCompilerResearch.md section 5.7). The
	lowering keeps memory coherent through the events below. Eviction is Belady when a use schedule is set (an imprecise
	one only picks a worse victim, never a wrong result), else LRU.
*/
class RegisterCache {
	public:		RegisterCache(const RegisterPool& pool, RegisterCacheBackend& backend);

	public:		void setUseSchedule(const UseSchedule* schedule) { useSchedule = schedule; }	// optional; enables Belady eviction
	public:		void setInstructionIndex(UInt index) { instructionIndex = index; }				// Belady scan position, advanced per instruction

	public:		int read(Int slot, RegisterClass registerClass);		// fills on a miss; read/define/scratch pin until endInstruction
	public:		int define(Int slot, RegisterClass registerClass);		// dirty, no store at the def
	public:		int scratch(RegisterClass registerClass);				// no home; dropped at endInstruction
	public:		void endInstruction();

	public:		void enterBlock();				// leader: map starts empty
	public:		void spillDirtyResident();		// before a pointer READ / back-edge: flush dirty, keep resident
	public:		void captureDirtyLines(ResidencyMap& map) const;	// snapshot for a terminal trap arm; no model change
	public:		void invalidateAll();			// after a pointer WRITE: flush dirty + drop all
	public:		void barrier();					// branch / fall-through to leader / CALL / RETU: flush dirty + drop all

	public:		void evict(int physicalRegister);	// spill if dirty and drop a fixed register
	public:		bool isResident(Int slot) const;

	public:		void capture(ResidencyMap& map, const std::set<Int>& wantedGeneral, const std::set<Int>& wantedFloat, const std::set<Int>& writtenInLoop);	// a loop header's entry state: keeps resident wanted slots, preloads absent ones up to capacity
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

// A branch edge's cache handoff: reconcile to the target's entry map if it has one, else barrier.
inline void reconcileOrBarrier(RegisterCache& cache, std::map<UInt, ResidencyMap>& entryMaps, UInt target) {
	std::map<UInt, ResidencyMap>::iterator it = entryMaps.find(target);
	if (it != entryMaps.end()) { cache.reconcileTo(it->second); } else { cache.barrier(); }
}

/*
	Cache state at block leader `j`, before its label is bound. A loop header outside any resident loop captures its
	residency map and marks [header, extent] resident; any other leader reconciles to its entry map if it has one, else
	barriers.
*/
void establishLeader(RegisterCache& cache, const Instruction* code, UInt j, const std::map<UInt, UInt>& loopExtent
		, const std::map<UInt, UInt>& loopWeight, const LiveSets& liveIn
		, std::map<UInt, ResidencyMap>& entryMaps, bool& resident, UInt& residentEnd);

/*
	A conditional branch's cache transition. Returns true for a loop exit from a resident map: the caller branches to a
	ColdEdge stub that stores `dirty`; false means branch straight to the target. The emitted reconcile code leaves the
	flags intact, so this may sit between compare and branch.
*/
bool planConditionalEdge(RegisterCache& cache, std::map<UInt, ResidencyMap>& entryMaps, UInt target, bool resident
		, ResidencyMap& dirty);

// Cold-section stores and reloads; `spillResidencyMap` stores only the `expectDirty` entries.
void spillResidencyMap(RegisterCacheBackend& backend, const ResidencyMap& map);
void fillResidencyMap(RegisterCacheBackend& backend, const ResidencyMap& map);

} // namespace GAZL

#endif
