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
	GAZL -> C++ source translator (the "C++ backend"). A pure source-to-source pass: assembled GAZL
	`Instruction[]` in, a standalone, self-contained, runnable C++ program out. Tier 0 (faithful): the frame
	is `dsp[slot]` and memory is one `Value[]`, exactly as the interpreter - no local lifting yet, so clang
	does the register allocation on the array accesses. It exists to be the optimizer-ceiling benchmark
	baseline (compare vs the interpreter and the arm64 JIT, at -O0 and -O2). See docs/CppBackendSpec.md.

	Run-to-completion only: GAZL calls become ordinary C++ calls (the C stack is the ipStack), so there is
	no fuel/suspend machinery. Returns an empty string if an opcode outside the supported subset is hit.
*/

#ifndef GAZLCpp_h
#define GAZLCpp_h

#include <string>
#include "GAZL.h"

namespace GAZL {

/*
	`promoteLocals` (Tier 1, optional): emit each function's locals as C++ `Value` locals instead of `dsp[-N]` frame
	words, for functions containing no ADRL/GETL/SETL - freeing the C++ optimizer from assuming every MEM store may
	alias them (measured ~2.8x on store-in-loop kernels). Conforming under the §1.1 memory-realm rule: a cross-realm
	access (e.g. a const-base POKE indexed past its symbol into the data stack) would hit the frame word that a promoted
	local no longer observes - UB under the rule, memory-safe-flat under Tier 0. Default off: Tier 0 stays the plain,
	always-available translation.
*/
std::string translateToCpp(const AssembledProgram& program, UInt mainOrdinal, bool promoteLocals = false);

} // namespace GAZL

#endif
