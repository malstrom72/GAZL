/*
	GAZL is released under the BSD 2-Clause License.
	
	Copyright (c) 2010-2025, Magnus Lidström
	
	Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
	following conditions are met:
	
	Redistributions of source code must retain the above copyright notice, this list of conditions and the following
	disclaimer.
	
	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
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
#include <fstream>
#include <cmath>
#include <cstdlib>
#include <string>
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


const int DATA_MEMORY_SIZE = 128 * 1024;
const int CODE_MEMORY_SIZE = 128 * 1024;
const int CALL_STACK_SIZE = 2048;

static const NativeFunc NATIVE_TABLE[] = {
	abort, assertFail, printInt, printFloat, print, printLF, input, gazlAtan2, gazlSqrt, gazlLog
};

static const char* NATIVE_NAMES[] = {
	"abort", "assertFail", "printInt", "printFloat", "print", "printLF", "input", "atan2", "sqrt", "log"
};

static Value memory[DATA_MEMORY_SIZE];
static Instruction code[CODE_MEMORY_SIZE];
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
			
		{
			std::istringstream gazlStream(std::string(reinterpret_cast<const char*>(Data), reinterpret_cast<const char*>(Data) + Size));			
			{
				Assembler assem(CODE_MEMORY_SIZE, code, DATA_MEMORY_SIZE, memory, globals);
				assem.newUnit("string");
				while (!gazlStream.eof()) {
					std::string line;
					getline(gazlStream, line);
					assem.feed(line.c_str());
				}
				assem.finalize(codeSize, globalsSize, constsSize);
			}
		}
		
		{
			Processor pmachine(codeSize, code, DATA_MEMORY_SIZE, memory, globalsSize, constsSize, CALL_STACK_SIZE
					, callStack, NATIVE_TABLE, 0);
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

		if (argc < 2) {
			std::cerr << "GAZLCmd <filename> [<function> = 'main'] [<define symbol> <define value> ...]" << std::endl;
			return 0;
		}

		Symbols globals;

		for (int i = 0; i < sizeof (NATIVE_TABLE) / sizeof (*NATIVE_TABLE); ++i)
			globals.registerNative(NATIVE_NAMES[i], i);

		for (int i = 3; i + 2 <= argc; i += 2) {
			Value v;
			v.i = atoi(argv[i + 1]);
			globals.defineConstant(argv[i + 0], false, v);
		}
		
		UInt codeSize;
		UInt globalsSize;
		UInt constsSize;
			
		{
			std::ifstream gazlStream(argv[1], std::ifstream::binary);
			if (!gazlStream.good()) throw CmdException("Could not open input file");
			gazlStream.exceptions(std::ios_base::badbit);
			
			{
				Assembler assem(CODE_MEMORY_SIZE, code, DATA_MEMORY_SIZE, memory, globals);
				assem.newUnit(argv[1]);
				
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

				assem.finalize(codeSize, globalsSize, constsSize);
				
				std::cerr << "Code size: " << codeSize << ", globals size: " << globalsSize << ", consts size: "
						<< constsSize << std::endl;
				std::cerr << "--------------------------------------------------------------------------------"
						<< std::endl;
			}
			
			gazlStream.close();
		}
		
		{
			clock_t c0 = clock();
			Processor pmachine(codeSize, code, DATA_MEMORY_SIZE, memory, globalsSize, constsSize, CALL_STACK_SIZE
					, callStack, NATIVE_TABLE, 0);
			const char* mainFunctionName = argc >= 3 ? argv[2] : "main";
			Pointer mainFunction = globals.findFunction(mainFunctionName);
			if (mainFunction == 0) throw CmdException(std::string("Could not locate function: ") + mainFunctionName);
			Status status = pmachine.enterCall(mainFunction);
			assert(status == OK);
			status = pmachine.run();
			clock_t c1 = clock();
			
			std::cerr << "--------------------------------------------------------------------------------"
					<< std::endl;
			std::cerr << "Status: " << status << ", time: " << static_cast<double>(c1 - c0) / CLOCKS_PER_SEC
					<< "s" << std::endl;
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
