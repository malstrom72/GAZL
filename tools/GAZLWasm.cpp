/*
	Emscripten wrapper exposing the real GAZL assembler+VM (src/GAZL.cpp) to the Impala playground.
	Build with tools/buildGazlWasm.sh; the output impala/gazlVm.js is one self-contained file (wasm
	inlined, synchronous startup) so playground.html keeps working straight off file://.

	The natives mirror GAZLCmd.cpp with three deliberate differences: output goes to a buffer instead
	of stdout (capped, so a print loop cannot eat the tab's memory), `input` returns an empty line
	(the browser cannot block on stdin), and nothing resets the cycle budget (GAZLCmd's print natives
	do, but here the budget is the only thing standing between an infinite loop and a frozen page).
*/

#include <string>
#include <sstream>
#include <cstring>
#include <cmath>
#include "../src/GAZL.h"

using namespace GAZL;

static const int DATA_MEMORY_SIZE = 128 * 1024;
static const int CODE_MEMORY_SIZE = 128 * 1024;
static const int FUNCTION_TABLE_SIZE = CODE_MEMORY_SIZE;
static const int CALL_STACK_SIZE = 2048;
static const size_t OUT_LIMIT = 1 << 20;
static const Int CYCLE_BUDGET = 100000000;

static const Status OUTPUT_LIMIT = 1;	// custom run-time status: outText reached OUT_LIMIT

static Value memory[DATA_MEMORY_SIZE];
static Instruction code[CODE_MEMORY_SIZE];
static UInt functionTable[FUNCTION_TABLE_SIZE];
static CallStackEntry callStack[CALL_STACK_SIZE];

static std::string outText;
static std::string errText;

static Status checkOutLimit() { return outText.size() > OUT_LIMIT ? OUTPUT_LIMIT : OK; }

static Status print(Processor* vpu) {
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	Pointer p = params[1].p;
	const Value* vp = vpu->accessConstMemory(p, 1);	// ok to clear access to one word: the last word of virtual memory is always 0
	if (vp == 0) return ACCESS_VIOLATION;
	while (vp->i != 0) {
		outText += static_cast<Char>(vp->i);
		++vp;
		++p;
	}
	if (vpu->accessConstMemory(p, 1) == 0) return ACCESS_VIOLATION;	// in case we ended up at the "guardian" element
	return checkOutLimit();
}

static Status abort(Processor*) {
	return ABORTED;
}

static Status assertFail(Processor* p) {
	outText += "Assertion failed: ";
	print(p);
	outText += '\n';
	return ABORTED;
}

static Status printInt(Processor* vpu) {
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	std::ostringstream s;
	s << params[1].i;
	outText += s.str();
	return checkOutLimit();
}

static Status printFloat(Processor* vpu) {
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	std::ostringstream s;
	s << params[1].f;
	outText += s.str();
	return checkOutLimit();
}

static Status printLF(Processor* vpu) {
	outText += '\n';
	return checkOutLimit();
}

static Status input(Processor* vpu) {
	Value* params = vpu->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	Value* bp = vpu->accessMemory(params[2].p, params[1].i + 1);
	if (bp == 0) return ACCESS_VIOLATION;
	bp[0].i = 0;
	params[0].i = 0;
	return OK;
}

static Status gazlSqrt(Processor* vpu) {
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = sqrt(params[1].f);
	return OK;
}

static Status gazlLog(Processor* vpu) {
	Value* params = vpu->accessParams(2);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = log(params[1].f);
	return OK;
}

static Status gazlAtan2(Processor* vpu) {
	Value* params = vpu->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = atan2(params[1].f, params[2].f);
	return OK;
}

static const NativeFunc NATIVE_TABLE[] = {
	abort, assertFail, printInt, printFloat, print, printLF, input, gazlAtan2, gazlSqrt, gazlLog
};

static const char* NATIVE_NAMES[] = {
	"abort", "assertFail", "printInt", "printFloat", "print", "printLF", "input", "atan2", "sqrt", "log"
};

static const char* statusName(Status status) {
	switch (status) {
		case BAD_PEEK:				return "BAD_PEEK (read outside memory)";
		case BAD_POKE:				return "BAD_POKE (write outside memory)";
		case BAD_CALL:				return "BAD_CALL (call through a bad function pointer)";
		case DATA_STACK_OVERFLOW:	return "DATA_STACK_OVERFLOW";
		case IP_STACK_OVERFLOW:		return "IP_STACK_OVERFLOW (recursion limit)";
		case DIVISION_BY_ZERO:		return "DIVISION_BY_ZERO";
		case ACCESS_VIOLATION:		return "ACCESS_VIOLATION";
		case ABORTED:				return "ABORTED";
		case TERMINATED:			return "TERMINATED";
		case OUTPUT_LIMIT:			return "output limit (1 MB) reached";
		default:					return "unknown status";
	}
}

extern "C" {

const char* gazlOut() { return outText.c_str(); }
const char* gazlErr() { return errText.c_str(); }

// Assemble `source` and run `entry` to completion. 0 = ok, 1 = load error, 2 = runtime error,
// 3 = timed out. gazlOut() returns whatever was printed (even on error), gazlErr() the diagnostic.
int gazlRun(const char* source, const char* entry) {
	outText.clear();
	errText.clear();
	std::memset(memory, 0, sizeof (memory));	// identical state every run, whatever the last run left behind
	try {
		Symbols globals;
		for (size_t i = 0; i < sizeof (NATIVE_TABLE) / sizeof (*NATIVE_TABLE); ++i)
			globals.registerNative(NATIVE_NAMES[i], i);

		UInt codeSize, globalsSize, constsSize;
		UInt functionCount = 0;
		{
			Assembler assem(CODE_MEMORY_SIZE, code, FUNCTION_TABLE_SIZE, functionTable, DATA_MEMORY_SIZE, memory, globals);
			assem.newUnit("output.gazl");
			std::istringstream stream(source);
			std::string line;
			int lineNumber = 0;
			try {
				while (std::getline(stream, line)) {
					++lineNumber;
					assem.feed(line.c_str());
				}
				lineNumber = 0;	// past the last feed: an error below is link-time, with no line to point at
				assem.finalize(codeSize, globalsSize, constsSize, functionCount);
			} catch (const Exception& x) {
				std::ostringstream s;
				if (lineNumber > 0) s << "output.gazl:" << lineNumber << ": error: " << x.what() << '\n' << line;
				else s << "link error: " << x.what();
				errText = s.str();
				return 1;
			}
		}

		Processor vm(codeSize, code, functionCount, functionTable, DATA_MEMORY_SIZE, memory, globalsSize
				, constsSize, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0);
		Pointer entryFunction = globals.findFunction(entry);
		if (entryFunction == 0) {
			errText = std::string("could not locate function: ") + entry;
			return 1;
		}
		Status status = vm.enterCall(entryFunction);
		if (status == OK) {
			vm.resetTimeOut(CYCLE_BUDGET);
			status = vm.run();
		}
		if (status == OK) return 0;
		if (status == TIME_OUT) {
			errText = "timed out (cycle budget exhausted) - infinite loop?";
			return 3;
		}
		errText = std::string("runtime error: ") + statusName(status);
		return 2;
	} catch (const std::exception& x) {
		errText = x.what();
		return 1;
	}
}

}
