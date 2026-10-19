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
	A reference executor for assembled GAZL: the oracle that the interpreter and the JIT are compared against.

	It is written for being obviously right, not fast. It shares nothing with `Processor::run()` but the instruction
	encoding the assembler produces (`src/GAZLOpcodes.h` and the operand layout of each opcode); the meaning of each
	instruction comes from docs/gazl/InstructionSet.md, spelled out in the plainest arithmetic:

	  - integer results wrap at 32 bits, shift counts are taken mod 32, `INT_MIN / -1` is `INT_MIN` and
	    `INT_MIN % -1` is 0; `FTOi` truncates and saturates, and NaN gives 0
	  - floats are IEEE-754 binary32 computed with integer operations only, so no compiler or FPU setting can change
	    them: rounding to nearest even, and denormals kept or flushed as `DenormalMode` says. `DIVf` by zero gives an
	    infinity or a NaN rather than an error. An arithmetic NaN is always 0x7FC00000, which matches an interpreter
	    built with GAZL_CANONICAL_NAN; the specification leaves NaN bits unspecified, so a caller compares against
	    such a build
	  - an address is `base + index` mod 2^32. The constants sit just below `MEMORY_OFFSET`, the globals and the
	    data stack from it up, so a read reaches the constants too and a write only from `MEMORY_OFFSET` up; the
	    zero guard after the data stack is out of reach of both. GETL / SETL index a local array and reach as far
	    as the end of the data stack
	  - every instruction costs one unit of fuel, the one that runs out included, as `Processor::run()` charges

	Where the interpreter trusts the assembler (a frame slot inside the data stack, a constant address inside
	memory, a branch target inside the code), this checks instead and stops with REFERENCE_VIOLATION and a message,
	so a broken assumption shows up as a difference rather than as memory corruption.
*/

#ifndef GAZLReference_h
#define GAZLReference_h

#include <stdint.h>
#include <string>
#include "../src/GAZL.h"

namespace GAZLReference {

using namespace GAZL;

const Status REFERENCE_VIOLATION = -1000;			// not a GAZL status: the program broke an assembler guarantee

class Machine;

// How float arithmetic treats denormals (docs/gazl/Specification.md section 4).
enum DenormalMode {
	KEEP_DENORMALS,				// gradual underflow, the standard
	FLUSH_AFTER_ROUNDING,		// flush-to-zero; a result that rounds up to the smallest normal float is kept (x86)
	FLUSH_BEFORE_ROUNDING		// flush-to-zero; that result is flushed too (ARM by default)
};

// This module's binary32 arithmetic against the host's own, which the caller has set to `mode`.
int64_t countFloatMismatches(int64_t caseCount, DenormalMode mode);

/*
	A native function as the reference calls it: `window[0]` is the result, `window[1..]` the arguments, and `room`
	the number of words from `window` to the end of the data stack.
*/
typedef Status (*Native)(Machine& machine, Value* window, UInt room);

/*
	The state of one reference run over one program. Memory is the caller's, laid out as `Processor`'s
	higher-level constructor lays it out: the constants, the globals, the data stack, then the zero guard.
*/
class Machine {
	public:
		Machine(const AssembledProgram& program, UInt ipStackSize, const Native* natives, DenormalMode denormalMode);
		Status call(Pointer function, int64_t startFuel);		// runs `function` to its return, a fault, or the end of `startFuel`
		int64_t fuelLeft() const;
		Value* accessMemory(Pointer pointer, UInt count);		// [pointer, pointer + count) inside writable memory, or 0
		const std::string& getViolation() const;				// why the last run stopped with REFERENCE_VIOLATION

	private:
		struct Frame {
			int64_t ip;
			int64_t dsp;
		};

		Status run(Pointer function);
		Value& stackWord(int64_t index);
		Value& slot(const Value& operand);
		int64_t readWord(Pointer pointer) const;				// the word `pointer` reads; readable below `readableWords`
		int64_t writeOffset(Pointer pointer) const;			// `pointer` past `MEMORY_OFFSET`; writable below `writableWords`
		int64_t constantReadWord(Pointer pointer) const;
		int64_t constantWriteWord(Pointer pointer) const;
		int64_t localArrayWord(const Value& arraySlot, Int index) const;	// -1 when `index` is outside the array's reach
		int64_t functionEntry(UInt ordinal) const;
		UInt floatOperand(const Value& operand) const;			// its bits, a denormal read as zero under flush-to-zero

		const Instruction* const code;
		const UInt codeSize;
		const UInt* const functionTable;
		const UInt functionCount;
		Value* const memory;
		const UInt constantWords;								// the words below `MEMORY_OFFSET`, at the start of `memory`
		const UInt readableWords;								// every word but the zero guard
		const UInt writableWords;								// the globals and the data stack
		const UInt stackBase;
		const UInt stackEnd;
		const UInt ipStackSize;
		const Native* const natives;
		const DenormalMode denormalMode;
		int64_t fuel;
		int64_t ip;
		int64_t dsp;
		std::string violation;
};

}																														// namespace GAZLReference

#endif
