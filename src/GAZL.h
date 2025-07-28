/**
       GAZL.h
	
	GAZL is an efficient low-level virtual machine and assembler for real-time applications. GAZL has static typing and
	all primitive types (int, floats and pointers) have the same word size (standard configuration is 32-bit). GAZL is
	100% interpreting but still very fast (it measures between 10% and 25% of fully optimized x86 machine-code). The
	persistent code format is in assembly language, allowing for compile-time decisions and optimizations when loading
	the code and transforming it to an efficient run-time representation.
	
	Goals

		- Make it the world's fastest interpreting virtual machine.
		- Should use a highly portable ASCII assembly language format.
		- Assembly language should be easy to learn and write and require few optimization tricks.
		- Assembly source should be compiled to internal representation immediately prior to execution.
		- Compile-time calculations and conditions should be supported.
		- Instruction set should be sufficiently advanced to allow for C-like languages.
		- Run-time should be sandboxed, 100% safe and interruptable.
		- Run-time should allow for cooperative multi-tasking within a single OS thread.
		- Should be possible to suspend and resume full machine state.
		- Any future changes to the assembler format should be backwards compatible.
		- Implementation should be portable and CPU-agnostic using standard C/C++.
		- Should be multi-thread safe (no global variables).
		- Easy to interface with C/C++ functions.
		- Compact and documented single C++ file and header.

	Non-goals

		- Run-time memory foot-print does not have to be small.
		- No need for a compact persistent code format.
		- Data types of different sizes and precisions are not necessary.
		- No need to support high-level language concepts like dynamic memory management, exceptions etc

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

#ifndef GAZL_h
#define GAZL_h

#include "assert.h"																										// Note: I always include assert.h like this so that you can override it with a "local" file.
#include <map>
#include <vector>
#include <string>

namespace GAZL {

#if !defined(GAZL_CHECK_INT_DIVS_BY_ZERO)
	#define GAZL_CHECK_INT_DIVS_BY_ZERO 1
#endif

#if !defined(GAZL_CHECK_FLOAT_DIVS_BY_ZERO)
	#define GAZL_CHECK_FLOAT_DIVS_BY_ZERO 1
#endif

// TODO : support unicode source
typedef char Char;
typedef int Int;
typedef unsigned int UInt;
typedef unsigned int Pointer;																							// Pointer must be unsigned and the same size as Int
typedef float Float;
typedef Int Status;																										// Run-time status code

const int VERSION = 1;
const int WORD_SIZE = 32;
const Pointer MEMORY_OFFSET = 0x12345678;																				// All memory pointers in GAZL are offsetted by this amount (thus the address of the first memory word is not zero). This makes it easier to detect invalid memory operations (such as writing to a null-pointer).
const Pointer IP_OFFSET = 0x56789ABC;																					// All instruction / function pointers in GAZL are offsetted by this amount (thus the address of the first instruction is not zero). This makes it easier to detect invalid function calls (such as performing a function call on a null-pointer).
const Pointer NULL_POINTER = 0;

union Value {
	Int i;
	Float f;
	Pointer p;
};

struct Instruction {
	Int opcode;
	Value p0;
	Value p1;
	Value p2;
};

enum AssemblerError {
	DATA_SECTION_FULL = 0
	, DATA_SECTION_MISSING = 1
	, DID_NOT_EXPECT_ADDRESS = 2
	, DID_NOT_EXPECT_CONSTANT = 3
	, DID_NOT_EXPECT_CONSTANT_FLOAT = 4
	, DID_NOT_EXPECT_CONSTANT_INT = 5
	, EXPECTED_END_OF_LINE_OR_COMMENT = 6
	, FAIL_DIRECTIVE = 7
	, FORWARD_SYMBOL_NOT_FOUND = 8
	, INCOMPATIBLE_COMPILE_TIME_VARIABLE_TYPE = 9
	, INCOMPATIBLE_TYPES = 10
	, INVALID_COMPILE_TIME_VARIABLE = 11
	, INVALID_IDENTIFIER = 12
	, INVALID_INSTRUCTION = 13
	, INVALID_MNENOMIC = 14
	, INVALID_NATIVE_LITERAL = 15
	, INVALID_NUMERICAL_LITERAL = 16
	, MISSING_COMPILE_TIME_LABEL = 17
	, MISSING_FUNCTION_DECLARATION = 18
	, MISSING_INSTRUCTION = 19
	, MISSING_OPERAND = 20
	, MISSING_RETURN_INSTRUCTION = 21
	, MUST_DEFINE_LOCALS_FIRST = 22
	, NEGATIVE_VALUE_NOT_ACCEPTED = 23
	, NON_FORWARD_SYMBOL_NOT_FOUND = 24
	, NOT_ENOUGH_CODE_SPACE = 25
	, NOT_ENOUGH_MEMORY_SPACE = 26
	, OFFSET_OUT_OF_BOUNDS = 27
	, SYMBOL_ALREADY_DEFINED = 28
	, UNKNOWN_NATIVE_FUNCTION = 29
	, CONSTANT_DIVISION_BY_ZERO = 30
	, EXPECTED_CONSTANT = 31
	, ASSEMBLER_ERROR_COUNT = 32
};

extern const char* ASSEMBLER_ERROR_TEXTS[];
/**
Exception thrown by the assembler.

Contains the error code and an optional detail string
referencing the offending source.
**/

class Exception : public std::exception {
	public:		Exception(AssemblerError error);
	public:		Exception(AssemblerError error, const Char* b, const Char* e);
	public:		Exception(AssemblerError error, const std::string& detail);
	public:		virtual const char* what() const throw();
	public:		virtual ~Exception() throw();
	public:		AssemblerError error;
	public:		std::string detail;
	protected:	mutable std::string whatString;
};
inline Exception::Exception(AssemblerError error) : error(error) { assert(0 <= error && error < ASSEMBLER_ERROR_COUNT); }
inline Exception::Exception(AssemblerError error, const Char* b, const Char* e) : error(error), detail(b, e) { assert(0 <= error && error < ASSEMBLER_ERROR_COUNT); }
inline Exception::Exception(AssemblerError error, const std::string& detail) : error(error), detail(detail) { assert(0 <= error && error < ASSEMBLER_ERROR_COUNT); }
inline Exception::~Exception() throw() { }																				// (GCC requires explicit destructor with one that has throw().)

/**
Symbol table shared between assembler and processor.

Stores function and global definitions, constants and
manages forward references during assembly.
**/
class Symbols {
	friend class Assembler;
	protected:	struct Symbol {
					Symbol(int types, Value value, UInt size) : types(types), value(value), size(size) { }
					int types;
					Value value;
					UInt size;
				};

	protected:	struct Reference {
					Reference(const std::string& label, Value* storage, int accepts, Int offset
							, UInt switchSize)
							: label(label), storage(storage), accepts(accepts), offset(offset)
							, switchSize(switchSize) { }
					std::string label;
					Value* storage;
					int accepts;
					Int offset;
					UInt switchSize;
				};
	
	protected:	typedef std::map<std::string, Symbol> SymbolMap;
	public:		typedef SymbolMap::const_iterator Iterator;
				
	public:		void registerNative(const Char* name, Int index);
	public:		Pointer findFunction(const Char* name) const;															/// Returns `NULL_POINTER` if not found.
	public:		Pointer findGlobal(const Char* name, UInt& size) const;													/// Returns `NULL_POINTER` if not found (in which case, `size` is left untouched).
	public:		void defineConstant(const Char* name, bool asFloat, const Value& value);
	public:		bool lookupConstant(const Char* name, bool* isFloat, Value* value) const;	// FIX : why * isFloat here, when we use & at other places
	public:		bool findFirstGlobal(Iterator& iterator, bool includeTemps) const;
	public:		bool findNextGlobal(Iterator& iterator, bool includeTemps) const;
	public:		const char* getGlobalInfo(const Iterator& iterator, bool& isTemp, Pointer& address, UInt& size) const;

	// TODO : all these below could take std::string&, would be more consistent
	protected:	bool lookup(const Char* nameBegin, const Char* nameEnd, int acceptedTypes, int& types, Value& value, UInt& size) const;
	protected:	void define(const std::string& name, int types, Value value, UInt size = 1);
	protected:	void link(const Char* labelBegin, const Char* labelEnd, Value* storage, int accepts, Int offset = 0); // FIX : name?
	protected:	void registerSwitch(const Char* labelBegin, const Char* labelEnd, UInt switchSize, Value* storage, Int offset = 0);
	protected:	void resolveForwardRefs();
	protected:	void clear() { symbols.clear(); forwardRefs.clear(); }
	protected:	void resolve(const Reference& ref, const Symbol& symbol);
	protected:	SymbolMap symbols;						// TODO : try a C variation with a sorted array (fixed size strings) and bsearch or alternatively a hash table (nick the one from NuXScript)
	protected:	std::vector<Reference> forwardRefs;		// TODO : move this to a Linker class (or some better name)? in assembler, the methods herein that needs forwardRefs only lookup things in Symbols so they can use lookup()
};

struct Operator;
/**
Parses GAZL source code and emits executable data.

Maintains symbol tables and compile-time variables while
converting assembly text to a binary representation.
**/
class Assembler {
	friend class Symbols;
	public:		Assembler(UInt maxCodeSize, Instruction* codeBase, UInt maxMemorySize, Value* memoryBase, Symbols& globals); // Create an assembler for the provided buffers.
	public:		void newUnit(const Char* unitName); // Begin assembling a new source unit.
	public:		const Char* feed(const Char* line); // Assemble a single line and return pointer to the next.
	public:		void finalize(UInt& codeSize, UInt& globalsSize, UInt& constsSize); // Finish assembly and report memory usage.
	
	protected:	struct CompileTimeVar {
					int types;
					Value value;
				};

	protected:	void declare(Symbols& where, const Char* labelBegin, const Char* labelEnd, int types, Value value, UInt size = 1);
	protected:	static int operatorCompare(const void* a, const void* b);
	protected:	static int nativeDefinitionCompare(const void* a, const void* b);
	protected:	static Char parseOperandType(const Char* b, const Char* e);
	protected:	void linkWithOffset(Symbols& defs, const char* b, const char* e, int accepts, Value* v);
	protected:	void parseConstant(const Char* b, const Char* e, int accepts, Value* v);
	protected:	void parseOperand(const Char* b, const Char* e, int accepts, Value* v);
	protected:	bool doConstantBranch(const Operator* op, const Char* op0Begin, const Char* op0End
						, const Char* op1Begin, const Char* op1End);
	protected:	Value calcConstant(const Operator* op, const Char* op1Begin, const Char* op1End, const Char* op2Begin
						, const Char* op2End);
	protected:	void finalizeFunction();
	protected:	Instruction* const codeBase;
	protected:	Instruction* const codeEnd;
	protected:	Value* const memoryBase;
	protected:	Value* const memoryEnd;
	protected:	Instruction* ip;
	protected:	Instruction* functionStart;
	protected:	UInt localsSize;
	protected:	UInt paramsSize;
	protected:	Value* globalsPointer;
	protected:	Value* constantsPointer;
	protected:	std::string dataLabel; // Only for error display.
	protected:	int dataLabelType;
	protected:	Value* dataPointer;
	protected:	Value* dataEnd;
	protected:	Symbols& globals;
	protected:	Symbols locals;
	protected:	CompileTimeVar compileTimeVars[128];
	protected:	std::string skipUntilLabel;
	
	private:	Assembler(const Assembler&); // N/A
	private:	Assembler& operator=(const Assembler&); // N/A
};

class Processor;

typedef Status (*NativeFunc)(Processor* processor);

struct CallStackEntry {
	const Instruction* ip;
	Value* dsp;					// Zero here means return to native function, pop previous stack entry for true dsp (ip is identical).
};

// Feel free to define your own run-time status codes above 0.
enum {
	OK = 0						// If returned from `run()` this means the virtual process has returned normally to the native caller.
	, TIME_OUT = -1				// The clock cycle limit has been reached.
	, BAD_PEEK = -2				// Trying to PEEK an address outside of memory (or GETL outside stack).
	, BAD_POKE = -3				// Trying to POKE an address outside of memory (or SETL outside stack).
	, BAD_CALL = -4				// Trying to call a function (through a function pointer) that doesn't begin with a FUNC opcode.
	, DATA_STACK_OVERFLOW = -5	// Data stack overflow (using too much local data)
	, IP_STACK_OVERFLOW = -6	// Instruction pointer stack overflow (recursive call limit reached)
	, DIVISION_BY_ZERO = -7		// Run-time division or modulo by zero.
	, ACCESS_VIOLATION = -8		// Return from native function and COPY instruction when memory cannot be accessed.
	, ABORTED = -9				// Return from native function for unexpected termination (e.g. assertion failure etc).
	, TERMINATED = -10			// Return from native function for expected termination (e.g. exit() function).
};

/*
						 +-----------------------+ <- memory pointer
						 | Globals   | RW Memory |
						 |           |           |
	  dataStackOffset -> |-----------|           |
					     | Data      |           |
					     | Stack     |           |
      + dataStackSize -> |-----------|           |
						 | (Data     |           |
						 | Stack 2)  |           |
						 |-----------|-----------| <- rwMemorySize
						 |           | Read Only |
						 | Constants | Memory    |
						 +-----------------------+ <- memorySize

                         +--------+  +----------+  +-----------+
						 |  Code  |  | IP Stack |  | Natives   |
						 |        |  | (Calls)  |  | Functions |
						 |        |  |          |  |           |
                         +--------+  +----------+  +-----------+
						 
						 
						 +------------+
						 | Parameters | <-- Frame #1 (caller)
						 |            |
						 |------------|
						 | Locals     |
						 |            |
						 |------------|
						 | Transients |
						 | +------------+
						 | | Parameters | <-- Frame #2 (callee)
						 +-|----------+ |
					       |------------|
						   | Locals     |
						   |            |
						   |------------|
						   | Transients |
						   |            |
						   +------------+
*/

// Processor is copyable.
/**
Executes compiled bytecode using an internal stack-based VM.

The processor owns memory and call stacks and provides
helper functions for interacting with native code.
**/
class Processor {
	public:		Processor(); // Default-initialized processor. It's illegal to call any methods on a default constructed processor.
	public:		Processor(UInt codeSize, const Instruction* code, UInt memorySize, Value* memory, UInt globalsSize
						, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* natives
						, void* userData = 0);	// higher level routine, data stack is full space between globals and constants
	public:		Processor(UInt codeSize, const Instruction* code, UInt memorySize, Value* memory, UInt rwMemorySize
						, UInt dataStackOffset, UInt dataStackSize, UInt ipStackSize, CallStackEntry* ipStack
						, NativeFunc const* natives, void* userData = 0);	// lower level routine, useful for running multiple processors on the same code (i.e. threads) where each processor needs its own stack
	public:		void resetTimeOut(Int clockCycles); // It is allowed to use `resetTimeOut(0)` from a native call to make the processor return immediately before executing it's next instruction. Alternatively if you want to suspend the processor from a native call, but retry the call when resumed (e.g. simulating a blocking call), return non-zero from the native call and the instruction pointer will not be incremented.
	public:		const Value* accessConstMemory(Pointer pointer, UInt count) const; // If returning null pointer you should normally return `ACCESS_VIOLATION`
	public:		Value* accessMemory(Pointer pointer, UInt count) const; // If returning null pointer you should normally return `ACCESS_VIOLATION`
	public:		Value* accessParams(UInt count) const; // If returning null pointer you should normally return `DATA_STACK_OVERFLOW`
	// FIX : stack alloc function
	public:		Status enterCall(Pointer functionPointer); // After `enterCall()`, call `run()` (and on time out, repeatedly call `run()` until it returns OK). It is ok to call `enterCall()` at any time, current instruction pointer and stack is pushed and popped as expected which makes `enterCall()` double as a mean to issue interrupts.
	public:		Status run();
	public:		void* getUserData() const;
	public:		int getClockCyclesLeft() const;
	
	protected:	UInt codeSize;
	protected:	const Instruction* codeBase;
	protected:	UInt memorySize;
	protected:	Value* memoryBase;
	protected:	UInt rwMemorySize;
	protected:	Value* dataStackBase;
	protected:	Value* dataStackEnd;
	protected:	CallStackEntry* ipStackBase;
	protected:	CallStackEntry* ipStackEnd;
	protected:	NativeFunc const* natives;
	protected:	Int clockCyclesLeft;
	protected:	const Instruction* ip;
	protected:	Value* dsp;
	protected:	CallStackEntry* ipsp;
	protected:	void* userData;
};

inline void Processor::resetTimeOut(Int clockCycles) { clockCyclesLeft = clockCycles; }
inline int Processor::getClockCyclesLeft() const { return clockCyclesLeft; }

inline const Value* Processor::accessConstMemory(Pointer pointer, UInt count) const {
	assert(memoryBase != 0);
	return (count < memorySize && pointer - MEMORY_OFFSET <= memorySize - count)
			? pointer - MEMORY_OFFSET + memoryBase : 0;
}

inline Value* Processor::accessMemory(Pointer pointer, UInt count) const {
	assert(memoryBase != 0);
	return (count < rwMemorySize && pointer - MEMORY_OFFSET <= rwMemorySize - count)
			? pointer - MEMORY_OFFSET + memoryBase : 0;
}

inline Value* Processor::accessParams(UInt count) const {
	assert(dsp != 0);
	assert(dsp >= dataStackBase);
	return (dsp + count <= dataStackEnd ? dsp : 0);
}

inline void* Processor::getUserData() const { return userData; }

#if !defined(NDEBUG)
bool unitTest();
#endif

}

#endif
