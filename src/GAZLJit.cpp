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
#include "GAZLJitMem.h"			// makeExecutable() — for the shared publishModule step

#include <cstddef>
#include <cstring>				// memset / memcpy — the jitAvailable() probe

#if defined(_WIN32)
#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	include <windows.h>			// __try/__except fault guard for the probe
#else
#	include <csignal>			// sigaction — POSIX fault guard for the probe
#	include <csetjmp>			// sigsetjmp / siglongjmp
#endif

namespace GAZL {

/*
	Arch-neutral pass-1 primitive: is instruction `instructionIndex` a branch, and if so, to which instruction? GOTO
	targets via p0; the conditional forms (FORi + the integer/float compare-branches) target via p2. Both backends' pass
	1 use this; SWCH's jump-table targets are read separately from const memory by each backend.
*/
bool jitBranchTarget(const Instruction* code, UInt instructionIndex, UInt& target) {
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

/*
	Shared publish: copy the emitted code into an executable page and populate `out`. Byte offsets keep it
	architecture-independent (arm64 passes word offsets * 4). On makeExecutable failure `out` is left empty, so
	out.ok() stays false and the caller falls back to the interpreter.
*/
void JitCompiler::publishModule(JitModule& out, const uint32_t* codeWords, size_t wordCount
		, const size_t* entryByteOffsets, UInt functionCount, size_t dispatchByteOffset) {
	void* page = makeExecutable(codeWords, wordCount);
	if (page == 0) { return; }
	void** entries = new void*[functionCount];
	for (UInt ordinal = 0; ordinal < functionCount; ++ordinal) {
		entries[ordinal] = reinterpret_cast<char*>(page) + entryByteOffsets[ordinal];
	}
	out.dispatch = reinterpret_cast<char*>(page) + dispatchByteOffset;
	out.nativeEntries = entries;
	out.codeWordCount = wordCount;
	out.ownedPage = page;
	out.ownedWords = wordCount;
}

/*
	The field ABI JitCompiler bakes into the machine code — byte offsets of the run-state fields within a JitProcessor.
	Arch-neutral (offsetof on JitProcessor fields), so it lives here rather than in a backend .cpp. Static /
	instance-independent: computed with offsetof, so no engine is needed. offsetof on this polymorphic type is
	universally correct (single inheritance, fixed layout); the one -Winvalid-offsetof is silenced locally. Non-virtual,
	so defining it here pulls no vtable into GAZLJit.o.
*/
Offsets JitProcessor::layout() {
	Offsets o;
#if defined(__GNUC__)
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Winvalid-offsetof"
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
#	pragma GCC diagnostic pop
#endif
	return o;
}

/*
	jitAvailable() — the two-layer capability probe declared in GAZLJit.h. Layer 1 is makeExecutable() itself (a policy
	denial such as a missing macOS allow-jit entitlement or Windows Arbitrary Code Guard fails the syscalls). Layer 2 runs
	a trivial position-independent stub — `return 0xC0DE` — under a fault guard, so a page that mapped but cannot execute
	turns into `false` instead of a crash. The stub is one word-aligned block (makeExecutable works in 32-bit words); it is
	architecture-specific but tiny, so it lives here rather than in a backend. Both shipping targets are little-endian.
*/
namespace {

typedef uint32_t (*ProbeFunc)();

#if defined(__aarch64__) || defined(_M_ARM64)
const uint32_t PROBE_STUB[2] = { 0x52981BC0u, 0xD65F03C0u };		// movz w0, #0xC0DE ; ret
#elif defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
const uint32_t PROBE_STUB[2] = { 0x00C0DEB8u, 0x9090C300u };		// mov eax, 0xC0DE ; ret ; nop ; nop
#else
#	define GAZL_NO_PROBE_STUB									// no JIT backend for this architecture
#endif

#if !defined(_WIN32) && !defined(GAZL_NO_PROBE_STUB)
sigjmp_buf gProbeJump;
void probeFaultHandler(int) { siglongjmp(gProbeJump, 1); }
#endif

#if !defined(GAZL_NO_PROBE_STUB)
// Run `func` under a fault guard: true only if it returned 0xC0DE. A SIGILL/SIGSEGV/SIGBUS/SIGTRAP (POSIX) or any
// structured exception (Windows) means executing generated code faults on this host -> false.
bool runProbe(ProbeFunc func) {
#if defined(_WIN32)
	__try {
		return func() == 0xC0DEu;
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
	if (sigsetjmp(gProbeJump, 1) == 0) { ok = (func() == 0xC0DEu); }
	sigaction(SIGILL, &savedIll, 0);
	sigaction(SIGSEGV, &savedSegv, 0);
	sigaction(SIGBUS, &savedBus, 0);
	sigaction(SIGTRAP, &savedTrap, 0);
	return ok;
#endif
}
#endif

} // anonymous namespace

bool jitAvailable() {
#if defined(GAZL_NO_PROBE_STUB)
	return false;
#else
	static int cached = -1;										// -1 unknown / 0 no / 1 yes; probed once (single-threaded startup)
	if (cached < 0) {
		const size_t words = sizeof(PROBE_STUB) / sizeof(PROBE_STUB[0]);
		void* page = makeExecutable(PROBE_STUB, words);			// layer 1: policy denial fails the syscalls here
		if (page == 0) {
			cached = 0;
		} else {
			ProbeFunc func;
			std::memcpy(&func, &page, sizeof(func));				// void* -> function pointer without an ISO-illegal cast
			cached = runProbe(func) ? 1 : 0;						// layer 2: the emitted stub actually runs and returns 0xC0DE
			freeExecutable(page, words);
		}
	}
	return cached != 0;
#endif
}

} // namespace GAZL
