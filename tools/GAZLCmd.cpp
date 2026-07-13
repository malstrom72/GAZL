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
#ifdef GAZL_JIT
	#include "../src/GAZLJit.h"		// JitEngine + compile() — arm64 only; enabled by the build on AArch64 hosts
#endif

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


const int DATA_MEMORY_SIZE = 128 * 1024;
const int CODE_MEMORY_SIZE = 128 * 1024;
const int FUNCTION_TABLE_SIZE = CODE_MEMORY_SIZE;	// A function is at least one instruction, so this can never overflow.
const int CALL_STACK_SIZE = 2048;

static const NativeFunc NATIVE_TABLE[] = {
	abort, assertFail, printInt, printFloat, print, printLF, input, gazlAtan2, gazlSqrt, gazlLog
};

static const char* NATIVE_NAMES[] = {
	"abort", "assertFail", "printInt", "printFloat", "print", "printLF", "input", "atan2", "sqrt", "log"
};

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
		
		UInt codeSize;
		UInt globalsSize;
		UInt constsSize;
		UInt functionCount = 0;

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
				assem.finalize(codeSize, globalsSize, constsSize, functionCount);
			}
		}

		{
			Processor pmachine(codeSize, code, functionCount, functionTable, DATA_MEMORY_SIZE, memory, globalsSize
					, constsSize, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0);
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

#include <dirent.h>

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
	for (int i = 1; i < argc; ++i) {
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
		for (int i = 0; i < argc; ++i) {
			const char* a = argv[i];
			if (i > 0 && a[0] == '-' && a[1] == '-') {
				if (strncmp(a, "--bench", 7) == 0) {
					benchRepeat = (a[7] == '=') ? atoi(a + 8) : 10;
				} else if (strncmp(a, "--warmup", 8) == 0) {
					benchWarmup = (a[8] == '=') ? atoi(a + 9) : benchWarmup;
				} else if (strcmp(a, "--jit") == 0) {
					useJit = true;
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
			return 0;
		}

		Symbols globals;

		for (int i = 0; i < sizeof (NATIVE_TABLE) / sizeof (*NATIVE_TABLE); ++i)
			globals.registerNative(NATIVE_NAMES[i], i);

		for (size_t i = 3; i + 2 <= pos.size(); i += 2) {
			Value v;
			v.i = atoi(pos[i + 1]);
			globals.defineConstant(pos[i + 0], false, v);
		}
		
		UInt codeSize;
		UInt globalsSize;
		UInt constsSize;
		UInt functionCount = 0;

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

				assem.finalize(codeSize, globalsSize, constsSize, functionCount);

				std::cerr << "Code size: " << codeSize << ", globals size: " << globalsSize << ", consts size: "
						<< constsSize << ", functions: " << functionCount << std::endl;
				std::cerr << "--------------------------------------------------------------------------------"
						<< std::endl;
			}
			
			gazlStream.close();
		}
		
		{
			// Pick the engine: the native JIT (--jit, arm64) if it can compile the whole program, else the interpreter.
			// Both are Processor subclasses, so the run loop below is identical (§5.1).
			std::unique_ptr<Processor> proc;
		#ifdef GAZL_JIT
			if (useJit) {
				std::unique_ptr<JitEngine> eng(new JitEngine(codeSize, code, functionCount, functionTable
						, DATA_MEMORY_SIZE, memory, globalsSize, constsSize, CALL_STACK_SIZE, callStack, NATIVE_TABLE));
				if (compile(*eng, code, functionTable, functionCount)) {
					std::cerr << "JIT: compiled " << functionCount << " function(s) to native arm64." << std::endl;
					proc = std::move(eng);
				} else {
					std::cerr << "JIT: a function used an opcode the backend can't lower; using the interpreter."
							<< std::endl;
				}
			}
		#else
			if (useJit) {
				std::cerr << "JIT: this build has no JIT support (needs an AArch64 GAZL_JIT build); using the interpreter."
						<< std::endl;
			}
		#endif
			if (!proc) {
				proc.reset(new Processor(codeSize, code, functionCount, functionTable, DATA_MEMORY_SIZE, memory
						, globalsSize, constsSize, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0));
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
				if (status != OK) throw CmdException(std::string("run returned status ") + std::to_string(status));
			};

			if (benchRepeat > 0) {
				std::vector<double> samples;			// milliseconds, measured iterations only
				for (int iter = 0; iter < benchWarmup + benchRepeat; ++iter) {
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
