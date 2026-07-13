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
	Executable-memory (W^X) interface for the JIT. Exactly one implementation is compiled per target OS — the build
	selects GAZLJitMemMacOS.cpp / GAZLJitMemPosix.cpp / GAZLJitMemWindows.cpp — so this stays free of #ifdef branches.
	Isolating the per-OS JIT-hardening story here (Apple's pthread_jit_write_protect toggle, POSIX mprotect, Windows
	VirtualAlloc/FlushInstructionCache) keeps it from leaking into the architecture backends, which vary on a different
	axis (see GAZLJit.h). This is platform-specific but architecture-neutral.
*/

#ifndef GAZLJitMem_h
#define GAZLJitMem_h

#include <stdint.h>
#include <cstddef>

namespace GAZL {

/*
	Copy `wordCount` machine words into a fresh page, make it executable (honoring W^X), flush the i-cache, and return
	the entry pointer — or null on failure.
*/
void* makeExecutable(const uint32_t* words, size_t wordCount);

// Release a page returned by makeExecutable (no-op if `page` is null). `wordCount` must match the makeExecutable call.
void freeExecutable(void* page, size_t wordCount);

} // namespace GAZL

#endif
