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

#include "assert.h"
#include <cmath>
#include <vector>
#include "GAZLReference.h"
#include "../src/GAZLOpcodes.h"

namespace GAZLReference {

// Adding a run-time opcode must break this build until the reference has a case for it.
typedef char FinalizedOpcodeCountCheck[(FINALIZED_OPCODE_COUNT == 93) ? 1 : -1];

// Thrown inside a run when the program breaks a guarantee the assembler gives the interpreter.
struct Violation {
	explicit Violation(const char* why) : why(why) { }
	const char* why;
};

static Int wrap(int64_t value) { return static_cast<Int>(static_cast<UInt>(value)); }

static Int floatToInt(Float value) {														// already scaled; truncates and saturates, NaN is 0
	if (value != value) {
		return 0;
	}
	if (static_cast<double>(value) >= 2147483648.0) {
		return 0x7FFFFFFF;
	}
	if (static_cast<double>(value) < -2147483648.0) {
		return static_cast<Int>(0x80000000u);
	}
	return static_cast<Int>(static_cast<int64_t>(value));
}

// Whether an opcode takes its first or second SOURCE operand as a constant: the `C` in `_VCV`, `_VVC` or `_CVB`.
static bool isFirstSourceConstant(int opcode) {
	switch (opcode) {
		case SUBI_VCV: case DIVI_VCV: case MODI_VCV: case SHLI_VCV: case SHRI_VCV: case SHRU_VCV:
		case SUBF_VCV: case DIVF_VCV:
		case LSSI_CVB: case NLSI_CVB: case LSSF_CVB: case NLSF_CVB:
			return true;
		default:
			return false;
	}
}

static bool isSecondSourceConstant(int opcode) {
	switch (opcode) {
		case ADDI_VVC: case SUBI_VVC: case MULI_VVC: case DIVI_VVC: case MODI_VVC: case ANDI_VVC: case IORI_VVC:
		case XORI_VVC: case SHLI_VVC: case SHRI_VVC: case SHRU_VVC:
		case ADDF_VVC: case SUBF_VVC: case MULF_VVC: case DIVF_VVC:
		case LSSI_VCB: case EQUI_VCB: case NLSI_VCB: case NEQI_VCB: case LSSF_VCB: case EQUF_VCB: case NLSF_VCB:
		case NEQF_VCB:
			return true;
		default:
			return false;
	}
}

/*
	Integer arithmetic in the plainest form: wrap at 32 bits, shift counts mod 32. `b` is not 0 for a division. Done in
	64 bits, `INT_MIN / -1` wraps back to `INT_MIN` and `INT_MIN % -1` is 0.
*/
static Int integerResult(int opcode, Int a, Int b) {
	const UInt shift = static_cast<UInt>(b) & 31;
	switch (opcode) {
		case ADDI_VVV: case ADDI_VVC: return wrap(static_cast<int64_t>(a) + b);
		case SUBI_VVV: case SUBI_VVC: case SUBI_VCV: return wrap(static_cast<int64_t>(a) - b);
		case MULI_VVV: case MULI_VVC: return wrap(static_cast<int64_t>(a) * b);
		case DIVI_VVV: case DIVI_VVC: case DIVI_VCV: return wrap(static_cast<int64_t>(a) / b);
		case MODI_VVV: case MODI_VVC: case MODI_VCV: return wrap(static_cast<int64_t>(a) % b);
		case ANDI_VVV: case ANDI_VVC: return a & b;
		case IORI_VVV: case IORI_VVC: return a | b;
		case XORI_VVV: case XORI_VVC: return a ^ b;
		case SHLI_VVV: case SHLI_VVC: case SHLI_VCV: return static_cast<Int>(static_cast<UInt>(a) << shift);
		case SHRI_VVV: case SHRI_VVC: case SHRI_VCV:											// a negative `a` shifts in ones
			return static_cast<Int>(a >= 0 ? static_cast<UInt>(a) >> shift : ~(~static_cast<UInt>(a) >> shift));
		default:
			assert(opcode == SHRU_VVV || opcode == SHRU_VVC || opcode == SHRU_VCV);
			return static_cast<Int>(static_cast<UInt>(a) >> shift);
	}
}

static Float floatResult(int opcode, Float a, Float b) {
	switch (opcode) {
		case ADDF_VVV: case ADDF_VVC: return a + b;
		case SUBF_VVV: case SUBF_VVC: case SUBF_VCV: return a - b;
		case MULF_VVV: case MULF_VVC: return a * b;
		default:
			assert(opcode == DIVF_VVV || opcode == DIVF_VVC || opcode == DIVF_VCV);
			return a / b;																	// IEEE: a zero divisor gives an infinity or a NaN
	}
}

static bool isBranchTaken(int opcode, const Value& a, const Value& b) {
	switch (opcode) {
		case LSSI_VVB: case LSSI_VCB: case LSSI_CVB: return a.i < b.i;
		case EQUI_VVB: case EQUI_VCB: return a.i == b.i;
		case NLSI_VVB: case NLSI_VCB: case NLSI_CVB: return !(a.i < b.i);
		case NEQI_VVB: case NEQI_VCB: return a.i != b.i;
		case LSSF_VVB: case LSSF_VCB: case LSSF_CVB: return a.f < b.f;
		case EQUF_VVB: case EQUF_VCB: return a.f == b.f;
		case NLSF_VVB: case NLSF_VCB: case NLSF_CVB: return !(a.f < b.f);					// true when either is NaN
		default:
			assert(opcode == NEQF_VVB || opcode == NEQF_VCB);
			return a.f != b.f;
	}
}

Machine::Machine(const AssembledProgram& program, UInt ipStackSize, const Native* natives)
		: code(program.code), codeSize(program.codeSize), functionTable(program.functionTable)
		, functionCount(program.functionCount), memory(program.memory), constantWords(program.constsSize - 1)
		, readableWords(program.memorySize - 1), writableWords(program.memorySize - program.constsSize)
		, stackBase(program.constsSize - 1 + program.globalsSize), stackEnd(program.memorySize - 1)
		, ipStackSize(ipStackSize), natives(natives), fuel(0), ip(0), dsp(0) {
	assert(program.constsSize >= 1 && program.globalsSize + program.constsSize <= program.memorySize);
}

Status Machine::call(Pointer function, int64_t startFuel) {
	fuel = startFuel;
	violation.clear();
	try {
		return run(function);
	}
	catch (const Violation& v) {
		violation = v.why;
		return REFERENCE_VIOLATION;
	}
}

Int Machine::fuelLeft() const { return static_cast<Int>(fuel); }

Value* Machine::accessMemory(Pointer pointer, UInt count) {
	const int64_t offset = writeOffset(pointer);
	return (offset + count <= writableWords ? &memory[constantWords + offset] : 0);
}

const std::string& Machine::getViolation() const { return violation; }

Value& Machine::stackWord(int64_t index) {
	if (index < stackBase || index >= stackEnd) {
		throw Violation("word outside the data stack");
	}
	return memory[index];
}

// A variable operand is a frame slot: `dsp` plus the operand. The assembler keeps it inside the frame.
Value& Machine::slot(const Value& operand) { return stackWord(dsp + operand.i); }

int64_t Machine::readWord(Pointer pointer) const {
	return static_cast<UInt>(pointer - MEMORY_OFFSET + constantWords);
}

int64_t Machine::writeOffset(Pointer pointer) const { return static_cast<UInt>(pointer - MEMORY_OFFSET); }

// A constant address (`&address` with no index) is checked at assembly; the reference checks it again.
int64_t Machine::constantReadWord(Pointer pointer) const {
	const int64_t word = readWord(pointer);
	if (word >= readableWords) {
		throw Violation("constant address outside memory");
	}
	return word;
}

int64_t Machine::constantWriteWord(Pointer pointer) const {
	const int64_t offset = writeOffset(pointer);
	if (offset >= writableWords) {
		throw Violation("constant address outside writable memory");
	}
	return constantWords + offset;
}

// GETL / SETL reach from the local array to the end of the data stack; a frame operand below the stack is a violation.
int64_t Machine::localArrayWord(const Value& arraySlot, Int index) const {
	const int64_t word = dsp + arraySlot.i + index;
	if (index < 0 || word >= stackEnd) {
		return -1;
	}
	if (word < stackBase) {
		throw Violation("local array below the data stack");
	}
	return word;
}

int64_t Machine::functionEntry(UInt ordinal) const {
	const UInt entry = functionTable[ordinal];
	if (entry >= codeSize || code[entry].opcode != FUNC_CC_) {
		throw Violation("call target is not a FUNC");
	}
	return entry;
}

Status Machine::run(Pointer function) {
	const UInt ordinal = static_cast<UInt>(function - FUNCTION_OFFSET);
	if (ordinal >= functionCount) {
		return BAD_CALL;
	}
	if (ipStackSize < 2) {																	// `enterCall` takes two entries: the caller and the return mark
		return IP_STACK_OVERFLOW;
	}
	std::vector<Frame> calls;																// the GAZL calls made since the entry, one entry each
	ip = functionEntry(ordinal);
	dsp = stackBase;
	for (;;) {
		--fuel;
		if (fuel < 0) {
			return TIME_OUT;
		}
		if (ip < 0 || ip >= codeSize) {
			throw Violation("instruction pointer outside the code");
		}
		const int opcode = code[ip].opcode;
		const Value& c0 = code[ip].p0;
		const Value& c1 = code[ip].p1;
		const Value& c2 = code[ip].p2;
		switch (opcode) {
			case FUNC_CC_: {																// C0 = locals, C1 = the deepest call window after them
				const int64_t frameWords = static_cast<int64_t>(static_cast<UInt>(c0.i)) + static_cast<UInt>(c1.i);
				if (frameWords > stackEnd - dsp) {
					return DATA_STACK_OVERFLOW;
				}
				dsp += static_cast<UInt>(c0.i);
				break;
			}
			case CALL_VVC:
			case CALL_CVC: {																// P0 = the function, C1 = the callee's window
				const UInt callee = static_cast<UInt>((opcode == CALL_VVC ? slot(c0).p : c0.p) - FUNCTION_OFFSET);
				if (callee >= functionCount) {
					return BAD_CALL;
				}
				if (calls.size() + 2 >= ipStackSize) {
					return IP_STACK_OVERFLOW;
				}
				const Frame frame = { ip, dsp };
				calls.push_back(frame);
				dsp += static_cast<UInt>(c1.i);
				ip = functionEntry(callee);
				continue;
			}
			case CALL_NVC: {																// C0 = the native's index, C1 = its window
				const int64_t window = dsp + static_cast<UInt>(c1.i);
				const int64_t fuelBefore = fuel;
				const Status status = natives[c0.i](*this, &stackWord(window), static_cast<UInt>(stackEnd - window));
				if (status != OK) {
					fuel = fuelBefore;														// a native that fails leaves the fuel as it found it
					return status;
				}
				break;
			}
			case RETU_C__: {
				if (calls.empty()) {
					return OK;																// back to the native caller
				}
				ip = calls.back().ip;
				dsp = calls.back().dsp;
				calls.pop_back();
				break;
			}
			case TAIL_CC_:
			case TAIL_VC_: {																// P0 = the function, C1 = window words, C2 = this frame's size
				const UInt callee = static_cast<UInt>((opcode == TAIL_VC_ ? slot(c0).p : c0.p) - FUNCTION_OFFSET);
				if (callee >= functionCount) {
					return BAD_CALL;
				}
				const int64_t base = dsp - static_cast<UInt>(c2.i);
				for (int64_t i = 0; i < static_cast<UInt>(c1.i); ++i) {					// ascending: the window slides down
					stackWord(base + i) = stackWord(dsp + i);
				}
				dsp = base;
				ip = functionEntry(callee);
				continue;
			}
			case MOVE_VV_: slot(c0) = slot(c1); break;
			case MOVE_VC_: slot(c0) = c1; break;
			case PEEK_VC_: slot(c0) = memory[constantReadWord(c1.p)]; break;
			case POKE_CV_: memory[constantWriteWord(c0.p)] = slot(c1); break;
			case POKE_CC_: memory[constantWriteWord(c0.p)] = c1; break;
			case PEEK_VVV:
			case PEEK_VCV: {																// P1 = the pointer, V2 = the index
				const Pointer base = (opcode == PEEK_VVV ? slot(c1).p : c1.p);
				const int64_t word = readWord(base + static_cast<UInt>(slot(c2).i));
				if (word >= readableWords) {
					return BAD_PEEK;
				}
				slot(c0) = memory[word];
				break;
			}
			case POKE_VVV:
			case POKE_CVV:
			case POKE_VVC:
			case POKE_CVC: {																// P0 = the pointer, V1 = the index, P2 = the value
				const Pointer base = (opcode == POKE_VVV || opcode == POKE_VVC ? slot(c0).p : c0.p);
				const Value value = (opcode == POKE_VVV || opcode == POKE_CVV ? slot(c2) : c2);
				const int64_t offset = writeOffset(base + static_cast<UInt>(slot(c1).i));
				if (offset >= writableWords) {
					return BAD_POKE;
				}
				memory[constantWords + offset] = value;
				break;
			}
			case GETL_VVV: {																// V0 = the result, C1 = the local array's slot, V2 = the index
				const int64_t word = localArrayWord(c1, slot(c2).i);
				if (word < 0) {
					return BAD_PEEK;
				}
				slot(c0) = memory[word];
				break;
			}
			case SETL_VVV:
			case SETL_VVC: {																// C0 = the local array's slot, V1 = the index, P2 = the value
				const Value value = (opcode == SETL_VVV ? slot(c2) : c2);
				const int64_t word = localArrayWord(c0, slot(c1).i);
				if (word < 0) {
					return BAD_POKE;
				}
				memory[word] = value;
				break;
			}
			case ADRL_VV_: slot(c0).p = static_cast<Pointer>(dsp + c1.i - constantWords) + MEMORY_OFFSET; break;
			case ABSI_VV_: slot(c0).i = wrap(slot(c1).i < 0 ? -static_cast<int64_t>(slot(c1).i) : slot(c1).i); break;
			case ABSF_VV_: slot(c0).f = std::fabs(slot(c1).f); break;
			case FLOF_VV_: slot(c0).f = std::floor(slot(c1).f); break;
			case FTOI_VVC: slot(c0).i = floatToInt(slot(c1).f * c2.f); break;				// C2 = scale
			case ITOF_VVC: slot(c0).f = static_cast<Float>(slot(c1).i) * c2.f; break;		// C2 = scale
			case ADDI_VVV: case ADDI_VVC: case SUBI_VVV: case SUBI_VVC: case SUBI_VCV:
			case MULI_VVV: case MULI_VVC: case DIVI_VVV: case DIVI_VVC: case DIVI_VCV:
			case MODI_VVV: case MODI_VVC: case MODI_VCV: case ANDI_VVV: case ANDI_VVC:
			case IORI_VVV: case IORI_VVC: case XORI_VVV: case XORI_VVC:
			case SHLI_VVV: case SHLI_VVC: case SHLI_VCV: case SHRI_VVV: case SHRI_VVC: case SHRI_VCV:
			case SHRU_VVV: case SHRU_VVC: case SHRU_VCV: {									// V0 = P1 op P2
				const Int a = (isFirstSourceConstant(opcode) ? c1.i : slot(c1).i);
				const Int b = (isSecondSourceConstant(opcode) ? c2.i : slot(c2).i);
				const bool isDivision = (opcode == DIVI_VVV || opcode == DIVI_VVC || opcode == DIVI_VCV
						|| opcode == MODI_VVV || opcode == MODI_VVC || opcode == MODI_VCV);
				if (isDivision && b == 0) {
					if (isSecondSourceConstant(opcode)) {
						throw Violation("constant division by zero");
					}
					return DIVISION_BY_ZERO;
				}
				slot(c0).i = integerResult(opcode, a, b);
				break;
			}
			case ADDF_VVV: case ADDF_VVC: case SUBF_VVV: case SUBF_VVC: case SUBF_VCV:
			case MULF_VVV: case MULF_VVC: case DIVF_VVV: case DIVF_VVC: case DIVF_VCV: {	// V0 = P1 op P2
				const Float a = (isFirstSourceConstant(opcode) ? c1.f : slot(c1).f);
				const Float b = (isSecondSourceConstant(opcode) ? c2.f : slot(c2).f);
				const Float result = floatResult(opcode, a, b);
				Value& target = slot(c0);
				target.f = result;
			#ifdef GAZL_CANONICAL_NAN
				if (result != result) {
					target.i = 0x7FC00000;													// the strict-FP build gives every arithmetic NaN one bit pattern
				}
			#endif
				break;
			}
			case COPY_VVC:
			case COPY_VCC:
			case COPY_CVC:
			case COPY_CCC: {																// P0 = destination, P1 = source, C2 = words
				const Pointer to = (opcode == COPY_VVC || opcode == COPY_VCC ? slot(c0).p : c0.p);
				const Pointer from = (opcode == COPY_VVC || opcode == COPY_CVC ? slot(c1).p : c1.p);
				const int64_t wordCount = static_cast<UInt>(c2.i);
				const int64_t destinationOffset = writeOffset(to);
				const int64_t source = readWord(from);
				if (destinationOffset + wordCount > writableWords || source + wordCount > readableWords) {
					return ACCESS_VIOLATION;
				}
				const int64_t destination = constantWords + destinationOffset;
				for (int64_t i = 0; i < wordCount; ++i) {											// ascending, so an overlap is defined (Specification.md section 3)
					memory[destination + i] = memory[source + i];
				}
				break;
			}
			case FORi_VVB:
			case FORi_VCB: {																// V0 = the counter, P1 = the limit, C2 = the branch
				Value& counter = slot(c0);
				const Int limit = (opcode == FORi_VVB ? slot(c1).i : c1.i);
				counter.i = wrap(static_cast<int64_t>(counter.i) + 1);
				if (counter.i < limit) {
					ip += c2.i;
					continue;
				}
				break;
			}
			case LSSI_VVB: case LSSI_VCB: case LSSI_CVB: case EQUI_VVB: case EQUI_VCB:
			case NLSI_VVB: case NLSI_VCB: case NLSI_CVB: case NEQI_VVB: case NEQI_VCB:
			case LSSF_VVB: case LSSF_VCB: case LSSF_CVB: case EQUF_VVB: case EQUF_VCB:
			case NLSF_VVB: case NLSF_VCB: case NLSF_CVB: case NEQF_VVB: case NEQF_VCB: {	// P0 compared with P1, C2 = the branch
				const Value a = (isFirstSourceConstant(opcode) ? c0 : slot(c0));
				const Value b = (isSecondSourceConstant(opcode) ? c1 : slot(c1));
				if (isBranchTaken(opcode, a, b)) {
					ip += c2.i;
					continue;
				}
				break;
			}
			case GOTO_B__: ip += c0.i; continue;
			case SWCH_VCC: {																// V0 = the case, C1 = the default's index, C2 = the table
				const UInt value = static_cast<UInt>(slot(c0).i);
				const UInt choice = (value < static_cast<UInt>(c1.i) ? value : static_cast<UInt>(c1.i));	// out of range: the default
				const int64_t word = readWord(c2.p) + choice;
				if (word >= readableWords) {
					throw Violation("switch table outside memory");
				}
				ip += memory[word].i;
				continue;
			}
			default: throw Violation("unknown opcode");
		}
		++ip;
	}
}

}																															// namespace GAZLReference
