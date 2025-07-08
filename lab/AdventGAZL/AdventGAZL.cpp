#include <iostream>
#include <string>
#include <ctime>
#include <fstream>
#include <cmath>
#include "src/GAZL.h"

using namespace GAZL;

template<class C> std::basic_ostream<C>& xcout();
template<class C> std::basic_istream<C>& xcin();
template<> std::basic_ostream<char>& xcout() { return std::cout; }
template<> std::basic_ostream<wchar_t>& xcout() { return std::wcout; }
template<> std::basic_istream<char>& xcin() { return std::cin; }
template<> std::basic_istream<wchar_t>& xcin() { return std::wcin; }

GAZL::Status abort(Processor*) {
	return ABORTED;
}

GAZL::Status assertfail(Processor* p) {
	assert(0);
	return abort(p);
}

GAZL::Status launch(Processor* p) {
	Value* params = p->accessParams(1);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].i = 0;
	/* dummy */
	return 0;
}

GAZL::Status loadText(Processor* p) {
	Value* params = p->accessParams(1);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].i = 0;
	/* dummy */
	return 0;
}

GAZL::Status sleep(GAZL::Processor* vpu) {
	GAZL::Value* params = vpu->accessParams(2);
	if (params == 0) return GAZL::DATA_STACK_OVERFLOW;
	/* dummy */
	return 0;
}

GAZL::Status printInt(Processor* p) {
	Value* params = p->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	printf("%d", params[1].i);
	return 0;
}

GAZL::Status printIntLeft(Processor* p) {
	Value* params = p->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	printf("%-*d", params[1].i, params[2].i);
	return 0;
}

GAZL::Status printIntRight(Processor* p) {
	Value* params = p->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	printf("%*d", params[1].i, params[2].i);
	return 0;
}

GAZL::Status printFloat(Processor* p) {
	Value* params = p->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	printf("%.16g", params[1].f);
	return 0;
};

GAZL::Status print(Processor* vpu) {
	std::basic_ostream<Char>& outstream = xcout<Char>();
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	Pointer p = params[1].p;
	const Value* vp = vpu->accessConstMemory(p, 1); // Note: it is ok to clear access to only one word since the last word of the virtual memory is always 0
	if (vp == 0) return ACCESS_VIOLATION;
	do {
		if (vp->i != 0) {
			outstream << static_cast<Char>(vp->i);
			++vp;
		}
	} while (vp->i != 0);
	if (vpu->accessConstMemory(p, 1) == 0) return ACCESS_VIOLATION; // In case we ended up at the "guardian" element...
	return 0;
};

GAZL::Status printLF(Processor*) {
	std::basic_ostream<Char>& outstream = xcout<Char>();
	outstream << std::endl;
	return 0;
};

GAZL::Status input(Processor* vpu) {
	std::basic_istream<Char>& instream = xcin<Char>();
	Value* params = vpu->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	int maxCount = params[1].i;
	Pointer p = params[2].p;
	Value* bp = vpu->accessMemory(p, maxCount + 1);
	if (bp == 0) return ACCESS_VIOLATION;
	const Value* ep = bp + maxCount;
	Value* vp = bp;
	Char c;
	while (vp < ep && instream.get(c) && c != '\n' && c != '\r') {
		vp->i = static_cast<Int>(c);
		++vp;
	}
	vp->i = 0;
	params[0].i = vp - bp;
	return 0;
};

GAZL::Status inputEOF(Processor* vpu) {
	std::basic_istream<Char>& instream = xcin<Char>();
	Value* params = vpu->accessParams(1);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].i = (instream.good() ? 0 : 1);
	return 0;
}

static const NativeFunc nativeTable[13] = {
	abort, assertfail, printInt, printIntLeft, printIntRight, printFloat, print, printLF, input, inputEOF, launch, loadText, sleep
};

std::basic_string<Char> load(const std::string& file) {
	std::basic_ifstream<Char> instream(file.c_str());														// Sorry, can't pass a wchar_t filename. MSVC supports it, but it is non-standard. So we convert to a std::string to be on the safe side.
	if (!instream.good()) throw std::exception(); // FIX : Xception(String(STR("Cannot open file for reading: ")) += escape(file));
	std::basic_string<Char> chars;
	while (!instream.eof()) {
		if (instream.bad()) throw std::exception(); // FIX : (String(STR("Error reading from file: ")) += escape(file));
		Char buffer[1024];
		instream.read(buffer, 1024);
		chars += std::basic_string<Char>(buffer, instream.gcount());
	}
	return chars;
}

#if !defined(NDEBUG)
	namespace GAZL { extern void unitTest(); extern void instructionReport(); }
#endif

int main(int argc, const char* argv[]) {
#if !defined(NDEBUG)
	unitTest();
//	instructionReport();
#endif

	std::string gazlFile = (argc >= 2 ? argv[1] : "fulladvent.gazl");
	std::string mainFunc = (argc >= 3 ? argv[2] : "main");
	
	const int CODE_MAX_SIZE = 5000;
	const int MEMORY_MAX_SIZE = 32768;
	const int DATA_STACK_SIZE = 8000;
	const int CALL_STACK_SIZE = 1000;

	static Value memory[MEMORY_MAX_SIZE + 10000];
	static Instruction cody[CODE_MAX_SIZE];

	Value v;
	v.i = 0xAACC5599;
	std::fill_n(&memory[0], MEMORY_MAX_SIZE + 10000, v);
	
	Symbols globals;
	
	for (int i = 3; i + 2 <= argc; i += 2) {
		Value v;
		v.i = atoi(argv[i + 1]);
		globals.defineConstant(argv[i + 0], false, v);
	}
	
	globals.registerNative("abort", 0);
	globals.registerNative("assertFail", 1);
	globals.registerNative("output", 6);
	globals.registerNative("outputLF", 7);
	globals.registerNative("input", 8);
	globals.registerNative("inputEOF", 9);
	globals.registerNative("launch", 10);
	globals.registerNative("loadText", 11);
	globals.registerNative("sleep", 12);

	UInt codySize;
	UInt rwSize;
	UInt constSize;
	{
		Assembler assem(CODE_MAX_SIZE, cody, MEMORY_MAX_SIZE, memory, globals);
		assem.newUnit(gazlFile.c_str());
		std::string code = load(gazlFile);
		
		const Char* cp = code.c_str();
		while (*cp != 0) {
			try {
				cp = assem.feed(cp);
				if (*cp == 0) {
					assem.finalize(codySize, rwSize, constSize);
					std::cout << "Code size: " << codySize << std::endl << "RW Size: " << rwSize << std::endl
							<< "Const Size: " << constSize << std::endl;
				}
			}
			catch (const Exception& e) {
				std::cout << e.what();
				if (!e.detail.empty()) std::cout << " : " << e.detail.c_str();
				std::cout << std::endl << std::string(cp, cp + strcspn(cp, "\r\n")) << std::endl;
				return -1;
			}
		}
	}
	
	if (false) {
		Symbols::Iterator it;
		if (globals.findFirstGlobal(it, true)) {
			do {
				bool isTemp;
				Pointer address;
				UInt size;
				const char* name = globals.getGlobalInfo(it, isTemp, address, size);
				std::cout << name << " type:" << (isTemp ? "TEMP" : "GLOB") << " address:" << std::hex << address
						<< std::dec << " size:" << size << std::endl;
			} while (globals.findNextGlobal(it, true));
		}
	}
	
	{
		clock_t c0 = clock();
		CallStackEntry callStack[CALL_STACK_SIZE];
		Processor pmachine(codySize, cody, MEMORY_MAX_SIZE, memory, rwSize + DATA_STACK_SIZE, rwSize, DATA_STACK_SIZE
				, CALL_STACK_SIZE, callStack, nativeTable);
		Pointer funcy = globals.findFunction(mainFunc.c_str());
		Value* p = pmachine.accessParams(2);
		assert(p != 0);
		p[1].i = 14;
		Int status = pmachine.enterCall(funcy);
		assert(status == OK);
		do {
			pmachine.resetTimeOut(25000);
			status = pmachine.run();
		} while (status == TIME_OUT);
		clock_t c1 = clock();
		printf("Status: %d\n%%0: %d / %g\nTime: %g\n", status, p[0].i, p[0].f, static_cast<double>(c1 - c0) / CLOCKS_PER_SEC);
	}
	
	{
		for (int i = MEMORY_MAX_SIZE; i < MEMORY_MAX_SIZE + 10000; ++i) {
			if (memory[i].i != 0xAACC5599) {
				std::cout << "Corrupt data after memory" << std::endl;
				return 1;
			}
		}
	}
	return 0;
}
