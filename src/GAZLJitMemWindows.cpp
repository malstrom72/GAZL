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
	Windows executable-memory backend. The W^X story mirrors the POSIX one: reserve+commit the page read/write with
	VirtualAlloc, copy the code in, flip it to read/execute with VirtualProtect, then flush the i-cache with
	FlushInstructionCache (mandatory on Windows-on-ARM64; a harmless no-op-cost call on x64). freeExecutable hands the
	page back with VirtualFree(MEM_RELEASE), whose second argument must be 0 for a release — so wordCount is ignored
	here (it is part of the interface only because the POSIX munmap needs the length).

	This needs no extra entitlement: unlike macOS' hardened runtime, a normal Windows process may VirtualAlloc executable
	memory. The only environments that refuse are those with an explicit policy (e.g. Arbitrary Code Guard); there
	makeExecutable returns 0 and the caller falls back to the interpreter.
*/

#include "GAZLJitMem.h"

#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace GAZL {

void* makeExecutable(const uint32_t* words, size_t wordCount) {
	const size_t bytes = wordCount * sizeof(uint32_t);
	void* page = ::VirtualAlloc(0, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (page == 0) { return 0; }
	std::memcpy(page, words, bytes);
	DWORD previous = 0;
	if (::VirtualProtect(page, bytes, PAGE_EXECUTE_READ, &previous) == 0) {
		::VirtualFree(page, 0, MEM_RELEASE);
		return 0;
	}
	::FlushInstructionCache(::GetCurrentProcess(), page, bytes);
	return page;
}

void freeExecutable(void* page, size_t wordCount) {
	(void)wordCount;										// MEM_RELEASE frees the whole original reservation; length must be 0
	if (page != 0) { ::VirtualFree(page, 0, MEM_RELEASE); }
}

} // namespace GAZL
