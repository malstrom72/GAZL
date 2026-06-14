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

/*
	NuXJS command-line tool: an interactive REPL and a script runner around the embeddable engine.

	Output stream contract (relied upon by the golden-file test suite in tools/test.pika, which compares
	stdout only and runs the binary with `-s --legacy-exceptions`):

	  * stdout  - program-visible output: anything printed by the script via print(), and the `!!!!`
	              lines reporting compile/runtime errors (and their stack traces). The test runner captures
	              and compares this stream.
	  * stderr  - REPL meta only: the interactive `\t=<result>` echo (shown only in interactive mode, and
	              suppressed there with -s) and the timing / memory figures (-t).

	Do not move script-visible output or `!!!!` reporting to stderr without regenerating the .io fixtures.
*/

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
#include <cstdlib>
#include <cstring>

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
			// NOTE: returns reached via JSR are not verified to restore the stack pointer they entered with;
			// doing so would require an extra JSR-tracking parameter threaded through this walk.
			case Processor::RETURN_OP:
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

// Lenient UTF-8 -> UTF-16 conversion. Malformed or truncated sequences are replaced with U+FFFD rather
// than aborting (this tool may be fed arbitrary bytes on stdin or from a file).
static std::vector<Char> utf8ToUtf16(const char* utf8, size_t size) {
	std::vector<Char> out;
	out.reserve(size);
	for (size_t i = 0; i < size;) {
		const Byte c = static_cast<Byte>(utf8[i]);
		UInt32 cp;
		size_t extra;
		if ((c & 0x80) == 0) { cp = c; extra = 0; }
		else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
		else { out.push_back(0xFFFD); ++i; continue; }
		if (i + extra >= size) { out.push_back(0xFFFD); ++i; continue; }
		bool ok = true;
		for (size_t k = 1; k <= extra; ++k) {
			const Byte cont = static_cast<Byte>(utf8[i + k]);
			if ((cont & 0xC0) != 0x80) { ok = false; break; }
			cp = (cp << 6) | (cont & 0x3F);
		}
		if (!ok) { out.push_back(0xFFFD); ++i; continue; }
		i += extra + 1;
		if (cp <= 0xFFFF) {
			out.push_back(static_cast<Char>(cp));
		} else {
			cp -= 0x10000;
			out.push_back(static_cast<Char>(0xD800 + (cp >> 10)));
			out.push_back(static_cast<Char>(0xDC00 + (cp & 0x3FF)));
		}
	}
	return out;
}

static const String* utf8ToString(Heap& heap, const char* utf8, size_t size) {
	const std::vector<Char> u = utf8ToUtf16(utf8, size);
	if (u.empty()) {
		return &EMPTY_STRING;
	}
	return new(heap) String(heap.managed(), u.data(), u.data() + u.size());
}

// ---- Session-shared state required by native callbacks registered on the globals ----

static bool doQuit = false;			// set by the quit() helper
static bool pauseBeforeQuit = false;	// -p; read by main()

// In-memory transcript of the interactive session, tagged for #save (see pushIOLines). It is recorded
// only while an interactive session is running; PrintFunction appends to it through a pointer that is null
// in script-file mode.
static std::vector<std::string> ioLines;

static void pushIOLines(char typeChar, const String& s) {
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

static void pushIOStop() {
	ioLines.push_back("-");
}

struct PrintFunction : public Function {
	std::vector<std::string>* capture;	// null => do not record (script-file mode)
	PrintFunction() : capture(0) { }
	virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
		const String* s = (argc >= 1 ? argv[0].toString(rt.getHeap()) : &EMPTY_STRING);
		std::wcout << s->toWideString().c_str() << std::endl;
		if (capture != 0) {
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

	if (errors != 0) {
		std::cerr << "Notice: disassembly stack-consistency check reported " << errors << " error(s)" << std::endl;
	}
}

Value disassemble(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
	Heap& heap = rt.getHeap();
	Function* f = (argc >= 1 ? argv[0].asFunction() : 0);
	if (f == 0) {
		const String* desc = (argc >= 1 ? argv[0].toString(heap) : String::allocate(heap, "undefined"));
		ScriptException::throwError(heap, TYPE_ERROR, String::concatenate(heap, *desc, String(" is not a function")));
	}
	const Code* code = f->getScriptCode();
	if (code == 0) {
		ScriptException::throwError(heap, TYPE_ERROR, "Cannot disassemble native code");
	}
	disassemble(heap, *code);
	return Value::UNDEFINED;
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
		contentsString = utf8ToString(rt.getHeap(), contents.data(), contents.size());
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

// write(path, content) - the file-writing complement to read(). Writes content's UTF-8 bytes to path,
// truncating any existing file. (Not to be confused with printing: program output still goes through print.)
Var writeFile(Runtime& rt, const Var& thisVar, const VarList& args) {
	if (args.size() < 1) {
		ScriptException::throwError(rt.getHeap(), GENERIC_ERROR, "write() requires a filename");
	}
	const String* filenameString = args[0];
	const std::string filename = filenameString->toUTF8String();
	std::ofstream file;
	try {
		file.open(filename.c_str(), std::ios::binary | std::ios::trunc);
		if (!file.good()) {
			ScriptException::throwError(rt.getHeap(), GENERIC_ERROR, "Could not open output file");
		}
		file.exceptions(std::ios_base::badbit | std::ios_base::failbit);
		if (args.size() >= 2) {
			const String* content = args[1];
			const std::string bytes = content->toUTF8String();
			if (!bytes.empty()) {
				file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
			}
		}
		file.close();
	}
	catch (const std::ios_base::failure& x) {
		ScriptException::throwError(rt.getHeap(), GENERIC_ERROR, x.what());
	}
	return Var(rt);
}

// system(command) - runs a shell command and returns the raw status from C's system() (same convention as
// PikaCmd: on POSIX the exit code is in the high byte, so callers divide by 256).
Var runSystem(Runtime& rt, const Var& thisVar, const VarList& args) {
	std::cout.flush();
	std::wcout.flush();
	std::cerr.flush();
	std::wcerr.flush();
	std::string command;
	if (args.size() >= 1) {
		const String* c = args[0];
		command = c->toUTF8String();
	}
	const int rc = ::system(command.c_str());
	return Var(rt, Value(static_cast<double>(rc)));
}

// getenv(name) - returns the environment variable value, or undefined if it is not set.
Var readEnv(Runtime& rt, const Var& thisVar, const VarList& args) {
	if (args.size() < 1) {
		return Var(rt);
	}
	const String* name = args[0];
	const std::string n = name->toUTF8String();
	const char* v = ::getenv(n.c_str());
	if (v == 0) {
		return Var(rt);
	}
	return Var(rt, Value(utf8ToString(rt.getHeap(), v, std::strlen(v))));
}

Var quit(Runtime& rt, const Var& thisVar, const VarList& args) {
	doQuit = true;
	return Var(rt);
}

Var help(Runtime& rt, const Var& thisVar, const VarList& args) {
	(void)thisVar;
	(void)args;
	std::wcout << L"Available REPL helpers:" << std::endl
			<< L"  quit()             - exit the REPL" << std::endl
			<< L"  read(file)         - return UTF-8 file as string" << std::endl
			<< L"  write(file, text)  - write text to a UTF-8 file" << std::endl
			<< L"  load(file)         - execute a UTF-8 JavaScript file" << std::endl
			<< L"  system(command)    - run a shell command, return its status" << std::endl
			<< L"  getenv(name)       - return an environment variable (or undefined)" << std::endl
			<< L"  gc()               - run garbage collection" << std::endl
			<< L"  dasm(func)         - disassemble a compiled function" << std::endl
			<< std::endl
			<< L"Special commands:" << std::endl
			<< L"  #save [name]       - save the current session (no name uses a timestamp in tests/)" << std::endl
			<< L"  #undo              - drop the last entered (unexecuted) line" << std::endl
			<< L"  #purge             - clear the session log" << std::endl
			<< L"  ?expr              - shortcut for print(expr)" << std::endl;
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
			<< "  -s: suppress the interactive '=<result>' echo" << std::endl
			<< "  -t: print timing and memory stats" << std::endl
			<< "  -p: pause before quitting" << std::endl
			<< "  -n: do not load the standard library" << std::endl
			<< "  -E, --legacy-exceptions: use legacy exception output" << std::endl
			<< "  -h, --help: show this usage" << std::endl
			<< std::endl
			<< "NuXJS options must appear before the script file name." << std::endl
			<< "JS receives global `arguments`: [script.js, arguments...]." << std::endl;
}

// Compiles one chunk of source and runs it to completion. All program-visible output (print() and `!!!!`
// error/stack reporting) goes to stdout; the result echo and timing go to stderr. When `capture` is
// non-null the `!!!!` lines are also appended to the interactive transcript. Returns true on success,
// false if a compile-time or run-time error was reported.
static bool compileAndRun(Runtime& rt, MyHeap& heap, Processor& processor, const String& source
		, const String* scriptFileName, Compiler::Target compileFor, bool timing, bool echoResult
		, bool legacyExceptions, std::vector<std::string>* capture, size_t& peakMemory) {
	try {
		const String* scriptSource = new(heap) String(heap.managed(), source.begin(), source.end());
		SourceCodeUnit* sourceCodeUnit = new(heap) SourceCodeUnit(heap.managed(), scriptSource, scriptFileName);
		Code globalCode(heap.roots(), 0, sourceCodeUnit);
		Compiler compiler(heap.roots(), &globalCode, compileFor, 1);
		try {
			compiler.compile(*scriptSource);
		}
		catch (const Exception&) {
			UInt32 offset;
			UInt32 lineNumber;
			UInt32 columnNumber;
			compiler.getStopPosition(offset, lineNumber, columnNumber);
			std::wstringstream ss;
			ss << L"!!!! Line: " << lineNumber;
			const std::wstring ws = ss.str();
			std::wcout << ws << std::endl;
			if (capture != 0) {
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

		if (echoResult) {
			Value v = processor.getResult();
			std::wcerr << L"\t=" << v.toString(heap)->toWideString() << std::endl;
		}
		if (timing) {
			const double end = getCPUSecs();
			std::cerr << (end - start) << "s" << std::endl;
			std::cerr << heap.size() / (1024.0 * 1024.0) << "MiB" << std::endl;
			std::cerr << peakMemory / (1024.0 * 1024.0) << "MiB" << std::endl;
			std::cerr << heap.peakSize / (1024.0 * 1024.0) << "MiB" << std::endl;
		}
		return true;
	}
	catch (const ScriptException& x) {
		const std::wstring ws = L"!!!! " + x.value.toString(heap)->toWideString();
		std::wcout << ws << std::endl;
		std::string stackLine;
		if (!legacyExceptions) {
			const char* stackTrace = x.getStackTrace();
			if (stackTrace[0] != '\0') {
				stackLine = std::string("!!!! stack: ") + stackTrace;
				std::wcout << std::wstring(stackLine.begin(), stackLine.end()) << std::endl;
			}
		}
		if (capture != 0) {
			pushIOLines('!', String(heap.roots(), ws.c_str()));
			if (!stackLine.empty()) {
				pushIOLines('!', String(heap.roots(), stackLine.c_str()));
			}
		}
		return false;
	}
	catch (const std::exception& x) {
		std::cout << "!!!! " << x.what() << std::endl;
		if (capture != 0) {
			pushIOLines('!', x.what());
		}
		return false;
	}
	catch (...) {
		std::cout << "Unknown exception" << std::endl;
		if (capture != 0) {
			pushIOLines('!', "Unknown exception");
		}
		return false;
	}
}

// Runs a single whole-file script (global code). Returns a process exit code.
static int runScriptFile(Runtime& rt, MyHeap& heap, Processor& processor, const String& source
		, const std::string& path, bool timing, bool legacyExceptions) {
	size_t peakMemory = 0;
	const String* scriptFileName = String::allocate(heap, path.c_str());
	// A script run is a pure runner: never echo the completion value (which is always undefined for global
	// code anyway). The `=<result>` echo is an interactive-REPL convenience only.
	const bool ok = compileAndRun(rt, heap, processor, source, scriptFileName, Compiler::FOR_GLOBAL
			, timing, false, legacyExceptions, 0, peakMemory);
	return ok ? 0 : 1;
}

// Reads lines (from a terminal or a piped stream), accumulating them until a blank line triggers
// evaluation as eval-global code. Handles the #save / #undo / #purge / ?expr conveniences and records the
// session transcript for #save. Returns a process exit code.
static int runInteractive(Runtime& rt, MyHeap& heap, Processor& processor, std::istream& in
		, bool timing, bool suppressResultEcho, bool legacyExceptions) {
	const String LF_STRING("\n");
	const String* scriptFileName = rt.newStringConstant("<anonymous>");
	String source(EMPTY_STRING);
	size_t peakMemory = 0;

	in.exceptions(std::ios_base::badbit);
	while (in.good() && !doQuit) {
		bool execute = false;
		try {
			std::string utf8Line;
			std::getline(in, utf8Line);
			if (!in.good() && !in.eof()) {
				throw std::runtime_error("Input error");
			}
			if (utf8Line == "#save" || utf8Line.compare(0, 6, "#save ") == 0) {
				std::string fn;
				if (utf8Line.size() > 6) {
					fn = "tests/" + utf8Line.substr(6) + ".io";
				} else {
					const time_t t = time(0);
					char buf[256];
					strftime(buf, sizeof (buf), "tests/%Y%m%d_%H%M%S.io", localtime(&t));
					fn = buf;
				}
				std::ofstream saveStream(fn.c_str());
				for (std::vector<std::string>::const_iterator it = ioLines.begin(); it != ioLines.end(); ++it) {
					saveStream << *it << std::endl;
				}
				saveStream.close();
				ioLines.clear();
				std::cout << "saved to " << fn << std::endl;
			} else if (utf8Line == "#undo") {
				UInt32 n = source.size();
				while (n > 0 && source[n - 1] != '\n') {
					--n;
				}
				if (n > 0) {
					--n; // drop the separating LF as well
				}
				source = String(heap.roots(), source.begin(), source.begin() + n);
				std::cout << "undone" << std::endl;
			} else if (utf8Line == "#purge") {
				ioLines.clear();
				std::cout << "purged" << std::endl;
			} else if (!utf8Line.empty()) {
				const std::vector<Char> u = utf8ToUtf16(utf8Line.data(), utf8Line.size());
				const String line(heap.roots(), u.empty() ? 0 : u.data(), u.empty() ? 0 : u.data() + u.size());
				if (!source.empty()) source = String(heap.roots(), source, LF_STRING);
				source = String(heap.roots(), source, line);
			} else {
				if (source.size() > 0 && source[0] == '?') {
					const String rest(heap.roots(), source.begin() + 1, source.end());
					const String* call = String::concatenate(heap, String("print("), rest);
					source = String(heap.roots(), *call, String(")"));
				}
				pushIOLines('>', source);
				execute = true;
			}

			if (execute) {
				compileAndRun(rt, heap, processor, source, scriptFileName, Compiler::FOR_EVAL
						, timing, !suppressResultEcho, legacyExceptions, &ioLines, peakMemory);
				source = EMPTY_STRING;
			}
		}
		catch (const std::exception& x) {
			source = EMPTY_STRING;
			std::cout << "!!!! " << x.what() << std::endl;
			pushIOLines('!', x.what());
		}
		catch (...) {
			source = EMPTY_STRING;
			std::cout << "Unknown exception" << std::endl;
			pushIOLines('!', "Unknown exception");
		}
		if (execute) {
			pushIOStop();
		}
	}
	if (!doQuit && !in.eof()) {
		std::cout << "input stream failure" << std::endl;
		return 1;
	}
	return 0;
}

int replMain(int argc, const char* argv[]) {
	try {
		std::string inputFilePath;
		std::vector<std::string> scriptArguments;
		bool doTime = false;
		bool suppressResultEcho = false;
		bool loadStdLib = true;
		bool legacyExceptions = false;
		for (int argi = 1; argi < argc; ++argi) {
			if (!inputFilePath.empty()) {
				scriptArguments.push_back(argv[argi]);
			} else if (strcmp(argv[argi], "-t") == 0) doTime = true;
			else if (strcmp(argv[argi], "-s") == 0) suppressResultEcho = true;
			else if (strcmp(argv[argi], "-p") == 0) pauseBeforeQuit = true;
			else if (strcmp(argv[argi], "-n") == 0) loadStdLib = false;
			else if (strcmp(argv[argi], "--legacy-exceptions") == 0 || strcmp(argv[argi], "-E") == 0) legacyExceptions = true;
			else if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
				printUsage();
				return 0;
			}
			else {
				inputFilePath = argv[argi];
			}
		}

		const bool scriptMode = !inputFilePath.empty();

		MyHeap heap;
		Runtime rt(heap);

		String source(EMPTY_STRING);
		if (scriptMode) {
			std::ifstream file(inputFilePath.c_str(), std::ios::binary);
			if (!file.good()) {
				std::cerr << "Could not open input stream" << std::endl;
				return 1;
			}
			std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			const std::vector<Char> u = utf8ToUtf16(contents.data(), contents.size());
			if (!u.empty()) {
				source = String(heap.roots(), u.data(), u.data() + u.size());
			}
		} else if (!std::cin.good()) {
			std::cerr << "Could not open input stream" << std::endl;
			return 1;
		}

		Object& globals = *rt.getGlobalObject();
		Var globs = rt.getGlobalsVar();
		globs["read"] = read;
		globs["write"] = writeFile;
		globs["load"] = load;
		globs["system"] = runSystem;
		globs["getenv"] = readEnv;
		globs["quit"] = quit;
		globs["help"] = help;
		{
			Var argumentsVar = scriptMode
					? rt.newArrayVar(static_cast<UInt32>(scriptArguments.size()) + 1)
					: rt.newArrayVar();
			if (scriptMode) {
				argumentsVar[0] = inputFilePath;
				for (UInt32 i = 0; i < static_cast<UInt32>(scriptArguments.size()); ++i) {
					argumentsVar[i + 1] = scriptArguments[i];
				}
			}
			globs["arguments"] = argumentsVar;
		}

		PrintFunction printFunction;
		printFunction.capture = (scriptMode ? 0 : &ioLines);
		const String PRINT_STRING("print");
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
		if (scriptMode) {
			return runScriptFile(rt, heap, processor, source, inputFilePath, doTime, legacyExceptions);
		} else {
			return runInteractive(rt, heap, processor, std::cin, doTime, suppressResultEcho, legacyExceptions);
		}
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
	int rc = replMain(argc, argv);
	if (pauseBeforeQuit) std::wcin.get();
	return rc;
}
#endif

#ifdef _MSC_VER
#pragma float_control(pop)
#endif
