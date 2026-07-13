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
	Whole-program compile driver: lower every function, emit the shared dispatcher, publish one executable page, and bind
	it to a JitEngine. Kept in its own translation unit because it pulls in makeExecutable() (a platform GAZLJitMem*.cpp
	backend) — GAZLJit.cpp deliberately does not, so the Emitter-only diff test still links without a memory backend.
*/

#include "GAZLJit.h"

#include <vector>

namespace GAZL {

bool compile(JitEngine& engine, const Instruction* code, const UInt* functionTable, UInt functionCount,
		size_t* outCodeWords) {
	const Offsets o = engine.offsets();					// field offsets are instance-independent — any engine will do
	Emitter e;
	std::vector<Label> entryLabels(functionCount);
	std::vector<size_t> entryOffset(functionCount, 0);
	for (UInt k = 0; k < functionCount; ++k) { entryLabels[k] = e.newLabel(); }
	for (UInt ord = 0; ord < functionCount; ++ord) {
		if (!lowerFunction(e, code, engine.memoryImage(), functionTable[ord], o, entryLabels, entryOffset, ord, functionCount)) {
			return false;								// unsupported opcode → caller should fall back to the interpreter
		}
	}
	const size_t dispatchOffset = emitDispatcher(e, o);
	e.finalize();
	void* page = makeExecutable(e.code(), e.wordCount());
	if (page == nullptr) { return false; }
	if (outCodeWords != 0) { *outCodeWords = e.wordCount(); }

	// The page and the ordinal->entry table live for the process (compile-once model; nothing frees them).
	void** funcEntries = new void*[functionCount];
	for (UInt ord = 0; ord < functionCount; ++ord) {
		funcEntries[ord] = reinterpret_cast<char*>(page) + entryOffset[ord] * 4;
	}
	engine.setCompiled(reinterpret_cast<char*>(page) + dispatchOffset * 4, funcEntries);
	return true;
}

} // namespace GAZL
