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
	  - `DIVf` is IEEE division, so a zero divisor gives an infinity or a NaN rather than an error
	  - an address is `base + index` mod 2^32. The constants sit just below `MEMORY_OFFSET`, the globals and the
	    data stack from it up, so a read reaches the constants too and a write only from `MEMORY_OFFSET` up; the
	    zero guard after the data stack is out of reach of both. GETL / SETL index a local array and reach as far
	    as the end of the data stack
	  - every instruction costs one unit of fuel, the one that runs out included, as `Processor::run()` charges

	Where the interpreter trusts the assembler (a frame slot inside the data stack, a constant address inside
	memory, a branch target inside the code), this checks instead and stops with REFERENCE_VIOLATION and a message,
	so a broken assumption shows up as a difference rather than as memory corruption.

	Overlapping COPY is undefined in GAZL. It runs here as an ascending copy and marks the run unspecified, so a caller
	can compare status only.
*/

#ifndef GAZLReference_h
#define GAZLReference_h

#include <string>
#include "../src/GAZL.h"

namespace GAZLReference {

using namespace GAZL;

const Status REFERENCE_VIOLATION = -1000;			// not a GAZL status: the program broke an assembler guarantee

class Machine;

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
		Machine(const AssembledProgram& program, UInt ipStackSize, const Native* natives);
		Status call(Pointer function, long long startFuel);		// runs `function` to its return, a fault, or the end of `startFuel`
		Int fuelLeft() const;
		Value* accessMemory(Pointer pointer, UInt count);		// [pointer, pointer + count) inside writable memory, or 0
		const std::string& getViolation() const;				// why the last run stopped with REFERENCE_VIOLATION
		bool isUnspecified() const;								// the last run did something GAZL leaves undefined

	private:
		struct Frame {
			long long ip;
			long long dsp;
		};

		Status run(Pointer function);
		Value& stackWord(long long index);
		Value& slot(const Value& operand);
		long long readWord(Pointer pointer) const;				// the word `pointer` reads; readable below `readableWords`
		long long writeOffset(Pointer pointer) const;			// `pointer` past `MEMORY_OFFSET`; writable below `writableWords`
		long long constantReadWord(Pointer pointer) const;
		long long constantWriteWord(Pointer pointer) const;
		long long localArrayWord(const Value& arraySlot, Int index) const;	// -1 when `index` is outside the array's reach
		long long functionEntry(UInt ordinal) const;

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
		long long fuel;
		long long ip;
		long long dsp;
		std::string violation;
		bool unspecified;
};

}																														// namespace GAZLReference

#endif
