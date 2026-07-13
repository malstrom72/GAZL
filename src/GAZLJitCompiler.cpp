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
	JitCompiler — the JIT's counterpart of `Assembler`: it lowers a whole finalized program (the `Instruction[]`) to
	native code and returns a `JitModule` (the executable page's dispatcher + ordinal→native-entry table). It targets the
	static `JitProcessor::layout()` ABI and never touches a processor instance. Kept in its own translation unit because
	it pulls in makeExecutable() (a platform GAZLJitMem*.cpp backend) — GAZLJit.cpp (the Emitter + lowering substrate)
	deliberately does not, so the Emitter-only diff test still links without a memory backend.
*/

#include "GAZLJit.h"

#include <vector>

namespace GAZL {

void JitCompiler::compile(const Instruction* code, UInt functionCount, const UInt* functionTable,
		const Value* memory, JitModule& out) {
	// `out` starts empty (ok() == false); we only populate it once the whole program has lowered and published.
	const Offsets o = JitProcessor::layout();		// the run-state ABI, obtained without an engine
	Emitter e;
	std::vector<Label> entryLabels(functionCount);
	std::vector<size_t> entryOffset(functionCount, 0);
	for (UInt k = 0; k < functionCount; ++k) { entryLabels[k] = e.newLabel(); }
	for (UInt ord = 0; ord < functionCount; ++ord) {
		if (!lowerFunction(e, code, memory, functionTable[ord], o, entryLabels, entryOffset, ord, functionCount)) {
			return;								// unsupported opcode → caller should fall back to the interpreter
		}
	}
	const size_t dispatchOffset = emitDispatcher(e, o);
	e.finalize();
	const size_t words = e.wordCount();
	void* page = makeExecutable(e.code(), words);
	if (page == 0) { return; }

	void** entries = new void*[functionCount];
	for (UInt ord = 0; ord < functionCount; ++ord) {
		entries[ord] = reinterpret_cast<char*>(page) + entryOffset[ord] * 4;
	}
	out.dispatch = reinterpret_cast<char*>(page) + dispatchOffset * 4;
	out.nativeEntries = entries;
	out.codeWords = words;
	out.ownedPage = page;						// hand the page + table to `out`; its destructor frees them
	out.ownedWords = words;
}

} // namespace GAZL
