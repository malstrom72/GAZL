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
#undef RegisterClass																									// winuser.h macro (RegisterClassW) collides with GAZL::RegisterClass
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
	Residency-compatible opcodes: a resident loop pays off when its body never drops the map. A frame-touching memory op
	(pointer/GETL/SETL), call, or COPY inside the body flushes or drops the map every iteration, and the header's dirty
	modeling then adds stores that the plain v2.0 fill-on-use block never paid - measured as a net LOSS on such loops -
	so they stay v2.0 until the general per-block reconciliation lands. Const-address SCALAR PEEK/POKE are globals-realm
	(§1.1) and flush-free: residency-safe. The INDEXED const forms are flush-free too but burn several scratch registers
	per op, and inside a resident loop that eviction churn measured 2x SLOWER (spectralnorm) - they stay excluded.
	DIVI/MODI/DIVF flush dirty lines on their main-path trap spill but keep the map resident (a net win); allowed.
*/
static bool jitResidencySafe(Int op) {
	switch (op) {
		case OP_MOVE_VV: case OP_MOVE_VC:
		case OP_PEEK_VC: case OP_POKE_CV: case OP_POKE_CC:
		case OP_ABSI: case OP_ABSF: case OP_FLOF:
		case OP_ADDI_VVV: case OP_ADDI_VVC: case OP_SUBI_VVV: case OP_SUBI_VVC: case OP_SUBI_VCV:
		case OP_MULI_VVV: case OP_MULI_VVC:
		case OP_DIVI_VVV: case OP_DIVI_VVC: case OP_DIVI_VCV:
		case OP_MODI_VVV: case OP_MODI_VVC: case OP_MODI_VCV:
		case OP_ANDI_VVV: case OP_ANDI_VVC: case OP_IORI_VVV: case OP_IORI_VVC: case OP_XORI_VVV: case OP_XORI_VVC:
		case OP_SHLI_VVV: case OP_SHLI_VVC: case OP_SHLI_VCV:
		case OP_SHRI_VVV: case OP_SHRI_VVC: case OP_SHRI_VCV:
		case OP_SHRU_VVV: case OP_SHRU_VVC: case OP_SHRU_VCV:
		case OP_ADDF_VVV: case OP_ADDF_VVC: case OP_SUBF_VVV: case OP_SUBF_VVC: case OP_SUBF_VCV:
		case OP_MULF_VVV: case OP_MULF_VVC:
		case OP_DIVF_VVV: case OP_DIVF_VVC: case OP_DIVF_VCV:
		case OP_FTOI_VVC: case OP_ITOF_VVC:
		case OP_FORi_VVB: case OP_FORi_VCB:
		case OP_LSSI_VVB: case OP_LSSI_VCB: case OP_LSSI_CVB:
		case OP_EQUI_VVB: case OP_EQUI_VCB:
		case OP_NLSI_VVB: case OP_NLSI_VCB: case OP_NLSI_CVB:
		case OP_NEQI_VVB: case OP_NEQI_VCB:
		case OP_LSSF_VVB: case OP_LSSF_VCB: case OP_LSSF_CVB:
		case OP_EQUF_VVB: case OP_EQUF_VCB:
		case OP_NLSF_VVB: case OP_NLSF_VCB: case OP_NLSF_CVB:
		case OP_NEQF_VVB: case OP_NEQF_VCB:
		case OP_GOTO:
			return true;
		default:
			return false;
	}
}

// v2.2 residency qualification - see GAZLJit.h. A loop header = a back-edge target no forward branch or SWCH reaches.
void JitCompiler::jitResidencyLeaders(const Instruction* code, UInt funcStart, UInt endIndex
		, const Value* memory, std::map<UInt, UInt>& loopExtent) {
	std::set<UInt> excluded;
	for (UInt j = funcStart; j <= endIndex; ++j) {
		UInt target;
		if (jitBranchTarget(code, j, target)) {
			if (target > j) { excluded.insert(target); }																	// a forward edge arrives with an empty cache
			else { std::map<UInt, UInt>::iterator it = loopExtent.find(target); if (it == loopExtent.end() || it->second < j) { loopExtent[target] = j; } }
		} else if (code[j].opcode == OP_SWCH) {
			const UInt size = static_cast<UInt>(code[j].p1.i) + 1;
			const UInt table = static_cast<UInt>(code[j].p2.p - MEMORY_OFFSET);
			for (UInt k = 0; k < size; ++k) { excluded.insert(static_cast<UInt>(static_cast<Int>(j) + memory[table + k].i)); }
		}
	}
	for (std::set<UInt>::const_iterator it = excluded.begin(); it != excluded.end(); ++it) { loopExtent.erase(*it); }
	for (std::map<UInt, UInt>::iterator it = loopExtent.begin(); it != loopExtent.end(); ) {							// keep only register-only single-block bodies (see jitResidencySafe)
		bool safe = true;
		for (UInt j = it->first; j <= it->second && safe; ++j) {
			if (!jitResidencySafe(code[j].opcode)) { safe = false; }
			UInt target;
			if (jitBranchTarget(code, j, target) && target > j) { safe = false; }										// an internal forward branch (early exit / if-join): the leader after it drops the map
		}
		if (safe) { ++it; } else { loopExtent.erase(it++); }
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
		if (code[j].opcode == OP_FORi_VVB || code[j].opcode == OP_FORi_VCB) { roles[0] = OPERAND_SLOT_READ; }	// the counter is read-modify-write
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if (roles[k] == OPERAND_SLOT_READ) { schedule[operands[k]->i].push_back(j); }				// ascending j -> lists stay sorted
		}
	}
}

// join two realms in the lattice BOTTOM < {NONFRAME, MYFRAME} < UNKNOWN (NONFRAME and MYFRAME are incomparable).
static int joinRealm(int a, int b) {
	if (a == REALM_BOTTOM) { return b; }
	if (b == REALM_BOTTOM || a == b) { return a; }
	return REALM_UNKNOWN;
}

void buildPointerRealms(const Instruction* code, UInt from, UInt to, std::map<Int, int>& realm) {
	/*
		Pass 1: live-in slots (first textual role is a READ, before any WRITE) are received parameters -> NONFRAME. A
		slot first WRITTEN in the function is not a parameter, so a value later used as a pointer base traces to a
		definition we classify below (BOTTOM until then = must-flush).
	*/
	std::set<Int> written;
	for (UInt j = from; j <= to; ++j) {
		OperandRole roles[3];
		operandRoles(code[j].opcode, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if (roles[k] == OPERAND_SLOT_READ && written.find(operands[k]->i) == written.end()) {
				realm.insert(std::make_pair(operands[k]->i, static_cast<int>(REALM_NONFRAME)));				// live-in => parameter
			}
		}
		for (int k = 0; k < 3; ++k) {
			if (roles[k] == OPERAND_SLOT_WRITE) { written.insert(operands[k]->i); }
		}
	}

	/*
		Pass 2: forward transfer joined to a fixed point (flow-insensitive: a slot's realm is the join over ALL its
		definitions, so a back-edge that redefines a pointer is already accounted for). Height-2 lattice -> converges in
		a few sweeps; loops with a pointer-carried induction (`ADDi $p $p $k`) settle at their operand's realm.
	*/
	bool changed = true;
	while (changed) {
		changed = false;
		for (UInt j = from; j <= to; ++j) {
			const Int op = code[j].opcode;
			int produced;
			switch (op) {
				case OP_ADRL: produced = REALM_MYFRAME; break;												// address of a local
				case OP_MOVE_VC: produced = (code[j].p1.p >= MEMORY_OFFSET) ? REALM_NONFRAME : REALM_UNKNOWN; break;	// global/const symbol vs bare int
				case OP_MOVE_VV: {
					const std::map<Int, int>::const_iterator it = realm.find(code[j].p1.i);
					produced = (it != realm.end()) ? it->second : REALM_BOTTOM;
					break;
				}
				case OP_ADDI_VVV: case OP_ADDI_VVC: case OP_SUBI_VVV: case OP_SUBI_VVC: case OP_SUBI_VCV: {
					OperandRole roles[3];
					operandRoles(op, roles);
					const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
					produced = REALM_BOTTOM;																// pointer +/- int keeps the pointer's realm; join the slot reads
					for (int k = 0; k < 3; ++k) {
						if (roles[k] == OPERAND_SLOT_READ) {
							const std::map<Int, int>::const_iterator it = realm.find(operands[k]->i);
							produced = joinRealm(produced, (it != realm.end()) ? it->second : REALM_BOTTOM);
						}
					}
					break;
				}
				default: {																				// loads, calls, native, and every non-pointer producer
					OperandRole roles[3];
					operandRoles(op, roles);
					if (roles[0] != OPERAND_SLOT_WRITE) { continue; }									// writes no slot -> nothing to stamp
					produced = REALM_UNKNOWN;
					break;
				}
			}
			OperandRole roles[3];
			operandRoles(op, roles);
			if (roles[0] != OPERAND_SLOT_WRITE) { continue; }
			const Int dst = code[j].p0.i;
			const int merged = joinRealm(realm.count(dst) ? realm[dst] : REALM_BOTTOM, produced);
			if (!realm.count(dst) || realm[dst] != merged) { realm[dst] = merged; changed = true; }
		}
	}
}

// Successors of instruction j in [from,to]: fall-through (j+1) unless j is an unconditional GOTO/RETU; plus branch and
// SWCH-table targets. Conservative (extra successors only widen liveness). Appends to `succ`.
static void jitSuccessors(const Instruction* code, UInt from, UInt to, const Value* memory, UInt j, std::vector<UInt>& succ) {
	const Int op = code[j].opcode;
	if (op == OP_RETU) { return; }																						// terminal
	if (op == OP_GOTO) { succ.push_back(static_cast<UInt>(static_cast<Int>(j) + code[j].p0.i)); return; }				// unconditional: no fall-through
	UInt target;
	if (jitBranchTarget(code, j, target)) { succ.push_back(target); }													// conditional branch target
	else if (op == OP_SWCH) {
		const UInt size = static_cast<UInt>(code[j].p1.i) + 1;
		const UInt table = static_cast<UInt>(code[j].p2.p - MEMORY_OFFSET);
		for (UInt k = 0; k < size; ++k) { succ.push_back(static_cast<UInt>(static_cast<Int>(j) + memory[table + k].i)); }
	}
	if (j + 1 <= to) { succ.push_back(j + 1); }																			// fall-through (also after a conditional branch / SWCH)
}

void buildLiveIn(const Instruction* code, UInt from, UInt to, const Value* memory, std::map<UInt, std::set<Int> >& liveIn) {
	// gen (reads) / kill (writes) per instruction; a FORi counter is read-modify-write, so it is both.
	std::map<UInt, std::set<Int> > gen, kill;
	for (UInt j = from; j <= to; ++j) {
		OperandRole roles[3];
		operandRoles(code[j].opcode, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if (roles[k] == OPERAND_SLOT_READ) { gen[j].insert(operands[k]->i); }
			else if (roles[k] == OPERAND_SLOT_WRITE) { kill[j].insert(operands[k]->i); }
		}
		if (code[j].opcode == OP_FORi_VVB || code[j].opcode == OP_FORi_VCB) { gen[j].insert(code[j].p0.i); kill[j].insert(code[j].p0.i); }
	}
	// Backward fixed point: liveIn[j] = gen[j] U (liveOut[j] - kill[j]); liveOut[j] = U liveIn[succ]. Reverse sweeps converge fast.
	bool changed = true;
	while (changed) {
		changed = false;
		for (UInt jj = to + 1; jj > from; --jj) {
			const UInt j = jj - 1;
			std::set<Int> out;
			std::vector<UInt> succ;
			jitSuccessors(code, from, to, memory, j, succ);
			for (size_t s = 0; s < succ.size(); ++s) {
				const std::map<UInt, std::set<Int> >::const_iterator it = liveIn.find(succ[s]);
				if (it != liveIn.end()) { out.insert(it->second.begin(), it->second.end()); }
			}
			std::set<Int> in = gen[j];																					// gen U (out - kill)
			for (std::set<Int>::const_iterator it = out.begin(); it != out.end(); ++it) {
				if (kill[j].find(*it) == kill[j].end()) { in.insert(*it); }
			}
			if (in != liveIn[j]) { liveIn[j].swap(in); changed = true; }
		}
	}
}

void buildLoopSlotSets(const Instruction* code, UInt from, UInt to, std::set<Int>& readSlots, std::set<Int>& writtenSlots) {
	for (UInt j = from; j <= to; ++j) {
		OperandRole roles[3];
		operandRoles(code[j].opcode, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if (roles[k] == OPERAND_SLOT_READ) { readSlots.insert(operands[k]->i); }
			else if (roles[k] == OPERAND_SLOT_WRITE) { writtenSlots.insert(operands[k]->i); }
		}
		if (code[j].opcode == OP_FORi_VVB || code[j].opcode == OP_FORi_VCB) { readSlots.insert(code[j].p0.i); }	// the counter is read-modify-write
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

/*
	Snapshot the dirty lines for a terminal trap ARM (no model change, no emission). Captured at the trap BRANCH's
	emission point - where the compile-time model equals the trap path's runtime register state - the backend then emits
	plain stores from the snapshot on the cold arm, making the exit image interpreter-identical. The arm never rejoins
	the mainline, so the model must not observe those stores (marking lines clean was the historical mid-op trap bug);
	and capturing anywhere later is wrong too - the op's own define holds garbage on the trap path, and a later eviction
	may reuse a captured register.
*/
void RegisterCache::captureDirtyLines(ResidencyMap& map) const {
	assert(map.entries.empty());
	for (int c = 0; c < 2; ++c) {
		const RegisterClass registerClass = (c == 0) ? GENERAL_REGISTER : FLOAT_REGISTER;
		const size_t count = (registerClass == GENERAL_REGISTER) ? registerPool.generalCount : registerPool.floatCount;
		const Line* lines = (registerClass == GENERAL_REGISTER) ? generalLines : floatLines;
		const int* registers = registersOf(registerClass);
		for (size_t i = 0; i < count; ++i) {
			if (lines[i].occupied && lines[i].dirty && !lines[i].scratchTemp) {
				ResidencyMap::Entry entry = { lines[i].slot, registerClass, static_cast<int>(i), registers[i], true };
				map.entries.push_back(entry);
			}
		}
	}
}

// spill all dirty (so the home image is current), then drop every mapping (the next block/read starts from memory).
void RegisterCache::flushAndClear() {
	spillDirtyResident();
	for (size_t i = 0; i < registerPool.generalCount; ++i) { generalLines[i].occupied = false; }
	for (size_t i = 0; i < registerPool.floatCount; ++i) { floatLines[i].occupied = false; }
}

void RegisterCache::barrier() { flushAndClear(); }

/*
	Snapshot the resident lines as a loop header's fixed entry state (see ResidencyMap). Pruned: a slot the loop never
	reads is spilled-and-dropped here (keeping it would only make every back-edge refill it). A kept slot the loop
	WRITES is modeled dirty from iteration one (the redefined value really is dirty when the header is reached again; a
	clean model would let an eviction skip the store and lose it). A kept read-only slot is canonicalized to
	register==home (one store, emitted before the header label so it runs once), so hazard flushes inside the loop never
	re-store it.
*/
void RegisterCache::capture(ResidencyMap& map, const std::set<Int>& readInLoop, const std::set<Int>& writtenInLoop) {
	assert(map.entries.empty() && "a header's entry map is captured once");
	const size_t RESIDENCY_HEADROOM = 3;																				// registers per class left free for the body's temps and constants (halved for small pools)
	for (int c = 0; c < 2; ++c) {
		const RegisterClass registerClass = (c == 0) ? GENERAL_REGISTER : FLOAT_REGISTER;
		size_t count;
		Line* lines = linesOf(registerClass, count);
		const int* registers = registersOf(registerClass);
		size_t kept = 0;
		const size_t headroom = (RESIDENCY_HEADROOM < count / 2) ? RESIDENCY_HEADROOM : count / 2;
		const size_t keepMax = count - headroom;
		for (size_t i = 0; i < count; ++i) {
			if (!lines[i].occupied) { continue; }
			assert(!lines[i].pinned && !lines[i].scratchTemp && "capture between instructions only");
			if (readInLoop.count(lines[i].slot) == 0 || kept >= keepMax) {												// unread in the loop, or the map would strangle the body
				spillLine(registerClass, static_cast<int>(i));
				lines[i].occupied = false;
				continue;
			}
			const bool expectDirty = (writtenInLoop.count(lines[i].slot) != 0);
			if (!expectDirty) { spillLine(registerClass, static_cast<int>(i)); }										// canonical clean: register==home for the whole loop
			ResidencyMap::Entry entry = { lines[i].slot, registerClass, static_cast<int>(i), registers[i], expectDirty };
			map.entries.push_back(entry);
			lines[i].dirty = expectDirty;
			++kept;
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
		if (entry.expectDirty) { line.dirty = true; }																	// the header models loop-written slots dirty
		else if (line.dirty) { spillLine(entry.registerClass, entry.poolIndex); }										// read-only slot: re-establish register==home
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

/*
	pushCall() under the JIT - the mirror of Processor::pushCall over compiled continuations. The native-call
	sequence (both backends) presets ctx.nativeAfter to its `after` label and, on native OK, continues with an
	indirect jump through it; each pushCall links the CURRENT continuation into a plain frame and retargets the
	slot at the pushed callee's compiled entry. So the last-pushed callee runs first and every RETU chains through
	the pushed frames (LIFO) back to `after`, which normalizes dsp to the caller frame - which is why every pushed
	frame stores the WINDOW (this->dsp), first and chained alike, unlike the interpreter's frames. `after` also
	zeroes ctx.nativeAfter, restoring the "only inside a native call" guard.
*/
Value* JitProcessor::pushCall(Pointer functionPointer) {
	UInt ui = functionPointer - IP_OFFSET;
	if (ui >= functionCount) return 0;
	if (nativeAfter == 0) return 0;										// only valid from inside a native callback
	if (ipsp >= ipStackEnd) return 0;
	const Instruction* func = codeBase + functionTable[ui];
	if (this->dsp + static_cast<UInt>(func->p0.i) + static_cast<UInt>(func->p1.i) > dataStackEnd) return 0;	// early FUNC check
	ipsp->returnAddress = nativeAfter;									// current continuation: `after`, or an earlier-pushed entry
	ipsp++->dsp = this->dsp;											// the window (see above)
	nativeAfter = funcEntries[ui];										// the OK path jumps straight into the pushed callee
	return this->dsp;
}

Status JitProcessor::run() {
	typedef int (*Disp)(JitProcessor*);
	/*
		nativeAfter is the one dispatch-scratch field live ACROSS a native call (the redirectable OK continuation
		the compiled sequence reads after the native returns). A blocking native that nests run() (enterCall +
		run, the documented interrupt pattern) would clobber it via its own native calls - so give it the same
		C++-stack isolation the interpreter's locals enjoy. Everything else the sequence reads post-call
		(ctx.dsp / ctx.ipsp / ctx.fuel) is stable across a well-formed nested episode by the sentinel discipline.
	*/
	void* const outerNativeAfter = nativeAfter;
	const Status s = static_cast<Status>(reinterpret_cast<Disp>(jitDispatch)(this));
	nativeAfter = outerNativeAfter;
	return s;
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
