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

#include "GAZLJit.h"
#include "GAZLJitMem.h"																									// makeExecutable / freeExecutable - the JitModule owns the executable page

#include <cstddef>
#include <cstring>																										// memset / memcpy - the jitAvailable() probe
#include <cstdio>																										// std::snprintf - the unlowerable-opcode diagnostic
#include "assert.h"																										// assert - RegisterCache contracts + the unlowerable-opcode check (local-overridable, see GAZL.h)
#include <set>																											// jitFuelSafepoints - block-leader set
#include <algorithm>																									// std::upper_bound - Belady next-read lookup

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>																									// __try/__except fault guard for the probe
#else
#include <csignal>																										// sigaction - POSIX fault guard for the probe
#include <csetjmp>																										// sigsetjmp / siglongjmp
#endif

namespace GAZL {

/*
	Is instruction `instructionIndex` a branch, and if so, to which instruction? GOTO targets via p0; the conditional forms
	(FORi + the integer/float compare-branches) target via p2. Used only by jitFuelSafepoints below (file-static); SWCH's
	jump-table targets are read separately from const memory by each backend.
*/
static bool jitBranchTarget(const Instruction* code, UInt instructionIndex, UInt& target) {
	const Int op = code[instructionIndex].opcode;
	const Int j = static_cast<Int>(instructionIndex);
	switch (op) {
		case OP_GOTO: target = static_cast<UInt>(j + code[instructionIndex].p0.i); return true;
		case OP_FORi_VVB: case OP_FORi_VCB:
		case OP_LSSI_VVB: case OP_LSSI_VCB: case OP_LSSI_CVB:
		case OP_EQUI_VVB: case OP_EQUI_VCB:
		case OP_NLSI_VVB: case OP_NLSI_VCB: case OP_NLSI_CVB:
		case OP_NEQI_VVB: case OP_NEQI_VCB:
		case OP_LSSF_VVB: case OP_LSSF_VCB: case OP_LSSF_CVB:
		case OP_EQUF_VVB: case OP_EQUF_VCB:
		case OP_NLSF_VVB: case OP_NLSF_VCB: case OP_NLSF_CVB:
		case OP_NEQF_VVB: case OP_NEQF_VCB:
			target = static_cast<UInt>(j + code[instructionIndex].p2.i); return true;
		default: return false;
	}
}

// Fuel-check granularity: the host must grant at least this per resume, and no basic block is charged more (§5.5).
static const UInt MAX_BLOCK_WEIGHT = 64;

/*
	Fuel safepoints - see GAZLJit.h. Leaders partition the function into basic blocks; charging each block's static
	weight once, at its leader, makes the JIT's total fuel spend equal the interpreter's per-instruction spend. The
	MAX_BLOCK_WEIGHT cap splits any long straight run so a block always fits one fuel grant (§5.5).
*/
void JitCompiler::jitFuelSafepoints(const Instruction* code, UInt funcStart, UInt endIndex, const Value* memory
		, std::map<UInt, UInt>& weight) {
	std::set<UInt> leaders;
	leaders.insert(funcStart);
	for (UInt j = funcStart; j <= endIndex; ++j) {
		const Int op = code[j].opcode;
		UInt target;
		if (jitBranchTarget(code, j, target)) {
			leaders.insert(target);																						// branch/GOTO/FORi target begins a block
			if (j + 1 <= endIndex) { leaders.insert(j + 1); }															// so does the fall-through after it
		} else if (op == OP_SWCH) {
			const UInt size = static_cast<UInt>(code[j].p1.i) + 1;
			const UInt table = static_cast<UInt>(code[j].p2.p - MEMORY_OFFSET);
			for (UInt k = 0; k < size; ++k) { leaders.insert(static_cast<UInt>(static_cast<Int>(j) + memory[table + k].i)); }
			if (j + 1 <= endIndex) { leaders.insert(j + 1); }
		} else if (op == OP_CALL_CVC || op == OP_CALL_VVC || op == OP_CALL_NVC) {
			/*
				A call ends a block, so the instruction right after it is a leader with its own fuel check. For CALL_NVC
				this leader is REQUIRED, not just tidy block structure: a native may yield by resetTimeOut(0) + return OK
				(the standard cooperative-yield convention - Permut8/Prawn sleep() and launch() do exactly this). Each
				backend reloads the fuel pin from ctx immediately after the native returns, so THIS safepoint is what makes
				a zeroed fuel budget suspend on the very next instruction - zero slack, matching the interpreter's
				per-instruction suspend point. Remove it and a yielding native would keep running until the next natural
				block leader (e.g. a loop head), silently breaking real-time cooperative scheduling. Do not drop it.
			*/
			if (j + 1 <= endIndex) { leaders.insert(j + 1); }
		}
	}
	// Cap: split any [leader, nextLeader) run longer than MAX_BLOCK_WEIGHT so no single block exceeds one fuel grant.
	{
		std::vector<UInt> sorted(leaders.begin(), leaders.end());
		for (size_t i = 0; i < sorted.size(); ++i) {
			const UInt stop = (i + 1 < sorted.size()) ? sorted[i + 1] : endIndex + 1;
			for (UInt p = sorted[i] + MAX_BLOCK_WEIGHT; p < stop; p += MAX_BLOCK_WEIGHT) { leaders.insert(p); }
		}
	}
	// weight[leader] = instruction span to the next leader.
	std::vector<UInt> all(leaders.begin(), leaders.end());
	for (size_t i = 0; i < all.size(); ++i) {
		const UInt stop = (i + 1 < all.size()) ? all[i + 1] : endIndex + 1;
		weight[all[i]] = stop - all[i];
	}
}

/*
	JitModule - the RAII owner of the executable page: the constructor makes the emitted code executable and takes
	ownership (acquisition == initialization), then materializes the ordinal->entry table + dispatcher entry. makeExecutable
	is the last thing that acquires, and everything after it is plain pointer arithmetic that cannot throw, so a partially
	built module never leaks a page. Throws JitException if the host refuses. The destructor frees the page (a no-op when
	empty); swap() exchanges ownership in O(1).
*/
JitModule::JitModule(const EmittedModule& emitted)
		: ownedPage(0)
		, ownedWords(emitted.code.size())
		, entries(emitted.entryByteOffsets.size())																		// may throw (bad_alloc) before any page is acquired
		, dispatch(0) {
	ownedPage = makeExecutable(emitted.code.empty() ? 0 : &emitted.code[0], emitted.code.size());
	if (ownedPage == 0) {																								// host refused executable memory (should not happen after jitAvailable())
		throw JitException("JIT: host refused to make the code page executable");
	}
	for (size_t ordinal = 0; ordinal < entries.size(); ++ordinal) {														// noexcept from here: the page is owned, dtor will free it
		entries[ordinal] = reinterpret_cast<char*>(ownedPage) + emitted.entryByteOffsets[ordinal];
	}
	dispatch = reinterpret_cast<char*>(ownedPage) + emitted.dispatchByteOffset;
}

JitModule::~JitModule() {
	freeExecutable(ownedPage, ownedWords);																				// freeExecutable(0, ...) is a no-op
}

void JitModule::swap(JitModule& other) {
	void* const tmpPage = ownedPage;
	ownedPage = other.ownedPage;
	other.ownedPage = tmpPage;

	const size_t tmpWords = ownedWords;
	ownedWords = other.ownedWords;
	other.ownedWords = tmpWords;
	entries.swap(other.entries);
	
	void* const tmpDispatch = dispatch;
	dispatch = other.dispatch;
	other.dispatch = tmpDispatch;
}

/*
	A backend met a finalized opcode it does not cover. That is a programmer error - the backend must lower all 91
	finalized opcodes - so assert first (loud during development). Asserts vanish in release, so also throw: a shipped
	build then degrades to the interpreter instead of running past a gap in the switch. Never returns.
*/
void JitCompiler::throwUnlowerableOpcode(Int opcode) {
	assert(0 && "JIT backend does not cover a finalized opcode (backend bug)");
	char message[64];
	std::snprintf(message, sizeof (message), "JIT: backend cannot lower finalized opcode 0x%X", static_cast<unsigned>(opcode));
	throw JitException(message);
}

/*
	RegisterCache (GAZLJit.h, §5.7.1): arch-neutral bookkeeping, reaching machine code only via emitFill/emitSpill.
	Invariant: Line index i holds physical register registersOf(class)[i].
*/
void buildUseSchedule(const Instruction* code, UInt from, UInt to, UseSchedule& schedule) {
	for (UInt j = from; j <= to; ++j) {
		OperandRole roles[3];
		operandRoles(code[j].opcode, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if (roles[k] == OPERAND_SLOT_READ) { schedule[operands[k]->i].push_back(j); }				// ascending j -> lists stay sorted
		}
	}
}

RegisterCache::RegisterCache(const RegisterPool& pool, RegisterCacheBackend& backend)
		: registerPool(pool)
		, cacheBackend(backend)
		, useClock(0)
		, useSchedule(0)
		, instructionIndex(0) {
	assert(pool.generalCount <= POOL_CAPACITY && pool.floatCount <= POOL_CAPACITY);
	for (size_t i = 0; i < POOL_CAPACITY; ++i) {
		generalLines[i].occupied = false;
		floatLines[i].occupied = false;
	}
}

RegisterCache::Line* RegisterCache::linesOf(RegisterClass registerClass, size_t& count) {
	if (registerClass == GENERAL_REGISTER) { count = registerPool.generalCount; return generalLines; }
	count = registerPool.floatCount;
	return floatLines;
}

const int* RegisterCache::registersOf(RegisterClass registerClass) const {
	return (registerClass == GENERAL_REGISTER) ? registerPool.generalRegisters : registerPool.floatRegisters;
}

// Store a line to its home if it is dirty and has one (scratch temps have no home), then mark it clean. Keeps it resident.
void RegisterCache::spillLine(RegisterClass registerClass, int index) {
	size_t count;
	Line* lines = linesOf(registerClass, count);
	Line& line = lines[index];
	if (line.occupied && line.dirty && !line.scratchTemp) {
		cacheBackend.emitSpill(line.slot, registersOf(registerClass)[index], registerClass);
		line.dirty = false;
	}
}

// The next scheduled read of `slot` strictly after the current instruction, or UINT32_MAX if it is read no more (dead).
uint32_t RegisterCache::nextReadAfter(Int slot) const {
	if (useSchedule == 0) { return UINT32_MAX; }
	const UseSchedule::const_iterator it = useSchedule->find(slot);
	if (it == useSchedule->end()) { return UINT32_MAX; }
	const std::vector<UInt>& reads = it->second;
	const std::vector<UInt>::const_iterator r = std::upper_bound(reads.begin(), reads.end(), instructionIndex);
	return (r == reads.end()) ? UINT32_MAX : static_cast<uint32_t>(*r);
}

/*
	A pool index ready to be (re)assigned: a free entry if one exists, else an UNPINNED line (spilled). Belady picks the
	line whose next read is furthest (dead lines, nextUse = MAX, go first) when a schedule is set; otherwise the LRU line.
	Either choice is correct - a dirty victim is always spilled first - so an imprecise schedule only costs optimality.
*/
int RegisterCache::acquire(RegisterClass registerClass) {
	size_t count;
	Line* lines = linesOf(registerClass, count);
	for (size_t i = 0; i < count; ++i) {
		if (!lines[i].occupied) { return static_cast<int>(i); }
	}
	int victim = -1;
	uint32_t best = 0;
	for (size_t i = 0; i < count; ++i) {
		if (lines[i].pinned) { continue; }
		const uint32_t key = (useSchedule != 0) ? lines[i].nextUse : lines[i].lastUse;
		const bool better = (useSchedule != 0) ? (key > best) : (key < best);						// Belady: furthest; LRU: oldest
		if (victim < 0 || better) { victim = static_cast<int>(i); best = key; }
	}
	assert(victim >= 0 && "register pool exhausted by one instruction's pinned operands");
	spillLine(registerClass, victim);
	lines[victim].occupied = false;
	return victim;
}

/*
	A slot lives in one file at a time (transients are typeless). Resolve a copy in the OTHER file before use: a read
	spills it to the home first (the fill then reads it back); a define overwrites the slot whole, so just drop it.
*/
void RegisterCache::evictOtherClass(Int slot, RegisterClass wantedClass, bool spillFirst) {
	const RegisterClass other = (wantedClass == GENERAL_REGISTER) ? FLOAT_REGISTER : GENERAL_REGISTER;
	size_t count;
	Line* lines = linesOf(other, count);
	for (size_t i = 0; i < count; ++i) {
		if (lines[i].occupied && !lines[i].scratchTemp && lines[i].slot == slot) {
			if (spillFirst) { spillLine(other, static_cast<int>(i)); }
			lines[i].occupied = false;
			return;
		}
	}
}

int RegisterCache::read(Int slot, RegisterClass registerClass) {
	evictOtherClass(slot, registerClass, true);																			// the value may currently live in the other file
	size_t count;
	Line* lines = linesOf(registerClass, count);
	const int* registers = registersOf(registerClass);
	for (size_t i = 0; i < count; ++i) {
		if (lines[i].occupied && !lines[i].scratchTemp && lines[i].slot == slot) {
			lines[i].pinned = true;
			lines[i].lastUse = ++useClock;
			lines[i].nextUse = nextReadAfter(slot);
			return registers[i];																						// hit: no load
		}
	}
	const int i = acquire(registerClass);
	Line& line = lines[i];
	line.occupied = true;
	line.scratchTemp = false;
	line.slot = slot;
	line.registerClass = registerClass;
	line.dirty = false;																									// filled from the home, so still matches it
	line.pinned = true;
	line.lastUse = ++useClock;
	line.nextUse = nextReadAfter(slot);
	cacheBackend.emitFill(registers[i], slot, registerClass);
	return registers[i];
}

int RegisterCache::define(Int slot, RegisterClass registerClass) {
	evictOtherClass(slot, registerClass, false);																		// overwriting the slot whole: any other-file copy is now dead
	size_t count;
	Line* lines = linesOf(registerClass, count);
	const int* registers = registersOf(registerClass);
	for (size_t i = 0; i < count; ++i) {
		if (lines[i].occupied && !lines[i].scratchTemp && lines[i].slot == slot) {
			lines[i].dirty = true;																						// overwriting the resident value
			lines[i].pinned = true;
			lines[i].lastUse = ++useClock;
			lines[i].nextUse = nextReadAfter(slot);
			return registers[i];
		}
	}
	const int i = acquire(registerClass);
	Line& line = lines[i];
	line.occupied = true;
	line.scratchTemp = false;
	line.slot = slot;
	line.registerClass = registerClass;
	line.dirty = true;																									// defined value diverges from the home, and no store at the def
	line.pinned = true;
	line.lastUse = ++useClock;
	line.nextUse = nextReadAfter(slot);
	return registers[i];																								// no fill: we are about to overwrite it
}

int RegisterCache::scratch(RegisterClass registerClass) {
	const int i = acquire(registerClass);
	size_t count;
	Line* lines = linesOf(registerClass, count);
	Line& line = lines[i];
	line.occupied = true;
	line.scratchTemp = true;																							// no home; never spilled, dropped at endInstruction
	line.slot = 0;
	line.registerClass = registerClass;
	line.dirty = false;
	line.pinned = true;
	line.lastUse = ++useClock;
	line.nextUse = UINT32_MAX;																							// no home; pinned this instruction then dropped, so never a victim
	return registersOf(registerClass)[i];
}

void RegisterCache::endInstruction() {
	for (size_t i = 0; i < registerPool.generalCount; ++i) {
		if (generalLines[i].occupied) { if (generalLines[i].scratchTemp) { generalLines[i].occupied = false; } else { generalLines[i].pinned = false; } }
	}
	for (size_t i = 0; i < registerPool.floatCount; ++i) {
		if (floatLines[i].occupied) { if (floatLines[i].scratchTemp) { floatLines[i].occupied = false; } else { floatLines[i].pinned = false; } }
	}
}

void RegisterCache::spillDirtyResident() {
	for (size_t i = 0; i < registerPool.generalCount; ++i) { spillLine(GENERAL_REGISTER, static_cast<int>(i)); }
	for (size_t i = 0; i < registerPool.floatCount; ++i) { spillLine(FLOAT_REGISTER, static_cast<int>(i)); }
}

// spill all dirty (so the home image is current), then drop every mapping (the next block/read starts from memory).
void RegisterCache::flushAndClear() {
	spillDirtyResident();
	for (size_t i = 0; i < registerPool.generalCount; ++i) { generalLines[i].occupied = false; }
	for (size_t i = 0; i < registerPool.floatCount; ++i) { floatLines[i].occupied = false; }
}

void RegisterCache::barrier() { flushAndClear(); }

/*
	Snapshot the resident lines as a loop header's fixed entry state, and switch them to the all-dirty model (see
	ResidencyMap): a loop-carried value redefined in the body IS dirty when the header is reached again, so the model
	must assume dirty from iteration one or an eviction inside the body would skip the store and lose the value.
*/
void RegisterCache::capture(ResidencyMap& map) {
	assert(map.entries.empty() && "a header's entry map is captured once");
	for (int c = 0; c < 2; ++c) {
		const RegisterClass registerClass = (c == 0) ? GENERAL_REGISTER : FLOAT_REGISTER;
		size_t count;
		Line* lines = linesOf(registerClass, count);
		const int* registers = registersOf(registerClass);
		for (size_t i = 0; i < count; ++i) {
			if (!lines[i].occupied) { continue; }
			assert(!lines[i].pinned && !lines[i].scratchTemp && "capture between instructions only");
			ResidencyMap::Entry entry = { lines[i].slot, registerClass, static_cast<int>(i), registers[i] };
			map.entries.push_back(entry);
			lines[i].dirty = true;
		}
	}
}

/*
	Re-establish a header's entry state at a back-edge, from spill/fill primitives alone: spill-and-drop every line the
	map does not want (including a wanted slot sitting in the wrong register - its refill below reads the home the spill
	just made current), then fill the missing entries. Emits nothing when the state already matches, which is the common
	back-edge case. Flags are untouched (loads/stores only), so this may sit between a compare and its branch.
*/
void RegisterCache::reconcileTo(const ResidencyMap& map) {
	for (int c = 0; c < 2; ++c) {
		const RegisterClass registerClass = (c == 0) ? GENERAL_REGISTER : FLOAT_REGISTER;
		size_t count;
		Line* lines = linesOf(registerClass, count);
		for (size_t i = 0; i < count; ++i) {
			if (!lines[i].occupied) { continue; }
			assert(!lines[i].pinned && !lines[i].scratchTemp && "reconcile between instructions only");
			bool wanted = false;
			for (size_t k = 0; k < map.entries.size(); ++k) {
				const ResidencyMap::Entry& entry = map.entries[k];
				if (entry.registerClass == registerClass && entry.poolIndex == static_cast<int>(i)
						&& entry.slot == lines[i].slot) { wanted = true; break; }
			}
			if (!wanted) { spillLine(registerClass, static_cast<int>(i)); lines[i].occupied = false; }
		}
	}
	for (size_t k = 0; k < map.entries.size(); ++k) {
		const ResidencyMap::Entry& entry = map.entries[k];
		size_t count;
		Line* lines = linesOf(entry.registerClass, count);
		Line& line = lines[entry.poolIndex];
		if (!line.occupied) {
			cacheBackend.emitFill(entry.physicalRegister, entry.slot, entry.registerClass);							// home is current: the value was spilled above or never diverged
			line.occupied = true;
			line.scratchTemp = false;
			line.slot = entry.slot;
			line.registerClass = entry.registerClass;
			line.pinned = false;
			line.lastUse = ++useClock;
			line.nextUse = nextReadAfter(entry.slot);
		}
		line.dirty = true;																								// the header's all-dirty model
	}
}

void RegisterCache::invalidateAll() { flushAndClear(); }

void RegisterCache::enterBlock() {
	assertNoDirty();																									// contract: a leader is reached only via a barrier, so nothing is dirty
	flushAndClear();																									// release: never drop a dirty line - spill it - even if the contract was broken
}

void RegisterCache::evict(int physicalRegister) {
	for (size_t i = 0; i < registerPool.generalCount; ++i) {
		if (registerPool.generalRegisters[i] == physicalRegister) {
			if (generalLines[i].occupied) { assert(!generalLines[i].pinned); spillLine(GENERAL_REGISTER, static_cast<int>(i)); generalLines[i].occupied = false; }
			return;
		}
	}
	for (size_t i = 0; i < registerPool.floatCount; ++i) {
		if (registerPool.floatRegisters[i] == physicalRegister) {
			if (floatLines[i].occupied) { assert(!floatLines[i].pinned); spillLine(FLOAT_REGISTER, static_cast<int>(i)); floatLines[i].occupied = false; }
			return;
		}
	}
	// physicalRegister is not in either pool (e.g. a pinned VM-state register the cache never allocates): nothing to do.
}

bool RegisterCache::isResident(Int slot) const {
	for (size_t i = 0; i < registerPool.generalCount; ++i) {
		if (generalLines[i].occupied && !generalLines[i].scratchTemp && generalLines[i].slot == slot) { return true; }
	}
	for (size_t i = 0; i < registerPool.floatCount; ++i) {
		if (floatLines[i].occupied && !floatLines[i].scratchTemp && floatLines[i].slot == slot) { return true; }
	}
	return false;
}

void RegisterCache::assertNoDirty() const {
	for (size_t i = 0; i < registerPool.generalCount; ++i) { assert(!(generalLines[i].occupied && generalLines[i].dirty)); }
	for (size_t i = 0; i < registerPool.floatCount; ++i) { assert(!(floatLines[i].occupied && floatLines[i].dirty)); }
}

/*
	The field ABI JitCompiler bakes into the machine code - byte offsets of the run-state fields within a JitProcessor.
	Arch-neutral (offsetof on JitProcessor fields), so it lives here rather than in a backend .cpp. Static /
	instance-independent: computed with offsetof, so no engine is needed. offsetof on this polymorphic type is
	universally correct (single inheritance, fixed layout); the one -Winvalid-offsetof is silenced locally. Non-virtual,
	so defining it here pulls no vtable into GAZLJit.o.
*/
Offsets JitProcessor::layout() {
	Offsets o;
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
	o.dsp = offsetof(JitProcessor, dsp); o.mb = offsetof(JitProcessor, memoryBase);
	o.fuel = offsetof(JitProcessor, clockCyclesLeft); o.ipsp = offsetof(JitProcessor, ipsp);
	o.resume = offsetof(JitProcessor, resume); o.saveddsp = offsetof(JitProcessor, savedDsp);
	o.natives = offsetof(JitProcessor, natives); o.nativefn = offsetof(JitProcessor, nativeFn);
	o.funcentries = offsetof(JitProcessor, funcEntries);
	o.memsize = offsetof(JitProcessor, memorySize); o.rwmemsize = offsetof(JitProcessor, rwMemorySize);
	o.dsend = offsetof(JitProcessor, dataStackEnd); o.ipsend = offsetof(JitProcessor, ipStackEnd);
	o.nativeafter = offsetof(JitProcessor, nativeAfter);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	return o;
}

Status JitProcessor::enterCall(Pointer functionPointer) {
	const Status s = Processor::enterCall(functionPointer);
	if (s != OK) {
		return s;
	}
	resume = funcEntries[functionPointer - IP_OFFSET];
	return OK;
}

Status JitProcessor::run() {
	typedef int (*Disp)(JitProcessor*);
	return static_cast<Status>(reinterpret_cast<Disp>(jitDispatch)(this));
}

/*
	Bind the compiled module into this engine + zero the native-call scratch. Shared by both constructors (C++03 has no
	delegation). Precondition: the module holds compiled code - an empty module has no dispatcher to run.
*/
void JitProcessor::bindModule(const JitModule& module) {
	assert(module.isCompiled() && "JitProcessor requires a compiled JitModule");
	savedDsp = 0; nativeFn = 0; nativeAfter = 0;
	funcEntries = module.entryTable();
	jitDispatch = module.dispatchEntry();
}

/*
	jitAvailable() - the two-layer capability probe declared in GAZLJit.h. Layer 1 is makeExecutable() itself (a policy
	denial such as a missing macOS allow-jit entitlement or Windows Arbitrary Code Guard fails the syscalls). Layer 2 runs
	a position-independent stub under a fault guard, so a page that mapped but cannot execute turns into `false` instead of
	a crash - and the stub does a real JIT->C round-trip (it calls a C function through the platform convention and returns
	its result), so a broken native calling convention - the exact crossing CALL_NVC bakes in - also fails the probe rather
	than Permut8. The stub is one word-aligned block (makeExecutable works in 32-bit words); it is architecture- and
	ABI-specific but tiny, so it lives here rather than in a backend. Both shipping targets are little-endian.
*/
namespace {

/*
	The native ABI the JIT bakes into CALL_NVC: a plain C function of one pointer, returning a single-register Status.
	Pinned so a future change to either (a wider Status, a by-value struct return, a non-default convention) can't
	silently break the hand-emitted call/return. The runtime counterpart - an actual JIT->C round-trip - is the probe stub.
*/
typedef Status (*JitNativeSignatureCheck)(Processor*);
#if __cplusplus >= 201103L
static_assert(sizeof(Status) <= sizeof(void*), "JIT CALL_NVC assumes a native's Status returns in one integer register");
#endif

/*
	The probe stub: `uint32_t stub(ProbeCallee callee, const void* arg)` - move callee out of arg0, move arg into arg0,
	call it through the platform's native convention, hand back its return. Exercises the exact JIT->C crossing CALL_NVC
	bakes in (arg in the ABI arg register, the call/return, frame alignment + Win64 shadow space).
*/
#if defined(__aarch64__) || defined(_M_ARM64)
const uint32_t PROBE_STUB[] = {																							// stp x29,x30,[sp,#-16]! ; mov x9,x0 ; mov x0,x1 ; blr x9 ; ldp x29,x30,[sp],#16 ; ret
	0xA9BF7BFDu, 0xAA0003E9u, 0xAA0103E0u, 0xD63F0120u, 0xA8C17BFDu, 0xD65F03C0u
};
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
#	if defined(_WIN32)
const uint32_t PROBE_STUB[] = {																							// sub rsp,40 ; mov rax,rcx ; mov rcx,rdx ; call rax ; add rsp,40 ; ret   (Win64: 32B shadow + align)
	0x28EC8348u, 0x48C88948u, 0xD0FFD189u, 0x28C48348u, 0x909090C3u
};
#	else
const uint32_t PROBE_STUB[] = {																							// sub rsp,8 ; mov rax,rdi ; mov rdi,rsi ; call rax ; add rsp,8 ; ret   (SysV)
	0x08EC8348u, 0x48F88948u, 0xD0FFF789u, 0x08C48348u, 0x909090C3u
};
#	endif
#else
#	define GAZL_NO_PROBE_STUB																							// no JIT backend for this architecture
#endif

#if !defined(GAZL_NO_PROBE_STUB)
typedef uint32_t (*ProbeCallee)(const void*);
typedef uint32_t (*ProbeFunc)(ProbeCallee, const void*);
/*
	The C side of the round-trip: read the 32-bit input and add 0x1111, so a passing probe proves the call really ran (not
	a passthrough) and the arg pointer crossed intact. probeCallee(&PROBE_INPUT) == 0xC0DE - the value the stub returns.
*/
uint32_t probeCallee(const void* arg) { return *static_cast<const uint32_t*>(arg) + 0x1111u; }
const uint32_t PROBE_INPUT = 0xAFCDu;																					// 0xAFCD + 0x1111 == 0xC0DE
#endif

#if !defined(_WIN32) && !defined(GAZL_NO_PROBE_STUB)
sigjmp_buf gProbeJump;
void probeFaultHandler(int) { siglongjmp(gProbeJump, 1); }
#endif

#if !defined(GAZL_NO_PROBE_STUB)
/*
	Run `func` under a fault guard: true only if it returned 0xC0DE. A SIGILL/SIGSEGV/SIGBUS/SIGTRAP (POSIX) or any
	structured exception (Windows) means executing generated code faults on this host -> false.
*/
bool runProbe(ProbeFunc func) {
#if defined(_WIN32)
	__try {
		return func(&probeCallee, &PROBE_INPUT) == 0xC0DEu;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
#else
	struct sigaction guard, savedIll, savedSegv, savedBus, savedTrap;
	std::memset(&guard, 0, sizeof(guard));
	guard.sa_handler = probeFaultHandler;
	sigemptyset(&guard.sa_mask);
	sigaction(SIGILL, &guard, &savedIll);
	sigaction(SIGSEGV, &guard, &savedSegv);
	sigaction(SIGBUS, &guard, &savedBus);
	sigaction(SIGTRAP, &guard, &savedTrap);
	bool ok = false;
	if (sigsetjmp(gProbeJump, 1) == 0) { ok = (func(&probeCallee, &PROBE_INPUT) == 0xC0DEu); }
	sigaction(SIGILL, &savedIll, 0);
	sigaction(SIGSEGV, &savedSegv, 0);
	sigaction(SIGBUS, &savedBus, 0);
	sigaction(SIGTRAP, &savedTrap, 0);
	return ok;
#endif
}
#endif

}																														// anonymous namespace

bool jitAvailable() {
#if defined(GAZL_NO_PROBE_STUB)
	return false;
#else
	static int cached = -1;																								// -1 unknown / 0 no / 1 yes; probed once (single-threaded startup)
	if (cached < 0) {
		const size_t words = sizeof(PROBE_STUB) / sizeof(PROBE_STUB[0]);
		void* page = makeExecutable(PROBE_STUB, words);																	// layer 1: policy denial fails the syscalls here
		if (page == 0) {
			cached = 0;
		} else {
			ProbeFunc func;
			std::memcpy(&func, &page, sizeof(func));																	// void* -> function pointer without an ISO-illegal cast
			cached = runProbe(func) ? 1 : 0;																			// layer 2: the emitted stub actually runs and returns 0xC0DE
			freeExecutable(page, words);
		}
	}
	return cached != 0;
#endif
}

}																														// namespace GAZL
