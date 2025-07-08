#include <iostream>
#include <string>
#include <ctime>
#include <fstream>
#include <cmath>
#include "src/GAZL.h"

using namespace GAZL;

inline int absolute(int i) { int x = i >> (sizeof (Int) * 8 - 1); return (i ^ x) - x; }
inline float absolute(float f) { return fabsf(f); }
inline double absolute(double f) { return fabs(f); }
float test(float x, float y) { return x * y; }

int testMul(Processor* p) {
	Value* params = p->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = params[1].f * params[2].f;
	return 0;
}

Pointer globalPointer;
Pointer callBack;

int testCallback(Processor* p) {
	const Value* memory = p->accessConstMemory(globalPointer, 1);
	assert(memory != 0);
	if (memory == 0) return ACCESS_VIOLATION;
	assert(memory->p == 0);
	Value* params = p->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].p = 0;
	params[1].p = callBack;
	int err = p->enterCall(callBack);
	assert(err == 0);
	if (err != 0) return err;
	err = p->run();
	assert(memory->p == callBack);
	return 0;
}

Status abort(Processor*) {
	return ABORTED;
}

Status assertFail(Processor* p) {
	assert(0);
	return abort(p);
}

Status printInt(Processor* p) {
	Value* params = p->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	printf("%d", params[1].i);
	return 0;
}

Status printFloat(Processor* p) {
	Value* params = p->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	printf("%g", params[1].f);
	return 0;
};

Status print(Processor* vpu) {
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
	return 0;
};

Status printLF(Processor*) {
	std::cout << std::endl;
	return 0;
};

Status myFloor(Processor* p) {
	Value* params = p->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = floorf(params[1].f);
	return 0;
};

Status gazl_fabs(Processor* p) {
	Value* params = p->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = absolute(params[1].f);
	return 0;
};

Status gazl_fmod(Processor* p) {
	Value* params = p->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = fmodf(params[1].f, params[2].f);
	return 0;
};

static const NativeFunc nativeTable[] = {
	abort, assertFail, printInt, printFloat, print, printLF, testMul, testCallback, myFloor, gazl_fmod, gazl_fabs
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
	try {
	#if !defined(NDEBUG)
		unitTest();
//		instructionReport();
	#endif

		std::string gazlFile = (argc >= 2 ? argv[1] : "test.gazl");
		std::string mainFunc = (argc >= 3 ? argv[2] : "main");
		
		static Value memory[100000 + 10000];
		static Instruction cody[100000];

		Value v;
		v.i = 0xAACC5599;
		std::fill_n(&memory[0], 100000 + 10000, v);
		
		Symbols globals;
		
		for (int i = 3; i + 2 <= argc; i += 2) {
			Value v;
			v.i = atoi(argv[i + 1]);
			globals.defineConstant(argv[i + 0], false, v);
		}
		
		globals.registerNative("abort", 0);
		globals.registerNative("assertFail", 1);
		globals.registerNative("printInt", 2);
		globals.registerNative("printFloat", 3);
		globals.registerNative("print", 4);
		globals.registerNative("printLF", 5);
		globals.registerNative("testMul", 6);
		globals.registerNative("testCallback", 7);
		globals.registerNative("floor", 8);
		globals.registerNative("_fmod", 9);
		globals.registerNative("fabs", 10);

		UInt codySize;
		UInt rwSize;
		UInt constSize;
		{
			Assembler assem(100000, cody, 100000, memory, globals);
			assem.newUnit(gazlFile.c_str());
			std::string code = load(gazlFile);
			
			const Char* cp = code.c_str();
			while (*cp != 0) {
				try {
					cp = assem.feed(cp);
					if (*cp == 0) assem.finalize(codySize, rwSize, constSize);
				}
				catch (const Exception& e) {
					std::cout << e.what() << std::endl << std::string(cp, cp + strcspn(cp, "\r\n")) << std::endl;
					return -1;
				}
			}
		}
		
		{
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
		
		UInt size;
		globalPointer = globals.findGlobal("global.pointer", size);
		callBack = globals.findFunction("CallBack");
		
		{
			clock_t c0 = clock();
			CallStackEntry callStack[4096];
			Processor pmachine(codySize, cody, 100000, memory, rwSize + 30000, rwSize, 30000, 4096, callStack, nativeTable);
			//pmachine.resetTimeOut(250000000);
			Pointer funcy = globals.findFunction(mainFunc.c_str());
			if (funcy == 0) throw std::exception();
			Value* p = pmachine.accessParams(2);
			assert(p != 0);
			p[1].i = 14;
			Status status = pmachine.enterCall(funcy);
			assert(status == OK);
			status = pmachine.run();
			clock_t c1 = clock();
			printf("Status: %d\n%%0: %d / %g\nTime: %g\n", status, p[0].i, p[0].f, static_cast<double>(c1 - c0) / CLOCKS_PER_SEC);
		}
		
		{
			for (int i = 100000; i < 110000; ++i) {
				if (memory[i].i != 0xAACC5599) {
					std::cout << "Corrupt data after memory" << std::endl;
					return 1;
				}
			}
		}
	}
	catch (const std::exception& x) {
		std::cout << "Exception: " << x.what() << std::endl;
	}
	catch (...) {
		std::cout << "General exception" << std::endl;
	}
	return 0;
}