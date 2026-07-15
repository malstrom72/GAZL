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
	GAZLJitInternal - declarations shared between GAZLJit.cpp and the arch backends (GAZLJitArm64.cpp / GAZLJitX64.cpp),
	NOT part of the public JIT API. Clients include only GAZLJit.h; the backend .cpp files (and GAZLJit.cpp) include this
	for the arch-neutral lowering helpers below.
*/

#ifndef GAZLJitInternal_h
#define GAZLJitInternal_h

#include <map>
#include "GAZL.h"

namespace GAZL {

/*
	Fuel safepoints for one function (arch-neutral, defined in GAZLJit.cpp): the basic-block leaders - function entry,
	branch/SWCH targets, and the instruction after any branch/GOTO/SWCH/CALL - with long straight runs split so no block
	exceeds maxBlockWeight. Fills `weight` with leaderIndex -> charge (instruction span to the next safepoint). Both
	backends emit a fuel check charging `weight` at each leader, so the JIT consumes fuel at ~the interpreter's
	1/instruction rate (fuel-rate fidelity) and worst-case time-to-suspend is bounded by maxBlockWeight (§5.5).
*/
const UInt MAX_BLOCK_WEIGHT = 64;				// fuel-check granularity: the host must grant at least this per resume
void jitFuelSafepoints(const Instruction* code, UInt funcStart, UInt endIndex, const Value* memory
		, UInt maxBlockWeight, std::map<UInt, UInt>& weight);

/*
	A backend hit a finalized opcode it does not cover - a programmer error (every backend must lower all 91 finalized
	opcodes), not a runtime condition. asserts (loud in debug) and, because asserts vanish in release, also throws
	GAZL::JitException so a release build degrades to the interpreter rather than emitting wrong code. Never returns; the
	backends' switch defaults call it. Defined in GAZLJit.cpp.
*/
void throwUnlowerableOpcode(Int opcode);

} // namespace GAZL

#endif
