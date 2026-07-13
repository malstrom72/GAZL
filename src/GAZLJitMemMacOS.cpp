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
	Apple (macOS / iOS) executable-memory backend. Apple Silicon enforces hardened runtime W^X, so the page is mapped
	MAP_JIT and writes are wrapped in the per-thread write-protect toggle (pthread_jit_write_protect_np); afterwards the
	i-cache is flushed with sys_icache_invalidate. See docs/JitSpikeA1-Results.md (spike A1 rung-1). The toggle is a
	no-op / unsupported on older/Intel Macs, so it is applied only when advertised.
*/

#include "GAZLJitMem.h"

#include <cstring>
#include <sys/mman.h>
#include <pthread.h>
#include <libkern/OSCacheControl.h>

namespace GAZLJitLower {

void* makeExecutable(const uint32_t* words, size_t wordCount) {
	const size_t bytes = wordCount * sizeof(uint32_t);
	void* p = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	if (p == MAP_FAILED) { return nullptr; }
	const bool toggle = (pthread_jit_write_protect_supported_np() != 0);
	if (toggle) { pthread_jit_write_protect_np(0); }
	std::memcpy(p, words, bytes);
	if (toggle) { pthread_jit_write_protect_np(1); }
	sys_icache_invalidate(p, bytes);
	return p;
}

} // namespace GAZLJitLower
