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

/*
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

*/

#ifndef GAZL_h
#define GAZL_h

#include "assert.h"																										// Note: I always include assert.h like this so that you can override it with a "local" file.
#include <map>
#include <vector>
#include <string>

namespace GAZL {

typedef char Char;
typedef int Int;
typedef unsigned int UInt;
typedef unsigned int Pointer;																							// Pointer must be unsigned and the same size as Int
typedef unsigned int Target;																							// A call target: `FUNCTION_OFFSET` + a function's ordinal. Unsigned and the same size as Int, like Pointer.
typedef float Float;
typedef Int Status;																										// Run-time status code

const int VERSION = 2;						// What 2 adds is docs/gazl/Specification.md section 12. A program requires it with `! GEQi #GAZL_VERSION #2 @label`.
const int WORD_SIZE = 32;
const Pointer MEMORY_OFFSET = 0x12345678;																				// All memory pointers in GAZL are offsetted by this amount (thus the address of the first memory word is not zero). This makes it easier to detect invalid memory operations (such as writing to a null-pointer).
const Target FUNCTION_OFFSET = 0x56789ABC;																				// All function pointers in GAZL are offsetted by this amount (thus the ordinal of the first function is not zero). A function pointer is an ordinal indexing `functionTable`, not a code address.
const Pointer NULL_POINTER = 0;
const Target NULL_TARGET = 0;

union Value {
	Int i;
	Float f;
	Pointer p;
	Target t;
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
	, NOT_ENOUGH_FUNCTION_SPACE = 32
	, LABEL_ON_FUNCTION = 33
	, FRAME_TOO_LARGE = 34
	, NON_ASCII_TEXT = 35
	, UNNAMED_DATA_SECTION = 36
	, UNBALANCED_MARK = 37
	, OVERLAPPING_DATA_REGIONS = 38
	, UNSUPPORTED_GAZL_VERSION = 39
	, UNCLOSED_GAZL_REGION = 40
	, ASSEMBLER_ERROR_COUNT = 41
};

extern const char* ASSEMBLER_ERROR_TEXTS[];

// Thrown by the assembler; `detail` quotes the offending source when there is one.
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

enum DataSection {
	GLOB_SECTION				// writable, and saved by freezing
	, TEMP_SECTION				// writable, but left out of freezing
	, CNST_SECTION				// read-only
};

// What `Symbols::getGlobalInfo` reports about one name of data: a section's label, or a label on a data statement.
struct GlobalInfo {
	const char* name;				// valid as long as the `Symbols` it came from
	Pointer address;
	UInt declaredWords;				// what the source says: a section's `*n`, or the values on the labelled statement
	UInt ownedWords;				// to the next label or the section's end: what freezing saves under the name
	DataSection section;			// the kind of section it is declared in; freezing saves `GLOB_SECTION` only
};

enum ConstantKind {				// what `Symbols::lookupConstant` found
	INT_CONSTANT					// a `DEFi`, or an int from `defineConstant`
	, FLOAT_CONSTANT				// a `DEFf`, or a float from `defineConstant`
	, ADDRESS_CONSTANT				// a `DEFp` of data or of `NULL`, or `NULL`
	, TARGET_CONSTANT				// a `DEFt` or `DEFp` of a function; read `value.t`
};

// A program's global names: functions, natives, data labels and named constants.
class Symbols {
	friend class Assembler;
	/*
		One name: in `globals` a data label, function, native or named constant; in `locals` a parameter, local or
		branch label of the current function.
	*/
	protected:	struct Symbol {
					Symbol(int types, Value value, UInt words) : types(types), value(value), declaredWords(words)
							, ownedWords(words), minOffset(0), maxOffset(static_cast<Int>(words) - 1) { }
					int types;																			// What kind of name it is, matched against the types an operand accepts.
					Value value;																		// What the name stands for: an address, a function or native, a constant, a frame word or a code position.
					UInt declaredWords;																	// What the source says: a section's `*n`, the values on a labelled data statement, or a local's size.
					UInt ownedWords;																	// For data, its words to the next label or the section's end: what freezing saves.
					Int minOffset;																		// The lowest `k` a `:k` on it may take: for data, down to its section's first word.
					Int maxOffset;																		// The highest `k`: for data, up to its section's last word. Locals are checked against the frame instead.
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
	public:		Target findFunction(const Char* name) const;															// Returns `NULL_TARGET` if not found.
	public:		Pointer findGlobal(const Char* name, UInt& declaredWords) const;										// Returns `NULL_POINTER` if not found (in which case, `declaredWords` is left untouched).
	public:		void defineConstant(const Char* name, bool asFloat, const Value& value);
	public:		bool lookupConstant(const Char* name, ConstantKind& kind, Value& value) const;							// A named constant only, not data or a function. Returns false if `name` is none.
	public:		bool findFirstGlobal(Iterator& iterator) const;															// Every name of data, `GLOB`, `TEMP` and `CNST` alike, in ascending order (see `GlobalInfo`).
	public:		bool findNextGlobal(Iterator& iterator) const;
	public:		GlobalInfo getGlobalInfo(const Iterator& iterator) const;
	public:		bool getGlobalInfo(const Char* name, GlobalInfo& info) const;													// Returns false, with `info` cleared, if `name` is not data.

	protected:	bool lookup(const Char* nameBegin, const Char* nameEnd, int acceptedTypes, int& types, Value& value) const;
	protected:	Symbol* define(const std::string& name, int types, Value value, UInt words = 1);
	protected:	void link(const Char* labelBegin, const Char* labelEnd, Value* storage, int accepts, Int offset = 0);
	protected:	void registerSwitch(const Char* labelBegin, const Char* labelEnd, UInt switchSize, Value* storage, Int offset = 0);
	protected:	void resolveForwardRefs();
	protected:	void clear() { symbols.clear(); forwardRefs.clear(); }
	protected:	void resolve(const Reference& ref, const Symbol& symbol);
	protected:	SymbolMap symbols;
	protected:	std::vector<Reference> forwardRefs;
};

struct Operator;

/*
	The four sizes an assembly computes: what a program actually used, from `finalize`. `constsSize` counts the
	constants plus the one-word zero guard at the end of memory, so `globalsSize + constsSize` is the least memory
	a program needs before its data stack.
*/
struct ProgramSizes {
	UInt codeSize;
	UInt functionCount;
	UInt globalsSize;
	UInt constsSize;
};

// Assembles GAZL source, line by line, into the code, function-table and memory buffers the caller provides.
class Assembler {
	friend class Symbols;
	public:		Assembler(UInt maxCodeSize, Instruction* codeBase, UInt maxFunctionCount, UInt* functionTable, UInt maxMemorySize, Value* memoryBase, Symbols& globals); // Create an assembler for the provided buffers. `functionTable` maps each function's ordinal to its code offset (see `finalize`).
	public:		void newUnit(const Char* unitName);																		// Begin assembling a new source unit.
	public:		const Char* feed(const Char* line); 																	// Assemble a single line and return pointer to the next.
	public:		void finalize(ProgramSizes& sizes); 																	// Finish assembly and report memory usage. `sizes.functionCount` is the number of entries filled in `functionTable`.

	protected:	struct CompileTimeVar {
					int types;
					Value value;
				};

	protected:	Symbols::Symbol* declare(Symbols& where, const Char* labelBegin, const Char* labelEnd, int types, Value value
						, UInt words = 1);
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
	protected:	void threadBranches();		// Collapse GOTO chains and turn a GOTO onto a RETU into the RETU. In place: no address moves.
	protected:	void closeDataRegion();		// Record the closing region's claim and check it against the section's earlier regions.
	protected:	void declareData(const Char* labelBegin, const Char* labelEnd, const Value* first, UInt declaredWords);	// Declare a data label at `first`; what it owns is decided when the section closes.
	protected:	void closeDataSection();																				// Give each of the section's labels the words it owns, by address.
	protected:	static bool lowerAddress(const Symbols::Symbol* a, const Symbols::Symbol* b);							// Orders data labels by address.
	protected:	Pointer makeGAZLPointer(const Value* data, bool constant) const;										// The pointer to `data`: up from the base for globals, down from it for constants.
	protected:	Instruction* const codeBase;
	protected:	Instruction* const codeEnd;
	protected:	const UInt maxFunctionCount;
	protected:	UInt* const functionTable;																				// Maps function ordinal -> code offset (index into `codeBase`).
	protected:	Value* const memoryBase;
	protected:	Value* const memoryEnd;
	protected:	Instruction* ip;
	protected:	Instruction* functionStart;
	protected:	UInt functionCount;					// Running ordinal; assigned to each `FUNC` in declaration order.
	protected:	UInt localsSize;					// Running bump, rewound by FREE; the final frame size is `maxLocalsSize`.
	protected:	UInt maxLocalsSize;					// Frame high-water mark: the peak over nesting chains, not the sum.
	protected:	UInt markDepth;
	protected:	enum { MAX_MARK_DEPTH = 32 };
	protected:	UInt markStack[MAX_MARK_DEPTH];
	protected:	UInt paramsSize;
	protected:	Value* globalsPointer;
	protected:	Value* constantsPointer;
	protected:	std::string dataLabel;																					// Only for error display.
	protected:	int dataLabelType;
	protected:	Value* dataPointer;
	protected:	Value* dataEnd;						// Fence of the current SPAN region; `sectionEnd` for the implicit one.
	protected:	std::vector<Symbols::Symbol*> dataLabels;																// The current section's labels, in source order.
	protected:	bool dataIsConstant;																					// The current data section is a `CNST` one, placed below the base.
	protected:	Value* sectionBegin;				// The current data section's own range; SPAN offsets are relative to `sectionBegin`.
	protected:	Value* sectionEnd;
	protected:	Int regionStart;					// Current region as section offsets; extent -1 = unbounded, claims only what it writes.
	protected:	Int regionExtent;
	protected:	int dialect;						// `GAZL #n` sets it; 1 outside any region, so every existing file keeps its meaning. Must be 1 again at finalize.
	protected:	std::vector< std::pair<Int, Int> > dataRegions;	// Closed regions' claims, per section. Disjointness = no word initialized twice.
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
	What a `Processor` runs in: the labels are its constructor's parameters, and the frames below are the call frames
	`run()` builds.

						 +-----------------------+ <- memory pointer
						 | Constants | Read Only |
						 |           | Memory    |
						 |-----------|-----------| <- MEMORY_OFFSET, the base: constants below, the rest above
						 | Globals   | RW Memory |
						 |           |           |
	  dataStackOffset -> |-----------|           |
					     | Data      |           |
					     | Stack     |           |
      + dataStackSize -> |-----------|           |
						 | (Data     |           |
						 | Stack 2)  |           |
						 |-----------|-----------| <- base + rwMemorySize
						 | Zero guard            |
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
						 +-|------------|
						   | Locals     |
						   |            |
						   |------------|
						   | Transients |
						   |            |
						   +------------+
*/

// Processor is copyable.
class Processor {
	public:		Processor(); 																							// Default-initialized processor. It's illegal to call any methods on a default constructed processor.
	public:		Processor(UInt codeSize, const Instruction* code, UInt functionCount, const UInt* functionTable
						, UInt memorySize, Value* memory, UInt globalsSize, UInt constsSize, UInt ipStackSize
						, CallStackEntry* ipStack, NativeFunc const* natives, void* userData = 0);						// higher level routine, data stack is all the space between the globals and the zero guard
	public:		Processor(UInt codeSize, const Instruction* code, UInt functionCount, const UInt* functionTable
						, UInt memorySize, Value* memory, UInt constsSize, UInt rwMemorySize, UInt dataStackOffset
						, UInt dataStackSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* natives
						, void* userData = 0);																			// lower level routine, useful for running multiple processors on the same code (i.e. threads) where each processor needs its own stack; `rwMemorySize` and `dataStackOffset` count from the base
	public:		void resetTimeOut(Int clockCycles); 																	// It is allowed to use `resetTimeOut(0)` from a native call to make the processor return immediately before executing its next instruction. Alternatively if you want to suspend the processor from a native call, but retry the call when resumed (e.g. simulating a blocking call), return non-zero from the native call and the instruction pointer will not be incremented.
	public:		const Value* accessConstMemory(Pointer pointer, UInt count) const; 										// If returning null pointer you should normally return `ACCESS_VIOLATION`
	public:		Value* accessMemory(Pointer pointer, UInt count) const; 												// If returning null pointer you should normally return `ACCESS_VIOLATION`
	public:		Value* accessParams(UInt count) const; 																	// If returning null pointer you should normally return `DATA_STACK_OVERFLOW`
	public:		Status enterCall(Target target);																// After `enterCall()`, call `run()` (and on time out, repeatedly call `run()` until it returns OK). It is ok to call `enterCall()` at any time, current instruction pointer and stack is pushed and popped as expected which makes `enterCall()` double as a mean to issue interrupts.
	public:		Value* pushCall(Target target);																// From inside a native callback only: when the native returns OK, the function runs as if the `^native` call had called it. Returns its argument window ([0] = result), or 0 on error. Several pushes run last-pushed first and share that one window.
	public:		Status run();
	public:		void* getUserData() const;
	public:		int getClockCyclesLeft() const;
	
	protected:	UInt codeSize;
	protected:	const Instruction* codeBase;
	protected:	UInt functionCount;
	protected:	const UInt* functionTable;																				// Maps function ordinal -> code offset; a function pointer is `FUNCTION_OFFSET + ordinal`.
	protected:	UInt memorySize;																						// One less than the memory given: the last word is the assembler's zero guard, so no checked read reaches it.
	protected:	Value* memoryBase;																						// The lowest constant: the start of the memory given.
	protected:	UInt constantsSize;																						// Words below the base, `MEMORY_OFFSET`: the constants, without the zero guard.
	protected:	UInt readOffset;																						// `constantsSize - MEMORY_OFFSET`: a pointer plus this is its index from the lowest constant.
	protected:	UInt rwMemorySize;																						// Words from the base up: globals and data stack.
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
	const UInt index = pointer + readOffset;																			// the readable range starts at the lowest constant
	return (count <= memorySize && index <= memorySize - count) ? memoryBase + index : 0;
}

inline Value* Processor::accessMemory(Pointer pointer, UInt count) const {
	assert(memoryBase != 0);
	return (count <= rwMemorySize && pointer - MEMORY_OFFSET <= rwMemorySize - count)
			? memoryBase + constantsSize + (pointer - MEMORY_OFFSET) : 0;
}

inline Value* Processor::accessParams(UInt count) const {
	assert(dsp != 0);
	assert(dsp >= dataStackBase);
	return (count <= (UInt)(dataStackEnd - dsp) ? dsp : 0);
}

inline void* Processor::getUserData() const { return userData; }

// Frozen memory, docs/gazl/Specification.md section 11. Freeze and thaw only between `run()` calls.
enum MemoryLoad {
	MEMORY_OK = 0				// Every entry met an owner of the same size, and every owner had an entry.
	, MEMORY_BAD_MAGIC = 1		// Not frozen memory.
	, MEMORY_BAD_VERSION = 2	// Unknown format version, or a GAZL version this one cannot thaw.
	, MEMORY_BAD_WORDSIZE = 3	// Word size differs from this build.
	, MEMORY_BAD_CANARY = 4		// The check entry is missing or differs: the encoding was damaged or misread.
	, MEMORY_TRUNCATED = 5		// The header is incomplete, or a length runs past the end.
	, MEMORY_MALFORMED = 6		// A name is invalid or out of order, padding is not zero, or bytes follow the last entry.
	, MEMORY_MISMATCH = 7		// Restored as far as `prefix` allows, but entries and owners did not match exactly.
};

UInt freezeMemorySize(const Processor& processor, const Symbols& symbols);												// Exact byte count a freeze will occupy.
UInt freezeMemory(const Processor& processor, const Symbols& symbols, void* buffer, UInt bufferSize);					// Writes the frozen memory; returns its size (writes nothing and returns the needed size if `bufferSize` is too small).
MemoryLoad thawMemory(Processor& processor, const Symbols& symbols, const void* buffer, UInt bufferSize
		, bool prefix);																								// Restores nothing unless all of `buffer` is valid. If `prefix`, an owner and an entry of different sizes share their common words.
Float parseFloat(const Char*& p, const Char* e);																		// Reads a float literal at `p`, advancing it, rounded to the nearest float, ties to even.

#if !defined(NDEBUG)
bool unitTest();
#endif

}

#endif
