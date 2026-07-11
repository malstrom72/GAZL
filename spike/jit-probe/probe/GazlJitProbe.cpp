//
//  GazlJitProbe.cpp — see GazlJitProbe.h for what this is and the design rules.
//
//  Dependency-free apart from the platform's own headers. Primary target is macOS (Hardened Runtime is
//  the hard case); Windows and Linux paths are included for the "if easy" cross-platform sanity runs.
//

#include "GazlJitProbe.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <mutex>

#if defined(__APPLE__)
	#include <sys/mman.h>
	#include <sys/sysctl.h>
	#include <unistd.h>
	#include <pthread.h>
	#include <errno.h>
	#include <libproc.h>
	#include <mach-o/dyld.h>
	#include <libkern/OSCacheControl.h>
	#include <os/log.h>
	#include <CoreFoundation/CoreFoundation.h>
	#include <Security/Security.h>
#elif defined(_WIN32)
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#include <windows.h>
#else // assume POSIX / Linux
	#include <sys/mman.h>
	#include <unistd.h>
	#include <errno.h>
	#include <pthread.h>
#endif

namespace {

// --- the tiny payload: a function returning 0x5A, per architecture ------------------------------------

const uint8_t EXPECTED_RETURN = 0x5A;

#if defined(__aarch64__) || defined(_M_ARM64)
	// mov w0, #0x5A ; ret
	const uint8_t PROBE_CODE[] = { 0x40, 0x0B, 0x80, 0x52, 0xC0, 0x03, 0x5F, 0xD6 };
	const char* const ARCH_NAME = "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
	// mov eax, 0x5A ; ret
	const uint8_t PROBE_CODE[] = { 0xB8, 0x5A, 0x00, 0x00, 0x00, 0xC3 };
	const char* const ARCH_NAME = "x86_64";
#else
	const uint8_t PROBE_CODE[] = { 0 };
	const char* const ARCH_NAME = "unsupported";
	#define GAZL_PROBE_ARCH_UNSUPPORTED 1
#endif

typedef int (*ProbeFn)(void);

// --- logging -----------------------------------------------------------------------------------------
//
// Every record is appended to a per-process file so a host that hard-crashes still leaves its header and
// the last breadcrumb behind. We also mirror to os_log (macOS) / stderr. A "flush" forces the bytes to
// disk before we do anything dangerous.

struct Logger {
	FILE* file;

	void line(const char* text) {
		if (file != nullptr) {
			std::fprintf(file, "%s\n", text);
		}
#if defined(__APPLE__)
		os_log(OS_LOG_DEFAULT, "GazlJitProbe: %{public}s", text);
#else
		std::fprintf(stderr, "GazlJitProbe: %s\n", text);
#endif
	}

	// Use for breadcrumbs that must survive a crash on the very next instruction.
	void flushedLine(const char* text) {
		line(text);
		if (file != nullptr) {
			std::fflush(file);
#if !defined(_WIN32)
			::fsync(fileno(file));
#endif
		}
	}
};

// --- rung result -------------------------------------------------------------------------------------

enum RungStatus {
	RUNG_UNTRIED = 0,
	RUNG_ALLOC_FAILED,			// mmap/VirtualAlloc denied — expected for a blocked strategy
	RUNG_PROTECT_FAILED,		// write-then-protect step denied
	RUNG_WRONG_RESULT,			// executed but returned the wrong value (should never happen)
	RUNG_OK						// executed and returned 0x5A
};

const char* rungStatusName(RungStatus s) {
	switch (s) {
		case RUNG_UNTRIED:			return "untried";
		case RUNG_ALLOC_FAILED:		return "alloc-failed";
		case RUNG_PROTECT_FAILED:	return "protect-failed";
		case RUNG_WRONG_RESULT:		return "wrong-result";
		case RUNG_OK:				return "OK";
	}
	return "?";
}

// --- platform primitives -----------------------------------------------------------------------------
//
// Each rung is wrapped so that a *denied* syscall returns a status we can log and step past. The only
// uncatchable case is a fault on the execute step, which is exactly why the breadcrumb is flushed first.

const size_t PROBE_ALLOC_SIZE = 4096; // one page is plenty for 8 bytes

// Executes code already resident+executable at p, checks the return value. Caller must have flushed a
// breadcrumb first. Returns RUNG_OK or RUNG_WRONG_RESULT.
RungStatus callAndCheck(void* p) {
	ProbeFn fn = reinterpret_cast<ProbeFn>(p);
	const int result = fn();
	return (result == EXPECTED_RETURN) ? RUNG_OK : RUNG_WRONG_RESULT;
}

#if defined(__APPLE__)

// Rung 1: MAP_JIT + (Apple Silicon) per-thread W^X toggle. Needs `allow-jit`.
RungStatus rungMapJit(Logger& log, const char* threadLabel, char* detail, size_t detailSize) {
	void* p = ::mmap(nullptr, PROBE_ALLOC_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC
			, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	if (p == MAP_FAILED) {
		std::snprintf(detail, detailSize, "mmap(MAP_JIT) failed errno=%d (%s)", errno, std::strerror(errno));
		return RUNG_ALLOC_FAILED;
	}
#if defined(__aarch64__)
	const bool perThreadToggle = (pthread_jit_write_protect_supported_np() != 0);
	if (perThreadToggle) {
		pthread_jit_write_protect_np(0); // this thread: make MAP_JIT pages writable
	}
	std::memcpy(p, PROBE_CODE, sizeof (PROBE_CODE));
	if (perThreadToggle) {
		pthread_jit_write_protect_np(1); // this thread: back to executable
	}
#else
	std::memcpy(p, PROBE_CODE, sizeof (PROBE_CODE)); // x86-64 MAP_JIT pages are writable+executable under the entitlement
#endif
	sys_icache_invalidate(p, PROBE_ALLOC_SIZE);

	char crumb[256];
	std::snprintf(crumb, sizeof (crumb), "  [%s] about to EXECUTE rung 1 (MAP_JIT) at %p", threadLabel, p);
	log.flushedLine(crumb);

	const RungStatus status = callAndCheck(p);
	::munmap(p, PROBE_ALLOC_SIZE);
	std::snprintf(detail, detailSize, "mmap(MAP_JIT)+%s"
			, sizeof (PROBE_CODE) == 8 ? "pthread_jit_write_protect" : "direct-write");
	return status;
}

// Rung 2: mmap(RW) -> write -> mprotect(RX). Needs `allow-unsigned-executable-memory` on hardened macOS.
RungStatus rungMprotectFlip(Logger& log, const char* threadLabel, char* detail, size_t detailSize) {
	void* p = ::mmap(nullptr, PROBE_ALLOC_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (p == MAP_FAILED) {
		std::snprintf(detail, detailSize, "mmap(RW) failed errno=%d (%s)", errno, std::strerror(errno));
		return RUNG_ALLOC_FAILED;
	}
	std::memcpy(p, PROBE_CODE, sizeof (PROBE_CODE));
	if (::mprotect(p, PROBE_ALLOC_SIZE, PROT_READ | PROT_EXEC) != 0) {
		std::snprintf(detail, detailSize, "mprotect(RX) failed errno=%d (%s)", errno, std::strerror(errno));
		::munmap(p, PROBE_ALLOC_SIZE);
		return RUNG_PROTECT_FAILED;
	}
	sys_icache_invalidate(p, PROBE_ALLOC_SIZE);

	char crumb[256];
	std::snprintf(crumb, sizeof (crumb), "  [%s] about to EXECUTE rung 2 (mprotect flip) at %p", threadLabel, p);
	log.flushedLine(crumb);

	const RungStatus status = callAndCheck(p);
	::munmap(p, PROBE_ALLOC_SIZE);
	std::snprintf(detail, detailSize, "mmap(RW)->mprotect(RX)");
	return status;
}

// Rung 3: mmap(RWX) directly, no MAP_JIT. Expected to fail under Hardened Runtime; recorded anyway.
RungStatus rungRwx(Logger& log, const char* threadLabel, char* detail, size_t detailSize) {
	void* p = ::mmap(nullptr, PROBE_ALLOC_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC
			, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (p == MAP_FAILED) {
		std::snprintf(detail, detailSize, "mmap(RWX) failed errno=%d (%s)", errno, std::strerror(errno));
		return RUNG_ALLOC_FAILED;
	}
	std::memcpy(p, PROBE_CODE, sizeof (PROBE_CODE));
	sys_icache_invalidate(p, PROBE_ALLOC_SIZE);

	char crumb[256];
	std::snprintf(crumb, sizeof (crumb), "  [%s] about to EXECUTE rung 3 (RWX) at %p", threadLabel, p);
	log.flushedLine(crumb);

	const RungStatus status = callAndCheck(p);
	::munmap(p, PROBE_ALLOC_SIZE);
	std::snprintf(detail, detailSize, "mmap(RWX) direct");
	return status;
}

#elif defined(_WIN32)

RungStatus rungMapJit(Logger&, const char*, char* detail, size_t detailSize) {
	std::snprintf(detail, detailSize, "n/a on Windows");
	return RUNG_UNTRIED;
}

RungStatus rungMprotectFlip(Logger& log, const char* threadLabel, char* detail, size_t detailSize) {
	void* p = ::VirtualAlloc(nullptr, PROBE_ALLOC_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (p == nullptr) {
		std::snprintf(detail, detailSize, "VirtualAlloc(RW) failed err=%lu", (unsigned long)GetLastError());
		return RUNG_ALLOC_FAILED;
	}
	std::memcpy(p, PROBE_CODE, sizeof (PROBE_CODE));
	DWORD old = 0;
	if (!::VirtualProtect(p, PROBE_ALLOC_SIZE, PAGE_EXECUTE_READ, &old)) {
		std::snprintf(detail, detailSize, "VirtualProtect(RX) failed err=%lu", (unsigned long)GetLastError());
		::VirtualFree(p, 0, MEM_RELEASE);
		return RUNG_PROTECT_FAILED;
	}
	::FlushInstructionCache(GetCurrentProcess(), p, PROBE_ALLOC_SIZE);

	char crumb[256];
	std::snprintf(crumb, sizeof (crumb), "  [%s] about to EXECUTE rung 2 (VirtualProtect flip) at %p", threadLabel, p);
	log.flushedLine(crumb);

	const RungStatus status = callAndCheck(p);
	::VirtualFree(p, 0, MEM_RELEASE);
	std::snprintf(detail, detailSize, "VirtualAlloc(RW)->VirtualProtect(RX)");
	return status;
}

RungStatus rungRwx(Logger& log, const char* threadLabel, char* detail, size_t detailSize) {
	void* p = ::VirtualAlloc(nullptr, PROBE_ALLOC_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (p == nullptr) {
		std::snprintf(detail, detailSize, "VirtualAlloc(RWX) failed err=%lu", (unsigned long)GetLastError());
		return RUNG_ALLOC_FAILED;
	}
	std::memcpy(p, PROBE_CODE, sizeof (PROBE_CODE));
	::FlushInstructionCache(GetCurrentProcess(), p, PROBE_ALLOC_SIZE);

	char crumb[256];
	std::snprintf(crumb, sizeof (crumb), "  [%s] about to EXECUTE rung 3 (RWX) at %p", threadLabel, p);
	log.flushedLine(crumb);

	const RungStatus status = callAndCheck(p);
	::VirtualFree(p, 0, MEM_RELEASE);
	std::snprintf(detail, detailSize, "VirtualAlloc(RWX) direct");
	return status;
}

#else // POSIX / Linux

RungStatus rungMapJit(Logger&, const char*, char* detail, size_t detailSize) {
	std::snprintf(detail, detailSize, "n/a (no MAP_JIT on this platform)");
	return RUNG_UNTRIED;
}

RungStatus rungMprotectFlip(Logger& log, const char* threadLabel, char* detail, size_t detailSize) {
	void* p = ::mmap(nullptr, PROBE_ALLOC_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		std::snprintf(detail, detailSize, "mmap(RW) failed errno=%d (%s)", errno, std::strerror(errno));
		return RUNG_ALLOC_FAILED;
	}
	std::memcpy(p, PROBE_CODE, sizeof (PROBE_CODE));
	if (::mprotect(p, PROBE_ALLOC_SIZE, PROT_READ | PROT_EXEC) != 0) {
		std::snprintf(detail, detailSize, "mprotect(RX) failed errno=%d (%s)", errno, std::strerror(errno));
		::munmap(p, PROBE_ALLOC_SIZE);
		return RUNG_PROTECT_FAILED;
	}
	__builtin___clear_cache(reinterpret_cast<char*>(p), reinterpret_cast<char*>(p) + PROBE_ALLOC_SIZE);

	char crumb[256];
	std::snprintf(crumb, sizeof (crumb), "  [%s] about to EXECUTE rung 2 (mprotect flip) at %p", threadLabel, p);
	log.flushedLine(crumb);

	const RungStatus status = callAndCheck(p);
	::munmap(p, PROBE_ALLOC_SIZE);
	std::snprintf(detail, detailSize, "mmap(RW)->mprotect(RX)");
	return status;
}

RungStatus rungRwx(Logger& log, const char* threadLabel, char* detail, size_t detailSize) {
	void* p = ::mmap(nullptr, PROBE_ALLOC_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC
			, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		std::snprintf(detail, detailSize, "mmap(RWX) failed errno=%d (%s)", errno, std::strerror(errno));
		return RUNG_ALLOC_FAILED;
	}
	std::memcpy(p, PROBE_CODE, sizeof (PROBE_CODE));
	__builtin___clear_cache(reinterpret_cast<char*>(p), reinterpret_cast<char*>(p) + PROBE_ALLOC_SIZE);

	char crumb[256];
	std::snprintf(crumb, sizeof (crumb), "  [%s] about to EXECUTE rung 3 (RWX) at %p", threadLabel, p);
	log.flushedLine(crumb);

	const RungStatus status = callAndCheck(p);
	::munmap(p, PROBE_ALLOC_SIZE);
	std::snprintf(detail, detailSize, "mmap(RWX) direct");
	return status;
}

#endif

// --- the ladder, run on one thread -------------------------------------------------------------------

void runLadderOnThread(Logger& log, const char* threadLabel) {
	char banner[128];
	std::snprintf(banner, sizeof (banner), "-- ladder on thread [%s] --", threadLabel);
	log.line(banner);

#if defined(GAZL_PROBE_ARCH_UNSUPPORTED)
	log.line("  unsupported architecture — no probe code available");
	return;
#endif

	struct { const char* name; RungStatus (*fn)(Logger&, const char*, char*, size_t); } rungs[] = {
		{ "1 MAP_JIT",        rungMapJit },
		{ "2 mprotect-flip",  rungMprotectFlip },
		{ "3 RWX",            rungRwx },
	};

	bool anyOk = false;
	for (size_t i = 0; i < sizeof (rungs) / sizeof (rungs[0]); ++i) {
		char detail[256];
		detail[0] = 0;
		const RungStatus status = rungs[i].fn(log, threadLabel, detail, sizeof (detail));
		char record[512];
		std::snprintf(record, sizeof (record), "  rung %-16s : %-14s | %s"
				, rungs[i].name, rungStatusName(status), detail);
		log.line(record);
		if (status == RUNG_OK) {
			anyOk = true;
		}
	}
	log.line(anyOk ? "  => at least one rung works on this thread"
			: "  => NO rung works on this thread (interpreter-only here)");
}

// --- worker-thread entry -----------------------------------------------------------------------------

struct WorkerArg {
	Logger* log;
};

#if defined(_WIN32)
unsigned __stdcall workerMain(void* raw) {
	WorkerArg* arg = static_cast<WorkerArg*>(raw);
	runLadderOnThread(*arg->log, "worker");
	return 0;
}
#else
void* workerMain(void* raw) {
	WorkerArg* arg = static_cast<WorkerArg*>(raw);
	runLadderOnThread(*arg->log, "worker");
	return nullptr;
}
#endif

// --- process identity + entitlement header -----------------------------------------------------------

#if defined(__APPLE__)

// Reads a code-signing entitlement of the *running* process (ground truth: which process am I in?).
void appendEntitlement(char* out, size_t outSize, size_t& off, SecTaskRef task, const char* key) {
	CFStringRef cfKey = CFStringCreateWithCString(kCFAllocatorDefault, key, kCFStringEncodingUTF8);
	CFErrorRef err = nullptr;
	CFTypeRef value = (task != nullptr && cfKey != nullptr)
			? SecTaskCopyValueForEntitlement(task, cfKey, &err) : nullptr;

	const char* rendered = "absent";
	if (value != nullptr) {
		if (CFGetTypeID(value) == CFBooleanGetTypeID()) {
			rendered = CFBooleanGetValue((CFBooleanRef)value) ? "true" : "false";
		} else {
			rendered = "present(non-bool)";
		}
	}
	off += std::snprintf(out + off, (off < outSize) ? outSize - off : 0, "    %-52s = %s\n", key, rendered);

	if (value != nullptr) CFRelease(value);
	if (err != nullptr) CFRelease(err);
	if (cfKey != nullptr) CFRelease(cfKey);
}

void writeIdentityHeader(Logger& log, const char* triggerContext) {
	char header[4096];
	size_t off = 0;
	off += std::snprintf(header + off, sizeof (header) - off, "================ GAZL JIT probe ================\n");

	// timestamp
	time_t now = std::time(nullptr);
	struct tm tmv;
	localtime_r(&now, &tmv);
	char ts[64];
	std::strftime(ts, sizeof (ts), "%Y-%m-%d %H:%M:%S %z", &tmv);
	off += std::snprintf(header + off, sizeof (header) - off, "  time            : %s\n", ts);
	off += std::snprintf(header + off, sizeof (header) - off, "  trigger         : %s\n", triggerContext ? triggerContext : "?");

	// pid / ppid
	const pid_t pid = getpid();
	const pid_t ppid = getppid();

	// exec path (own + parent)
	char execPath[PROC_PIDPATHINFO_MAXSIZE];
	execPath[0] = 0;
	proc_pidpath(pid, execPath, sizeof (execPath));
	char parentPath[PROC_PIDPATHINFO_MAXSIZE];
	parentPath[0] = 0;
	proc_pidpath(ppid, parentPath, sizeof (parentPath));

	off += std::snprintf(header + off, sizeof (header) - off, "  pid             : %d\n", (int)pid);
	off += std::snprintf(header + off, sizeof (header) - off, "  exec path       : %s\n", execPath);
	off += std::snprintf(header + off, sizeof (header) - off, "  parent pid      : %d\n", (int)ppid);
	off += std::snprintf(header + off, sizeof (header) - off, "  parent path     : %s\n", parentPath);

	// architecture + Rosetta translation
	int translated = 0;
	size_t tsize = sizeof (translated);
	if (sysctlbyname("sysctl.proc_translated", &translated, &tsize, nullptr, 0) != 0) {
		translated = 0; // sysctl absent => not translated
	}
	off += std::snprintf(header + off, sizeof (header) - off, "  build arch      : %s\n", ARCH_NAME);
	off += std::snprintf(header + off, sizeof (header) - off, "  running under Rosetta : %s\n", translated ? "yes" : "no");

	// macOS product version
	char osver[64];
	osver[0] = 0;
	size_t osverSize = sizeof (osver);
	sysctlbyname("kern.osproductversion", osver, &osverSize, nullptr, 0);
	off += std::snprintf(header + off, sizeof (header) - off, "  macOS version   : %s\n", osver);

#if defined(__aarch64__)
	off += std::snprintf(header + off, sizeof (header) - off, "  pthread_jit_write_protect_supported_np : %s\n"
			, pthread_jit_write_protect_supported_np() ? "yes" : "no");
#endif

	// runtime-read entitlements — the real ground truth
	off += std::snprintf(header + off, sizeof (header) - off, "  entitlements (SecTaskCopyValueForEntitlement):\n");
	SecTaskRef task = SecTaskCreateFromSelf(nullptr);
	appendEntitlement(header, sizeof (header), off, task, "com.apple.security.cs.allow-jit");
	appendEntitlement(header, sizeof (header), off, task, "com.apple.security.cs.allow-unsigned-executable-memory");
	appendEntitlement(header, sizeof (header), off, task, "com.apple.security.cs.disable-executable-page-protection");
	appendEntitlement(header, sizeof (header), off, task, "com.apple.security.cs.disable-library-validation");
	if (task != nullptr) CFRelease(task);

	off += std::snprintf(header + off, sizeof (header) - off, "===============================================");

	log.flushedLine(header);
}

const char* logDirEnvHome() { return getenv("HOME"); }

#else // non-Apple: a leaner header

void writeIdentityHeader(Logger& log, const char* triggerContext) {
	char header[1024];
	size_t off = 0;
	off += std::snprintf(header + off, sizeof (header) - off, "================ GAZL JIT probe ================\n");
	time_t now = std::time(nullptr);
	char ts[64];
	std::strftime(ts, sizeof (ts), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
	off += std::snprintf(header + off, sizeof (header) - off, "  time    : %s\n", ts);
	off += std::snprintf(header + off, sizeof (header) - off, "  trigger : %s\n", triggerContext ? triggerContext : "?");
	off += std::snprintf(header + off, sizeof (header) - off, "  arch    : %s\n", ARCH_NAME);
#if defined(_WIN32)
	off += std::snprintf(header + off, sizeof (header) - off, "  pid     : %lu\n", (unsigned long)GetCurrentProcessId());
#else
	off += std::snprintf(header + off, sizeof (header) - off, "  pid     : %d\n", (int)getpid());
#endif
	off += std::snprintf(header + off, sizeof (header) - off, "===============================================");
	log.flushedLine(header);
}

#endif

// --- log file creation -------------------------------------------------------------------------------

FILE* openLogFile() {
#if defined(__APPLE__)
	const char* home = logDirEnvHome();
	if (home == nullptr) return nullptr;
	char dir[1024];
	std::snprintf(dir, sizeof (dir), "%s/Library/Logs/GazlJitProbe", home);
	// mkdir -p (two levels are enough: Logs exists; GazlJitProbe may not)
	char cmdDir[1200];
	std::snprintf(cmdDir, sizeof (cmdDir), "%s/Library/Logs", home);
	mkdir(cmdDir, 0755);
	mkdir(dir, 0755);

	char procName[256];
	procName[0] = 0;
	proc_name(getpid(), procName, sizeof (procName));
	if (procName[0] == 0) std::snprintf(procName, sizeof (procName), "proc");

	time_t now = std::time(nullptr);
	char ts[32];
	std::strftime(ts, sizeof (ts), "%Y%m%d-%H%M%S", std::localtime(&now));

	char path[1600];
	std::snprintf(path, sizeof (path), "%s/%s-%d-%s.log", dir, procName, (int)getpid(), ts);
	return std::fopen(path, "a");
#elif defined(_WIN32)
	char path[512];
	std::snprintf(path, sizeof (path), "%s\\GazlJitProbe-%lu.log"
			, getenv("TEMP") ? getenv("TEMP") : ".", (unsigned long)GetCurrentProcessId());
	return std::fopen(path, "a");
#else
	char path[512];
	std::snprintf(path, sizeof (path), "/tmp/GazlJitProbe-%d.log", (int)getpid());
	return std::fopen(path, "a");
#endif
}

// --- the one-shot entry point ------------------------------------------------------------------------

std::once_flag g_probeOnce;

void runProbeSequence(const char* triggerContext) {
	Logger log;
	log.file = openLogFile();

	writeIdentityHeader(log, triggerContext);

	// Rung ladder on the host's calling thread.
	runLadderOnThread(log, "caller");

	// ...and again on a spawned worker thread (per-thread W^X means results can differ).
	WorkerArg arg;
	arg.log = &log;
#if defined(_WIN32)
	// Use a plain thread; keep it simple and dependency-free.
	HANDLE h = (HANDLE)_beginthreadex(nullptr, 0, workerMain, &arg, 0, nullptr);
	if (h != nullptr) {
		WaitForSingleObject(h, INFINITE);
		CloseHandle(h);
	} else {
		log.line("  (could not spawn worker thread)");
	}
#else
	pthread_t worker;
	if (pthread_create(&worker, nullptr, workerMain, &arg) == 0) {
		pthread_join(worker, nullptr);
	} else {
		log.line("  (could not spawn worker thread)");
	}
#endif

	log.flushedLine("================ probe complete ================\n");
	if (log.file != nullptr) {
		std::fclose(log.file);
	}
}

} // namespace

extern "C" void gazlJitProbeRunOnce(const char* triggerContext) {
	// Copy the context into a stable buffer so call_once's lambda capture is trivially safe.
	static char storedContext[256];
	std::call_once(g_probeOnce, [triggerContext]() {
		std::snprintf(storedContext, sizeof (storedContext), "%s", triggerContext ? triggerContext : "?");
		runProbeSequence(storedContext);
	});
}
