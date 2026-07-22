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

#include <iostream>
#include <string>
#include <ctime>
#include <chrono>
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include "../src/GAZL.h"
#include "../src/GAZLCpp.h"			// translateToCpp - portable GAZL->C++ source (--emit-cpp)
#ifdef GAZL_JIT
	#include "../src/GAZLJit.h"		// JitProcessor + JitCompiler - arm64 only; enabled by the build on AArch64 hosts
#endif

// --- Deterministic FP runtime environment (ported from Numbstrict) --------------------------------------------
// Round-to-nearest, all FP exceptions masked, FTZ/DAZ off - so interp and JIT run the differential under a known,
// host-independent FP env instead of whatever the CRT/host left in MXCSR/FPCR. Restores on scope exit.
#include <cassert>
#include <cfenv>
#if defined(_MSC_VER)
#pragma fenv_access(on)
#endif
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#include <float.h>
#endif
class StandardFPEnvScope {
public:
	StandardFPEnvScope() {
	#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
		const unsigned int COMMON = _EM_INEXACT|_EM_UNDERFLOW|_EM_OVERFLOW|_EM_ZERODIVIDE|_EM_INVALID|_EM_DENORMAL|_RC_NEAR;
		unsigned int cur;
	#if defined(_M_IX86)
		{ int ok = __control87_2(0,0,&prevX87_,&prevMXCSR_); assert(ok); unsigned int t; ok = __control87_2(COMMON|_PC_53, _MCW_EM|_MCW_RC|_MCW_PC, &t, 0); assert(ok); }
		cur = prevMXCSR_;
	#else
		prevMXCSR_ = _mm_getcsr(); cur = prevMXCSR_; prevX87_ = _control87(0,0); _control87(COMMON, _MCW_EM|_MCW_RC);
	#endif
		cur &= ~(_MM_FLUSH_ZERO_MASK|_MM_DENORMALS_ZERO_MASK);
		cur = (cur & ~_MM_ROUND_MASK) | _MM_ROUND_NEAREST;
		cur |= (_MM_MASK_INVALID|_MM_MASK_DENORM|_MM_MASK_DIV_ZERO|_MM_MASK_OVERFLOW|_MM_MASK_UNDERFLOW|_MM_MASK_INEXACT);	// x64 uses SSE/MXCSR: mask exceptions here, not just x87
		_mm_setcsr(cur);
	#elif defined(__aarch64__)
		int r; r = fegetenv(&prevEnv_); assert(r==0); r = fesetenv(FE_DFL_ENV); assert(r==0); feholdexcept(&dummyEnv_);
	#if defined(__has_builtin) && __has_builtin(__builtin_aarch64_get_fpcr) && __has_builtin(__builtin_aarch64_set_fpcr)
		prevFPCR_ = __builtin_aarch64_get_fpcr(); unsigned long long cur = prevFPCR_; cur &= ~(1ull<<24); cur &= ~(3ull<<22); __builtin_aarch64_set_fpcr(cur);
	#else
		asm volatile("mrs %0, fpcr" : "=r"(prevFPCR_)); unsigned long long cur = prevFPCR_; cur &= ~(1ull<<24); cur &= ~(3ull<<22); asm volatile("msr fpcr, %0" :: "r"(cur));
	#endif
	#elif defined(__arm__)
		int r; r = fegetenv(&prevEnv_); assert(r==0); r = fesetenv(FE_DFL_ENV); assert(r==0); feholdexcept(&dummyEnv_);
		asm volatile("vmrs %0, fpscr" : "=r"(prevFPSCR_)); unsigned int cur = prevFPSCR_; cur &= ~(1u<<24); cur &= ~(3u<<22); asm volatile("vmsr fpscr, %0" :: "r"(cur));
	#else
		int r; r = fegetenv(&prevEnv_); assert(r==0); r = fesetenv(FE_DFL_ENV); assert(r==0); feholdexcept(&dummyEnv_);
	#endif
	}
	~StandardFPEnvScope() {
	#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	#if defined(_M_IX86)
		{ unsigned int t; __control87_2(prevX87_, _MCW_EM|_MCW_RC|_MCW_PC, &t, 0); }
	#else
		_control87(prevX87_, _MCW_EM|_MCW_RC);
	#endif
		_mm_setcsr(prevMXCSR_);
	#elif defined(__aarch64__)
	#if defined(__has_builtin) && __has_builtin(__builtin_aarch64_set_fpcr)
		__builtin_aarch64_set_fpcr(prevFPCR_);
	#else
		asm volatile("msr fpcr, %0" :: "r"(prevFPCR_));
	#endif
		fesetenv(&prevEnv_);
	#elif defined(__arm__)
		asm volatile("vmsr fpscr, %0" :: "r"(prevFPSCR_)); fesetenv(&prevEnv_);
	#else
		fesetenv(&prevEnv_);
	#endif
	}
private:
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	unsigned int prevX87_, prevMXCSR_;
#elif defined(__aarch64__)
	fenv_t prevEnv_, dummyEnv_;
	unsigned long long prevFPCR_;
#elif defined(__arm__)
	fenv_t prevEnv_, dummyEnv_;
	unsigned int prevFPSCR_;
#else
	fenv_t prevEnv_, dummyEnv_;
#endif
};

using namespace GAZL;

class CmdException : public std::exception {
	public:		CmdException(const char* string = "General Exception") throw() : string(string) { }
	public:		CmdException(const std::string& string) throw() : string(string) { }
	public:		virtual ~CmdException() throw() { }
    public:		virtual const char* what() const throw() { return string.c_str(); }
    public:		const std::string& getString() const { return string; }
	public:		std::string string;
};

Status print(Processor* vpu) {
	vpu->resetTimeOut(0x7FFFFFFF);
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	Pointer p = params[1].p;
	const Value* vp = vpu->accessConstMemory(p, 1); // Note: it is ok to clear access to only one word since the last word of the virtual memory is always 0
	if (vp == 0) return ACCESS_VIOLATION;
	do {
		if (vp->i != 0) {
			// FIX : unicode support
			std::cout << static_cast<Char>(vp->i);
			++vp;
			++p;
		}
	} while (vp->i != 0);
	if (vpu->accessConstMemory(p, 1) == 0) return ACCESS_VIOLATION; // In case we ended up at the "guardian" element...
	std::cout.flush();
	return OK;
};

Status abort(Processor*) {
	return ABORTED;
}

Status assertFail(Processor* p) {
	std::cout << "Assertion failed: ";
	print(p);
	std::cout << std::endl;
	std::cout.flush();
	assert(0);
	return abort(p);
}

Status printInt(Processor* vpu) {
	vpu->resetTimeOut(0x7FFFFFFF);
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	std::cout << params[1].i;
	std::cout.flush();
	return OK;
}

Status printFloat(Processor* vpu) {
	vpu->resetTimeOut(0x7FFFFFFF);
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	std::cout << params[1].f;
	std::cout.flush();
	return OK;
};

Status printLF(Processor* vpu) {
	vpu->resetTimeOut(0x7FFFFFFF);
	std::cout << std::endl;
	std::cout.flush();
	return OK;
};

Status input(Processor* vpu) {
	vpu->resetTimeOut(0x7FFFFFFF);
	std::string s;
	getline(std::cin, s);
	Value* params = vpu->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	int maxCount = params[1].i;
	Pointer p = params[2].p;
	Value* bp = vpu->accessMemory(p, maxCount + 1);
	if (bp == 0) return ACCESS_VIOLATION;
	int i = 0;
	std::string::const_iterator it = s.begin();
	while (i < maxCount && it != s.end()) {
		bp[i].i = static_cast<Int>(*it);
		++i;
		++it;
	}
	bp[i].i = 0;
	params[0].i = i;
	return OK;
};

Status gazlSqrt(Processor* vpu) {
	Value* params = vpu->accessParams(2);
	if (params == 0) {
		return DATA_STACK_OVERFLOW;
	}
	params[0].f = sqrt(params[1].f);
	return OK;
};

Status gazlLog(Processor* vpu) {
	Value* params = vpu->accessParams(2);
	if (params == 0) {
		return DATA_STACK_OVERFLOW;
	}
	params[0].f = log(params[1].f);
	return OK;
};

Status gazlAtan2(Processor* vpu) {
	Value* params = vpu->accessParams(3);
	if (params == 0) {
		return DATA_STACK_OVERFLOW;
	}
	params[0].f = atan2(params[1].f, params[2].f);
	return OK;
};

Status gazlExit(Processor*) {
	return TERMINATED;					// expected, clean termination (e.g. a firmware harness reaching its frame budget)
}

/*
	--forward=native:function[,native:function...]: satisfy `^native` call sites with GAZL functions. Each listed native
	name is registered as one of these stubs; after assembly the paired GAZL function is looked up by name and every call
	to the native pushCall()s it - the `^native` call then behaves exactly like a `&function` call (args and return value
	in the same window). This is how the Permut8 firmware harness supplies yield/read/write/trace as concatenated GAZL
	(see tools/permut8Host.js). Interpreter only for now: pushCall is not yet supported under the JIT.
*/
const int MAX_FORWARDS = 8;
static Pointer forwardTargets[MAX_FORWARDS];
template<int INDEX> Status forwardNative(Processor* p) {
	return p->pushCall(forwardTargets[INDEX]) != 0 ? OK : BAD_CALL;
}


const int DATA_MEMORY_SIZE = 128 * 1024;
const int CODE_MEMORY_SIZE = 128 * 1024;
const int FUNCTION_TABLE_SIZE = CODE_MEMORY_SIZE;	// A function is at least one instruction, so this can never overflow.
const int CALL_STACK_SIZE = 2048;

static const NativeFunc NATIVE_TABLE[] = {
	abort, assertFail, printInt, printFloat, print, printLF, input, gazlAtan2, gazlSqrt, gazlLog, gazlExit,
	forwardNative<0>, forwardNative<1>, forwardNative<2>, forwardNative<3>,							// --forward slots;
	forwardNative<4>, forwardNative<5>, forwardNative<6>, forwardNative<7>							// names registered on demand
};

static const char* NATIVE_NAMES[] = {
	"abort", "assertFail", "printInt", "printFloat", "print", "printLF", "input", "atan2", "sqrt", "log", "exit"
};

const int FIRST_FORWARD_INDEX = 11;						// index of forwardNative<0> in NATIVE_TABLE

static Value memory[DATA_MEMORY_SIZE];
static Instruction code[CODE_MEMORY_SIZE];
static UInt functionTable[FUNCTION_TABLE_SIZE];
static CallStackEntry callStack[CALL_STACK_SIZE];

#if defined(LIBFUZZ) || defined(LIBFUZZ_STANDALONE)

#include <sstream>

struct TestCallbackData {
	Pointer globalPointer;
	Pointer callBack;
};

int testMul(Processor* p);
int testCallback(Processor* p);

int testMul(Processor* p) {
	Value* params = p->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = params[1].f * params[2].f;
	return 0;
}

int testCallback(Processor* p) {
	return 0;
}

#if defined(JITDIFF) && defined(GAZL_JIT)

#include <cstdio>

/*
	JIT-vs-interpreter differential fuzzer (docs/JitFuzzPlan.md). Assemble the input, run the interpreter and the JIT
	from an identical initial image, and require byte-identical final memory + status at full AND tiny fuel. A mismatch
	is a miscompile: dump the program and abort so libFuzzer captures and minimizes it.

	FUZZ_TEXT_INPUT (the text lane) is the exception: it feeds arbitrary/mutated source, which can legally break the
	aliasing (realm) contract, so its runs are permitted to diverge and it does NOT compare - it is crash/assert
	coverage of the assembler + JIT compiler + both engines only. See the FUZZ_TEXT_INPUT block below.
*/
static Status fuzzStub(Processor*) { return OK; }			// abort / assertFail / input -> deterministic no-op (natives must be pure here)

static Status runEngine(Processor& engine, Pointer mainFn, Int fuel) {
	Status s = engine.enterCall(mainFn);
	if (s == OK) { do { engine.resetTimeOut(fuel); s = engine.run(); } while (s == TIME_OUT); }
	return s;
}

static uint32_t g_currentSeed = 0;		// set by the --gen loop before each program so a divergence names the EXACT seed (not the band start)
static void requireMatch(const char* which, const uint8_t* data, size_t size, Status refStatus, const Value* refImage
		, Status gotStatus, const Value* gotImage) {
	int diffWord = -1;
	for (int i = 0; i < DATA_MEMORY_SIZE; ++i) {
		if (refImage[i].i == gotImage[i].i) { continue; }
		const float rf = refImage[i].f, gf = gotImage[i].f;
		if (rf != rf && gf != gf) { continue; }				// both NaN: sign + payload are unspecified in IEEE 754, so two conforming engines may differ bit-wise (MSVC float vs the JIT's SSE) - not a miscompile
		diffWord = i; break;
	}
	if (gotStatus == refStatus && diffWord < 0) { return; }
	std::fprintf(stderr, "\n=== JIT/interp divergence (%s) SEED=%u: interp status=%d got status=%d", which
			, g_currentSeed, static_cast<int>(refStatus), static_cast<int>(gotStatus));
	if (diffWord >= 0) { std::fprintf(stderr, "; word[%d] interp=%d got=%d", diffWord, refImage[diffWord].i, gotImage[diffWord].i); }
	std::fprintf(stderr, " ===\n");
	if (diffWord >= 0) {																						// neighborhood dump: locate the slot + see who wrote it
		const int lo = diffWord - 6 < 0 ? 0 : diffWord - 6, hi = diffWord + 6 >= DATA_MEMORY_SIZE ? DATA_MEMORY_SIZE - 1 : diffWord + 6;
		for (int i = lo; i <= hi; ++i) { std::fprintf(stderr, "  mem[%d] interp=%-12d got=%-12d %s\n", i, refImage[i].i, gotImage[i].i, i == diffWord ? "<<<" : ""); }
	}
	std::fprintf(stderr, "%.*s\n", static_cast<int>(size), reinterpret_cast<const char*>(data));
	std::fflush(stderr);
	std::abort();										// fuzzer harness: a found miscompile is a crash libFuzzer captures
}

/*
	Program generator (docs/JitFuzzPlan.md): decode a choice stream into an always-valid GAZL program. We generate TEXT
	and let the assembler be the gatekeeper (validate + finalize); the computed locals + globals stay in the region the
	diff compares, so a miscompiled value is caught without an explicit store. G1 = straight-line int/float value ops (VVV
	and VVC). G2 adds memory ops interleaved so the pointer coherence gets stressed: const-address globals, a LOCA array
	via an ADRL pointer (PEEK/POKE_VVV), and GETL/SETL - indices masked in-bounds so the ops execute (store/load +
	invalidate). G3 adds control flow (nested FORi + if-skips), G4 calls (helpers, native math, recursion).

	The stream is either an LCG seeded by a number (`--gen`, reproducible) or the raw libFuzzer input bytes (coverage-
	guided). Same 32-bit-word interface for both, so seed-mode output is unchanged and libFuzzer mutations map to choices.
*/
struct Rng {
	uint32_t state;					// LCG state (seed mode)
	const uint8_t* data;			// byte source, or 0 in seed mode
	size_t size;
	size_t position;
	uint32_t word() {
		if (data != 0) {			// libFuzzer bytes: big-endian quad, zero-padded past the end
			uint32_t v = 0;
			for (int i = 0; i < 4; ++i) { v = (v << 8) | (position < size ? data[position++] : 0u); }
			return v;
		}
		state = state * 1664525u + 1013904223u;
		return state;
	}
};

/* Bounded pick via multiply-shift on the full word: uses the high bits (full period), dodging LCG low-bit cycling. */
static unsigned pick(Rng& r, unsigned n) { return static_cast<unsigned>((static_cast<uint64_t>(r.word()) * n) >> 32); }

enum { NI = 8, NF = 4 };	// int / float scalar slots the generated frame provides

/* Append one instruction line (leading space + text + '\n'), attaching+consuming a pending label prefix if present. */
static void putLine(std::string& p, std::string& pending, const char* line) {
	if (!pending.empty()) { p += pending; pending.clear(); }		// label carries its trailing ':'; the line's space separates
	p += line;
}

/* One straight-line op: an int/float value op (VVV or VVC) or a G2 memory op (index masked in-bounds). */
static void emitSimpleOp(std::string& p, Rng& r, std::string& pending) {
	static const char* const IOPS[] = { "ADDi", "SUBi", "MULi", "ANDi", "IORi", "XORi" };
	static const char* const FOPS[] = { "ADDf", "SUBf", "MULf", "DIVf" };
	char buf[64];
	const unsigned choice = pick(r, 18);
	if (choice < 5) {
		const char* op = IOPS[pick(r, 6)];
		if (pick(r, 2)) { std::snprintf(buf, sizeof buf, " %s $i%u $i%u #%d\n", op, pick(r, NI), pick(r, NI), static_cast<int>(r.word())); }
		else { std::snprintf(buf, sizeof buf, " %s $i%u $i%u $i%u\n", op, pick(r, NI), pick(r, NI), pick(r, NI)); }
	} else if (choice == 5) {
		std::snprintf(buf, sizeof buf, " ABSi $i%u $i%u\n", pick(r, NI), pick(r, NI));
	} else if (choice == 6) {
		std::snprintf(buf, sizeof buf, " ANDi $idx $i%u #7\n", pick(r, NI)); putLine(p, pending, buf);	// divisor 0-7: ~1/8 traps
		std::snprintf(buf, sizeof buf, " %s $i%u $i%u $idx\n", pick(r, 2) ? "DIVi" : "MODi", pick(r, NI), pick(r, NI));
	} else if (choice < 9) {
		const unsigned fop = pick(r, 4);
		if (fop == 3) {																			// DIVf: force a finite divisor >= 1 (ABSf then +1) - avoids 0/0 (NaN, unspecified bits) and x/0 (Inf) and runaway growth
			const unsigned dv = pick(r, NF);
			std::snprintf(buf, sizeof buf, " ABSf $f%u $f%u\n", dv, dv); putLine(p, pending, buf);
			std::snprintf(buf, sizeof buf, " ADDf $f%u $f%u #1.0\n", dv, dv); p += buf;
			std::snprintf(buf, sizeof buf, " DIVf $f%u $f%u $f%u\n", pick(r, NF), pick(r, NF), dv);
		} else {
			std::snprintf(buf, sizeof buf, " %s $f%u $f%u $f%u\n", FOPS[fop], pick(r, NF), pick(r, NF), pick(r, NF));
		}
	} else if (choice == 9) {
		std::snprintf(buf, sizeof buf, " %s $f%u $f%u\n", pick(r, 2) ? "ABSf" : "FLOf", pick(r, NF), pick(r, NF));
	} else {
		std::snprintf(buf, sizeof buf, " ANDi $idx $i%u #7\n", pick(r, NI)); putLine(p, pending, buf);	// in-bounds index (size 8)
		const unsigned a = pick(r, NI);
		switch (pick(r, 8)) {
			case 0: std::snprintf(buf, sizeof buf, " POKE &buf $idx $i%u\n", a); break;		// POKE_CVV
			case 1: std::snprintf(buf, sizeof buf, " PEEK $i%u &buf $idx\n", a); break;		// PEEK_VCV
			case 2: std::snprintf(buf, sizeof buf, " POKE $p $idx $i%u\n", a); break;		// POKE_VVV (frame pointer)
			case 3: std::snprintf(buf, sizeof buf, " PEEK $i%u $p $idx\n", a); break;		// PEEK_VVV
			case 4: std::snprintf(buf, sizeof buf, " SETL $arr $idx $i%u\n", a); break;		// SETL
			case 5: std::snprintf(buf, sizeof buf, " GETL $i%u $arr $idx\n", a); break;		// GETL
			/*
				Scalar deref of a pointer aimed at a LIVE scalar: `PEEK $x $ps` / `POKE $ps $x` finalize to PEEK_VCV /
				POKE_CVV with a NON-address const base and the pointer in the variable operand - the case v2.3a-lite
				must NOT skip the flush for (the alias is a dirty cached scalar). Re-aim $ps each time so it stays live.
			*/
			case 6: std::snprintf(buf, sizeof buf, " ADRL $ps $i%u *0\n", pick(r, NI)); putLine(p, pending, buf);
					std::snprintf(buf, sizeof buf, " PEEK $i%u $ps\n", a); break;			// PEEK_VCV, const-0 base
			default: std::snprintf(buf, sizeof buf, " ADRL $ps $i%u *0\n", pick(r, NI)); putLine(p, pending, buf);
					std::snprintf(buf, sizeof buf, " POKE $ps $i%u\n", a); break;			// POKE_CVV, const-0 base
		}
	}
	putLine(p, pending, buf);
}

/*
	G3 control flow. Emit `stmts` statements; each is a simple op, a counted FORi loop, or a forward if-skip. Loops and
	skips recurse (depth <= MAX_DEPTH) to nest; a loop's counter is keyed by depth so a nested loop never clobbers its
	enclosing counter. limit >= 1 so the body always runs (no pre-guard needed). Loops at small fuel exercise suspend/
	resume. A label is a prefix on the next instruction (`pending`), so the loop back-edge target rides the body's first
	op and each skip's forward target rides a guaranteed trailing op - never a bare/stacked label line.
*/
enum { NFUNC = 3 };			// non-recursive int helpers fn0..fn2
static bool g_deepRecursion = false;	// when set, some rec calls take a raw (possibly huge) count -> ipStack overflow trap

static void emitBody(std::string& p, Rng& r, std::string& pending, int stmts, int depth, int& label) {
	static const char* const CMP[] = { "LSSi", "LEQi", "GEQi", "EQUi", "NEQi" };
	const int MAX_DEPTH = 2;
	char buf[64];
	for (int n = 0; n < stmts; ++n) {
		unsigned kind = pick(r, 18);
		if ((kind == 0 || kind == 1) && depth >= MAX_DEPTH) { kind = 99; }	// no deeper loops/skips; calls still allowed
		if (kind == 0) {
			const int id = label++;
			const int limit = 2 + static_cast<int>(pick(r, 38));
			std::snprintf(buf, sizeof buf, " MOVi $c%d #0\n", depth); putLine(p, pending, buf);	// consumes any incoming label
			std::string back = ".l" + std::to_string(id) + ":";									// rides the body's first op
			emitBody(p, r, back, 1 + static_cast<int>(pick(r, 4)), depth + 1, label);
			std::snprintf(buf, sizeof buf, " FORi $c%d #%d @.l%d\n", depth, limit, id); putLine(p, pending, buf);
		} else if (kind == 1) {
			const int id = label++;
			std::snprintf(buf, sizeof buf, " %s $i%u $i%u @.s%d\n", CMP[pick(r, 5)], pick(r, NI), pick(r, NI), id); putLine(p, pending, buf);
			std::string none;
			emitBody(p, r, none, 1 + static_cast<int>(pick(r, 3)), depth + 1, label);
			std::string skip = ".s" + std::to_string(id) + ":";									// rides a guaranteed trailing op
			emitSimpleOp(p, r, skip);
		} else if (kind == 2) {																	// direct GAZL call: fnK(int, int) -> int
			std::snprintf(buf, sizeof buf, " MOVi %%1 $i%u\n", pick(r, NI)); putLine(p, pending, buf);
			std::snprintf(buf, sizeof buf, " MOVi %%2 $i%u\n", pick(r, NI)); p += buf;
			std::snprintf(buf, sizeof buf, " CALL &fn%u %%0 *3\n", pick(r, NFUNC)); p += buf;
			std::snprintf(buf, sizeof buf, " MOVi $i%u %%0\n", pick(r, NI)); p += buf;
		} else if (kind == 3) {																	// native math call (identical C in both engines)
			if (pick(r, 2)) {
				std::snprintf(buf, sizeof buf, " MOVf %%1 $f%u\n", pick(r, NF)); putLine(p, pending, buf);
				p += " ABSf %1 %1\n";								// sqrt domain guard: a negative arg yields a NaN whose SIGN/payload is unspecified (IEEE 754). If that NaN is later read as an integer (window-slot pun) and branched on, the two engines pick different NaN bits -> divergent control flow. Not a miscompile - keep the fuzzer on defined behavior.
				p += " CALL ^sqrt %0 *2\n";
				std::snprintf(buf, sizeof buf, " MOVf $f%u %%0\n", pick(r, NF)); p += buf;
			} else {
				std::snprintf(buf, sizeof buf, " MOVf %%1 $f%u\n", pick(r, NF)); putLine(p, pending, buf);
				std::snprintf(buf, sizeof buf, " MOVf %%2 $f%u\n", pick(r, NF)); p += buf;
				p += " CALL ^atan2 %0 *3\n";
				std::snprintf(buf, sizeof buf, " MOVf $f%u %%0\n", pick(r, NF)); p += buf;
			}
		} else if (kind == 5) {																	// pointer-param call: fnp(&arr, index) derefs a NONFRAME param
			std::snprintf(buf, sizeof buf, " ADRL %%1 $arr *0\n"); putLine(p, pending, buf);	// pass the caller's array (MYFRAME here -> NONFRAME param in fnp)
			std::snprintf(buf, sizeof buf, " ANDi $idx $i%u #7\n", pick(r, NI)); p += buf;
			p += " MOVi %2 $idx\n";
			p += " CALL &fnp %0 *3\n";
			std::snprintf(buf, sizeof buf, " MOVi $i%u %%0\n", pick(r, NI)); p += buf;
		} else if (kind == 4) {																	// recursion: rec(count) -> sum
			if (g_deepRecursion && pick(r, 8) == 0) {
				std::snprintf(buf, sizeof buf, " MOVi %%1 $i%u\n", pick(r, NI)); putLine(p, pending, buf);	// raw count: may overflow ipStack
			} else {
				std::snprintf(buf, sizeof buf, " ANDi $idx $i%u #15\n", pick(r, NI)); putLine(p, pending, buf);	// bounded, shallow
				p += " MOVi %1 $idx\n";
			}
			p += " CALL &rec %0 *2\n";
			std::snprintf(buf, sizeof buf, " MOVi $i%u %%0\n", pick(r, NI)); p += buf;
		} else {
			emitSimpleOp(p, r, pending);
		}
	}
}

/* G4 callees: three int helpers fn(int,int)->int (window %0=out, %1/%2=in) and one bounded self-recursive summer. */
static const char* const CALLEES =
	"fn0: FUNC\n$r: OUTi\n$a: INPi\n$b: INPi\n$t: LOCi\n ADDi $r $a $b\n MULi $t $a $b\n XORi $r $r $t\n SUBi $r $r $b\n RETU\n"
	"fn1: FUNC\n$r: OUTi\n$a: INPi\n$b: INPi\n$t: LOCi\n LSSi $a $b @.lt\n SUBi $r $a $b\n IORi $t $a $b\n ADDi $r $r $t\n RETU\n"
	".lt: ADDi $r $a $b\n RETU\n"																						// MULTI-RETU callee: polices the extent-to-next-FUNC rule (branch past the first RETU)
	"fn2: FUNC\n$r: OUTi\n$a: INPi\n$b: INPi\n MULi $r $a $b\n ADDi $r $r $a\n SUBi $r $r $b\n RETU\n"
	"rec: FUNC\n PARA *2\n$r: OUTi\n$n: INPi\n$t: LOCi\n MOVi $r #0\n LEQi $n #0 @.base\n"
	" SUBi $t $n #1\n MOVi %1 $t\n CALL &rec %0 *2\n ADDi $r %0 $n\n.base: RETU\n"
	/*
		Pointer-param callee (v2.3a realm stressor): $pp is a live-in INPp -> NONFRAME, so its PEEK/POKE skip the frame
		flush. $t is a dirty local held across the POKE; the caller passes &(caller array), so $pp can't alias $t - the
		skip must be sound. Exercises the §1.1 win the firmware relies on (a received pointer never reaches this frame).
	*/
	"fnp: FUNC\n$r: OUTi\n$pp: INPp\n$k: INPi\n$m: LOCi\n$t: LOCi\n"
	" ANDi $m $k #7\n PEEK $t $pp $m\n ADDi $t $t #1\n POKE $pp $m $t\n MOVi $r $t\n RETU\n";

static std::string buildProgram(Rng& r) {
	std::string p = "buf: GLOB *8\n";
	for (int i = 0; i < 8; ++i) { p += " DATi #0\n"; }
	p += CALLEES;
	p += "main: FUNC\n PARA *3\n";
	for (int i = 0; i < NI; ++i) { p += " $i" + std::to_string(i) + ": LOCi\n"; }
	for (int i = 0; i < NF; ++i) { p += " $f" + std::to_string(i) + ": LOCf\n"; }
	p += " $arr: LOCA *8\n $p: LOCp\n $ps: LOCp\n $idx: LOCi\n $c0: LOCi\n $c1: LOCi\n";
	char buf[48];
	for (int i = 0; i < NI; ++i) { std::snprintf(buf, sizeof buf, " MOVi $i%d #%d\n", i, static_cast<int>(r.word())); p += buf; }
	for (int i = 0; i < NF; ++i) { std::snprintf(buf, sizeof buf, " MOVf $f%d #%d.%u\n", i, static_cast<int>(pick(r, 2000)) - 1000, pick(r, 1000)); p += buf; }
	p += " ADRL $p $arr *0\n";
	p += " MOVi $c0 #0\n";									// fully initialize the LOCA array: GETL/PEEK read every slot, so an
	for (int i = 0; i < 8; ++i) {							// unwritten slot would be a read-before-write - UNSPECIFIED, and the
		std::snprintf(buf, sizeof buf, " MOVi $idx #%d\n SETL $arr $idx $c0\n", i);	// interp (memory home) and JIT (register cache) legitimately read it
		p += buf;											// differently. Defined init keeps the differential comparing only defined state.
	}
	std::string pending;
	int label = 0;
	emitBody(p, r, pending, 30 + static_cast<int>(pick(r, 60)), 0, label);
	putLine(p, pending, " RETU\n");
	return p;
}

static std::string generateProgram(uint32_t seed) {					// reproducible seed stream (--gen / --gen1)
	Rng r; r.state = seed ? seed : 1; r.data = 0; r.size = 0; r.position = 0;
	return buildProgram(r);
}

static std::string generateProgramFromBytes(const uint8_t* data, size_t size) {	// libFuzzer input as the choice stream
	Rng r; r.state = 0; r.data = data; r.size = size; r.position = 0;
	return buildProgram(r);
}

/* Assemble one generated program and run the four-way diff; on divergence dump the program + abort. */
static void runDiff(const std::string& programText) {
	StandardFPEnvScope fpEnv;			// interp + JIT run this program under a known, host-independent FP env
	const uint8_t* Data = reinterpret_cast<const uint8_t*>(programText.data());
	const size_t Size = programText.size();
	try {
		Symbols globals;
		// Mirror GAZLCmd's real native names/order so corpus programs assemble; stub the side-effecting ones (deterministic,
		// no stdout, no real abort), keep the pure math real for richer float coverage. Same table for both engines = a valid diff.
		static const NativeFunc NATIVE_TABLE[] = { fuzzStub, fuzzStub, fuzzStub, fuzzStub, fuzzStub, fuzzStub, fuzzStub, gazlAtan2, gazlSqrt, gazlLog };
		static const char* const NATIVE_NAMES[] = { "abort", "assertFail", "printInt", "printFloat", "print", "printLF", "input", "atan2", "sqrt", "log" };
		for (size_t i = 0; i < sizeof (NATIVE_TABLE) / sizeof (*NATIVE_TABLE); ++i) { globals.registerNative(NATIVE_NAMES[i], static_cast<int>(i)); }

		AssembledProgram program;
		for (int i = 0; i < DATA_MEMORY_SIZE; ++i) { memory[i].i = 0; }	// clean slate before assembly: the frame-local region is otherwise stale residue from the previous program (platform- and stream-position-dependent), so an uninitialized-local read would be nondeterministic across engines/platforms
		{
			std::istringstream gazlStream(std::string(reinterpret_cast<const char*>(Data), reinterpret_cast<const char*>(Data) + Size));
			Assembler assem(CODE_MEMORY_SIZE, code, FUNCTION_TABLE_SIZE, functionTable, DATA_MEMORY_SIZE, memory, globals);
			assem.newUnit("string");
			std::string line;
			while (!gazlStream.eof()) { getline(gazlStream, line); assem.feed(line.c_str()); }
			assem.finalize(program);
		}
		const Pointer mainFunction = globals.findFunction("main");
		if (mainFunction == 0 || !GAZL::jitAvailable()) { return; }

		static Value snapshot[DATA_MEMORY_SIZE];
		static Value interpImage[DATA_MEMORY_SIZE];
		std::memcpy(snapshot, memory, sizeof (memory));					// clean post-assembly image; every run restores from it

		NativeJitCompiler compiler;
		JitModule module;
		compiler.compile(program, module);								// throws JitException -> caught below

#ifdef FUZZ_TEXT_INPUT
		// TEXT LANE = crash / assert coverage ONLY, no differential comparison. Arbitrary / mutated source can legally
		// break the aliasing (realm) contract - e.g. a global-provenance POKE whose out-of-array index reaches a frame
		// slot the JIT holds in a register. Under the rules (docs/JitAliasingRegAlloc.md) the JIT is *permitted* to
		// resolve that differently from the interpreter, so a memory / status diff here is a rule violation, not a
		// miscompile, and a cheap address filter can't separate the two (cross-realm UB is a provenance/extent question,
		// not an address range). So we exercise the assembler + the JIT compiler + BOTH engines to surface crashes,
		// asserts and hard codegen traps on real / arbitrary programs, but we do NOT compare their results. Contract-
		// abiding differential checking is lanes 1/2 - the structured generator, which never crosses realms by design.
		// Bounded SINGLE run (not runEngine's resetTimeOut-on-TIME_OUT loop): we are crash-hunting, not comparing, so the
		// program need not finish. A fixed step budget keeps throughput high - a pathological big-loop program otherwise
		// runs tens of seconds and starves the lane (and its own corpus replay). Codegen crashes/asserts trip early, well
		// inside this budget; running to completion adds no crash coverage.
		const Int TEXT_FUEL = 2000000;
		std::memcpy(memory, snapshot, sizeof (memory));
		{ Processor interp(program, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0); if (interp.enterCall(mainFunction) == OK) { interp.resetTimeOut(TEXT_FUEL); interp.run(); } }
		std::memcpy(memory, snapshot, sizeof (memory));
		{ JitProcessor jit(module, program, CALL_STACK_SIZE, callStack, NATIVE_TABLE); if (jit.enterCall(mainFunction) == OK) { jit.resetTimeOut(TEXT_FUEL); jit.run(); } }
#else
		std::memcpy(memory, snapshot, sizeof (memory));
		Processor interp(program, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0);
		const Status interpStatus = runEngine(interp, mainFunction, 10000000);	// the oracle
		std::memcpy(interpImage, memory, sizeof (memory));

		std::memcpy(memory, snapshot, sizeof (memory));
		{ JitProcessor jit(module, program, CALL_STACK_SIZE, callStack, NATIVE_TABLE); const Status s = runEngine(jit, mainFunction, 10000000); requireMatch("jit full-fuel", Data, Size, interpStatus, interpImage, s, memory); }
		// tiny-fuel suspend/resume lanes (structured lane only): re-enter run() thousands of times - seconds/unit on the
		// larger text programs - and the small structured programs already cover suspend/resume.
		std::memcpy(memory, snapshot, sizeof (memory));
		{ JitProcessor jit(module, program, CALL_STACK_SIZE, callStack, NATIVE_TABLE); const Status s = runEngine(jit, mainFunction, 64); requireMatch("jit tiny-fuel", Data, Size, interpStatus, interpImage, s, memory); }
		std::memcpy(memory, snapshot, sizeof (memory));
		{ Processor it(program, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0); const Status s = runEngine(it, mainFunction, 64); requireMatch("interp tiny-fuel", Data, Size, interpStatus, interpImage, s, memory); }
#endif
	}
	catch (GAZL::Exception& e) {
#ifdef FUZZ_TRACE_SKIP
		extern long g_fuzzSkips; if (g_fuzzSkips < 3) { fprintf(stderr, "SKIP: %s\n", e.what()); } ++g_fuzzSkips;
#endif
		return;
	}
#ifdef FUZZ_TRACE_SKIP
	{ extern long g_fuzzRan; ++g_fuzzRan; }
#endif
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
#ifdef FUZZ_TEXT_INPUT
	// text->JIT: feed the raw fuzz bytes to the assembler as GAZL SOURCE, then run the same interp-vs-JIT diff.
	// Non-assembling / non-compiling inputs are caught inside runDiff and skipped, exactly like the structured lane.
	runDiff(std::string(reinterpret_cast<const char*>(Data), reinterpret_cast<const char*>(Data) + Size));
#else
	runDiff(generateProgramFromBytes(Data, Size));	// libFuzzer bytes -> structured always-valid program -> diff
#endif
	return 0;
}

#ifdef FUZZ_TRACE_SKIP
long g_fuzzSkips = 0, g_fuzzRan = 0;
#endif

#else

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    try {
		Symbols globals;

		static const NativeFunc NATIVE_TABLE[] = {
			abort, abort, input, gazlAtan2, gazlSqrt, gazlLog, testMul, testCallback
		};

		static const char* NATIVE_NAMES[] = {
			"abort", "assertFail", "input", "atan2", "sqrt", "log", "testMul", "testCallback"
		};

		for (int i = 0; i < sizeof (NATIVE_TABLE) / sizeof (*NATIVE_TABLE); ++i) {
			globals.registerNative(NATIVE_NAMES[i], i);
		}
		
		AssembledProgram program;

		{
			std::istringstream gazlStream(std::string(reinterpret_cast<const char*>(Data), reinterpret_cast<const char*>(Data) + Size));
			{
				Assembler assem(CODE_MEMORY_SIZE, code, FUNCTION_TABLE_SIZE, functionTable, DATA_MEMORY_SIZE, memory, globals);
				assem.newUnit("string");
				while (!gazlStream.eof()) {
					std::string line;
					getline(gazlStream, line);
					assem.feed(line.c_str());
				}
				assem.finalize(program);
			}
		}

		{
			Processor pmachine(program, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0);
			Pointer mainFunction = globals.findFunction("main");
			if (mainFunction != 0) {
				Status status = pmachine.enterCall(mainFunction);
				assert(status == OK);
				pmachine.resetTimeOut(10000000);
				status = pmachine.run();
			}
		}
	}
	catch (GAZL::Exception& x) {
		// std::cerr << "Exception: " << x.what() << std::endl;
		return 0;
	}
  	return 0;  // Non-zero return values are reserved for future use.
}

#endif	// JITDIFF
#endif

#ifdef LIBFUZZ_STANDALONE

#ifndef _WIN32
#include <dirent.h>							// POSIX corpus-directory replay; MSVC has no dirent (Windows uses --gen / single-file replay)
#endif

void doOne(const char* fn) {
	printf ("%s\n", fn);
	fprintf(stderr, "Running: %s\n", fn);
	FILE *f = fopen(fn, "r");
	assert(f);
	fseek(f, 0, SEEK_END);
	size_t len = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = (unsigned char*)malloc(len);
	size_t n_read = fread(buf, 1, len, f);
	fclose(f);
	assert(n_read == len);
	LLVMFuzzerTestOneInput(buf, len);
	free(buf);
	fprintf(stderr, "Done:    %s: (%zd bytes)\n", fn, n_read);
}

int main(int argc, const char* argv[]) {
#if defined(JITDIFF) && defined(GAZL_JIT)
	if (argc >= 3 && strcmp(argv[1], "--gen1") == 0) {		// dump the generated program for a seed (repro / inspection)
		if (argc >= 4 && strcmp(argv[3], "deep") == 0) { g_deepRecursion = true; }
		fputs(generateProgram(static_cast<uint32_t>(strtoul(argv[2], 0, 10))).c_str(), stdout);
		return 0;
	}
	if (argc >= 3 && strcmp(argv[1], "--file") == 0) {		// run a hand-written GAZL file through the 4-way differential (minimization / repro)
		std::ifstream in(argv[2]);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		runDiff(text);
		fprintf(stderr, "--file: no divergence\n");
		return 0;
	}
	if (argc >= 3 && strcmp(argv[1], "--ick") == 0) {		// interpreter-only checksum for a seed (localize interp-vs-JIT platform divergences)
		const uint32_t seed = static_cast<uint32_t>(strtoul(argv[2], 0, 10));
		if (argc >= 4 && strcmp(argv[3], "deep") == 0) { g_deepRecursion = true; }
		const std::string programText = generateProgram(seed);
		Symbols globals;
		static const NativeFunc NT[] = { fuzzStub, fuzzStub, fuzzStub, fuzzStub, fuzzStub, fuzzStub, fuzzStub, gazlAtan2, gazlSqrt, gazlLog };
		static const char* const NN[] = { "abort", "assertFail", "printInt", "printFloat", "print", "printLF", "input", "atan2", "sqrt", "log" };
		for (size_t i = 0; i < sizeof (NT) / sizeof (*NT); ++i) { globals.registerNative(NN[i], static_cast<int>(i)); }
		AssembledProgram program;
		{
			std::istringstream gazlStream(programText);
			Assembler assem(CODE_MEMORY_SIZE, code, FUNCTION_TABLE_SIZE, functionTable, DATA_MEMORY_SIZE, memory, globals);
			assem.newUnit("string");
			std::string line;
			while (!gazlStream.eof()) { getline(gazlStream, line); assem.feed(line.c_str()); }
			assem.finalize(program);
		}
		const Pointer mainFunction = globals.findFunction("main");
#ifdef FUZZ_POISON
		for (UInt i = program.globalsSize; i + program.constsSize < program.memorySize; ++i) { memory[i].i = 0x7F7F7F7F; }
#endif
		Processor interp(program, CALL_STACK_SIZE, callStack, NT, 0);
		const Status st = runEngine(interp, mainFunction, 10000000);
		uint32_t sum = 2166136261u;
		for (int i = 0; i < DATA_MEMORY_SIZE; ++i) { sum = (sum ^ static_cast<uint32_t>(memory[i].i)) * 16777619u; }
		fprintf(stderr, "ick seed=%u status=%d word13=%d sum=%08x\n", seed, static_cast<int>(st), memory[13].i, sum);
		return 0;
	}
	if (argc >= 2 && strcmp(argv[1], "--gen") == 0) {		// self-contained generative fuzzing: --gen COUNT [SEED0] [deep]
		const uint32_t count = argc >= 3 ? static_cast<uint32_t>(strtoul(argv[2], 0, 10)) : 100000;
		const uint32_t seed0 = argc >= 4 ? static_cast<uint32_t>(strtoul(argv[3], 0, 10)) : 1;
		if (argc >= 5 && strcmp(argv[4], "deep") == 0) { g_deepRecursion = true; }	// some rec calls overflow the ipStack
		for (uint32_t i = 0; i < count; ++i) {
			if ((i % 5000) == 0) { fprintf(stderr, "gen %u/%u (seed %u)\n", i, count, seed0 + i); }
			g_currentSeed = seed0 + i;				// so a divergence prints the EXACT seed
			runDiff(generateProgram(seed0 + i));	// seed -> program -> diff; aborts on any JIT/interp divergence
		}
		fprintf(stderr, "gen %u programs, no divergence\n", count);
#ifdef FUZZ_TRACE_SKIP
		{ extern long g_fuzzSkips, g_fuzzRan; fprintf(stderr, "  ran=%ld skipped=%ld\n", g_fuzzRan, g_fuzzSkips); }
#endif
		return 0;
	}
#endif
	for (int i = 1; i < argc; ++i) {
#ifndef _WIN32
		DIR *dir;
		struct dirent *ent;
		if ((dir = opendir (argv[i])) != NULL) {
			while ((ent = readdir (dir)) != NULL) {
				if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
					char fn[1024];
					strcpy(fn, argv[i]);
					strcat(fn, ent->d_name);
					doOne(fn);
				}
			}
				closedir (dir);
		} else {
			if (errno == ENOTDIR) {
				doOne(argv[i]);
			} else {
				perror("");
				return EXIT_FAILURE;
			}
		}
#else
		doOne(argv[i]);							// Windows: single-file replay only (no dirent corpus-dir walk)
#endif
	}
	return 0;
}
#endif

#ifndef LIBFUZZ
#ifndef LIBFUZZ_STANDALONE
int main(int argc, const char* argv[]) {
	try {
	#if !defined(NDEBUG)
		unitTest();
	#endif

		// Separate `--` options from positional arguments so the positional layout stays
		// `<file> [<function>] [<define symbol> <define value> ...]` regardless of flag placement.
		std::vector<const char*> pos;
		int benchRepeat = 0;	// 0 = normal single run; >0 = benchmark mode with this many measured iterations
		int benchWarmup = 3;	// iterations run and discarded before measuring
		bool useJit = false;	// --jit: run on the native (arm64) JIT instead of the interpreter (see GAZL_JIT build)
		bool jitStats = false;	// --jit-stats: implies --jit; print `jitstats compile_ms=.. code_bytes=.. funcs=..`
		bool noLibm = false;	// --no-libm: don't register the atan2/sqrt/log natives (for programs that define their own)
		const char* noNativeSpec = 0;	// --no-native=name,...: don't register the listed built-in natives (for programs defining same-named functions)
		const char* forwardSpec = 0;	// --forward=native:function,...: satisfy ^native calls with GAZL functions (see forwardNative)
		const char* emitCpp = 0;	// --emit-cpp=<path>: write a standalone C++ translation of the program and exit
		bool promoteLocals = false;	// --promote-locals: emit-cpp Tier 1 - locals become C++ locals (see GAZLCpp.h)
		const char* emitJit = 0;	// --emit-jit=<path>: write the JIT's raw machine code + <path>.txt layout sidecar and exit (see tools/jitDisasm.sh)
		for (int i = 0; i < argc; ++i) {
			const char* a = argv[i];
			if (i > 0 && a[0] == '-' && a[1] == '-') {
				if (strncmp(a, "--bench", 7) == 0) {
					benchRepeat = (a[7] == '=') ? atoi(a + 8) : 10;
				} else if (strncmp(a, "--warmup", 8) == 0) {
					benchWarmup = (a[8] == '=') ? atoi(a + 9) : benchWarmup;
				} else if (strcmp(a, "--jit") == 0) {
					useJit = true;
				} else if (strcmp(a, "--jit-stats") == 0) {
					useJit = true; jitStats = true;
				} else if (strcmp(a, "--no-libm") == 0) {
					noLibm = true;
				} else if (strncmp(a, "--forward=", 10) == 0) {
					forwardSpec = a + 10;
				} else if (strncmp(a, "--no-native=", 12) == 0) {
					noNativeSpec = a + 12;
				} else if (strncmp(a, "--emit-cpp=", 11) == 0) {
					emitCpp = a + 11;
				} else if (strcmp(a, "--promote-locals") == 0) {
					promoteLocals = true;
				} else if (strncmp(a, "--emit-jit=", 11) == 0) {
					emitJit = a + 11;
				} else {
					throw CmdException(std::string("Unknown option: ") + a);
				}
			} else {
				pos.push_back(a);
			}
		}

		if (pos.size() < 2) {
			std::cerr << "GAZLCmd <filename> [<function> = 'main'] [<define symbol> <define value> ...]" << std::endl;
			std::cerr << "        [--bench[=N]] [--warmup=W]   run N timed iterations (default 10), W warmups (default 3)"
					<< std::endl;
			std::cerr << "        [--jit]                      run on the native JIT (arm64 / x64) (falls back to interpreter)"
					<< std::endl;
			std::cerr << "        [--no-libm]                  skip the atan2/sqrt/log natives (for self-contained libm)"
					<< std::endl;
			std::cerr << "        [--forward=nat:func,...]     satisfy ^nat native calls with GAZL functions (interpreter only)"
					<< std::endl;
			std::cerr << "        [--emit-cpp=F]               write a standalone C++ translation to F and exit (Tier 0)"
					<< std::endl;
			std::cerr << "        [--promote-locals]           emit-cpp Tier 1: locals become C++ locals (realm-rule conforming)"
					<< std::endl;
			return 0;
		}

		Symbols globals;

		for (int i = 0; i < sizeof (NATIVE_NAMES) / sizeof (*NATIVE_NAMES); ++i) {	// NAMES, not TABLE: the unnamed tail of
			if (noLibm && (strcmp(NATIVE_NAMES[i], "atan2") == 0 || strcmp(NATIVE_NAMES[i], "sqrt") == 0	// NATIVE_TABLE is the
					|| strcmp(NATIVE_NAMES[i], "log") == 0))											// --forward slots
				continue;								// a self-contained libm (e.g. perfTest) defines these itself
			if (noNativeSpec != 0) {					// --no-native: the program defines a same-named GAZL function itself
				const std::string spec(std::string(",") + noNativeSpec + ",");
				if (spec.find(std::string(",") + NATIVE_NAMES[i] + ",") != std::string::npos) continue;
			}
			globals.registerNative(NATIVE_NAMES[i], i);
		}

		for (size_t i = 3; i + 2 <= pos.size(); i += 2) {
			Value v;
			v.i = atoi(pos[i + 1]);
			globals.defineConstant(pos[i + 0], false, v);
		}

		// --forward: register each native name now (the assembler must resolve `^name`); the paired GAZL function names
		// are remembered and looked up after assembly.
		std::vector<std::string> forwardFunctionNames;
		if (forwardSpec != 0) {
			std::string spec(forwardSpec);
			size_t at = 0;
			while (at < spec.size()) {
				const size_t comma = spec.find(',', at);
				const std::string pair = spec.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
				const size_t colon = pair.find(':');
				if (colon == std::string::npos) throw CmdException(std::string("--forward: expected native:function, got '") + pair + "'");
				if (forwardFunctionNames.size() >= MAX_FORWARDS) throw CmdException("--forward: too many forwards");
				globals.registerNative(pair.substr(0, colon).c_str(), FIRST_FORWARD_INDEX + static_cast<int>(forwardFunctionNames.size()));
				forwardFunctionNames.push_back(pair.substr(colon + 1));
				if (comma == std::string::npos) break;
				at = comma + 1;
			}
		}
		
		AssembledProgram program;

		{
			std::ifstream gazlStream(pos[1], std::ifstream::binary);
			if (!gazlStream.good()) throw CmdException("Could not open input file");
			gazlStream.exceptions(std::ios_base::badbit);

			{
				Assembler assem(CODE_MEMORY_SIZE, code, FUNCTION_TABLE_SIZE, functionTable, DATA_MEMORY_SIZE, memory, globals);
				assem.newUnit(pos[1]);
				
				int lineCounter = 1;
				while (gazlStream.good()) {
					std::string line;
					try {
						getline(gazlStream, line);
						assem.feed(line.c_str());
						++lineCounter;
					}
					catch (const GAZL::Exception& e) {
						std::cerr << e.what() << std::endl << "Line " << lineCounter << ": " << line.c_str()
								<< std::endl;
						return -1;
					}
				}
				if (gazlStream.bad()) throw CmdException("Problem with input stream");

				assem.finalize(program);

				std::cerr << "Code size: " << program.codeSize << ", globals size: " << program.globalsSize << ", consts size: "
						<< program.constsSize << ", functions: " << program.functionCount << std::endl;
				std::cerr << "--------------------------------------------------------------------------------"
						<< std::endl;
			}
			
			gazlStream.close();
		}

		if (emitCpp != 0) {
			const char* mfn = (pos.size() > 2) ? pos[2] : "main";
			Pointer mainPtr = globals.findFunction(mfn);
			if (mainPtr == NULL_POINTER) throw CmdException(std::string("emit-cpp: no function '") + mfn + "'");
			std::string src = translateToCpp(program, static_cast<UInt>(mainPtr - IP_OFFSET), promoteLocals);
			if (src.empty()) { std::cerr << "emit-cpp: program uses an opcode outside the Tier-0 subset" << std::endl; return -1; }
			std::ofstream out(emitCpp, std::ofstream::binary);
			if (!out.good()) throw CmdException(std::string("emit-cpp: cannot write '") + emitCpp + "'");
			out << src;
			std::cerr << "emit-cpp: wrote " << emitCpp << std::endl;
			return 0;
		}

#ifdef GAZL_JIT
		if (emitJit != 0) {
			NativeJitCompiler compiler;
			JitModule module;
			compiler.compile(program, module);
			/*
				The page base is not exposed; the lowest interior pointer (all function entries + the dispatcher) is the
				start of everything emitted, and codeWords() spans to the end. Offsets in the sidecar are bytes from it.
			*/
			const char* base = static_cast<const char*>(module.dispatchEntry());
			void* const* entries = module.entryTable();
			for (UInt f = 0; f < program.functionCount; ++f) {
				if (static_cast<const char*>(entries[f]) < base) { base = static_cast<const char*>(entries[f]); }
			}
			std::ofstream binary(emitJit, std::ofstream::binary);
			if (!binary.good()) throw CmdException(std::string("emit-jit: cannot write '") + emitJit + "'");
			binary.write(base, static_cast<std::streamsize>(module.codeWords() * 4));
			std::ofstream sidecar((std::string(emitJit) + ".txt").c_str());
#if defined(__aarch64__) || defined(_M_ARM64)
			sidecar << "arch arm64\n";
#else
			sidecar << "arch x64\n";
#endif
			sidecar << "code_bytes " << (module.codeWords() * 4) << "\n";
			sidecar << "dispatch " << (static_cast<const char*>(module.dispatchEntry()) - base) << "\n";
			for (UInt f = 0; f < program.functionCount; ++f) {
				sidecar << "function " << f << " " << (static_cast<const char*>(entries[f]) - base) << "\n";
			}
			std::cerr << "emit-jit: wrote " << emitJit << " + sidecar (" << (module.codeWords() * 4) << " bytes, "
					<< program.functionCount << " functions)" << std::endl;
			return 0;
		}
#else
		if (emitJit != 0) { std::cerr << "emit-jit: this build has no JIT" << std::endl; return -1; }
#endif

		{
			/*
				Pick the engine: the native JIT (--jit, arm64) if it can compile the whole program, else the interpreter.
				Both are Processor subclasses, so the run loop below is identical (§5.1).
			*/
		#ifdef GAZL_JIT
			JitModule module;					// declared before `proc`: it owns the code page and must outlive the engine
		#endif
			std::unique_ptr<Processor> proc;
		#ifdef GAZL_JIT
			if (useJit && !GAZL::jitAvailable()) {	// host forbids executable memory (entitlement / ACG) - never risk a crash
				std::cerr << "JIT: this host does not permit executable memory; using the interpreter." << std::endl;
			} else if (useJit) {
				const auto t0 = std::chrono::steady_clock::now();
				try {
					NativeJitCompiler compiler;							// the host backend; each instance owns its own
					compiler.compile(program, module);					// compiles a valid program or throws
					const auto t1 = std::chrono::steady_clock::now();
					if (jitStats) {							// machine-readable line for the benchmark harness
						const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
						std::cerr << "jitstats compile_ms=" << ms << " code_bytes=" << (module.codeWords() * 4)
								<< " funcs=" << program.functionCount << std::endl;
					} else {
						std::cerr << "JIT: compiled " << program.functionCount << " function(s) to native code." << std::endl;
					}
					proc.reset(new JitProcessor(module, program, CALL_STACK_SIZE, callStack, NATIVE_TABLE));
				} catch (const JitException& x) {			// host refused executable memory, or an opcode the backend can't lower
					std::cerr << "JIT: " << x.what() << "; using the interpreter." << std::endl;
				}
			}
		#else
			(void)jitStats;
			if (useJit) {
				std::cerr << "JIT: this build has no JIT support (needs an AArch64 GAZL_JIT build); using the interpreter."
						<< std::endl;
			}
		#endif
			const bool jitEngine = (proc != 0);				// remember which engine was picked (for per-iteration recreation)
			if (!proc) {
				proc.reset(new Processor(program, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0));
			}
			/*
				Recreate the engine (fresh dsp/ipsp). exit() terminates MID-FLIGHT, leaving frames on both stacks -
				re-entering main without this makes every --bench iteration creep upward until the deepest workload
				overflows the data stack. Also gives each iteration identical, fresh-engine conditions.
			*/
			auto remakeProc = [&]() {
			#ifdef GAZL_JIT
				if (jitEngine) { proc.reset(new JitProcessor(module, program, CALL_STACK_SIZE, callStack, NATIVE_TABLE)); return; }
			#endif
				proc.reset(new Processor(program, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0));
			};

			for (size_t i = 0; i < forwardFunctionNames.size(); ++i) {		// resolve --forward targets against the assembled program
				forwardTargets[i] = globals.findFunction(forwardFunctionNames[i].c_str());
				if (forwardTargets[i] == NULL_POINTER)
					throw CmdException(std::string("--forward: no function '") + forwardFunctionNames[i] + "'");
			}

			const char* mainFunctionName = pos.size() >= 3 ? pos[2] : "main";
			Pointer mainFunction = globals.findFunction(mainFunctionName);
			if (mainFunction == 0) throw CmdException(std::string("Could not locate function: ") + mainFunctionName);

			// Enter `main` and run it to completion, chunking across TIME_OUT so long workloads finish.
			// (Opcode counts can't be recovered here: the print* natives call resetTimeOut(), which clobbers
			// the cycle budget mid-run. Benchmarks compare wall time of the identical workload instead.)
			auto runToCompletion = [&]() {
				Status status = proc->enterCall(mainFunction);
				if (status != OK) throw CmdException(std::string("enterCall returned status ") + std::to_string(status));
				do {
					proc->resetTimeOut(0x7FFFFFFF);
					status = proc->run();
				} while (status == TIME_OUT);
				// TERMINATED is a clean, expected stop via the exit() native (e.g. a firmware harness reaching its budget).
				if (status != OK && status != TERMINATED) throw CmdException(std::string("run returned status ") + std::to_string(status));
			};

			if (benchRepeat > 0) {
				std::vector<double> samples;			// milliseconds, measured iterations only
				for (int iter = 0; iter < benchWarmup + benchRepeat; ++iter) {
					if (iter != 0) remakeProc();		// fresh stacks per iteration (see remakeProc); outside the timed span
					auto t0 = std::chrono::steady_clock::now();
					runToCompletion();
					auto t1 = std::chrono::steady_clock::now();
					if (iter >= benchWarmup)
						samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
				}
				std::sort(samples.begin(), samples.end());
				const double mn = samples.front();
				const double median = samples[samples.size() / 2];
				double sum = 0.0;
				for (size_t i = 0; i < samples.size(); ++i) sum += samples[i];
				const double mean = sum / samples.size();
				double var = 0.0;
				for (size_t i = 0; i < samples.size(); ++i) var += (samples[i] - mean) * (samples[i] - mean);
				const double stddev = std::sqrt(var / samples.size());

				std::cerr << "--------------------------------------------------------------------------------"
						<< std::endl;
				// Leading newline: workload output (e.g. printInt with no trailing LF) may not end the line.
				std::cout << "\nbench\t" << pos[1]
						<< "\titers=" << benchRepeat
						<< "\tmin_ms=" << mn
						<< "\tmedian_ms=" << median
						<< "\tmean_ms=" << mean
						<< "\tstddev_ms=" << stddev << std::endl;
			} else {
				clock_t c0 = clock();
				runToCompletion();
				clock_t c1 = clock();

				std::cerr << "--------------------------------------------------------------------------------"
						<< std::endl;
				std::cerr << "Status: 0, time: " << static_cast<double>(c1 - c0) / CLOCKS_PER_SEC
						<< "s" << std::endl;
			}
		}
	}
	catch (const std::exception& x) {
		std::cerr << "Exception: " << x.what() << std::endl;
		return 1;
	}
	catch (...) {
		std::cerr << "General exception" << std::endl;
		return 1;
	}
	return 0;
}
#endif
#endif
