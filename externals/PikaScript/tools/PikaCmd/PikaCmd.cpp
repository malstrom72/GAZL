/**
	\file PikaCmd.cpp

	PikaCmd is a simple command-line tool for executing a PikaScript source code file.
	
	Use PikaCmd -h for more info.

	\version

	Version 0.97
	
	\page Copyright

	PikaScript is released under the "New Simplified BSD License". http://www.opensource.org/licenses/bsd-license.php
	
	Copyright (c) 2008-2025, NuEdge Development / Magnus Lidstroem
	All rights reserved.

	Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
	following conditions are met:

	Redistributions of source code must retain the above copyright notice, this list of conditions and the following
	disclaimer. 
	
	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
	disclaimer in the documentation and/or other materials provided with the distribution. 
	
	Neither the name of the NuEdge Development nor the names of its contributors may be used to endorse or promote
	products derived from this software without specific prior written permission.
	
	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
	SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
	WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define PIKA_UNICODE 0
#define QUICKER_SCRIPT 1

#if (!defined(PLATFORM_STRING))
	#error Must define PLATFORM_STRING
#endif

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <fstream>
#include <cstdint>
#if !defined(PikaScript_h)
#include "../../src/PikaScript.h"
#endif

#if (QUICKER_SCRIPT)
	#if !defined(QStrings_h)
	#include "../../src/QStrings.h"
	#endif
	#if !defined(PikaScriptImpl_h)
	#include "../../src/PikaScriptImpl.h"
	#endif
	#if !defined(QuickVars_h)
	#include "../../src/QuickVars.h"
	#endif

	struct QuickerScriptConfig;
	typedef Pika::Script<QuickerScriptConfig> Script;

	struct QuickerScriptConfig {
		typedef Pika::STLValue< QStrings::QString<char> > Value;
		typedef Pika::QuickVars<Script::STLVariables> Locals;
		typedef Locals Globals;
	};
#else
	typedef Pika::StdScript Script;
#endif

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

extern const char* BUILT_IN_DEBUG;
extern const char* BUILT_IN_HELP;
extern const char* BUILT_IN_INTERACTIVE;
extern const char* BUILT_IN_STDLIB;

const char* BUILT_IN_USAGE =
		"print('"
		"\n"
		"PikaCmd [ -h | <filename> [<arguments> ...] | ''{'' <code> ''}'' ]\n"
		"\n"
		"Command-line arguments are available in the global scope variables $1, $2 etc. ($0 is the script filename.) "
		"The process exit code will be that of the global variable ''exitCode'' (default is 0), or 255 if an exception "
		"occurs. ''PLATFORM'' will contain an operating system identifier (e.g. ''WINDOWS''). ''s'' = "
		"getenv(''var'') can be used to retrieve environment variables.\n"
		"\n"
		"Notice that you may need to enclose <code> in double quotes (\") to prevent the special interpretation of "
		"some characters (e.g. < and >). Double quotes inside <code> may need to be escaped, for example: \\\".\n"
		"\n"
		"The standard library ''load'' function is overloaded to look for files in the directory of PikaCmd when "
		"files cannot be found in the current directory. There are also \"built-in\" versions of some standard "
		".pika files that will be used in case external files do not exist. The built-in files are: ''stdlib.pika''"
		", ''debug.pika'', ''help.pika'', ''interactive.pika'' and ''default.pika''.\n"
		"\n"
		"If you run PikaCmd without arguments it will execute ''default.pika''. The built-in ''default.pika'' runs "
		"''interactive.pika''.\n"
		"');";
		
const char* BUILT_IN_DEFAULT = "run('interactive.pika', 'go')";
			
const char* BUILT_IN_DIRECT =
		"{ include('stdlib.pika'); s = ''; for (i = 0; i < $n; ++i) s #= ' ' # $[i]; "
		"print('---- (' # evaluate(s, @::) # ')') }";

std::pair<Script::String, Script::String> BUILT_IN_FILES[] = {
	std::pair<Script::String, Script::String>("debug.pika", Script::String(BUILT_IN_DEBUG))
	, std::pair<Script::String, Script::String>("default.pika", Script::String(BUILT_IN_DEFAULT))
	, std::pair<Script::String, Script::String>("help.pika", Script::String(BUILT_IN_HELP))
	, std::pair<Script::String, Script::String>("interactive.pika", Script::String(BUILT_IN_INTERACTIVE))
	, std::pair<Script::String, Script::String>("stdlib.pika", Script::String(BUILT_IN_STDLIB))
	, std::pair<Script::String, Script::String>("-h", Script::String(BUILT_IN_USAGE))
};

static Script::String loadFile(std::basic_ifstream<Script::Char>& instream, const std::string& filename) {
	Script::String chars;
	Script::Char* buffer = new Script::Char[1024 * 1024];
	try {
		while (!instream.eof()) {
			if (instream.bad())
				throw Script::Xception(Script::String("Error reading from file: ") += Pika::escape(filename));
			instream.read(buffer, 1024 * 1024);
			chars += Script::String(buffer, static_cast<Script::String::size_type>(instream.gcount()));
		}
		instream.close();
	}
	catch (...) {
		delete [] buffer;
		throw;
	}
	delete [] buffer;
	return chars;
}

std::string pikaCmdDir;

Script::String overloadedLoad(const Script::String& filename) {
	std::string name(Pika::toStdString(filename));	// Sorry, can't pass a wchar_t filename. MSVC supports it, but it is non-standard. So we convert to a std::string to be on the safe side.
	{
		std::basic_ifstream<Script::Char> instream(name.c_str());
		if (instream.good()) return loadFile(instream, filename);
	}
	if (!pikaCmdDir.empty()) {
		std::basic_ifstream<Script::Char> instream((pikaCmdDir + name).c_str());
		if (instream.good()) return loadFile(instream, filename);
	}
	{
		for (int i = 0; i < sizeof (BUILT_IN_FILES) / sizeof (*BUILT_IN_FILES); ++i)
			if (filename == BUILT_IN_FILES[i].first) return BUILT_IN_FILES[i].second;
	}
	throw Script::Xception(Script::String("Cannot open file for reading: ") += Pika::escape(filename));
}

Script::String getEnvironmentVar(const Script::String& var) {
	const char* s = getenv(var.c_str());
	return (s == 0 ? Script::Value() : Script::String(s));
}

#ifdef LIBFUZZ

struct CallDepthException { };
struct TimeOutException { };

class LibFuzzRoot : public Script::FullRoot {
	typedef Script::FullRoot Super;
	public:		LibFuzzRoot() : userLevel(Pika::NO_TRACE), counter(0), deadline(0), callDepth(0) {
					deadline = time(0) + 5;
					updateTracer();
				}
	public:		virtual void trace(Frame& frame, const Script::String& source, Script::SizeType offset, bool lvalue
						, const Script::Value& value, Pika::Precedence level, bool exit) {
					if (level <= userLevel) Super::trace(frame, source, offset, lvalue, value, level, exit);
					if (level <= Pika::TRACE_LOOP && ++counter >= 100) {
						counter = 0;
						int timeNow = time(0);
						if (deadline - timeNow < 0) {
							throw TimeOutException();
						}
					}
					if (level == Pika::TRACE_CALL) {
						if (!exit && callDepth >= 20) {
							throw CallDepthException();
						}
						callDepth += (exit ? -1 : 1);
						assert(callDepth >= 0);
					}
				}
	public:		virtual void setTracer(Pika::Precedence traceLevel, const Script::Value& tracerFunction) throw() {
					userLevel = traceLevel;
					userTracer = tracerFunction;
					updateTracer();
				}
	protected:	void updateTracer() {
					Super::setTracer(static_cast<Pika::Precedence>(std::max(static_cast<int>(userLevel)
							, static_cast<int>(Pika::TRACE_LOOP))), userTracer);
				}
	protected:	Pika::Precedence userLevel;
	protected:	Script::Value userTracer;
	protected:	int counter;
	protected:	int deadline;
	protected:	int callDepth;
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    try {
		LibFuzzRoot root;
		root.registerNative("load", overloadedLoad);
		root.registerNative("getenv", getEnvironmentVar);
		root.assign("exitCode", Script::Value(0));
		root.assign("PLATFORM", Script::String(TO_STRING(PLATFORM_STRING)));
		root.assign("save", Script::String("{}"));
		root.assign("input", Script::String("{void}"));
		root.erase("system");
		root.evaluate(Script::String(reinterpret_cast<const char*>(Data), reinterpret_cast<const char*>(Data) + Size));
	}
	catch (const Script::Xception& x) {
	}
	catch (const TimeOutException&) {
		std::cerr << "timed out" << std::endl;
	}
	catch (const CallDepthException&) {
		std::cerr << "call depth limit exceeded" << std::endl;
	}
  	return 0;  // Non-zero return values are reserved for future use.
}

#endif

#ifndef LIBFUZZ
int main(int argc, const char* argv[]) {
	int exitCode = 255;
	std::srand(static_cast<unsigned int>(std::time(0)) ^ static_cast<unsigned int>(std::clock()));
	rand();
	if (argc < 2)
		std::cout << "PikaCmd version " << PIKA_SCRIPT_VERSION << ". (C) 2008-2025 NuEdge Development. "
				"All rights reserved." << std::endl << "Run PikaCmd -h for command-line argument syntax."
				<< std::endl << std::endl;
	try {
		pikaCmdDir = argv[0];
		size_t pos = pikaCmdDir.find_last_of("/\\:");
		if (pos == std::string::npos) pikaCmdDir.clear();
		else pikaCmdDir = pikaCmdDir.substr(0, pos + 1);
		Script::FullRoot root;
		root.registerNative("load", overloadedLoad);
		root.registerNative("getenv", getEnvironmentVar);
		root.assign("exitCode", Script::Value(0));
		root.assign("PLATFORM", Script::String(TO_STRING(PLATFORM_STRING)));
		const Script::String fn(argc < 2 ? "default.pika" : argv[1]);
		root.assign(Script::String("$0"), fn);	// Also assign args to global scope so we can access them anywhere.
		std::vector<Script::Value> args(1, fn);
		for (int i = 2; i < argc; ++i) {
			args.push_back(Script::String(argv[i]));
			root.assign(Script::String("$") += Pika::intToString<Script::String>(i - 1), argv[i]);	// Also assign args to global scope so we can access them anywhere.
		}
		root.call("run", (fn[0] == '{' ? Script::String(BUILT_IN_DIRECT) : Script::Value()), args.size(), &args[0]);
		exitCode = static_cast<int>(root.getOptional("exitCode"));
	} catch (const Script::Xception& x) {
		std::cerr << "!!!! " << x.what() << std::endl;
		exitCode = 255;
	}
#if (QUICKER_SCRIPT)
	QStrings::QString<char>::deinit();
#endif
	return exitCode;
}
#endif

