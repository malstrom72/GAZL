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
#include <cstring>
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

/*
	IEEE-754 binary32 on the bits, with integer operations only, so no compiler flag or FPU mode (fused multiply-add,
	excess precision, fast math, flush to zero) can change a result. Rounding is to nearest, ties to even, underflow is
	gradual, and every NaN an arithmetic instruction makes is QUIET_NAN.
*/
static const UInt SIGN_BIT = 0x80000000u;
static const UInt MAGNITUDE_BITS = 0x7FFFFFFFu;
static const UInt INFINITY_BITS = 0x7F800000u;
static const UInt QUIET_NAN = 0x7FC00000u;
static const UInt FRACTION_BITS = 0x007FFFFFu;

static UInt bitsOf(const Value& value) { return static_cast<UInt>(value.i); }
static bool isNaN(UInt bits) { return (bits & MAGNITUDE_BITS) > INFINITY_BITS; }
static bool isInfinite(UInt bits) { return (bits & MAGNITUDE_BITS) == INFINITY_BITS; }
static bool isZero(UInt bits) { return (bits & MAGNITUDE_BITS) == 0; }

static int leadingZeros(uint64_t value) {													// `value` is not 0
	int count = 0;
	while (((value << count) >> 63) == 0) {
		++count;
	}
	return count;
}

// A finite float as `significand` times 2 to the `exponent`.
struct Unpacked {
	uint64_t significand;
	int exponent;
};

static Unpacked unpack(UInt bits) {
	const UInt field = (bits >> 23) & 0xFF;
	assert(field != 0xFF);
	const Unpacked unpacked = { (field == 0 ? bits & FRACTION_BITS : (bits & FRACTION_BITS) | 0x800000)
			, (field == 0 ? -149 : static_cast<int>(field) - 150) };
	return unpacked;
}

// `normalized` without its low `droppedBits` bits, rounded to nearest, ties to even.
static uint64_t roundedBits(uint64_t normalized, int droppedBits) {
	if (droppedBits > 64) {
		return 0;																			// below half the lowest kept bit
	}
	const uint64_t kept = (droppedBits == 64 ? 0 : normalized >> droppedBits);
	const uint64_t droppedMask = (droppedBits == 64 ? ~static_cast<uint64_t>(0)
			: (static_cast<uint64_t>(1) << droppedBits) - 1);
	const uint64_t dropped = normalized & droppedMask;
	const uint64_t half = static_cast<uint64_t>(1) << (droppedBits - 1);
	return kept + ((dropped > half || (dropped == half && (kept & 1) != 0)) ? 1 : 0);
}

/*
	The float nearest to `significand` times 2 to the `exponent`, with `sign`. Bit 0 of `significand` may be a sticky
	bit standing for nonzero bits below it: every caller leaves it at least two bits below the rounding position.
*/
static UInt roundToFloat(UInt sign, uint64_t significand, int exponent, DenormalMode mode) {
	if (significand == 0) {
		return sign;
	}
	const int normalization = leadingZeros(significand);
	const uint64_t normalized = significand << normalization;
	const int field = exponent - normalization + 63 + 127;									// the exponent field of the leading one
	if (field >= 255) {
		return sign | INFINITY_BITS;
	}
	if (field < 1) {
		if (mode == KEEP_DENORMALS) {
			return sign | static_cast<UInt>(roundedBits(normalized, 41 - field));			// rounding up into bit 23 gives the smallest normal
		}
		const bool roundsUpToNormal = (field == 0 && (roundedBits(normalized, 40) >> 24) != 0);	// at full precision
		return sign | (mode == FLUSH_AFTER_ROUNDING && roundsUpToNormal ? 0x00800000u : 0);
	}
	const uint64_t rounded = roundedBits(normalized, 40);
	const bool carried = (rounded >> 24) != 0;
	const UInt finalField = static_cast<UInt>(field) + (carried ? 1 : 0);
	if (finalField >= 255) {
		return sign | INFINITY_BITS;
	}
	return sign | (finalField << 23) | (carried ? 0 : static_cast<UInt>(rounded) & FRACTION_BITS);
}

static uint64_t shiftRightSticky(uint64_t value, int distance) {
	if (distance == 0) {
		return value;
	}
	if (distance >= 64) {
		return (value != 0 ? 1 : 0);
	}
	const bool lost = (value & ((static_cast<uint64_t>(1) << distance) - 1)) != 0;
	return (value >> distance) | (lost ? 1 : 0);
}

static UInt floatAdd(UInt a, UInt b, DenormalMode mode) {
	if (isNaN(a) || isNaN(b) || (isInfinite(a) && isInfinite(b) && ((a ^ b) & SIGN_BIT) != 0)) {
		return QUIET_NAN;
	}
	if (isInfinite(a) || isInfinite(b)) {
		return (isInfinite(a) ? a : b);
	}
	if (isZero(a) && isZero(b)) {
		return a & b;																		// -0 only when both are -0
	}
	const Unpacked aParts = unpack(a);
	const Unpacked bParts = unpack(b);
	const bool isAHigh = (aParts.exponent >= bParts.exponent);
	const Unpacked high = (isAHigh ? aParts : bParts);
	const Unpacked low = (isAHigh ? bParts : aParts);
	const UInt highSign = (isAHigh ? a : b) & SIGN_BIT;
	const UInt lowSign = (isAHigh ? b : a) & SIGN_BIT;
	const int distance = high.exponent - low.exponent;
	const int headroom = (distance < 38 ? distance : 38);									// a significand below 2^24 shifted by 38 leaves room for a carry
	const uint64_t highSignificand = high.significand << headroom;
	const uint64_t lowSignificand = shiftRightSticky(low.significand, distance - headroom);
	const int exponent = high.exponent - headroom;
	if (highSign == lowSign) {
		return roundToFloat(highSign, highSignificand + lowSignificand, exponent, mode);
	}
	if (highSignificand == lowSignificand) {
		return 0;																			// an exact cancellation is +0
	}
	return (highSignificand > lowSignificand ? roundToFloat(highSign, highSignificand - lowSignificand, exponent, mode)
			: roundToFloat(lowSign, lowSignificand - highSignificand, exponent, mode));
}

static UInt floatMultiply(UInt a, UInt b, DenormalMode mode) {
	const UInt sign = (a ^ b) & SIGN_BIT;
	if (isNaN(a) || isNaN(b) || (isInfinite(a) && isZero(b)) || (isZero(a) && isInfinite(b))) {
		return QUIET_NAN;
	}
	if (isInfinite(a) || isInfinite(b)) {
		return sign | INFINITY_BITS;
	}
	const Unpacked aParts = unpack(a);
	const Unpacked bParts = unpack(b);
	return roundToFloat(sign, aParts.significand * bParts.significand, aParts.exponent + bParts.exponent, mode);
}

static UInt floatDivide(UInt a, UInt b, DenormalMode mode) {
	const UInt sign = (a ^ b) & SIGN_BIT;
	if (isNaN(a) || isNaN(b) || (isInfinite(a) && isInfinite(b)) || (isZero(a) && isZero(b))) {
		return QUIET_NAN;
	}
	if (isInfinite(a) || isZero(b)) {
		return sign | INFINITY_BITS;
	}
	if (isZero(a) || isInfinite(b)) {
		return sign;
	}
	const Unpacked aParts = unpack(a);
	const Unpacked bParts = unpack(b);
	const int shift = leadingZeros(aParts.significand);									// the dividend up to bit 63 leaves a quotient of 40 bits or more
	const uint64_t dividend = aParts.significand << shift;
	const uint64_t quotient = dividend / bParts.significand;
	const uint64_t sticky = (dividend % bParts.significand != 0 ? 1 : 0);
	return roundToFloat(sign, quotient | sticky, aParts.exponent - shift - bParts.exponent, mode);
}

static UInt floatFloor(UInt bits) {
	if (isNaN(bits)) {
		return bits | 0x00400000u;															// quieted, sign and payload kept, as IEEE propagates a NaN
	}
	const UInt field = (bits >> 23) & 0xFF;
	if (field >= 150 || isZero(bits)) {
		return bits;																		// already whole: large, infinite or zero
	}
	const bool isNegative = (bits & SIGN_BIT) != 0;
	if (field < 127) {
		return (isNegative ? 0xBF800000u : 0);												// between -1 and 1: -1 or +0
	}
	const UInt fractionMask = (1u << (150 - field)) - 1;
	if ((bits & fractionMask) == 0) {
		return bits;
	}
	return (bits & ~fractionMask) + (isNegative ? fractionMask + 1 : 0);					// a negative value steps away from zero
}

static Int floatToInt(UInt bits) {															// truncates and saturates; NaN is 0
	const UInt field = (bits >> 23) & 0xFF;
	if (isNaN(bits) || field < 127) {
		return 0;
	}
	const bool isNegative = (bits & SIGN_BIT) != 0;
	if (field >= 158) {
		return (isNegative ? static_cast<Int>(0x80000000u) : 0x7FFFFFFF);					// 2^31 or more, infinities included
	}
	const UInt significand = (bits & FRACTION_BITS) | 0x800000;
	const UInt magnitude = (field >= 150 ? significand << (field - 150) : significand >> (150 - field));
	return (isNegative ? -static_cast<Int>(magnitude) : static_cast<Int>(magnitude));
}

static UInt intToFloat(Int value) {
	const int64_t wide = value;
	return roundToFloat((value < 0 ? SIGN_BIT : 0), static_cast<uint64_t>(wide < 0 ? -wide : wide), 0, KEEP_DENORMALS);	// never tiny
}

// Orders floats as numbers, -0 and +0 alike. Not for NaN.
static int64_t orderKey(UInt bits) {
	const int64_t magnitude = bits & MAGNITUDE_BITS;
	return ((bits & SIGN_BIT) != 0 ? -magnitude : magnitude);
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

static UInt floatResult(int opcode, UInt a, UInt b, DenormalMode mode) {
	switch (opcode) {
		case ADDF_VVV: case ADDF_VVC: return floatAdd(a, b, mode);
		case SUBF_VVV: case SUBF_VVC: case SUBF_VCV: return floatAdd(a, b ^ SIGN_BIT, mode);
		case MULF_VVV: case MULF_VVC: return floatMultiply(a, b, mode);
		default:
			assert(opcode == DIVF_VVV || opcode == DIVF_VVC || opcode == DIVF_VCV);
			return floatDivide(a, b, mode);
	}
}

// A float operand as arithmetic reads it: under flush-to-zero a denormal is a zero of the same sign.
static UInt operandBits(UInt bits, DenormalMode mode) {
	const bool isDenormal = ((bits >> 23) & 0xFF) == 0;
	return (mode != KEEP_DENORMALS && isDenormal ? bits & SIGN_BIT : bits);
}

static bool isBranchTaken(int opcode, const Value& a, const Value& b, DenormalMode mode) {
	const UInt x = operandBits(bitsOf(a), mode);
	const UInt y = operandBits(bitsOf(b), mode);
	const bool isOrdered = (!isNaN(x) && !isNaN(y));
	switch (opcode) {
		case LSSI_VVB: case LSSI_VCB: case LSSI_CVB: return a.i < b.i;
		case EQUI_VVB: case EQUI_VCB: return a.i == b.i;
		case NLSI_VVB: case NLSI_VCB: case NLSI_CVB: return !(a.i < b.i);
		case NEQI_VVB: case NEQI_VCB: return a.i != b.i;
		case LSSF_VVB: case LSSF_VCB: case LSSF_CVB: return isOrdered && orderKey(x) < orderKey(y);
		case EQUF_VVB: case EQUF_VCB: return isOrdered && orderKey(x) == orderKey(y);
		case NLSF_VVB: case NLSF_VCB: case NLSF_CVB: return !(isOrdered && orderKey(x) < orderKey(y));	// true when either is NaN
		default:
			assert(opcode == NEQF_VVB || opcode == NEQF_VCB);
			return !(isOrdered && orderKey(x) == orderKey(y));
	}
}

Machine::Machine(const AssembledProgram& program, UInt ipStackSize, const Native* natives, DenormalMode denormalMode)
		: code(program.code), codeSize(program.codeSize), functionTable(program.functionTable)
		, functionCount(program.functionCount), memory(program.memory), constantWords(program.constsSize - 1)
		, readableWords(program.memorySize - 1), writableWords(program.memorySize - program.constsSize)
		, stackBase(program.constsSize - 1 + program.globalsSize), stackEnd(program.memorySize - 1)
		, ipStackSize(ipStackSize), natives(natives), denormalMode(denormalMode), fuel(0), ip(0), dsp(0) {
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

int64_t Machine::fuelLeft() const { return fuel; }

UInt Machine::floatOperand(const Value& operand) const { return operandBits(bitsOf(operand), denormalMode); }

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
			case ABSF_VV_: slot(c0).i = static_cast<Int>(bitsOf(slot(c1)) & MAGNITUDE_BITS); break;
			case FLOF_VV_: slot(c0).i = static_cast<Int>(floatFloor(floatOperand(slot(c1)))); break;
			case FTOI_VVC: {																// C2 = scale
				const UInt scaled = floatMultiply(floatOperand(slot(c1)), floatOperand(c2), denormalMode);
				slot(c0).i = floatToInt(scaled);
				break;
			}
			case ITOF_VVC: {																// C2 = scale
				const UInt scaled = floatMultiply(intToFloat(slot(c1).i), floatOperand(c2), denormalMode);
				slot(c0).i = static_cast<Int>(scaled);
				break;
			}
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
				const UInt a = floatOperand(isFirstSourceConstant(opcode) ? c1 : slot(c1));
				const UInt b = floatOperand(isSecondSourceConstant(opcode) ? c2 : slot(c2));
				slot(c0).i = static_cast<Int>(floatResult(opcode, a, b, denormalMode));
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
				if (isBranchTaken(opcode, a, b, denormalMode)) {
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

static float hostFloat(UInt bits) {
	float value;
	std::memcpy(&value, &bits, sizeof (value));
	return value;
}

static UInt hostBits(float value) {
	UInt bits;
	std::memcpy(&bits, &value, sizeof (bits));
	return bits;
}

static bool isSameFloat(UInt x, UInt y) { return x == y || (isNaN(x) && isNaN(y)); }

/*
	Operands drawn to reach the hard cases: any bits, close exponents, denormals and the smallest normals, near-equal
	pairs, and values around 1. A xorshift keeps the sequence the same on every host.
*/
static UInt testOperand(uint64_t& state, int kind, UInt other) {
	state ^= state << 13;
	state ^= state >> 7;
	state ^= state << 17;
	const UInt r = static_cast<UInt>(state >> 16);
	switch (kind) {
		case 0: return r;
		case 1: return (other & 0xFF800000u) ^ (r & 0x807FFFFFu) ^ ((r >> 3) & 0x01800000u);
		case 2: return (r & 0x807FFFFFu) | ((r & 1) != 0 ? 0 : 0x00800000u);
		case 3: return other ^ (r & 0x0000000Fu) ^ (r & SIGN_BIT);
		default: return (r & 0x803FFFFFu) | 0x3F000000u | ((r & 7) << 23);
	}
}

int64_t countFloatMismatches(int64_t caseCount, DenormalMode mode) {
	uint64_t state = 0x9E3779B97F4A7C15ull;
	int64_t mismatchCount = 0;
	for (int64_t n = 0; n < caseCount; ++n) {
		const bool isBoundary = (n < 4);												// first the one case flush modes may decide differently
		const UInt randomA = testOperand(state, static_cast<int>(n % 5), 0);
		const UInt randomB = testOperand(state, static_cast<int>((n / 5) % 5), randomA);
		const UInt rawA = (isBoundary ? 0x3F000400u ^ ((n & 1) != 0 ? SIGN_BIT : 0) : randomA);	// 2^-126 * (1 - 2^-26) as a product
		const UInt rawB = (isBoundary ? 0x00FFF800u ^ ((n & 2) != 0 ? SIGN_BIT : 0) : randomB);
		const volatile float x = hostFloat(rawA);										// volatile: each host operation is one SSE / NEON instruction
		const volatile float y = hostFloat(rawB);
		const UInt a = operandBits(rawA, mode);
		const UInt b = operandBits(rawB, mode);
		const bool isOrdered = (!isNaN(a) && !isNaN(b));
		const Int expectedInt = (x != x ? 0 : (x >= 2147483648.0f ? 0x7FFFFFFF
				: (x < -2147483648.0f ? static_cast<Int>(0x80000000u) : static_cast<Int>(x))));
		const bool isMismatch = !isSameFloat(hostBits(x + y), floatAdd(a, b, mode))
				|| !isSameFloat(hostBits(x - y), floatAdd(a, b ^ SIGN_BIT, mode))
				|| !isSameFloat(hostBits(x * y), floatMultiply(a, b, mode))
				|| !isSameFloat(hostBits(x / y), floatDivide(a, b, mode))
				|| !isSameFloat(hostBits(std::floor(static_cast<float>(x))), floatFloor(a))
				|| hostBits(static_cast<float>(static_cast<Int>(rawA))) != intToFloat(static_cast<Int>(rawA))
				|| expectedInt != floatToInt(a)
				|| (x < y) != (isOrdered && orderKey(a) < orderKey(b))
				|| (x == y) != (isOrdered && orderKey(a) == orderKey(b));
		if (isMismatch) {
			++mismatchCount;
		}
	}
	return mismatchCount;
}

}																															// namespace GAZLReference
