#include <iostream>
#include <string>
#include <ctime>
#include <fstream>
#include <cmath>
#include "../../src/GAZL.h"

using namespace GAZL;

template<class C> std::basic_ostream<C>& xcout();
template<class C> std::basic_istream<C>& xcin();
template<> std::basic_ostream<char>& xcout() { return std::cout; }
template<> std::basic_ostream<wchar_t>& xcout() { return std::wcout; }
template<> std::basic_istream<char>& xcin() { return std::cin; }
template<> std::basic_istream<wchar_t>& xcin() { return std::wcin; }

std::ostream* audioStream;

static GAZL::Pointer leftPointer;
static GAZL::Pointer rightPointer;

GAZL::Status trace(Processor* vpu) {
	std::basic_ostream<Char>& outstream = xcout<Char>();
	Value* params = vpu->accessParams(2);
	if (params == 0) return GAZL::DATA_STACK_OVERFLOW;
	Pointer p = params[1].p;
	const Value* vp = vpu->accessConstMemory(p, 1); // Note: it is ok to clear access to only one word since the last word of the virtual memory is always 0
	if (vp == 0) return GAZL::ACCESS_VIOLATION;
	do {
		if (vp->i != 0) {
			outstream << static_cast<Char>(vp->i);
			++vp;
		}
	} while (vp->i != 0);
	outstream << std::endl;
	if (vpu->accessConstMemory(p, 1) == 0) return ACCESS_VIOLATION; // In case we ended up at the "guardian" element...
	return GAZL::OK;
}

GAZL::Status abort(Processor*) {
	std::basic_ostream<Char>& outstream = xcout<Char>();
	outstream << "Aborted";
	return GAZL::ABORTED;
}

GAZL::Status assertfail(Processor* vpu) {
	std::basic_ostream<Char>& outstream = xcout<Char>();
	outstream << "Assertion failed: ";
	trace(vpu);
	outstream << std::endl;
	return GAZL::ABORTED;
}

GAZL::Status yield(Processor* vpu) {
	GAZL::Value* leftValue = vpu->accessMemory(leftPointer, 1);
	GAZL::Value* rightValue = vpu->accessMemory(rightPointer, 1);
	if (leftValue == 0 || rightValue == 0) return GAZL::ACCESS_VIOLATION;
	assert(rightValue->i == leftValue->i);
	(*audioStream) << static_cast<char>((leftValue->i >> 4) + 0x80);
	return GAZL::OK;
}

static const NativeFunc nativeTable[4] = {
	abort, assertfail, trace, yield
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

	std::string gazlFile = "sam.gazl";
	
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
	globals.registerNative("trace", 2);
	globals.registerNative("yield", 3);

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
		//			std::cout << "Code size: " << codySize << std::endl << "RW Size: " << rwSize << std::endl
		//					<< "Const Size: " << constSize << std::endl;
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
	
	std::ofstream fileOutput;
	audioStream = &std::cout;
	if (argc >= 3) {
		fileOutput.open(argv[2], std::ofstream::binary);
		if (!fileOutput.good()) {
			std::cerr << "Could not open output file" << std::endl;
			throw std::exception();
		}
		fileOutput.exceptions(std::ios_base::badbit);		
		audioStream = &fileOutput;
	}
	
	{
		CallStackEntry callStack[CALL_STACK_SIZE];
		Processor pmachine(codySize, cody, MEMORY_MAX_SIZE, memory, rwSize + DATA_STACK_SIZE, rwSize, DATA_STACK_SIZE
				, CALL_STACK_SIZE, callStack, nativeTable);
		GAZL::UInt size;
		leftPointer = globals.findGlobal("left", size);
		rightPointer = globals.findGlobal("right", size);
		GAZL::Pointer configPointer = globals.findGlobal("config", size);
		if (configPointer != GAZL::GAZL_NULL && argc >= 2) {
			Value* vp = pmachine.accessMemory(configPointer, size);
			if (vp == 0) return GAZL::ACCESS_VIOLATION;
			Value* ep = vp + size;
			int i = 0;
			for (; vp < ep; ++vp) if ((vp->i = argv[1][i]) != 0) ++i;
		}
		Pointer initFunc = globals.findFunction("init");
		Value* p = pmachine.accessParams(2);
		assert(p != 0);
		p[1].i = 14;
		Int status = pmachine.enterCall(initFunc);
		assert(status == OK);
		do {
			pmachine.resetTimeOut(25000);
			status = pmachine.run();
		} while (status == TIME_OUT);
		if (status == OK) {
			Int status = pmachine.enterCall(globals.findFunction("process"));
			assert(status == OK);
			do {
				pmachine.resetTimeOut(25000);
				status = pmachine.run();
			} while (status == TIME_OUT);
		}
		if (status != OK) std::cerr << "Aborted with error code: " << status << std::endl;
	}
	
	{
		for (int i = MEMORY_MAX_SIZE; i < MEMORY_MAX_SIZE + 10000; ++i) {
			if (memory[i].i != 0xAACC5599) {
				std::cout << "Corrupt data after memory" << std::endl;
				return 1;
			}
		}
	}
	
	fileOutput.close();

	return 0;
}
