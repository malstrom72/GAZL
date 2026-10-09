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
#include <stdio.h>
#include <string>
#include <vector>
#include <algorithm>
#include "../src/GAZL.h"

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
	to the native pushCall()s it, so the `^native` call behaves exactly like a `&function` call (args and return value
	in the same window). This is how the Permut8 firmware harness supplies yield/read/write/trace as concatenated GAZL
	(see tools/permut8Host.js).
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

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
	memset(memory, 0, sizeof memory);																					// zeroed as in a fresh process, so no crash depends on the previous input
	memset(code, 0, sizeof code);
	memset(functionTable, 0, sizeof functionTable);
	memset(callStack, 0, sizeof callStack);
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
		
		ProgramSizes sizes = { 0, 0, 0, 0 };

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
				assem.finalize(sizes);
			}
		}

		{
			Processor pmachine(sizes.codeSize, code, sizes.functionCount, functionTable, DATA_MEMORY_SIZE, memory
					, sizes.globalsSize, sizes.constsSize, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0);
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
#endif

#ifdef LIBFUZZ_STANDALONE

#ifndef _WIN32
#include <dirent.h>							// POSIX directory replay only; MSVC has no dirent, so Windows passes `@listfile`
#endif

void doOne(const char* fn) {
	printf ("%s\n", fn);
	fprintf(stderr, "Running: %s\n", fn);
	FILE *f = fopen(fn, "rb");																							// binary: on Windows text mode folds CRLF and fread comes up short
	assert(f);
	fseek(f, 0, SEEK_END);
	size_t len = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *buf = (unsigned char*)malloc(len);
	size_t n_read = fread(buf, 1, len, f);
	fclose(f);
	assert(n_read == len);
	LLVMFuzzerTestOneInput(buf, n_read);		// what was actually read, never more
	free(buf);
	fprintf(stderr, "Done:    %s: (%zd bytes)\n", fn, n_read);
}

/*
	Replay driver. Each argument is a file to feed to LLVMFuzzerTestOneInput, a directory whose entries to feed
	(POSIX only), or `@listfile` naming a file that holds one path per line. The list form exists because a corpus
	replay cannot be expressed any other way on Windows: there is no dirent, and a thousand-odd paths do not fit in
	a command line. Both tools/test-fuzz.sh and its .cmd twin use it, so the two behave identically.
	A crash is the signal; surviving every input is the pass.
*/
int main(int argc, const char* argv[]) {
	for (int i = 1; i < argc; ++i) {
		if (argv[i][0] == '@') {
			FILE* listFile = fopen(argv[i] + 1, "r");
			if (listFile == 0) { perror(argv[i] + 1); return EXIT_FAILURE; }
			char line[1024];
			while (fgets(line, sizeof line, listFile) != 0) {
				size_t n = strlen(line);
				while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;	// tolerate either line ending
				if (n > 0) doOne(line);
			}
			fclose(listFile);
			continue;
		}
	#ifdef _WIN32
		doOne(argv[i]);							// no directory walk here; pass files, or an @listfile
	#else
		DIR *dir;
		struct dirent *ent;
		if ((dir = opendir (argv[i])) != NULL) {
			while ((ent = readdir (dir)) != NULL) {
				if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
					char fn[1024];
					const size_t dirLength = strlen(argv[i]);
					// the directory argument need not end with a separator
					const char* separator = (dirLength > 0 && argv[i][dirLength - 1] != '/') ? "/" : "";
					if (snprintf(fn, sizeof fn, "%s%s%s", argv[i], separator, ent->d_name) >= (int)(sizeof fn)) {
						fprintf(stderr, "Path too long, skipped: %s%s%s\n", argv[i], separator, ent->d_name);
						continue;						// a truncated path would replay the WRONG file, or none
					}
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
	#endif
	}
	return 0;
}
#endif

#ifndef LIBFUZZ
#ifndef LIBFUZZ_STANDALONE

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <time.h>
#ifdef __linux__
#include <sched.h>
#endif
#endif

/*
	CPU time of the calling thread, counted only while it runs. Windows counts constant-rate cycles, elsewhere it
	is nanoseconds, so `CPU_TICKS_M_UNIT` names what a million ticks are.
*/
static uint64_t threadCpuTicks() {
#ifdef _WIN32
	ULONG64 cycles = 0;
	QueryThreadCycleTime(GetCurrentThread(), &cycles);
	return cycles;
#else
	timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
	return static_cast<uint64_t>(ts.tv_sec) * 1000000000u + static_cast<uint64_t>(ts.tv_nsec);
#endif
}
#ifdef _WIN32
static const char* const CPU_TICKS_M_UNIT = "Mcyc";
#else
static const char* const CPU_TICKS_M_UNIT = "ms";
#endif

static void pinToCore(int core) {	/// restrict the calling thread to logical processor `core`
	if (core < 0 || core >= 64) throw CmdException("--pin: core must be 0 to 63");
#if defined(_WIN32)
	if (SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(1) << core) == 0)
		throw CmdException("--pin: cannot pin to that core");
#elif defined(__linux__)
	cpu_set_t set;
	CPU_ZERO(&set);
	CPU_SET(core, &set);
	if (sched_setaffinity(0, sizeof (set), &set) != 0) throw CmdException("--pin: cannot pin to that core");
#else
	throw CmdException("--pin: this platform has no hard thread affinity");
#endif
}

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
		int pinCore = -1;		// --pin=N: run on logical processor N only (-1 = anywhere)
		bool noLibm = false;	// --no-libm: don't register the atan2/sqrt/log natives (for programs that define their own)
		const char* noNativeSpec = 0;	// --no-native=name,...: don't register the listed built-in natives (for programs defining same-named functions)
		const char* forwardSpec = 0;	// --forward=native:function,...: satisfy ^native calls with GAZL functions (see forwardNative)
		for (int i = 0; i < argc; ++i) {
			const char* a = argv[i];
			if (i > 0 && a[0] == '-' && a[1] == '-') {
				if (strncmp(a, "--bench", 7) == 0) {
					benchRepeat = (a[7] == '=') ? atoi(a + 8) : 10;
				} else if (strncmp(a, "--warmup", 8) == 0) {
					benchWarmup = (a[8] == '=') ? atoi(a + 9) : benchWarmup;
				} else if (strncmp(a, "--pin=", 6) == 0) {
					pinCore = atoi(a + 6);
				} else if (strcmp(a, "--no-libm") == 0) {
					noLibm = true;
				} else if (strncmp(a, "--forward=", 10) == 0) {
					forwardSpec = a + 10;
				} else if (strncmp(a, "--no-native=", 12) == 0) {
					noNativeSpec = a + 12;
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
			std::cerr << "        [--pin=N]                    run on logical processor N only (Windows, Linux)"
					<< std::endl;
			std::cerr << "        [--no-libm]                  skip the atan2/sqrt/log natives (for self-contained libm)"
					<< std::endl;
			std::cerr << "        [--forward=nat:func,...]     satisfy ^nat native calls with GAZL functions"
					<< std::endl;
			return 0;
		}
		if (pinCore >= 0) pinToCore(pinCore);

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

		ProgramSizes sizes = { 0, 0, 0, 0 };

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

				assem.finalize(sizes);

				std::cerr << "Code size: " << sizes.codeSize << ", globals size: " << sizes.globalsSize << ", consts size: "
						<< sizes.constsSize << ", functions: " << sizes.functionCount << std::endl;
				std::cerr << "--------------------------------------------------------------------------------"
						<< std::endl;
			}
			
			gazlStream.close();
		}
		
		{
			Processor pmachine(sizes.codeSize, code, sizes.functionCount, functionTable, DATA_MEMORY_SIZE, memory
					, sizes.globalsSize, sizes.constsSize, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0);

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
				Status status = pmachine.enterCall(mainFunction);
				if (status != OK) throw CmdException(std::string("enterCall returned status ") + std::to_string(status));
				do {
					pmachine.resetTimeOut(0x7FFFFFFF);
					status = pmachine.run();
				} while (status == TIME_OUT);
				// TERMINATED is a clean, expected stop via the exit() native (e.g. a firmware harness reaching its budget).
				if (status != OK && status != TERMINATED) throw CmdException(std::string("run returned status ") + std::to_string(status));
			};

			if (benchRepeat > 0) {
				std::vector<double> samples;			// milliseconds, measured iterations only
				std::vector<double> cpuSamples;			// millions of `threadCpuTicks`, measured iterations only
				for (int iter = 0; iter < benchWarmup + benchRepeat; ++iter) {
					const uint64_t c0 = threadCpuTicks();
					auto t0 = std::chrono::steady_clock::now();
					runToCompletion();
					auto t1 = std::chrono::steady_clock::now();
					const uint64_t c1 = threadCpuTicks();
					if (iter >= benchWarmup) {
						samples.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
						cpuSamples.push_back((c1 - c0) / 1e6);
					}
				}
				std::sort(samples.begin(), samples.end());
				std::sort(cpuSamples.begin(), cpuSamples.end());
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
						<< "\tstddev_ms=" << stddev
						<< "\tcpu_min_" << CPU_TICKS_M_UNIT << "=" << cpuSamples.front()
						<< "\tcpu_median_" << CPU_TICKS_M_UNIT << "=" << cpuSamples[cpuSamples.size() / 2] << std::endl;
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
