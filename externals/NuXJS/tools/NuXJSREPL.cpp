/**
	NuXJS is released under the BSD 2-Clause License.

	Copyright (c) 2018-2025, Magnus Lidström

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
**/

#ifdef _MSC_VER
#pragma float_control(precise, on, push)  
#endif

#include <stdint.h>
#include "../src/NuXJS.h"
#include <fstream>
#include <memory>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <ctime>

using namespace NuXJS;

static int recursiveStackCheck(const Code& code, Vector<Int32>& stackDepths
		, Int32 offset, Int32 currentStackDepth, Int32& maxStackDepth) {
	const CodeWord* codeWords = code.getCodeWords();
	const UInt32 codeSize = code.getCodeSize();
	
	int errors = 0;
	for (; offset < static_cast<Int32>(codeSize); ++offset) {
		if (stackDepths[offset] != DEAD_CODE_STACK_DEPTH) {
			if (stackDepths[offset] != currentStackDepth) {
				std::cerr << "Conflicting stack depth @" << offset << ": " << currentStackDepth << " & " << stackDepths[offset] << std::endl;
				++errors;
			}
			return errors;
		}

		const std::pair<Processor::Opcode, Int32> pair = Processor::unpackInstruction(codeWords[offset]);
		const Processor::Opcode opcode = pair.first;
		const Int32 operand = pair.second;
		if (opcode < 0 || opcode >= Processor::OP_COUNT) {
			std::cerr << "Invalid opcode " << static_cast<int>(opcode) << "@" << offset << std::endl;
			++errors;
		}
		stackDepths[offset] = currentStackDepth;
		if (currentStackDepth > static_cast<Int32>(code.getMaxStackDepth())) {
			std::cerr << "Stack overflow (code max is " << code.getMaxStackDepth() << ", stack depth @" << offset << " is " << currentStackDepth << ")" << std::endl;
			++errors;
		}
		maxStackDepth = std::max(maxStackDepth, currentStackDepth);
		const Processor::OpcodeInfo& info = Processor::getOpcodeInfo(opcode);
		const Int32 thisStackUse = info.stackUse + (((info.flags & Processor::OpcodeInfo::POP_OPERAND) != 0) ? -operand : 0);
		currentStackDepth += thisStackUse;
		maxStackDepth = std::max(maxStackDepth, currentStackDepth);
		if (currentStackDepth < 0) {
			std::cerr << "Negative stack depth @" << offset << std::endl;
			++errors;
		}
		switch (opcode) {
			case Processor::JT_OP:
			case Processor::JF_OP:
			case Processor::JT_OR_POP_OP:
			case Processor::JF_OR_POP_OP:
			case Processor::JMP_OP:
			case Processor::JSR_OP:
			case Processor::TRY_OP:
			case Processor::NEXT_PROPERTY_OP: {
				const Int32 targetOffset = offset + 1 + operand;
				if (targetOffset < 0 || targetOffset >= static_cast<Int32>(codeSize)) {
					std::cerr << "Invalid target @" << offset << ": @" << targetOffset << std::endl;
					++errors;
					return errors;
				}
				const Int32 targetStackDepth = currentStackDepth + (((info.flags & Processor::OpcodeInfo::NO_POP_ON_BRANCH) != 0) ? 1 : 0)
						+ (((info.flags & Processor::OpcodeInfo::POP_ON_BRANCH) != 0) ? -1 : 0);
				errors += recursiveStackCheck(code, stackDepths, targetOffset, targetStackDepth, maxStackDepth);
				if (opcode == Processor::JMP_OP) {
					return errors;
				}
				break;
			}
			case Processor::RETURN_OP: // fix: check all returns from jsr end with the same stack pointer as entry (keep track with extra parameter that only updates with JSR)
			case Processor::THROW_OP: {
				return errors;
			}
	
			default: break;
		}
	}

	return errors;
}

#if (_MSC_VER)
#include <Windows.h>
#undef min
#undef max
double getCPUSecs() {
	::FILETIME creationTime;
	::FILETIME exitTime;
	::FILETIME kernelTime;
	::FILETIME userTime;
	BOOL success = ::GetProcessTimes(::GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime);
	assert(success);
	return ((static_cast<__int64>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime) / 10000000.0;
}
#else
#include <sys/time.h>
#include <sys/resource.h>
double getCPUSecs() {
	rusage rus;
	int res = getrusage(RUSAGE_SELF, &rus);
	(void)res;
	assert(res == 0);
	return rus.ru_utime.tv_sec + rus.ru_utime.tv_usec / 1000000.0;
}
#endif

bool interactive = true;

std::vector<std::string> ioLines;

void pushIOLines(char typeChar, const String& s) {
	const Char* p = s.begin();
	const Char* e = s.end();
	do {
		const Char* b = p;
		while (p != e && *p != '\n') {
			++p;
		}
		ioLines.push_back(typeChar + std::string(" ") + std::string(b, p));
		if (p != e) {
			++p;
		}
	} while (p != e);
}

void pushIOStop() {
	ioLines.push_back("-");
}

struct PrintFunction : public Function {
	virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
		const String* s = (argc >= 1 ? argv[0].toString(rt.getHeap()) : &EMPTY_STRING);
		std::wcout << s->toWideString().c_str() << std::endl;
		if (interactive) {
			pushIOLines('<', *s);
		}
		return Value::UNDEFINED;
	}
};

struct GCFunction : public Function {
	virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
	   Heap& heap = rt.getHeap();
	   const UInt32 preCount = heap.count();
	   const size_t preSize = heap.size();
	   heap.gc();
	   const UInt32 postCount = heap.count();
	   const size_t postSize = heap.size();
	   const size_t pooled = heap.pooled();
	   heap.drain();
	   JSObject* o = new(heap) JSObject(heap.managed(), rt.getObjectPrototype());
	   o->setOwnProperty(rt, String::allocate(heap, "preCount"), preCount);
	   o->setOwnProperty(rt, String::allocate(heap, "preSize"), static_cast<double>(preSize));
	   o->setOwnProperty(rt, String::allocate(heap, "postCount"), postCount);
	   o->setOwnProperty(rt, String::allocate(heap, "postSize"), static_cast<double>(postSize));
	   o->setOwnProperty(rt, String::allocate(heap, "pooled"), static_cast<double>(pooled));
	   return o;
	}
};

static void disassemble(Heap& heap, const Code& code) {
	const CodeWord* codeWords = code.getCodeWords();
	const Value* constants = code.getConstants()->begin();
	const UInt32 codeSize = code.getCodeSize();
	
	Vector<Int32> stackDepths(codeSize, &heap);
	std::fill(stackDepths.begin(), stackDepths.end(), DEAD_CODE_STACK_DEPTH);
	
	Int32 maxStackDepth = 0;
	int errors = recursiveStackCheck(code, stackDepths, 0, 1, maxStackDepth);
	if (maxStackDepth != code.getMaxStackDepth()) {
		std::cerr << "Notice: code definition max stack depth is " << code.getMaxStackDepth() << " but max stack depth actually used is " << maxStackDepth << std::endl;
	}
	
	for (UInt32 offset = 0; offset < codeSize; ++offset) {
		const std::pair<Processor::Opcode, Int32> pair = Processor::unpackInstruction(codeWords[offset]);
		const Processor::Opcode opcode = pair.first;
		const Int32 operand = pair.second;
		if (opcode < 0 || opcode >= Processor::OP_COUNT) {
			break;
		}

		if (stackDepths[offset] == DEAD_CODE_STACK_DEPTH) {
			std::cerr << "?";
		} else {
			std::cerr << stackDepths[offset];
		}
		std::cerr << "\t";
		std::cerr << "@" << offset << ":\t" << Processor::getOpcodeInfo(opcode).mnemonic;
		
		switch (opcode) {
			case Processor::JT_OP:
			case Processor::JF_OP:
			case Processor::JT_OR_POP_OP:
			case Processor::JF_OR_POP_OP:
			case Processor::JMP_OP:
			case Processor::JSR_OP:
			case Processor::TRY_OP:
			case Processor::NEXT_PROPERTY_OP: {
				const Int32 target = offset + 1 + operand;
				std::cerr << " @" << target;
				break;
			}
			case Processor::READ_LOCAL_OP:
			case Processor::WRITE_LOCAL_OP:
			case Processor::WRITE_LOCAL_POP_OP: {
				const Int32 index = operand;
				const String* name = code.getLocalName(index);
				if (name != 0) std::wcerr << L" $" << name->toWideString();
				std::wcerr << L" (" << index << L")";
				break;
			}
			case Processor::CALL_OP:
			case Processor::CALL_METHOD_OP:
			case Processor::CALL_EVAL_OP:
			case Processor::NEW_OP:
			case Processor::POP_OP:
			case Processor::PUSH_BACK_OP:
			case Processor::PUSH_ELEMENTS_OP: std::cerr << " *" << operand; break;
			case Processor::REPUSH_OP: std::cerr << " " << operand; break;
			case Processor::CONST_OP:
			case Processor::GEN_FUNC_OP:
			case Processor::CATCH_SCOPE_OP: std::wcerr << L" #" << constants[operand].toString(heap)->toWideString(); break;
			case Processor::DECLARE_OP:
			case Processor::READ_NAMED_OP:
			case Processor::WRITE_NAMED_OP:
			case Processor::WRITE_NAMED_POP_OP:
			case Processor::ADD_PROPERTY_OP:
			case Processor::DELETE_NAMED_OP:
			case Processor::TYPEOF_NAMED_OP: std::wcerr << L" " << constants[operand].toString(heap)->toWideString(); break;
			default: break;
		}
		std::cerr << std::endl;
	}
	
	std::cerr << "\tMax stack depth: " << code.getMaxStackDepth() << std::endl;
	std::cerr << "\tCode: " << code.getCodeSize() << std::endl;
	std::cerr << "\tVars: " << code.getVarsCount() << std::endl;
	std::cerr << "\tArguments: " << code.getArgumentsCount() << std::endl;

	(void)errors;
	assert(errors == 0);
}

Value disassemble(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
	Heap& heap = rt.getHeap();
	Function* f = (argc >= 1 ? argv[0].asFunction() : 0);
	if (f == 0) { // FIX : sub to make is not a function error or something?
		ScriptException::throwError(heap, TYPE_ERROR, String::concatenate(heap, *argv[0].toString(heap), String(" is not a function")));
	}
	const Code* code = f->getScriptCode();
	if (code == 0) {
		ScriptException::throwError(heap, TYPE_ERROR, "Cannot disassemble native code");
	}
	disassemble(heap, *code);
	return Value::UNDEFINED;
}

bool pauseBeforeQuit = false;

static UInt32 calcUTF8ToUTF16Size(UInt32 utf8Size, const char* utf8Chars) {
	UInt32 n = 0;
	for (UInt32 i = 0; i < utf8Size; ++i) {
		Byte c = static_cast<Byte>(utf8Chars[i]);
		if ((c & 0x80) == 0) {
			++n;
		} else if ((c & 0xE0) == 0xC0) {
			assert(i + 1 < utf8Size && (utf8Chars[i + 1] & 0xC0) == 0x80);
			++n;
			++i;
		} else if ((c & 0xF0) == 0xE0) {
			assert(i + 2 < utf8Size && (utf8Chars[i + 1] & 0xC0) == 0x80 && (utf8Chars[i + 2] & 0xC0) == 0x80);
			++n;
			i += 2;
		} else if ((c & 0xF8) == 0xF0) {
			assert(i + 3 < utf8Size && (utf8Chars[i + 1] & 0xC0) == 0x80 && (utf8Chars[i + 2] & 0xC0) == 0x80 && (utf8Chars[i + 3] & 0xC0) == 0x80);
			n += 2;
			i += 3;
		} else {
			assert(false);
		}
	}
	return n;
}

static UInt32 convertUTF8ToUTF16(UInt32 utf8Size, const char* utf8Chars, Char* utf16Chars) {
	UInt32 outIndex = 0;
	for (UInt32 i = 0; i < utf8Size;) {
		Byte c = static_cast<Byte>(utf8Chars[i]);
		if ((c & 0x80) == 0) {
			utf16Chars[outIndex++] = static_cast<Char>(c);
			++i;
		} else if ((c & 0xE0) == 0xC0) {
			assert(i + 1 < utf8Size);
			Char cc = static_cast<Char>(((c & 0x1F) << 6) | (static_cast<Byte>(utf8Chars[i + 1]) & 0x3F));
			utf16Chars[outIndex++] = cc;
			i += 2;
		} else if ((c & 0xF0) == 0xE0) {
			assert(i + 2 < utf8Size);
			Char cc = static_cast<Char>(((c & 0x0F) << 12)
					| ((static_cast<Byte>(utf8Chars[i + 1]) & 0x3F) << 6)
					| (static_cast<Byte>(utf8Chars[i + 2]) & 0x3F));
			utf16Chars[outIndex++] = cc;
			i += 3;
		} else if ((c & 0xF8) == 0xF0) {
			assert(i + 3 < utf8Size);
			UInt32 c32 = ((c & 0x07) << 18)
					| ((static_cast<Byte>(utf8Chars[i + 1]) & 0x3F) << 12)
					| ((static_cast<Byte>(utf8Chars[i + 2]) & 0x3F) << 6)
					| (static_cast<Byte>(utf8Chars[i + 3]) & 0x3F);
			utf16Chars[outIndex++] = static_cast<Char>(((c32 - 0x10000) >> 10) + 0xD800);
			utf16Chars[outIndex++] = static_cast<Char>(((c32 - 0x10000) & 0x3FF) + 0xDC00);
			i += 4;
		} else {
			assert(false);
		}
	}
	return outIndex;
}

Var read(Runtime& rt, const Var& thisVar, const VarList& args) {
	std::ifstream file;
	const String* contentsString = 0;
	try {
		const String* filenameString = args[0];
		const std::string filename = filenameString->toUTF8String();
		file.open(filename.c_str(), std::ios::binary);
		if (!file.good()) {
			ScriptException::throwError(rt.getHeap(), GENERIC_ERROR, "Could not open input file");
		}
		file.exceptions(std::ios_base::badbit | std::ios_base::failbit);
		std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		Heap& heap = rt.getHeap();
		UInt32 utf16Size = calcUTF8ToUTF16Size(static_cast<UInt32>(contents.size()), contents.data());
		std::vector<Char> buffer(utf16Size);
		convertUTF8ToUTF16(static_cast<UInt32>(contents.size()), contents.data(), buffer.data());
		contentsString = new(heap) String(heap.managed(), buffer.data(), buffer.data() + utf16Size);
	}
	catch (const std::ios_base::failure& x) {
		ScriptException::throwError(rt.getHeap(), GENERIC_ERROR, x.what());
	}
	return Var(rt, Value(contentsString));
}

Var load(Runtime& rt, const Var& thisVar, const VarList& args) {
	Var source = read(rt, thisVar, args);
	rt.run(source);
	return Var(rt);
}

bool doQuit = false;

Var quit(Runtime& rt, const Var& thisVar, const VarList& args) {
        doQuit = true;
        return Var(rt);
}

Var help(Runtime& rt, const Var& thisVar, const VarList& args) {
        (void)thisVar;
        (void)args;
        std::wcout << L"Available REPL helpers:" << std::endl
                        << L"  read(file)   - return UTF-8 file as string" << std::endl
                        << L"  load(file)   - execute a UTF-8 JavaScript file" << std::endl
                        << L"  quit()       - exit the REPL" << std::endl
                        << L"  gc()         - run garbage collection" << std::endl
                        << L"  dasm(func)   - disassemble a compiled function" << std::endl
                        << L"Special commands:" << std::endl
                        << L"  #save [name] - save the current session" << std::endl
                        << L"                 (no name uses a timestamp in tests/)" << std::endl
                        << L"  #purge       - clear the session log" << std::endl
                        << L"  ?expr        - shortcut for print(expr)" << std::endl;
        return Var(rt);
}

struct MyHeap : public Heap {
	MyHeap() : peakSize(0) { }
	virtual void* acquireMemory(size_t size) {
		void* p = Heap::acquireMemory(size);
		if (allocatedSize > peakSize) {
			peakSize = allocatedSize;
		}
		return p;
	}
	size_t peakSize;
};

#if (_MSC_VER)
#include <Windows.h>
#include <time.h>
#elif defined(__APPLE__)
#include <mach/mach_time.h>
#include <libkern/OSAtomic.h>
#else
#include <time.h>
#include <sys/time.h>
#endif

void randomSeed() {
	unsigned int seed;
#if (_MSC_VER)
	::LARGE_INTEGER count;
	::BOOL success = ::QueryPerformanceCounter(&count);
	if (!success) {
		count.LowPart = 0;
		count.HighPart = 0;
	}
	seed = (static_cast<unsigned int>(time(0)) ^ count.LowPart)
			+ (static_cast<unsigned int>(::GetTickCount()) ^ count.HighPart);
#elif defined(__APPLE__)
	const uint64_t t = ::mach_absolute_time();
	seed = (static_cast<unsigned int>(time(0)) ^ static_cast<unsigned int>(t & 0xFFFFFFFFU))
			+ (static_cast<unsigned int>(clock()) ^ static_cast<unsigned int>((t >> 32) & 0xFFFFFFFFU));
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	const uint64_t t = static_cast<uint64_t>(ts.tv_sec) ^ static_cast<uint64_t>(ts.tv_nsec);
	seed = (static_cast<unsigned int>(time(0)) ^ static_cast<unsigned int>(t & 0xFFFFFFFFU))
			+ (static_cast<unsigned int>(clock()) ^ static_cast<unsigned int>((t >> 32) & 0xFFFFFFFFU));
#endif
	srand(seed);
}

void printUsage() {
	std::cout << "Usage: NuXJS [options] [script.js [arguments...]]" << std::endl
			<< "Options:" << std::endl
			<< "  -s: suppress result output" << std::endl
			<< "  -t: print timing and memory stats" << std::endl
			<< "  -p: pause before quitting" << std::endl
			<< "  -n: do not load the standard library" << std::endl
			<< "  -E, --legacy-exceptions: use legacy exception output" << std::endl
			<< "  -h, --help: show this usage" << std::endl
			<< std::endl
			<< "NuXJS options must appear before the script file name." << std::endl
			<< "JS receives global `arguments`: [script.js, arguments...]." << std::endl;
}

int testMain(int argc, const char* argv[]) {
	try {
		String source(EMPTY_STRING);
		std::string inputFilePath;
		std::vector<std::string> scriptArguments;
		std::istream* inStream = &std::cin;
		bool doTime = false;
		size_t peakMemory = 0;
		bool doSuppressStdErr = false;
		bool loadStdLib = true;
		bool useLegacyExceptionOutput = false;
		for (int argi = 1; argi < argc; ++argi) {
			if (!inputFilePath.empty()) {
				scriptArguments.push_back(argv[argi]);
			} else if (strcmp(argv[argi], "-t") == 0) doTime = true;
			else if (strcmp(argv[argi], "-s") == 0) doSuppressStdErr = true;
			else if (strcmp(argv[argi], "-p") == 0) pauseBeforeQuit = true;
			else if (strcmp(argv[argi], "-n") == 0) loadStdLib = false;
			else if (strcmp(argv[argi], "--legacy-exceptions") == 0 || strcmp(argv[argi], "-E") == 0) useLegacyExceptionOutput = true;
			else if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
				printUsage();
				return 0;
			}
			else {
				inputFilePath = argv[argi];
				interactive = false;
			}
		}

		if (inputFilePath.empty() && !inStream->good()) {
			std::cerr << "Could not open input stream" << std::endl;
			return 1;
		}

		const String LF_STRING("\n");
		const String PRINT_STRING("print");

		MyHeap heap;
		Runtime rt(heap);
		if (!inputFilePath.empty()) {
			std::ifstream file(inputFilePath.c_str(), std::ios::binary);
			if (!file.good()) {
				std::cerr << "Could not open input stream" << std::endl;
				return 1;
			}
			std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			UInt32 utf16Size = calcUTF8ToUTF16Size(static_cast<UInt32>(contents.size()), contents.data());
			std::vector<Char> buffer(utf16Size);
			convertUTF8ToUTF16(static_cast<UInt32>(contents.size()), contents.data(), buffer.data());
			source = String(heap.roots(), buffer.data(), buffer.data() + utf16Size);
		} else {
			if (!inStream->good()) {
				std::cerr << "Could not open input stream" << std::endl;
				return 1;
			}
		}

		Object& globals = *rt.getGlobalObject();
		Var globs = rt.getGlobalsVar();
		globs["read"] = read;
		globs["load"] = load;
		globs["quit"] = quit;
		globs["help"] = help;
		{
			Var argumentsVar = inputFilePath.empty()
					? rt.newArrayVar()
					: rt.newArrayVar(static_cast<UInt32>(scriptArguments.size()) + 1);
			if (!inputFilePath.empty()) {
				argumentsVar[0] = inputFilePath;
				for (UInt32 i = 0; i < static_cast<UInt32>(scriptArguments.size()); ++i) {
					argumentsVar[i + 1] = scriptArguments[i];
				}
			}
			globs["arguments"] = argumentsVar;
		}

		PrintFunction printFunction;
		globals.setOwnProperty(rt, &PRINT_STRING, &printFunction, DONT_ENUM_FLAG);
		GCFunction gcFunction;
		const String GC_STRING("gc");
		globals.setOwnProperty(rt, &GC_STRING, &gcFunction, DONT_ENUM_FLAG);
		globals.setOwnProperty(rt, String::allocate(heap, "dasm"), new(heap) FunctorAdapter<NativeFunction>(heap.managed(), disassemble), DONT_ENUM_FLAG);

		randomSeed();

		if (loadStdLib) {
			try {
				rt.setupStandardLibrary();
			}
			catch (const std::exception& x) {
				std::cerr << "exception setting up standard lib: " << x.what() << std::endl;
				return 1;
			}
			catch (...) {
				std::cerr << "exception setting up standard lib" << std::endl;
				return 1;
			}
		}


		Processor processor(rt);
		processor.run(STANDARD_CYCLES_BETWEEN_AUTO_GC); // just testing some weird behaviour

		inStream->exceptions(std::ios_base::badbit);
		while ((interactive ? inStream->good() : true) && !doQuit) {
			if (interactive) {
				assert(!inStream->fail());
			}
			bool execute = false;
			try {
				if (interactive) {
					std::string utf8Line;
					std::getline(*inStream, utf8Line);
					if (!inStream->good() && !inStream->eof()) throw std::runtime_error("Input error");
					// FIX : add #undo that drops just the last one
					if (utf8Line == "#save" || utf8Line.compare(0, 6, "#save ") == 0) {
						std::wstring s;
						UInt32 size16 = calcUTF8ToUTF16Size(static_cast<UInt32>(utf8Line.size()), utf8Line.data());
						std::vector<Char> buf(size16);
						convertUTF8ToUTF16(static_cast<UInt32>(utf8Line.size()), utf8Line.data(), buf.data());
						s.assign(buf.begin(), buf.end());
						const time_t t = time(0);
						char fn[257];
						if (s.size() >= 6) {
							std::wstring ws = L"tests/";
							ws += s.substr(6);
							ws += L".io";
							strncpy(fn, std::string(ws.begin(), ws.end()).c_str(), 256);
							fn[256] = 0;
						} else {
							strftime(fn, 256, "tests/%Y%m%d_%H%M%S.io", localtime(&t));
						}
						std::ofstream saveStream(fn);
						for (std::vector<std::string>::const_iterator it = ioLines.begin(); it != ioLines.end(); ++it) {
							saveStream << *it << std::endl;
						}
						saveStream.close();
						ioLines.clear();
						std::cout << "saved to " << fn << std::endl;
					} else if (utf8Line == "#purge") {
						ioLines.clear();
						std::cout << "purged" << std::endl;
					} else if (!utf8Line.empty()) {
						UInt32 size16 = calcUTF8ToUTF16Size(static_cast<UInt32>(utf8Line.size()), utf8Line.data());
						std::vector<Char> buf(size16);
						convertUTF8ToUTF16(static_cast<UInt32>(utf8Line.size()), utf8Line.data(), buf.data());
						String line(heap.roots(), buf.data(), buf.data() + size16);
						if (!source.empty()) source = String(heap.roots(), source, LF_STRING);
						source = String(heap.roots(), source, line);
					} else {
						if (source.size() > 0 && source[0] == '?') {
							source = String(heap.roots(), String(heap.roots(), String("print("), String(heap.roots(), source.begin() + 1, source.end())), String(")")); // FIX : operator +
						}
						if (interactive) {
							pushIOLines('>', source);
						}
						execute = true;
					}
				} else {
					if (inputFilePath.empty()) {
						std::string contents((std::istreambuf_iterator<char>(*inStream)), std::istreambuf_iterator<char>());
						UInt32 size16 = calcUTF8ToUTF16Size(static_cast<UInt32>(contents.size()), contents.data());
						std::vector<Char> buf(size16);
						convertUTF8ToUTF16(static_cast<UInt32>(contents.size()), contents.data(), buf.data());
						source = String(heap.roots(), buf.data(), buf.data() + size16);
					}
					execute = true;
					doQuit = true; // FIX : we shouldn't use the same loop for interactive and non-interactive
				}
				
				if (execute) {
					const String* scriptFileName = 0;
					if (!inputFilePath.empty()) {
						scriptFileName = String::allocate(heap, inputFilePath.c_str());
					} else {
						scriptFileName = rt.newStringConstant("<anonymous>");
					}
					Heap& runtimeHeap = rt.getHeap();
					const Char* scriptBegin = source.begin();
					const Char* scriptEnd = source.end();
					const String* scriptSource = new(runtimeHeap) String(runtimeHeap.managed(), scriptBegin, scriptEnd);
					SourceCodeUnit* sourceCodeUnit = new(runtimeHeap) SourceCodeUnit(heap.managed(), scriptSource, scriptFileName);
					Code globalCode(heap.roots(), 0, sourceCodeUnit);
					Compiler compiler(heap.roots(), &globalCode, (interactive ? Compiler::FOR_EVAL : Compiler::FOR_GLOBAL), 1);
					try {
						compiler.compile(*scriptSource);
					}
					catch (const Exception&) {
						UInt32 offset;
						UInt32 lineNumber;
						UInt32 columnNumber;
						compiler.getStopPosition(offset, lineNumber, columnNumber);
						std::wstring ws;
						{
							std::wstringstream ss;
							ss << L"!!!! Line: " << lineNumber;
							ws = ss.str();
						}
						std::wcout << ws << std::endl;
						if (interactive) {
							pushIOLines('!', String(heap.roots(), ws.c_str()));
						}
						throw;
					}
					processor.enterGlobalCode(&globalCode);

					bool done = false;
					rt.resetTimeOut(60);
					const double start = getCPUSecs();
					do {
						done = !processor.run(STANDARD_CYCLES_BETWEEN_AUTO_GC);
						rt.autoGC(true);
						rt.checkTimeOut();
						peakMemory = std::max<size_t>(peakMemory, heap.size());
					} while (!done);
					
					if (!doSuppressStdErr) {
						Value v = processor.getResult();
						std::wcerr << L"\t=" << v.toString(heap)->toWideString() << std::endl;
					}
					if (doTime) {
						const double end = getCPUSecs();
						std::cerr << (end - start) << "s" << std::endl;
						std::cerr << heap.size() / (1024.0 * 1024.0) << "MiB" << std::endl;
						std::cerr << peakMemory / (1024.0 * 1024.0) << "MiB" << std::endl;
						std::cerr << heap.peakSize / (1024.0 * 1024.0) << "MiB" << std::endl;
					}

					source = EMPTY_STRING;
				}
			}
			catch (const ScriptException& x) {
				source = EMPTY_STRING;
				std::wstring ws = x.value.toString(heap)->toWideString();
				ws = L"!!!! " + ws;
				std::wcout << ws << std::endl;
				std::string locationLine;
				std::string stackLine;
				if (!useLegacyExceptionOutput) {
					const char* stackTrace = x.getStackTrace();
					if (stackTrace[0] != '\0') {
						stackLine = std::string("!!!! stack: ") + stackTrace;
						const std::wstring stackWide(stackLine.begin(), stackLine.end());
						std::wcout << stackWide << std::endl;
					}
				}
				if (!interactive) {
					return 1;
				} else {
					pushIOLines('!', String(heap.roots(), ws.c_str()));
					if (!useLegacyExceptionOutput) {
						if (!locationLine.empty()) {
							pushIOLines('!', String(heap.roots(), locationLine.c_str()));
						}
						if (!stackLine.empty()) {
							pushIOLines('!', String(heap.roots(), stackLine.c_str()));
						}
					}
				}
			}
			catch (const std::exception& x) {
				source = EMPTY_STRING;
				std::cout << "!!!! " << x.what() << std::endl;
				if (!interactive) {
					return 1;
				} else {
					pushIOLines('!', x.what());
				}
			}
			catch (...) {
				source = EMPTY_STRING;
				std::cout << "Unknown exception" << std::endl;
				if (!interactive) {
					return 1;
				} else {
					pushIOLines('!', "Unknown exception");
				}
			}
			if (execute && interactive) {
				pushIOStop();
			}
		}
		if (interactive && !doQuit && !inStream->eof()) {
			std::cout << "input stream failure" << std::endl;
			return 1;
		}
		return 0;
	}
	catch (const Exception& x) {
		std::cerr << "Uncaught exception: " << x.what() << std::endl;
	}
	catch (const std::exception& x) {
		std::cerr << "Uncaught std::exception: " << x.what() << std::endl;
	}
	catch (...) {
		std::cerr << "Uncaught unknown exception" << std::endl;
	}
	return 1;
}

#ifdef LIBFUZZ
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
	Heap heap;
	Runtime rt(heap);
	rt.resetTimeOut(2);
	rt.setMemoryCap(64*1024*1024);
	try {
	    rt.run(String(reinterpret_cast<const Char*>(Data), reinterpret_cast<const Char*>(Data) + Size / 2));
	}
	catch (Exception&) {
		;
	}
	return 0;  // Non-zero return values are reserved for future use.
}
#endif

#ifndef LIBFUZZ
int main(int argc, const char* argv[]) {
	int rc = testMain(argc, argv);
	if (pauseBeforeQuit) std::wcin.get();
	return rc;
}
#endif

#ifdef _MSC_VER
#pragma float_control(pop)  
#endif
