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
#include "GAZLJitMem.h"

#include <cstddef>
#include <cstring>
#include <cstdio>
#include "assert.h"																										// local-overridable, see GAZL.h
#include <set>
#include <algorithm>
#if defined(_M_X64) || defined(__x86_64__)															// the x64 backend lowers FLOF with SSE4.1 `roundss`
	#if defined(_MSC_VER)
		#include <intrin.h>
	#elif defined(__GNUC__) || defined(__clang__)
		#include <cpuid.h>
	#endif
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>																									// __try/__except fault guard for the probe
#undef RegisterClass																									// winuser.h macro (RegisterClassW) collides with GAZL::RegisterClass
#else
#include <csignal>																										// sigaction: POSIX fault guard for the probe
#include <csetjmp>
#endif

namespace GAZL {

// The target of a GOTO or conditional branch; SWCH targets come from switchTargets.
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

std::vector<UInt> switchTargets(const Instruction* code, UInt instructionIndex, const Value* memory) {
	assert(code[instructionIndex].opcode == OP_SWCH && "only a SWCH has a jump table");
	const Value* table = memory + static_cast<Int>(code[instructionIndex].p2.p - MEMORY_OFFSET);					// a table is a constant, below `memory`
	std::vector<UInt> targets(static_cast<UInt>(code[instructionIndex].p1.i) + 1);
	for (UInt k = 0; k < targets.size(); ++k) {
		targets[k] = static_cast<UInt>(static_cast<Int>(instructionIndex) + table[k].i);
	}
	return targets;
}

// The host must grant at least this much fuel per resume; no basic block is charged more.
static const UInt MAX_BLOCK_WEIGHT = 64;

/*
	Fuel safepoints (see GAZLJit.h and design/jit/JitCompilerResearch.md section 5.5): each block's static weight is
	charged once at its leader, so the JIT spends exactly the interpreter's fuel.
*/
void JitCompiler::jitFuelSafepoints(const Instruction* code, UInt funcStart, UInt endIndex, const Value* memory
		, std::map<UInt, UInt>& weight) {
	std::set<UInt> leaders;
	leaders.insert(funcStart);
	for (UInt j = funcStart; j <= endIndex; ++j) {
		const Int op = code[j].opcode;
		UInt target;
		if (jitBranchTarget(code, j, target)) {
			// A target outside the function is malformed input (not a backend bug): decline the function.
			if (target < funcStart || target > endIndex) { throw JitException("JIT: branch target outside the function"); }
			leaders.insert(target);
			// A conditional branch's fall-through also starts a block; a GOTO has none.
			if (op != OP_GOTO && j + 1 <= endIndex) { leaders.insert(j + 1); }
		} else if (op == OP_SWCH) {
			const std::vector<UInt> targets = switchTargets(code, j, memory);
			for (size_t k = 0; k < targets.size(); ++k) {
				const UInt t = targets[k];
				// As for a branch: a corrupt jump-table entry is malformed input, so decline the function.
				if (t < funcStart || t > endIndex) { throw JitException("JIT: SWCH jump-table target outside the function"); }
				leaders.insert(t);
			}																												// SWCH always jumps: no fall-through leader
		} else if (op == OP_CALL_CVC || op == OP_CALL_VVC || op == OP_CALL_NVC) {
			/*
				A call ends a block. For CALL_NVC this leader is required: a native yields with resetTimeOut(0) and OK,
				and the backends reload fuel after it returns, so this fuel check suspends on the very next instruction,
				as the interpreter does.
			*/
			if (j + 1 <= endIndex) { leaders.insert(j + 1); }
		}
	}
	// Split any run longer than MAX_BLOCK_WEIGHT, so a block always fits one fuel grant.
	{
		std::vector<UInt> sorted(leaders.begin(), leaders.end());
		for (size_t i = 0; i < sorted.size(); ++i) {
			const UInt stop = (i + 1 < sorted.size()) ? sorted[i + 1] : endIndex + 1;
			for (UInt p = sorted[i] + MAX_BLOCK_WEIGHT; p < stop; p += MAX_BLOCK_WEIGHT) { leaders.insert(p); }
		}
	}
	/*
		A block's weight stops at its terminator: code between an unconditional terminator and the next leader is never
		executed by entering the block, and the interpreter never charges it.
	*/
	std::vector<UInt> all(leaders.begin(), leaders.end());
	for (size_t i = 0; i < all.size(); ++i) {
		const UInt stop = (i + 1 < all.size()) ? all[i + 1] : endIndex + 1;
		UInt end = stop;
		for (UInt p = all[i]; p < stop; ++p) {
			const Int op = code[p].opcode;
			if (op == OP_GOTO || op == OP_SWCH || op == OP_RETU || op == OP_TAIL_CC || op == OP_TAIL_VC) {
				end = p + 1;
				break;
			}
		}
		weight[all[i]] = end - all[i];
	}
}

/*
	Opcodes a resident loop body may contain. ADRL, GETL/SETL and the pointer and indexed PEEK/POKE forms are excluded:
	they flush the map or churn scratch registers every iteration, which loses to fill-on-use.
*/
static bool jitResidencySafe(Int op) {
	switch (op) {
		case OP_ADRL: case OP_GETL_VVV: case OP_SETL_VVV: case OP_SETL_VVC:
		case OP_PEEK_VCV: case OP_PEEK_VVV:
		case OP_POKE_CVV: case OP_POKE_CVC: case OP_POKE_VVV: case OP_POKE_VVC:
			return false;
		default:
			return isCacheLowered(op);
	}
}

// Opcodes whose operands route through the cache; any other opcode barriers it (JitCompilerResearch.md section 5.7).
bool isCacheLowered(Int op) {
	switch (op) {
		case OP_MOVE_VV: case OP_MOVE_VC:
		case OP_ADDI_VVV: case OP_ADDI_VVC:
		case OP_SUBI_VVV: case OP_SUBI_VVC: case OP_SUBI_VCV:
		case OP_MULI_VVV: case OP_MULI_VVC:
		case OP_ANDI_VVV: case OP_ANDI_VVC:
		case OP_IORI_VVV: case OP_IORI_VVC:
		case OP_XORI_VVV: case OP_XORI_VVC:
		case OP_ABSI: case OP_ABSF: case OP_FLOF:
		case OP_ADDF_VVV: case OP_ADDF_VVC:
		case OP_SUBF_VVV: case OP_SUBF_VVC: case OP_SUBF_VCV:
		case OP_MULF_VVV: case OP_MULF_VVC:
		case OP_DIVF_VVV: case OP_DIVF_VVC: case OP_DIVF_VCV:
		case OP_FTOI_VVC: case OP_ITOF_VVC:
		case OP_SHLI_VVV: case OP_SHLI_VVC: case OP_SHLI_VCV:
		case OP_SHRI_VVV: case OP_SHRI_VVC: case OP_SHRI_VCV:
		case OP_SHRU_VVV: case OP_SHRU_VVC: case OP_SHRU_VCV:
		case OP_PEEK_VC: case OP_POKE_CV: case OP_POKE_CC: case OP_ADRL:
		case OP_PEEK_VCV: case OP_PEEK_VVV:
		case OP_POKE_CVV: case OP_POKE_CVC: case OP_POKE_VVV: case OP_POKE_VVC:
		case OP_GETL_VVV: case OP_SETL_VVV: case OP_SETL_VVC:
		case OP_DIVI_VVV: case OP_DIVI_VVC: case OP_DIVI_VCV:
		case OP_MODI_VVV: case OP_MODI_VVC: case OP_MODI_VCV:
		case OP_FORi_VVB: case OP_FORi_VCB:
		case OP_LSSI_VVB: case OP_LSSI_VCB: case OP_LSSI_CVB:
		case OP_EQUI_VVB: case OP_EQUI_VCB:
		case OP_NLSI_VVB: case OP_NLSI_VCB: case OP_NLSI_CVB:
		case OP_NEQI_VVB: case OP_NEQI_VCB:
		case OP_LSSF_VVB: case OP_LSSF_VCB: case OP_LSSF_CVB:
		case OP_EQUF_VVB: case OP_EQUF_VCB:
		case OP_NLSF_VVB: case OP_NLSF_VCB: case OP_NLSF_CVB:
		case OP_NEQF_VVB: case OP_NEQF_VCB:
		case OP_GOTO:																									// flushes inside its case: reconcile (backward, qualified) or barrier
			return true;
		default:
			return false;
	}
}

/*
	Residency loop headers (see GAZLJit.h): back-edge targets that no forward branch or SWCH reaches. A body may branch
	forward internally, but a non-residency-safe op, a backward branch to anything but the header, or a side entry from
	outside disqualifies it; a side entry would arrive without the map's registers loaded.
*/
void JitCompiler::jitResidencyLeaders(const Instruction* code, UInt funcStart, UInt endIndex
		, const Value* memory, std::map<UInt, UInt>& loopExtent) {
	std::set<UInt> excluded;
	for (UInt j = funcStart; j <= endIndex; ++j) {
		UInt target;
		if (jitBranchTarget(code, j, target)) {
			if (target > j) { excluded.insert(target); }																	// a forward edge arrives with an empty cache
			else { std::map<UInt, UInt>::iterator it = loopExtent.find(target); if (it == loopExtent.end() || it->second < j) { loopExtent[target] = j; } }
		} else if (code[j].opcode == OP_SWCH) {
			const std::vector<UInt> targets = switchTargets(code, j, memory);
			excluded.insert(targets.begin(), targets.end());
		}
	}
	for (std::set<UInt>::const_iterator it = excluded.begin(); it != excluded.end(); ++it) { loopExtent.erase(*it); }
	for (std::map<UInt, UInt>::iterator it = loopExtent.begin(); it != loopExtent.end(); ) {
		const UInt header = it->first, end = it->second;
		bool safe = true;
		for (UInt j = header; j <= end && safe; ++j) {
			if (!jitResidencySafe(code[j].opcode)) { safe = false; }
			UInt target;
			if (jitBranchTarget(code, j, target) && target < j && target != header) { safe = false; }
		}
		for (UInt j = funcStart; j <= endIndex && safe; ++j) {
			if (j >= header && j <= end) { continue; }
			UInt target;
			if (jitBranchTarget(code, j, target)) {
				if (target > header && target <= end) { safe = false; }
			} else if (code[j].opcode == OP_SWCH) {
				const std::vector<UInt> targets = switchTargets(code, j, memory);
				for (size_t k = 0; k < targets.size(); ++k) {
					if (targets[k] > header && targets[k] <= end) { safe = false; }
				}
			}
		}
		if (safe) { ++it; } else { loopExtent.erase(it++); }
	}
}

// Takes ownership of the executable page; nothing after makeExecutable can throw, so a page is never leaked.
JitModule::JitModule(const EmittedModule& emitted)
		: ownedPage(0)
		, ownedWords(emitted.code.size())
		, entries(emitted.entryByteOffsets.size())																		// may throw (bad_alloc) before any page is acquired
		, dispatch(0) {
	ownedPage = makeExecutable(emitted.code.empty() ? 0 : &emitted.code[0], emitted.code.size());
	if (ownedPage == 0) {																								// should not happen after jitAvailable()
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
	std::swap(ownedPage, other.ownedPage);
	std::swap(ownedWords, other.ownedWords);
	entries.swap(other.entries);
	std::swap(dispatch, other.dispatch);
}

// A backend bug: asserts, and also throws so a release build degrades to the interpreter. Never returns.
void JitCompiler::throwUnlowerableOpcode(Int opcode) {
	assert(0 && "JIT backend does not cover a finalized opcode (backend bug)");
	char message[64];
	std::snprintf(message, sizeof (message), "JIT: backend cannot lower finalized opcode 0x%X", static_cast<unsigned>(opcode));
	throw JitException(message);
}

void buildUseSchedule(const Instruction* code, UInt from, UInt to, UseSchedule& schedule) {
	for (UInt j = from; j <= to; ++j) {
		OperandRole roles[3];
		operandRoles(code[j].opcode, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if ((roles[k] & OPERAND_SLOT_READ) != 0) { schedule[operands[k]->i].push_back(j); }			// ascending j -> lists stay sorted
		}
	}
}

// join two realms in the lattice BOTTOM < {NONFRAME, MYFRAME} < UNKNOWN (NONFRAME and MYFRAME are incomparable).
static int joinRealm(int a, int b) {
	if (a == REALM_BOTTOM) { return b; }
	if (b == REALM_BOTTOM || a == b) { return a; }
	return REALM_UNKNOWN;
}

bool isDataAddress(const AssembledProgram& program, Pointer pointer) {
	const UInt constantsSize = program.sizes.constsSize - 1;
	return pointer - (MEMORY_OFFSET - constantsSize) < constantsSize + program.sizes.globalsSize;
}

void buildPointerRealms(const AssembledProgram& program, UInt from, UInt to, std::map<Int, int>& realm) {
	const Instruction* const code = program.code;
	// Pass 1: a slot read before it is written is a received parameter, so NONFRAME.
	std::set<Int> written;
	for (UInt j = from; j <= to; ++j) {
		OperandRole roles[3];
		operandRoles(code[j].opcode, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if ((roles[k] & OPERAND_SLOT_READ) != 0 && written.find(operands[k]->i) == written.end()) {
				realm.insert(std::make_pair(operands[k]->i, static_cast<int>(REALM_NONFRAME)));
			}
		}
		for (int k = 0; k < 3; ++k) {
			if ((roles[k] & OPERAND_SLOT_WRITE) != 0) { written.insert(operands[k]->i); }
		}
	}

	// Pass 2: flow-insensitive fixed point, where a slot's realm is the join over all its definitions.
	bool changed = true;
	while (changed) {
		changed = false;
		for (UInt j = from; j <= to; ++j) {
			const Int op = code[j].opcode;
			OperandRole roles[3];
			operandRoles(op, roles);
			if ((roles[0] & OPERAND_SLOT_WRITE) == 0) { continue; }
			int produced;
			switch (op) {
				case OP_ADRL: produced = REALM_MYFRAME; break;
				case OP_MOVE_VC: produced = isDataAddress(program, code[j].p1.p) ? REALM_NONFRAME : REALM_UNKNOWN; break;	// global/const symbol vs bare int
				case OP_MOVE_VV: {
					const std::map<Int, int>::const_iterator it = realm.find(code[j].p1.i);
					produced = (it != realm.end()) ? it->second : REALM_BOTTOM;
					break;
				}
				case OP_ADDI_VVV: case OP_ADDI_VVC: case OP_SUBI_VVV: case OP_SUBI_VVC: case OP_SUBI_VCV: {
					const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
					produced = REALM_BOTTOM;																// pointer +/- int keeps the pointer's realm; join the slot reads
					for (int k = 0; k < 3; ++k) {
						if ((roles[k] & OPERAND_SLOT_READ) != 0) {
							const std::map<Int, int>::const_iterator it = realm.find(operands[k]->i);
							produced = joinRealm(produced, (it != realm.end()) ? it->second : REALM_BOTTOM);
						}
					}
					break;
				}
				default: produced = REALM_UNKNOWN; break;													// loads, calls, native, and every non-pointer producer
			}
			const Int dst = code[j].p0.i;
			const int merged = joinRealm(realm.count(dst) ? realm[dst] : REALM_BOTTOM, produced);
			if (!realm.count(dst) || realm[dst] != merged) { realm[dst] = merged; changed = true; }
		}
	}
}

// Appends the successors of `j`, conservatively: an extra successor only widens liveness.
static void jitSuccessors(const Instruction* code, UInt from, UInt to, const Value* memory, UInt j, std::vector<UInt>& succ) {
	const Int op = code[j].opcode;
	if (op == OP_RETU) { return; }
	if (op == OP_GOTO) { succ.push_back(static_cast<UInt>(static_cast<Int>(j) + code[j].p0.i)); return; }
	UInt target;
	if (jitBranchTarget(code, j, target)) { succ.push_back(target); }
	else if (op == OP_SWCH) {
		const std::vector<UInt> targets = switchTargets(code, j, memory);
		succ.insert(succ.end(), targets.begin(), targets.end());
	}
	if (j + 1 <= to) { succ.push_back(j + 1); }
}

int LiveSets::slotIndex(Int slot) const {
	const std::vector<Int>::const_iterator it = std::lower_bound(slots.begin(), slots.end(), slot);
	if (it == slots.end() || *it != slot) { return -1; }
	return static_cast<int>(it - slots.begin());
}

bool LiveSets::isLive(UInt at, Int slot) const {
	const int bit = slotIndex(slot);																					// a slot this function never touches is never live
	if (bit < 0 || bits.empty()) { return false; }
	assert(at >= firstIndex && (at - firstIndex) * rowWords() < bits.size());
	return (row(at)[bit / 32] & (1u << (bit % 32))) != 0;
}

bool LiveSets::empty(UInt at) const {
	if (bits.empty()) { return true; }																					// never built: the caller's loopExtent was empty
	assert(at >= firstIndex && (at - firstIndex) * rowWords() < bits.size());
	const UInt* r = row(at);
	for (size_t w = 0; w < rowWords(); ++w) { if (r[w] != 0) { return false; } }
	return true;
}

void LiveSets::build(const Instruction* code, UInt from, UInt to, const Value* memory) {
	firstIndex = from;
	const size_t rowCount = to - from + 1;
	for (UInt j = from; j <= to; ++j) {																					// the row's bit order: every slot this function touches, sorted
		OperandRole roles[3];
		operandRoles(code[j].opcode, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if ((roles[k] & (OPERAND_SLOT_READ | OPERAND_SLOT_WRITE)) != 0) { slots.push_back(operands[k]->i); }
		}
	}
	std::sort(slots.begin(), slots.end());
	slots.erase(std::unique(slots.begin(), slots.end()), slots.end());
	if (slots.empty()) { return; }
	const size_t words = rowWords();
	// gen (reads) / kill (writes) per instruction; a FORi counter is read-modify-write, so it is both.
	std::vector<UInt> gen(rowCount * words, 0), kill(rowCount * words, 0);
	for (UInt j = from; j <= to; ++j) {
		OperandRole roles[3];
		operandRoles(code[j].opcode, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		for (int k = 0; k < 3; ++k) {
			if ((roles[k] & (OPERAND_SLOT_READ | OPERAND_SLOT_WRITE)) == 0) { continue; }
			const int bit = slotIndex(operands[k]->i);
			assert(bit >= 0);
			const size_t at = (j - from) * words + bit / 32;
			const UInt mask = (1u << (bit % 32));
			if ((roles[k] & OPERAND_SLOT_READ) != 0) { gen[at] |= mask; }
			if ((roles[k] & OPERAND_SLOT_WRITE) != 0) { kill[at] |= mask; }
		}
	}
	/*
		Backward fixed point, accumulated in place: the transfer only adds bits, so a reverse sweep reaches the same
		least fixed point. A self-looping GOTO/FORi reads the row it is writing, which is safe as those bits are set.
	*/
	bits.assign(rowCount * words, 0);
	std::vector<UInt> succ;
	bool changed = true;
	while (changed) {
		changed = false;
		for (UInt jj = to + 1; jj > from; --jj) {
			const UInt j = jj - 1;
			UInt* in = &bits[(j - from) * words];
			const UInt* killed = &kill[(j - from) * words];
			const UInt* generated = &gen[(j - from) * words];
			succ.clear();
			jitSuccessors(code, from, to, memory, j, succ);
			for (size_t w = 0; w < words; ++w) {
				UInt live = in[w] | generated[w];
				for (size_t sIndex = 0; sIndex < succ.size(); ++sIndex) {
					const UInt t = succ[sIndex];
					if (t < from || t > to) { continue; }																// an edge out of this function contributes nothing
					live |= (bits[(t - from) * words + w] & ~killed[w]);
				}
				if (live != in[w]) { in[w] = live; changed = true; }
			}
		}
	}
}

/*
	Loop-body working sets (see GAZLJit.h). The class per operand must match the lowering both backends share; a slot
	used both ways lands in both class sets.
*/
void buildLoopSets(const Instruction* code, UInt from, UInt to, std::set<Int>& readSlots, std::set<Int>& writtenSlots
		, std::set<Int>& generalSlots, std::set<Int>& floatSlots) {
	for (UInt j = from; j <= to; ++j) {
		const Int op = code[j].opcode;
		OperandRole roles[3];
		operandRoles(op, roles);
		const Value* operands[3] = { &code[j].p0, &code[j].p1, &code[j].p2 };
		bool isFloat[3] = { false, false, false };
		switch (op) {
			case OP_ADDF_VVV: case OP_ADDF_VVC: case OP_SUBF_VVV: case OP_SUBF_VVC: case OP_SUBF_VCV:
			case OP_MULF_VVV: case OP_MULF_VVC:
			case OP_DIVF_VVV: case OP_DIVF_VVC: case OP_DIVF_VCV:
			case OP_ABSF: case OP_FLOF:
			case OP_LSSF_VVB: case OP_LSSF_VCB: case OP_LSSF_CVB:
			case OP_EQUF_VVB: case OP_EQUF_VCB:
			case OP_NLSF_VVB: case OP_NLSF_VCB: case OP_NLSF_CVB:
			case OP_NEQF_VVB: case OP_NEQF_VCB:
				isFloat[0] = isFloat[1] = isFloat[2] = true; break;
			case OP_FTOI_VVC: isFloat[1] = true; break;																// float read -> general write
			case OP_ITOF_VVC: isFloat[0] = true; break;																// general read -> float write
			default: break;
		}
		for (int k = 0; k < 3; ++k) {
			if ((roles[k] & OPERAND_SLOT_READ) != 0) { readSlots.insert(operands[k]->i); }
			if ((roles[k] & OPERAND_SLOT_WRITE) != 0) { writtenSlots.insert(operands[k]->i); }
			if (roles[k] != OPERAND_OTHER) {
				if (isFloat[k]) { floatSlots.insert(operands[k]->i); } else { generalSlots.insert(operands[k]->i); }
			}
		}
	}
}

// Arch-neutral bookkeeping that reaches machine code only through the backend. Line `i` holds `registersOf(class)[i]`.
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
	A free pool index, else a spilled unpinned victim: the furthest next read (Belady) with a schedule, otherwise LRU.
	Either is correct, so an imprecise schedule only costs speed.
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
		const bool better = (useSchedule != 0) ? (key > best) : (key < best);
		if (victim < 0 || better) { victim = static_cast<int>(i); best = key; }
	}
	assert(victim >= 0 && "register pool exhausted by one instruction's pinned operands");
	spillLine(registerClass, victim);
	lines[victim].occupied = false;
	return victim;
}

// A slot lives in one file at a time: drop its copy in the other file, spilling it first if `spillFirst`.
void RegisterCache::evictOtherClass(Int slot, RegisterClass wantedClass, bool spillFirst) {
	const RegisterClass other = (wantedClass == GENERAL_REGISTER) ? FLOAT_REGISTER : GENERAL_REGISTER;
	size_t count;
	Line* lines = linesOf(other, count);
	for (size_t i = 0; i < count; ++i) {
		if (lines[i].occupied && !lines[i].scratchTemp && lines[i].slot == slot) {
			if (spillFirst) { spillLine(other, static_cast<int>(i)); }
			lines[i].occupied = false;
			lines[i].dirty = false;																	// unoccupied: leave no stale flag for the next occupant to read
			return;
		}
	}
}

int RegisterCache::read(Int slot, RegisterClass registerClass) {
	size_t count;
	Line* lines = linesOf(registerClass, count);
	const int* registers = registersOf(registerClass);
	for (size_t i = 0; i < count; ++i) {
		if (lines[i].occupied && !lines[i].scratchTemp && lines[i].slot == slot) {
			lines[i].pinned = true;
			lines[i].lastUse = ++useClock;
			lines[i].nextUse = nextReadAfter(slot);
			return registers[i];
		}
	}
	/*
		Miss, but the value may sit in the other file (PEEK lands a float in the general file): move it register to
		register, carrying its dirty flag. The pools are disjoint, so acquire() cannot take the source.
	*/
	{
		const RegisterClass other = (registerClass == GENERAL_REGISTER) ? FLOAT_REGISTER : GENERAL_REGISTER;
		size_t otherCount;
		Line* otherLines = linesOf(other, otherCount);
		const int* otherRegisters = registersOf(other);
		for (size_t k = 0; k < otherCount; ++k) {
			if (otherLines[k].occupied && !otherLines[k].scratchTemp && otherLines[k].slot == slot) {
				/*
					Only a dirty copy is bridged; a clean one just refills from the home. Whether the bridge also
					writes the home is the backend's call (RegisterCacheBackend::bridgeWritesHome).
				*/
				if (!otherLines[k].dirty) { otherLines[k].occupied = false; break; }	// clean: drop it and fill from the home
				const int sourceRegister = otherRegisters[k];
				const bool writeHome = cacheBackend.bridgeWritesHome();
				if (writeHome) { cacheBackend.emitSpill(slot, sourceRegister, other); }				// clean the line here, or leave it dirty and let the block end do it
				otherLines[k].occupied = false;
				otherLines[k].dirty = false;
				const int b = acquire(registerClass);
				Line& bridged = lines[b];
				bridged.occupied = true;
				bridged.scratchTemp = false;
				bridged.slot = slot;
				bridged.registerClass = registerClass;
				bridged.dirty = !writeHome;												// in sync if we just wrote it; otherwise it still owes a store
				bridged.pinned = true;
				bridged.lastUse = ++useClock;
				bridged.nextUse = nextReadAfter(slot);
				cacheBackend.emitCrossMove(registers[b], registerClass, sourceRegister);
				return registers[b];
			}
		}
	}
	const int i = acquire(registerClass);
	Line& line = lines[i];
	line.occupied = true;
	line.scratchTemp = false;
	line.slot = slot;
	line.registerClass = registerClass;
	line.dirty = false;
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
			lines[i].dirty = true;
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
	Snapshot the dirty lines for a trap arm's stores, without changing the model (the arm never rejoins). Call it at
	the trap branch, where the model matches the trap path's registers; later, the op's own define holds garbage there
	and an eviction may reuse a captured register.
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
	The per-class cap on the entries capture() keeps, leaving headroom. A multi-block loop whose working set in either
	class exceeds it stays fill-on-use, since its interior leaders would keep refilling evicted map lines.
*/
void RegisterCache::residencyCapacity(size_t& generalMax, size_t& floatMax) const {
	const size_t RESIDENCY_HEADROOM = 3;
	const size_t gc = registerPool.generalCount, fc = registerPool.floatCount;
	generalMax = gc - ((RESIDENCY_HEADROOM < gc / 2) ? RESIDENCY_HEADROOM : gc / 2);
	floatMax = fc - ((RESIDENCY_HEADROOM < fc / 2) ? RESIDENCY_HEADROOM : fc / 2);
}

/*
	Snapshot the resident lines as a loop header's entry state (see ResidencyMap). Unwanted slots are spilled and
	dropped. A slot the loop writes is modeled dirty from the first iteration, or an eviction could skip its store; a
	read-only slot is spilled once here, before the header, so register==home throughout the loop.
*/
void RegisterCache::capture(ResidencyMap& map, const std::set<Int>& wantedGeneral, const std::set<Int>& wantedFloat, const std::set<Int>& writtenInLoop) {
	assert(map.entries.empty() && "a header's entry map is captured once");
	size_t classMax[2];
	residencyCapacity(classMax[0], classMax[1]);
	size_t kept[2] = { 0, 0 };
	std::set<Int> mapped[2];
	for (int c = 0; c < 2; ++c) {																						// pass 1: keep resident wanted lines, spill the rest
		const RegisterClass registerClass = (c == 0) ? GENERAL_REGISTER : FLOAT_REGISTER;
		const std::set<Int>& wanted = (c == 0) ? wantedGeneral : wantedFloat;
		size_t count;
		Line* lines = linesOf(registerClass, count);
		const int* registers = registersOf(registerClass);
		const size_t keepMax = classMax[c];
		for (size_t i = 0; i < count; ++i) {
			if (!lines[i].occupied) { continue; }
			assert(!lines[i].pinned && !lines[i].scratchTemp && "capture between instructions only");
			if (wanted.count(lines[i].slot) == 0 || kept[c] >= keepMax) {												// unwanted (unread / dead at header / dual-class), or full
				spillLine(registerClass, static_cast<int>(i));
				lines[i].occupied = false;
				continue;
			}
			const bool expectDirty = (writtenInLoop.count(lines[i].slot) != 0);
			if (!expectDirty) { spillLine(registerClass, static_cast<int>(i)); }
			ResidencyMap::Entry entry = { lines[i].slot, registerClass, static_cast<int>(i), registers[i], expectDirty };
			map.entries.push_back(entry);
			lines[i].dirty = expectDirty;
			mapped[c].insert(lines[i].slot);
			++kept[c];
		}
	}
	/*
		Pass 2, only after every class has spilled: preload the absent wanted slots. A wanted-general slot may sit dirty
		in a float line, and preloading it before the float pass spilled that line would read a stale home.
	*/
	for (int c = 0; c < 2; ++c) {
		const RegisterClass registerClass = (c == 0) ? GENERAL_REGISTER : FLOAT_REGISTER;
		const std::set<Int>& wanted = (c == 0) ? wantedGeneral : wantedFloat;
		size_t count;
		Line* lines = linesOf(registerClass, count);
		const int* registers = registersOf(registerClass);
		const size_t keepMax = classMax[c];
		for (std::set<Int>::const_iterator s = wanted.begin(); s != wanted.end() && kept[c] < keepMax; ++s) {
			if (mapped[c].count(*s) != 0) { continue; }
			size_t i = 0;
			while (i < count && lines[i].occupied) { ++i; }
			if (i == count) { break; }
			cacheBackend.emitFill(registers[i], *s, registerClass);
			const bool expectDirty = (writtenInLoop.count(*s) != 0);
			lines[i].occupied = true;
			lines[i].scratchTemp = false;
			lines[i].slot = *s;
			lines[i].registerClass = registerClass;
			lines[i].pinned = false;
			lines[i].dirty = expectDirty;
			lines[i].lastUse = ++useClock;
			lines[i].nextUse = nextReadAfter(*s);
			ResidencyMap::Entry entry = { *s, registerClass, static_cast<int>(i), registers[i], expectDirty };
			map.entries.push_back(entry);
			++kept[c];
		}
	}
}

void filterResidencyMap(const ResidencyMap& full, const LiveSets& liveIn, UInt at, ResidencyMap& out) {
	for (size_t k = 0; k < full.entries.size(); ++k) {
		if (liveIn.isLive(at, full.entries[k].slot)) { out.entries.push_back(full.entries[k]); }
	}
}

void establishLeader(RegisterCache& cache, const Instruction* code, UInt j, const std::map<UInt, UInt>& loopExtent
		, const std::map<UInt, UInt>& loopWeight, const LiveSets& liveIn
		, std::map<UInt, ResidencyMap>& entryMaps, bool& resident, UInt& residentEnd) {
	std::map<UInt, UInt>::const_iterator loopIt = loopExtent.find(j);
	bool gated = false;
	// A header that is an interior leader of an enclosing resident loop already has a map: reconcile, do not capture.
	const bool freshHeader = (loopIt != loopExtent.end() && entryMaps.count(j) == 0);
	if (freshHeader) {
		std::map<UInt, UInt>::const_iterator w0 = loopWeight.upper_bound(j);
		const bool multiBlock = (w0 != loopWeight.end() && w0->first <= loopIt->second);
		std::set<Int> readSlots, writtenSlots, generalSlots, floatSlots;
		buildLoopSets(code, j, loopIt->second, readSlots, writtenSlots, generalSlots, floatSlots);
		/*
			Wanted bindings: read in the loop, live-in at the header, and single-class (a dual-class slot gets
			evicted across classes mid-loop). The pressure gate applies to this set.
		*/
		std::set<Int> wantedGeneral, wantedFloat;
		for (std::set<Int>::const_iterator si = readSlots.begin(); si != readSlots.end(); ++si) {
			if (!liveIn.isLive(j, *si)) { continue; }
			const bool g = (generalSlots.count(*si) != 0), f = (floatSlots.count(*si) != 0);
			if (g && f) { continue; }
			if (f) { wantedFloat.insert(*si); } else { wantedGeneral.insert(*si); }
		}
		size_t generalMax, floatMax;
		cache.residencyCapacity(generalMax, floatMax);
		if (multiBlock && (wantedGeneral.size() > generalMax || wantedFloat.size() > floatMax)) {						// pressure gate: a strangling map loses to fill-on-use
			gated = true;
			cache.barrier();
		} else {
			ResidencyMap& headerMap = entryMaps[j];
			cache.capture(headerMap, wantedGeneral, wantedFloat, writtenSlots);
			for (std::map<UInt, UInt>::const_iterator w = w0; w != loopWeight.end() && w->first <= loopIt->second
					; ++w) {
				filterResidencyMap(headerMap, liveIn, w->first, entryMaps[w->first]);									// interior leader: the bindings live there
			}
			resident = !headerMap.entries.empty(); residentEnd = loopIt->second;
		}
	}
	if (!freshHeader || gated) {
		std::map<UInt, ResidencyMap>::const_iterator m = entryMaps.find(j);
		if (m != entryMaps.end()) { cache.reconcileTo(m->second); }													// interior leader or inherited header: re-establish its entry state
		else { cache.barrier(); }																						// any other leader starts empty
	}
}

bool planConditionalEdge(RegisterCache& cache, std::map<UInt, ResidencyMap>& entryMaps, UInt target, bool resident
		, ResidencyMap& dirty) {
	std::map<UInt, ResidencyMap>::iterator it = entryMaps.find(target);
	if (it != entryMaps.end()) { cache.reconcileTo(it->second); return false; }
	if (resident) { cache.captureDirtyLines(dirty); return true; }
	cache.barrier();
	return false;
}

void spillResidencyMap(RegisterCacheBackend& backend, const ResidencyMap& map) {
	for (size_t k = 0; k < map.entries.size(); ++k) {
		const ResidencyMap::Entry& entry = map.entries[k];
		if (entry.expectDirty) { backend.emitSpill(entry.slot, entry.physicalRegister, entry.registerClass); }
	}
}

void fillResidencyMap(RegisterCacheBackend& backend, const ResidencyMap& map) {
	for (size_t k = 0; k < map.entries.size(); ++k) {
		const ResidencyMap::Entry& entry = map.entries[k];
		backend.emitFill(entry.physicalRegister, entry.slot, entry.registerClass);
	}
}

/*
	Re-establish a map's entry state: spill and drop every line it does not want (a wanted slot in the wrong register
	included), then fill the missing entries. Emits only loads and stores, so it may sit between a compare and a branch.
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
			line.dirty = false;																			// filled from the home: never inherit the previous occupant's flag
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
	flushAndClear();																									// release: spill rather than drop a dirty line, even if the contract was broken
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
	The run-state field offsets the machine code bakes in. offsetof on this polymorphic type is safe (single
	inheritance, fixed layout), so -Winvalid-offsetof is silenced locally.
*/
Offsets JitProcessor::layout() {
	Offsets o;
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
	o.dsp = offsetof(JitProcessor, dsp); o.mb = offsetof(JitProcessor, memoryBase);
	o.fuel = offsetof(JitProcessor, clockCyclesLeft); o.ipsp = offsetof(JitProcessor, ipsp);
	o.resume = offsetof(JitProcessor, resume);
	o.natives = offsetof(JitProcessor, natives);
	o.funcentries = offsetof(JitProcessor, funcEntries);
	o.memsize = offsetof(JitProcessor, memorySize); o.rwmemsize = offsetof(JitProcessor, rwMemorySize);
	o.dsend = offsetof(JitProcessor, dataStackEnd); o.ipsend = offsetof(JitProcessor, ipStackEnd);
	o.nativeafter = offsetof(JitProcessor, nativeAfter);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
	return o;
}

Status JitProcessor::enterCall(Target target) {
	const Status s = Processor::enterCall(target);
	if (s != OK) {
		return s;
	}
	resume = funcEntries[target - FUNCTION_OFFSET];
	return OK;
}

/*
	On native OK the compiled call sequence jumps through `nativeAfter`, so each push links the current continuation
	into a frame and retargets `nativeAfter` at the callee. Callees run LIFO and chain back to `after`, which restores
	dsp; that is why every pushed frame stores the window (this->dsp), unlike the interpreter's frames.
*/
Value* JitProcessor::pushCall(Target target) {
	UInt ui = target - FUNCTION_OFFSET;
	if (ui >= functionCount) return 0;
	if (nativeAfter == 0) return 0;										// only valid from inside a native callback
	if (ipsp >= ipStackEnd) return 0;
	const Instruction* func = codeBase + functionTable[ui];
	if (static_cast<UInt>(func->p0.i) + static_cast<UInt>(func->p1.i) > static_cast<UInt>(dataStackEnd - this->dsp)) return 0;	// early FUNC check
	ipsp->returnAddress = nativeAfter;									// current continuation: `after`, or an earlier-pushed entry
	ipsp++->dsp = this->dsp;											// the window (see above)
	nativeAfter = funcEntries[ui];										// the OK path jumps straight into the pushed callee
	return this->dsp;
}

Status JitProcessor::run() {
	typedef int (*Disp)(JitProcessor*);
	// nativeAfter is live across a native call, and a native that nests run() would clobber it: save and restore it.
	void* const outerNativeAfter = nativeAfter;
	const Status s = static_cast<Status>(reinterpret_cast<Disp>(jitDispatch)(this));
	nativeAfter = outerNativeAfter;
	return s;
}

// Shared by both constructors. The module must be compiled: an empty one has no dispatcher.
void JitProcessor::bindModule(const JitModule& module) {
	assert(module.isCompiled() && "JitProcessor requires a compiled JitModule");
	nativeAfter = 0;
	funcEntries = module.entryTable();
	jitDispatch = module.dispatchEntry();
}

/*
	Helpers for the jitAvailable() probe. Its stub runs under a fault guard and calls back into C, so a page that cannot
	execute or a broken native calling convention makes the probe fail instead of crashing. The stub is one block of
	32-bit words (what makeExecutable takes), little-endian on both targets.
*/
namespace {

// The native ABI CALL_NVC bakes in: a C function of one pointer, returning Status in one integer register.
#if __cplusplus >= 201103L
static_assert(sizeof(Status) <= sizeof(void*), "JIT CALL_NVC assumes a native's Status returns in one integer register");
#endif

// `uint32_t stub(ProbeCallee callee, const void* arg)`: returns `callee(arg)`, called through the native convention.
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
// Adds 0x1111, so a passing probe proves the call really ran and the argument arrived intact.
uint32_t probeCallee(const void* arg) { return *static_cast<const uint32_t*>(arg) + 0x1111u; }
const uint32_t PROBE_INPUT = 0xAFCDu;																					// 0xAFCD + 0x1111 == 0xC0DE
#endif

#if !defined(_WIN32) && !defined(GAZL_NO_PROBE_STUB)
sigjmp_buf gProbeJump;
void probeFaultHandler(int) { siglongjmp(gProbeJump, 1); }
#endif

#if !defined(GAZL_NO_PROBE_STUB)
// True only if `func` returns 0xC0DE; a fault (signal or structured exception) gives false.
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

#if defined(_M_X64) || defined(__x86_64__)
/*
	The x64 backend needs SSE4.1 for `roundss` (FLOF), which the SSE2 probe stub cannot detect, so a host without it
	uses the interpreter. arm64 emits only base ARMv8-A.
*/
static bool x64BaselineIsa() {
	unsigned int regs[4] = { 0, 0, 0, 0 };
	#if defined(_MSC_VER)
		__cpuid(reinterpret_cast<int*>(regs), 1);
	#elif defined(__GNUC__) || defined(__clang__)
		if (__get_cpuid(1, &regs[0], &regs[1], &regs[2], &regs[3]) == 0) return false;
	#else
		return false;																			// unknown compiler: cannot ask, so do not promise
	#endif
	return (regs[2] & (1u << 19)) != 0;														// CPUID.01H:ECX.SSE4_1
}
#endif

bool jitAvailable() {
#if defined(GAZL_NO_PROBE_STUB)
	return false;
#else
	static int cached = -1;																								// -1 unknown / 0 no / 1 yes; probed once (single-threaded startup)
	if (cached < 0) {
#if defined(_M_X64) || defined(__x86_64__)
		if (!x64BaselineIsa()) { cached = 0; return false; }								// ISA first, before spending a page on the probe
#endif
		const size_t words = sizeof(PROBE_STUB) / sizeof(PROBE_STUB[0]);
		void* page = makeExecutable(PROBE_STUB, words);																	// a policy denial fails here
		if (page == 0) {
			cached = 0;
		} else {
			ProbeFunc func;
			std::memcpy(&func, &page, sizeof(func));																	// void* -> function pointer without an ISO-illegal cast
			cached = runProbe(func) ? 1 : 0;																			// the stub actually runs and returns 0xC0DE
			freeExecutable(page, words);
		}
	}
	return cached != 0;
#endif
}

}																														// namespace GAZL
