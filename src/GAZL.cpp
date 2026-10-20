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

#include "GAZL.h"
#include "GAZLOpcodes.h"																					// the ONE opcode enum, shared with the JIT
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <string>
#include <iostream>

#define STR(x) x

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC push_options
#pragma GCC optimize ("no-finite-math-only")
#endif
// An older GCC on x87 may also need `#pragma GCC optimize ("float-store")` to round each instruction once.
#endif

#ifdef _MSC_VER
#pragma float_control(precise, on, push)
#endif

#ifdef __FAST_MATH__
#error This code requires IEEE compliant floating point handling. Avoid -Ofast / -ffast-math etc (at least for this source file).
#endif

namespace GAZL {

const char* ASSEMBLER_ERROR_TEXTS[] = {
	/*   DATA_SECTION_FULL							*/	  "Not enough space in data section"
	/* , DATA_SECTION_MISSING						*/	, "Data section not declared"
	/* , DID_NOT_EXPECT_ADDRESS						*/	, "Did not expect address"
	/* , DID_NOT_EXPECT_CONSTANT					*/	, "Did not expect constant / compile-time variable"
	/* , DID_NOT_EXPECT_CONSTANT_FLOAT				*/	, "Did not expect constant float"
	/* , DID_NOT_EXPECT_CONSTANT_INT				*/	, "Did not expect constant int"
	/* , EXPECTED_END_OF_LINE_OR_COMMENT			*/	, "Expected end of line or comment"
	/* , FAIL_DIRECTIVE								*/	, "FAIL directive encountered"
	/* , FORWARD_SYMBOL_NOT_FOUND					*/	, "Symbol not found (in expected scope)"
	/* , INCOMPATIBLE_COMPILE_TIME_VARIABLE_TYPE	*/	, "Incompatible / non-existing compile-time variable"
	/* , INCOMPATIBLE_TYPES							*/	, "Incompatible types"
	/* , INVALID_COMPILE_TIME_VARIABLE				*/	, "Invalid compile-time variable"
	/* , INVALID_IDENTIFIER							*/	, "Invalid identifier"
	/* , INVALID_INSTRUCTION						*/	, "Unknown mnemonic or invalid operands"
	/* , INVALID_MNENOMIC							*/	, "Invalid mnemonic"
	/* , INVALID_NATIVE_LITERAL						*/	, "Invalid native function"
	/* , INVALID_NUMERICAL_LITERAL					*/	, "Invalid number"
	/* , MISSING_COMPILE_TIME_LABEL					*/  , "Compile time label not found"
	/* , MISSING_FUNCTION_DECLARATION				*/	, "Missing FUNC"
	/* , MISSING_INSTRUCTION						*/	, "Missing instruction"
	/* , MISSING_OPERAND							*/	, "Missing operand"
	/* , MISSING_RETURN_INSTRUCTION					*/	, "Missing RETU"
	/* , MUST_DEFINE_LOCALS_FIRST					*/	, "Parameters and locals must be declared at top of FUNC"
	/* , NEGATIVE_VALUE_NOT_ACCEPTED				*/	, "Negative value not accepted"
	/* , NON_FORWARD_SYMBOL_NOT_FOUND				*/	, "Symbol not previously defined (in expected scope)"
	/* , NOT_ENOUGH_CODE_SPACE						*/	, "Not enough space for code"
	/* , NOT_ENOUGH_MEMORY_SPACE					*/	, "Not enough space for globals and constants"
	/* , OFFSET_OUT_OF_BOUNDS						*/	, "Offset out of bounds"
	/* , SYMBOL_ALREADY_DEFINED						*/	, "Symbol already defined"
	/* , UNKNOWN_NATIVE_FUNCTION					*/	, "Unknown native function"
	/* , CONSTANT_DIVISION_BY_ZERO					*/	, "Constant zero divisor or modulus"
	/* , EXPECTED_CONSTANT							*/	, "Expected constant"
	/* , NOT_ENOUGH_FUNCTION_SPACE					*/	, "Not enough space for function table"
	/* , LABEL_ON_FUNCTION							*/	, "Branch target lands on a FUNC"
	/* , FRAME_TOO_LARGE								*/	, "Frame too large"
	/* , NON_ASCII_TEXT								*/	, "Non-ASCII character in text"
	/* , UNNAMED_DATA_SECTION						*/	, "Unnamed GLOB section must start with a labelled data statement"
	/* , UNBALANCED_MARK								*/	, "Unbalanced MARK / FREE"
	/* , OVERLAPPING_DATA_REGIONS					*/	, "Data regions overlap"
	/* , UNSUPPORTED_GAZL_VERSION					*/	, "File requires a newer GAZL engine"
	/* , UNCLOSED_GAZL_REGION						*/	, "GAZL 2 region not closed with GAZL #1"
};

/*
	Integer and `fTOi` arithmetic with defined results on every target, shared by `Processor::run()` and
	`calcConstant()` so that run time and constant folding cannot differ (design/gazl/PortabilityAudit.md).
*/
inline int absolute(int i) { Int x = i >> (Int)(sizeof (Int) * 8 - 1); return (Int)(((UInt)i ^ (UInt)x) - (UInt)x); } // INT_MIN -> INT_MIN
inline float absolute(float f) { return fabsf(f); }
inline double absolute(double f) { return fabs(f); }
#ifdef GAZL_CANONICAL_NAN
inline Float canonicalNaN(Float f) {	// One NaN bit pattern, so the interpreter and the JIT agree on a word a program can read as an int.
	if (f != f) { const UInt bits = 0x7FC00000u; Float c; memcpy(&c, &bits, sizeof c); return c; }
	return f;
}
#define CANON_F(x) (canonicalNaN(x))
#else
#define CANON_F(x) (x)
#endif
inline Int idiv(Int a, Int b) { return (b == -1) ? (Int)(0u - (UInt)a) : a / b; }	// caller guarantees b != 0; INT_MIN / -1 -> INT_MIN
inline Int imod(Int a, Int b) { return (b == -1) ? 0 : a % b; }						// caller guarantees b != 0; INT_MIN % -1 -> 0
inline Int iadd(Int a, Int b) { return (Int)((UInt)a + (UInt)b); }					// two's-complement wrap
inline Int isub(Int a, Int b) { return (Int)((UInt)a - (UInt)b); }					// two's-complement wrap
inline Int imul(Int a, Int b) { return (Int)((UInt)a * (UInt)b); }					// two's-complement wrap
inline Int ishl(Int a, Int n) { return (Int)((UInt)a << (n & 31)); }				// count mod 32; no neg-shift / overflow UB
inline Int ashr(Int a, Int n) { return a >> (n & 31); }								// count mod 32; arithmetic (sign-extending)
inline Int lshr(Int a, Int n) { return (Int)((UInt)a >> (n & 31)); }				// count mod 32; logical (zero-fill)
inline Int ftoi(Float v) {															// v is the ALREADY-SCALED float
	/*
		Classified by bit pattern, not by float comparisons, so that saturation and NaN survive a finite-math
		build, which would delete a `v != v` or `v >= 2^31` test as unreachable.
	*/
	UInt b;
	memcpy(&b, &v, sizeof b);
	const UInt e = (b >> 23) & 0xFF;											// biased exponent
	if (e >= 158) {																// |v| >= 2^31 (exp 158), or inf/NaN (exp 255)
		if (e == 255 && (b & 0x7FFFFF) != 0) return 0;							// NaN
		return (b >> 31) != 0 ? (Int)(-2147483647 - 1) : 2147483647;			// sign -> INT_MIN / INT_MAX (+/-inf too)
	}
	return (Int)v;																// |v| < 2^31: cast is in range and defined
}
template<typename T> inline T minimum(T a, T b) { return (a < b) ? a : b; }
template<typename T> inline T maximum(T a, T b) { return (a < b) ? b : a; }

static bool isEOL(const Char p) { return (p == 0 || p == '\n' || p == '\r'); }
static const Char* eatSpace(const Char* p) { return p + strspn(p, STR(" \t")); }

static const Char* eatSpaceAndComment(const Char* p) {
	return (*(p = eatSpace(p)) == ';') ? p + strcspn(p, STR("\n\r")) : p;
}

static const Char* eatToken(const Char* p) {
	do {
		p += strcspn(p, STR("!; \t\r\n'\""));
		if (*p == '\"') { while (*(++p) != 0 && *p != '\"') { }; if (*p != 0) ++p; }
		else if (*p == '\'') { while (*(++p) != 0 && *p != '\'') { }; if (*p != 0) ++p; }
		else return p;
	} while (true);
}

static const Char* eatEOL(const Char* p) {
	if (*p == '\r') ++p;
	if (*p == '\n') ++p;
	return p;
}

static Int stringToInt(const Char* &p, const Char* e) {
	UInt i = 0;
	Int sign = 1;
	switch (p < e ? *p : 0) {
		case '+': ++p; sign = 1; break;
		case '-': ++p; sign = -1; break;
	}
	switch (p < e ? *p : 0) {
		case '\'':
		case '\"':	{
						const Char quote = *p;
						while (++p < e && *p != quote) {
							if ((*p & 0x80) != 0) throw Exception(NON_ASCII_TEXT);
							i = (i << 8) | (*p & 0xFFU);
						}
						if (p < e) ++p;
						break;
					}
		case '0':	if (*++p == 'x') {
						while (++p < e && ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f')))
							i = (i << 4) + (*p <= '9' ? *p - '0' : (*p & ~0x20) - ('A' - 10));
						break;
					} /* else continue */
		default:	for (; p < e && *p >= '0' && *p <= '9'; ++p) i = i * 10 + (*p - '0'); break;
	}
	return (Int)(sign < 0 ? 0u - i : i);																	// negate in UNSIGNED: `(Int)i * -1` is signed-overflow UB at i == 2^31, the literal -2147483648
}

// An unsigned integer of up to 1024 bits, enough for the exact comparisons in `parseFloat`.
class ExactInt {
	public:		explicit ExactInt(UInt value) : size(1) { words[0] = value; }
	public:		void multiplyAdd(UInt factor, UInt addend);													// `this` = `this` * `factor` + `addend`
	public:		void multiplyByPowerOfTen(int power);
	public:		void shiftLeft(int bits);
	public:		int compare(const ExactInt& other) const;													// -1, 0 or 1
	protected:	enum { MAX_WORDS = 32 };
	protected:	int size;																					// words in use, the top one non-zero unless the value is 0
	protected:	UInt words[MAX_WORDS];																		// least significant first
};

void ExactInt::multiplyAdd(UInt factor, UInt addend) {
	uint64_t carry = addend;
	for (int i = 0; i < size; ++i) {
		carry += static_cast<uint64_t>(words[i]) * factor;
		words[i] = static_cast<UInt>(carry);
		carry >>= 32;
	}
	if (carry != 0) {
		assert(size < MAX_WORDS);
		words[size++] = static_cast<UInt>(carry);
	}
}

void ExactInt::multiplyByPowerOfTen(int power) {
	static const UInt SMALL_POWERS[9] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000 };
	for (; power >= 9; power -= 9) multiplyAdd(1000000000, 0);
	multiplyAdd(SMALL_POWERS[power], 0);
}

void ExactInt::shiftLeft(int bits) {
	const int wordShift = bits / 32;
	const int bitShift = bits % 32;
	assert(size + wordShift + 1 <= MAX_WORDS);
	words[size + wordShift] = 0;
	for (int i = size - 1; i >= 0; --i) {
		if (bitShift != 0) words[i + wordShift + 1] |= words[i] >> (32 - bitShift);
		words[i + wordShift] = words[i] << bitShift;
	}
	for (int i = 0; i < wordShift; ++i) words[i] = 0;
	size += wordShift + 1;
	while (size > 1 && words[size - 1] == 0) --size;
}

int ExactInt::compare(const ExactInt& other) const {
	if (size != other.size) return (size < other.size ? -1 : 1);
	for (int i = size - 1; i >= 0; --i) {
		if (words[i] != other.words[i]) return (words[i] < other.words[i] ? -1 : 1);
	}
	return 0;
}

/*
	Compares the decimal `digits` * 10^`power`, plus a non-zero `tail` below its last digit if `tail` is set, with
	the midpoint between the positive floats with bits `bits` and `bits + 1`: -1 below, 0 on it, 1 above.
*/
static int compareWithMidpoint(const ExactInt& digits, int power, bool tail, UInt bits) {
	const UInt field = bits >> 23;
	const UInt mantissa = (field != 0 ? (bits & 0x7FFFFF) | 0x800000 : bits);
	const int exponent = static_cast<int>(field != 0 ? field : 1) - 150;										// the float is `mantissa` * 2^`exponent`
	ExactInt left(digits);
	ExactInt right(2 * mantissa + 1);																			// the midpoint is `right` * 2^(`exponent` - 1)
	if (power >= 0) left.multiplyByPowerOfTen(power);
	else right.multiplyByPowerOfTen(-power);
	if (exponent >= 1) right.shiftLeft(exponent - 1);
	else left.shiftLeft(1 - exponent);
	const int comparison = left.compare(right);
	return (comparison == 0 && tail ? 1 : comparison);
}

/*
	The bits of a positive float within a step or so of `value`, built from its binary exponent and mantissa with
	integer operations only, so that a flush-to-zero floating-point environment cannot move it.
*/
static UInt approximateFloatBits(double value) {
	if (!(value > 0.0)) return 0;
	if (value >= 4.0e38) return 0x7F800000;
	int exponent;
	const UInt mantissa = static_cast<UInt>(frexp(value, &exponent) * 16777216.0);								// `value` = `mantissa` * 2^(`exponent` - 24)
	if (exponent + 126 >= 255) return 0x7F800000;
	if (exponent + 126 >= 1) return (static_cast<UInt>(exponent + 126) << 23) | (mantissa & 0x7FFFFF);
	const int shift = -(exponent + 125);																			// subnormal: count steps of 2^-149
	return (shift >= 32 ? 0 : mantissa >> shift);
}

/*
	A double gives a candidate within a step or so, and exact comparisons with the midpoints next to it correct it. A
	float midpoint has at most 113 significant digits, so `MAX_DIGITS` decide every comparison. From Numbstrict's
	`parseReal` (https://github.com/malstrom72/Numbstrict), cut down to float.
*/
Float parseFloat(const Char*& p, const Char* e) {
	const int MAX_DIGITS = 120;
	const int MAX_APPROXIMATE_DIGITS = 17;
	bool negative = false;
	switch (p < e ? *p : 0) {
		case '+': ++p; break;
		case '-': ++p; negative = true; break;
	}
	ExactInt digits(0);
	int count = 0;																									// significant digits in `digits`
	int power = 0;																									// the value is `digits` * 10^`power` (+ `tail`)
	bool tail = false;
	double approximate = 0.0;
	int approximateCount = 0;
	if (p < e && *p >= '0' && *p <= '9') {
		bool fraction = false;
		do {
			const UInt digit = static_cast<UInt>(*p - '0');
			if (count == 0 && digit == 0) {																			// leading zero
				if (fraction) --power;
			} else if (count < MAX_DIGITS) {
				digits.multiplyAdd(10, digit);
				++count;
				if (fraction) --power;
				if (approximateCount < MAX_APPROXIMATE_DIGITS) {
					approximate = approximate * 10.0 + digit;
					++approximateCount;
				}
			} else {
				if (!fraction) ++power;
				tail = tail || (digit != 0);
			}
			++p;
			if (!fraction && p + 1 < e && *p == '.' && p[1] >= '0' && p[1] <= '9') {
				fraction = true;
				++p;
			}
		} while (p < e && *p >= '0' && *p <= '9');
		if (p + 1 < e && (*p == 'E' || *p == 'e')) {
			const Int exponent = stringToInt(++p, e);
			power += (exponent < -100000 ? -100000 : exponent > 100000 ? 100000 : exponent);					// far beyond any float
		}
	}
	UInt bits;
	const int lead = power + count - 1;																				// decimal exponent of the leading digit
	if (count == 0 || lead < -46) {																					// below 1e-46, under half the least float
		bits = 0;
	} else if (lead > 38) {																							// 1e39 or more
		bits = 0x7F800000;
	} else {
		int scale = power + count - approximateCount;
		for (; scale > 0; --scale) approximate *= 10.0;
		double divisor = 1.0;
		for (; scale < 0; ++scale) divisor *= 10.0;
		bits = approximateFloatBits(approximate / divisor);
		while (bits < 0x7F800000) {
			const int comparison = compareWithMidpoint(digits, power, tail, bits);
			if (comparison < 0 || (comparison == 0 && (bits & 1) == 0)) break;
			++bits;
		}
		while (bits > 0) {
			const int comparison = compareWithMidpoint(digits, power, tail, bits - 1);
			if (comparison > 0 || (comparison == 0 && ((bits - 1) & 1) != 0)) break;
			--bits;
		}
	}
	Float value;
	bits |= (negative ? 0x80000000U : 0U);
	memcpy(&value, &bits, sizeof (value));
	return value;
}

static Char* int2string(Int i, int radix, int minLength, Char buffer[33]) {
	assert(1 <= radix && radix <= 16);
	assert(0 <= minLength);
	Char* p = buffer + 32;
	*p = 0;
	Char* e = p - minLength;
	for (Int x = i; p > e || x != 0; x /= radix) {
		assert(p >= buffer + 1);
		*--p = STR("fedcba9876543210123456789abcdef")[15 + x % radix];													// Mirrored hex string to handle negative x.
	}
	if (i < 0) *--p = '-';
	return p;
}

const Char* NULL_STRING = STR("NULL");
const Char* GAZL_VERSION_STRING = STR("GAZL_VERSION");
const Char* GAZL_WORD_SIZE_STRING = STR("GAZL_WORD_SIZE");
const Char* GAZL_MEMORY_SIZE_STRING = STR("GAZL_MEMORY_SIZE");


const int TRANSIENT			= 0x00001;
const int VAR_INT_R			= 0x00002 | TRANSIENT;		// Readable int variable.
const int VAR_INT_W			= 0x00004 | TRANSIENT;		// Writable int variable.
const int VAR_FLOAT_R		= 0x00008 | TRANSIENT;		// Readable float variable.
const int VAR_FLOAT_W		= 0x00010 | TRANSIENT;		// Writable float variable.
const int VAR_PTR_R			= 0x00020 | TRANSIENT;		// Readable pointer variable.
const int VAR_PTR_W			= 0x00040 | TRANSIENT;		// Writable pointer variable.
const int VAR_TGT_R			= 0x100000 | TRANSIENT;		// Readable call-target variable (GAZL 2 `t`).
const int VAR_TGT_W			= 0x200000 | TRANSIENT;		// Writable call-target variable.
const int ANY_VAR_R			= VAR_INT_R | VAR_FLOAT_R | VAR_PTR_R | VAR_TGT_R;		// so PEEK/POKE/GETL/SETL carry targets too: memory is typeless
const int ANY_VAR_W			= VAR_INT_W | VAR_FLOAT_W | VAR_PTR_W | VAR_TGT_W;
const int ANY_VAR			= ANY_VAR_R | ANY_VAR_W;
const int CONST_INT_P		= 0x00080;					// Constant positive (or zero) integer.
const int CONST_INT_N		= 0x00100;					// Constant negative integer.
const int CONST_INT			= CONST_INT_P | CONST_INT_N;
const int CONST_FLOAT		= 0x00200;					// Constant float.
const int ADDRESS_R			= 0x00400;					// Address to readable data.
const int ADDRESS_W			= 0x00800;					// Address to writable data.
const int ADDRESS			= ADDRESS_R | ADDRESS_W;
const int TEMPORARY			= 0x01000;					// Declared as TEMP (which hints that memory need not be serialized)
const int NULL_PTR			= 0x02000;					// Null pointer (the only pointer constant).
const int FUNC				= 0x04000;
const int BRANCH			= 0x08000;					// Constant positive (or zero) integer.
const int NATIVE			= 0x10000;
const int COMPILE_TIME		= 0x20000;
const int FORWARD			= 0x40000;
const int UNCHECKED_ADDRESS	= 0x80000;					// Do *not* check the offset against the symbol's range: the address is only a value (`MOVp`, `ADDp` and the like).
const int TARGET			= 0x400000;					// Call-target constant: what `&function` MINTS inside a `GAZL #2` region (FUNC outside one).
const int OFFSET_GIVEN		= 0x1000000;				// Set per operand, not in the table: a `:offset` was written, `:0` included, which only data and locals take.
const int POINTER_CONSTANT	= 0x2000000;				// On a symbol, never in an operand: a `! DEFp` name, an address with no data of its own.
const int FWD_ADDRESS_W		= ADDRESS_W | FORWARD;
const int FWD_ADDRESS_R		= ADDRESS_R | FORWARD;
const int FWD_BRANCH		= BRANCH | FORWARD;
const int FREE_ADDRESS		= ADDRESS | UNCHECKED_ADDRESS;
const int FWD_FREE			= FREE_ADDRESS | FORWARD;
const int FWD_FREE_W		= ADDRESS_W | UNCHECKED_ADDRESS | FORWARD;
const int FWD_FREE_R		= ADDRESS_R | UNCHECKED_ADDRESS | FORWARD;
// A function declared in a `GAZL #2` region is a `TARGET`, never `FUNC`, so it cannot enter a `p` position
// (design/gazl/GAZL2FunctionPointers.md). `CALL` takes both, so calls work across dialects.
const int ANY_FREE			= NULL_PTR | FREE_ADDRESS | FUNC;
const int ANY_FWD_FREE		= ANY_FREE | FORWARD;
const int TGT_FREE			= NULL_PTR | TARGET;
const int TGT_FWD_FREE		= TGT_FREE | FORWARD;
const int ANY_VAR_FREE_W	= ANY_VAR_W | UNCHECKED_ADDRESS;
const int ANY_VAR_FREE_R	= ANY_VAR_R | UNCHECKED_ADDRESS;
const int ANY_VAR_FREE		= ANY_VAR | UNCHECKED_ADDRESS;
const int KONST				= CONST_INT | CONST_FLOAT | ANY_FWD_FREE | TARGET; // called KONST because windows.h defines a CONST macro; TARGET keeps DATA untyped in both dialects

const int SWAP_0_AND_1		= 0x01; // Used for commutative operations where operand 0 and operand 1 can be swapped in order to minimize the effective instruction set when operands have different addressing modes.
const int SWAP_1_AND_2		= 0x02; // Used for commutative operations where operand 1 and operand 2 can be swapped in order to minimize the effective instruction set when operands have different addressing modes.
const int YIELDS_CONST		= 0x04; // Result of instruction is a constant (e.g. all source operands are constants).
const int YIELDS_GOTO		= 0x08; // Instruction can be resolved to either a GOTO or a NOOP (e.g. comparison of two constants).
const int LOCAL_BOUNDS		= 0x10;	// Operand 1 is a variable (local or transient), operand 2 is a size. Make sure frame bounds >= &variable + size.
const int CHECK_DIV_BY_0	= 0x20; // Operand 1 is a constant used in a division or modulo operation, must check for division by zero. Can only be used if operand 2 is either a CONST_FLOAT or CONST_INT
const int BRANCH_IS_TABLE	= 0x40; // The BRANCH operand is a jump-table BASE in memory, not a relative displacement - threadBranches must not rewrite it (SWCH).
const int GAZL_2_ONLY		= 0x80; // A GAZL 2 word: outside a `GAZL #2` region it is unknown, as on a GAZL 1 engine.

struct Operator {
	char key[10];
	Opcode opcode;
	int accepts[3];
	int otherFlags;
	int declareTypes;
};

static const Operator OPERATORS[] = {
	  { " ABSf_vc_", ABSF_CC_,	{ VAR_FLOAT_W	, CONST_FLOAT	, 0				}		, YIELDS_CONST	, CONST_FLOAT	} ,
	  { " ABSf_vv_", ABSF_VV_,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, 0				}		, 0				, 0				}
	, { " ABSi_vc_", ABSI_CC_,	{ VAR_INT_W		, CONST_INT		, 0				}		, YIELDS_CONST	, CONST_INT		}
	, { " ABSi_vv_", ABSI_VV_,	{ VAR_INT_W		, VAR_INT_R		, 0				}		, 0				, 0				} ,
	  { " ADDf_vcc", ADDF_CCC,	{ VAR_FLOAT_W	, CONST_FLOAT	, CONST_FLOAT	}		, YIELDS_CONST	, CONST_FLOAT	} ,
	  { " ADDf_vcv", ADDF_VVC,	{ VAR_FLOAT_W	, CONST_FLOAT	, VAR_FLOAT_R	}		, SWAP_1_AND_2	, 0				} ,
	  { " ADDf_vvc", ADDF_VVC,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, CONST_FLOAT	}		, 0				, 0				}
	, { " ADDf_vvv", ADDF_VVV,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, VAR_FLOAT_R	}		, 0				, 0				}
	, { " ADDi_vcc", ADDI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST	, CONST_INT		}
	, { " ADDi_vcv", ADDI_VVC,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, SWAP_1_AND_2	, 0				}
	, { " ADDi_vvc", ADDI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, 0				, 0				}
	, { " ADDi_vvv", ADDI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " ADDp_vcc", ADDI_CCC,	{ VAR_PTR_W		, FREE_ADDRESS	, CONST_INT		}		, YIELDS_CONST	, FREE_ADDRESS		}
	, { " ADDp_vcv", ADDI_VVC,	{ VAR_PTR_W		, FWD_FREE		, VAR_INT_R		}		, SWAP_1_AND_2	, 0				}
	, { " ADDp_vvc", ADDI_VVC,	{ VAR_PTR_W		, VAR_PTR_R		, CONST_INT		}		, 0				, 0				}
	, { " ADDp_vvv", ADDI_VVV,	{ VAR_PTR_W		, VAR_PTR_R		, VAR_INT_R		}		, 0				, 0				}
	, { " ADRL_vvs", ADRL_VV_,	{ VAR_PTR_W		, ANY_VAR_FREE	, CONST_INT_P	}		, LOCAL_BOUNDS	, 0				}
	, { " ANDi_vcc", ANDI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST	, CONST_INT		}
	, { " ANDi_vcv", ANDI_VVC,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, SWAP_1_AND_2	, 0				}
	, { " ANDi_vvc", ANDI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, 0				, 0				}
	, { " ANDi_vvv", ANDI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " CALL_c__", CALL_CVC,	{ FUNC|TARGET|FORWARD, 0		, 0				}		, 0				, 0				}
	, { " CALL_cvs", CALL_CVC,	{ FUNC|TARGET|FORWARD, TRANSIENT, CONST_INT_P	}		, LOCAL_BOUNDS	, 0				}
	, { " CALL_n__", CALL_NVC,	{ NATIVE|FORWARD, 0			, 0				}		, 0				, 0				}
	, { " CALL_nvs", CALL_NVC,	{ NATIVE|FORWARD, TRANSIENT	, CONST_INT_P	}		, LOCAL_BOUNDS	, 0				}
	, { " CALL_v__", CALL_VVC,	{ VAR_PTR_R|VAR_TGT_R, 0		, 0				}		, 0				, 0				}
	, { " CALL_vvs", CALL_VVC,	{ VAR_PTR_R|VAR_TGT_R, TRANSIENT, CONST_INT_P	}		, LOCAL_BOUNDS	, 0				}
	, { " CNST_s__", CNST____,	{ CONST_INT_P	, 0				, 0				}		, 0				, ADDRESS_R		}
	, { " COPY_ccs", COPY_CCC,	{ FWD_ADDRESS_W	, FWD_ADDRESS_R	, CONST_INT_P	}		, 0				, 0				}
	, { " COPY_cvs", COPY_CVC,	{ FWD_ADDRESS_W	, VAR_PTR_R		, CONST_INT_P	}		, 0				, 0				}
	, { " COPY_vcs", COPY_VCC,	{ VAR_PTR_R		, FWD_ADDRESS_R	, CONST_INT_P	}		, 0				, 0				}
	, { " COPY_vvs", COPY_VVC,	{ VAR_PTR_R		, VAR_PTR_R		, CONST_INT_P	}		, 0				, 0				}
	, { " DATA_c__", DATA____,	{ KONST			, 0				, 0				}		, 0				, 0				}
	, { " DATf_c__", DATA____,	{ CONST_FLOAT	, 0				, 0				}		, 0				, 0				}
	, { " DATi_c__", DATA____,	{ CONST_INT		, 0				, 0				}		, 0				, 0				}
	, { " DATp_c__", DATA____,	{ ANY_FWD_FREE	, 0				, 0				}		, 0				, 0				}
	, { " DATs____", DATA____,	{ 0				, 0				, 0				}		, 0				, 0				}
	, { " DATt_c__", DATA____,	{ TGT_FWD_FREE	, 0				, 0				}		, GAZL_2_ONLY	, 0				}
	, { " DIFp_vcc", SUBI_CCC,	{ VAR_INT_W		, FREE_ADDRESS		, FREE_ADDRESS		}		, YIELDS_CONST	, CONST_INT		}
	, { " DIFp_vcv", SUBI_VCV,	{ VAR_INT_W		, FWD_FREE		, VAR_PTR_R		}		, 0				, 0				}
	, { " DIFp_vvc", SUBI_VVC,	{ VAR_INT_W		, VAR_PTR_R		, FWD_FREE		}		, 0				, 0				}
	, { " DIFp_vvv", SUBI_VVV,	{ VAR_INT_W		, VAR_PTR_R		, VAR_PTR_R		}		, 0				, 0				}
	, { " DIFt_vcc", SUBI_CCC,	{ VAR_INT_W		, TGT_FREE		, TGT_FREE		}		, YIELDS_CONST | GAZL_2_ONLY, CONST_INT }
	, { " DIFt_vcv", SUBI_VCV,	{ VAR_INT_W		, TGT_FWD_FREE	, VAR_TGT_R		}		, GAZL_2_ONLY	, 0				}
	, { " DIFt_vvc", SUBI_VVC,	{ VAR_INT_W		, VAR_TGT_R		, TGT_FWD_FREE	}		, GAZL_2_ONLY	, 0				}
	, { " DIFt_vvv", SUBI_VVV,	{ VAR_INT_W		, VAR_TGT_R		, VAR_TGT_R		}		, GAZL_2_ONLY	, 0				}
	, { " DIVf_vcc", DIVF_CCC,	{ VAR_FLOAT_W	, CONST_FLOAT	, CONST_FLOAT	}		, YIELDS_CONST	, CONST_FLOAT	}
	, { " DIVf_vcv", DIVF_VCV,	{ VAR_FLOAT_W	, CONST_FLOAT	, VAR_FLOAT_R	}		, 0				, 0				}
	, { " DIVf_vvc", DIVF_VVC,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, CONST_FLOAT	}		, 0				, 0				}
	, { " DIVf_vvv", DIVF_VVV,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, VAR_FLOAT_R	}		, 0				, 0				}
	, { " DIVi_vcc", DIVI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST|CHECK_DIV_BY_0, CONST_INT }
	, { " DIVi_vcv", DIVI_VCV,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, 0				, 0				}
	, { " DIVi_vvc", DIVI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, CHECK_DIV_BY_0, 0				}
	, { " DIVi_vvv", DIVI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " EQUf_ccb", EQUF_CCB,	{ CONST_FLOAT	, CONST_FLOAT	, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " EQUf_cvb", EQUF_VCB,	{ CONST_FLOAT	, VAR_FLOAT_R	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " EQUf_vcb", EQUF_VCB,	{ VAR_FLOAT_R	, CONST_FLOAT	, FWD_BRANCH	}		, 0				, 0				}
	, { " EQUf_vvb", EQUF_VVB,	{ VAR_FLOAT_R	, VAR_FLOAT_R	, FWD_BRANCH	}		, 0				, 0				}
	, { " EQUi_ccb", EQUI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " EQUi_cvb", EQUI_VCB,	{ CONST_INT		, VAR_INT_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " EQUi_vcb", EQUI_VCB,	{ VAR_INT_R		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { " EQUi_vvb", EQUI_VVB,	{ VAR_INT_R		, VAR_INT_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " EQUp_ccb", EQUI_CCB,	{ ANY_FREE		, ANY_FREE		, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " EQUp_cvb", EQUI_VCB,	{ ANY_FWD_FREE	, VAR_PTR_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " EQUp_vcb", EQUI_VCB,	{ VAR_PTR_R		, ANY_FWD_FREE	, FWD_BRANCH	}		, 0				, 0				}
	, { " EQUp_vvb", EQUI_VVB,	{ VAR_PTR_R		, VAR_PTR_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " EQUt_ccb", EQUI_CCB,	{ TGT_FREE		, TGT_FREE		, FWD_BRANCH	}		, YIELDS_GOTO | GAZL_2_ONLY, 0	}
	, { " EQUt_cvb", EQUI_VCB,	{ TGT_FWD_FREE	, VAR_TGT_R		, FWD_BRANCH	}		, SWAP_0_AND_1 | GAZL_2_ONLY, 0	}
	, { " EQUt_vcb", EQUI_VCB,	{ VAR_TGT_R		, TGT_FWD_FREE	, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " EQUt_vvb", EQUI_VVB,	{ VAR_TGT_R		, VAR_TGT_R		, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " FLOf_vc_", FLOF_CC_,	{ VAR_FLOAT_W	, CONST_FLOAT	, 0				}		, YIELDS_CONST	, CONST_FLOAT	}
	, { " FLOf_vv_", FLOF_VV_,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, 0				}		, 0				, 0				}
	, { " FORi_vcb", FORi_VCB,	{ VAR_INT_W		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { " FORi_vvb", FORi_VVB,	{ VAR_INT_W		, VAR_INT_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " FORp_vcb", FORi_VCB,	{ VAR_PTR_W		, FWD_FREE		, FWD_BRANCH	}		, 0				, 0				}
	, { " FORp_vvb", FORi_VVB,	{ VAR_PTR_W		, VAR_PTR_R		, FWD_BRANCH	}		, 0				, 0				}
#if GAZL_2
	, { " FREE____", FREE____,	{ 0				, 0				, 0				}		, GAZL_2_ONLY	, 0				}
#endif
	, { " FUNC____", FUNC_CC_,	{ 0				, 0				, 0				}		, 0				, FUNC			}
#if GAZL_2
	, { " GAZL_c__", GAZL____,	{ CONST_INT_P	, 0				, 0				}		, 0				, 0				}
#endif
	, { " GEQf_ccb", NLSF_CCB,	{ CONST_FLOAT	, CONST_FLOAT	, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " GEQf_cvb", NLSF_CVB,	{ CONST_FLOAT	, VAR_FLOAT_R	, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQf_vcb", NLSF_VCB,	{ VAR_FLOAT_R	, CONST_FLOAT	, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQf_vvb", NLSF_VVB,	{ VAR_FLOAT_R	, VAR_FLOAT_R	, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQi_ccb", NLSI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " GEQi_cvb", NLSI_CVB,	{ CONST_INT		, VAR_INT_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQi_vcb", NLSI_VCB,	{ VAR_INT_R		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQi_vvb", NLSI_VVB,	{ VAR_INT_R		, VAR_INT_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQp_ccb", NLSI_CCB,	{ ANY_FREE		, ANY_FREE		, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " GEQp_cvb", NLSI_CVB,	{ ANY_FWD_FREE	, VAR_PTR_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQp_vcb", NLSI_VCB,	{ VAR_PTR_R		, ANY_FWD_FREE	, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQp_vvb", NLSI_VVB,	{ VAR_PTR_R		, VAR_PTR_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " GEQt_ccb", NLSI_CCB,	{ TGT_FREE		, TGT_FREE		, FWD_BRANCH	}		, YIELDS_GOTO | GAZL_2_ONLY, 0	}
	, { " GEQt_cvb", NLSI_CVB,	{ TGT_FWD_FREE	, VAR_TGT_R		, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " GEQt_vcb", NLSI_VCB,	{ VAR_TGT_R		, TGT_FWD_FREE	, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " GEQt_vvb", NLSI_VVB,	{ VAR_TGT_R		, VAR_TGT_R		, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " GETL_vvv", GETL_VVV,	{ ANY_VAR_W		, ANY_VAR_FREE_R, VAR_INT_R		}		, 0				, 0				}
	, { " GLOB_s__", GLOB____,	{ CONST_INT_P	, 0				, 0				}		, 0				, FREE_ADDRESS	}
	, { " GOTO_b__", GOTO_B__,	{ FWD_BRANCH	, 0				, 0				}		, 0				, 0				}
	, { " GRTf_ccb", LSSF_CCB,	{ CONST_FLOAT	, CONST_FLOAT	, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO, 0	}
	, { " GRTf_cvb", LSSF_VCB,	{ CONST_FLOAT	, VAR_FLOAT_R	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTf_vcb", LSSF_CVB,	{ VAR_FLOAT_R	, CONST_FLOAT	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTf_vvb", LSSF_VVB,	{ VAR_FLOAT_R	, VAR_FLOAT_R	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTi_ccb", LSSI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO, 0	}
	, { " GRTi_cvb", LSSI_VCB,	{ CONST_INT		, VAR_INT_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTi_vcb", LSSI_CVB,	{ VAR_INT_R		, CONST_INT		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTi_vvb", LSSI_VVB,	{ VAR_INT_R		, VAR_INT_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTp_ccb", LSSI_CCB,	{ ANY_FREE		, ANY_FREE		, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO, 0	}
	, { " GRTp_cvb", LSSI_VCB,	{ ANY_FWD_FREE	, VAR_PTR_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTp_vcb", LSSI_CVB,	{ VAR_PTR_R		, ANY_FWD_FREE	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTp_vvb", LSSI_VVB,	{ VAR_PTR_R		, VAR_PTR_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " GRTt_ccb", LSSI_CCB,	{ TGT_FREE		, TGT_FREE		, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO | GAZL_2_ONLY, 0 }
	, { " GRTt_cvb", LSSI_VCB,	{ TGT_FWD_FREE	, VAR_TGT_R		, FWD_BRANCH	}		, SWAP_0_AND_1 | GAZL_2_ONLY, 0	}
	, { " GRTt_vcb", LSSI_CVB,	{ VAR_TGT_R		, TGT_FWD_FREE	, FWD_BRANCH	}		, SWAP_0_AND_1 | GAZL_2_ONLY, 0	}
	, { " GRTt_vvb", LSSI_VVB,	{ VAR_TGT_R		, VAR_TGT_R		, FWD_BRANCH	}		, SWAP_0_AND_1 | GAZL_2_ONLY, 0	}
	, { " INPf____", LOCA____,	{ 0				, 0				, 0				}		, 0				, VAR_FLOAT_R & ~TRANSIENT }
	, { " INPi____", LOCA____,	{ 0				, 0				, 0				}		, 0				, VAR_INT_R & ~TRANSIENT }
	, { " INPp____", LOCA____,	{ 0				, 0				, 0				}		, 0				, VAR_PTR_R & ~TRANSIENT }
	, { " INPt____", LOCA____,	{ 0				, 0				, 0				}		, GAZL_2_ONLY	, VAR_TGT_R & ~TRANSIENT }
	, { " IORi_vcc", IORI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST	, CONST_INT		}
	, { " IORi_vcv", IORI_VVC,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, SWAP_1_AND_2	, 0				}
	, { " IORi_vvc", IORI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, 0				, 0				}
	, { " IORi_vvv", IORI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " LEQf_ccb", NLSF_CCB,	{ CONST_FLOAT	, CONST_FLOAT	, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO, 0	}
	, { " LEQf_cvb", NLSF_VCB,	{ CONST_FLOAT	, VAR_FLOAT_R	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQf_vcb", NLSF_CVB,	{ VAR_FLOAT_R	, CONST_FLOAT	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQf_vvb", NLSF_VVB,	{ VAR_FLOAT_R	, VAR_FLOAT_R	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQi_ccb", NLSI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO, 0	}
	, { " LEQi_cvb", NLSI_VCB,	{ CONST_INT		, VAR_INT_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQi_vcb", NLSI_CVB,	{ VAR_INT_R		, CONST_INT		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQi_vvb", NLSI_VVB,	{ VAR_INT_R		, VAR_INT_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQp_ccb", NLSI_CCB,	{ ANY_FREE		, ANY_FREE		, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO, 0	}
	, { " LEQp_cvb", NLSI_VCB,	{ ANY_FWD_FREE	, VAR_PTR_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQp_vcb", NLSI_CVB,	{ VAR_PTR_R		, ANY_FWD_FREE	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQp_vvb", NLSI_VVB,	{ VAR_PTR_R		, VAR_PTR_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " LEQt_ccb", NLSI_CCB,	{ TGT_FREE		, TGT_FREE		, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO | GAZL_2_ONLY, 0 }
	, { " LEQt_cvb", NLSI_VCB,	{ TGT_FWD_FREE	, VAR_TGT_R		, FWD_BRANCH	}		, SWAP_0_AND_1 | GAZL_2_ONLY, 0	}
	, { " LEQt_vcb", NLSI_CVB,	{ VAR_TGT_R		, TGT_FWD_FREE	, FWD_BRANCH	}		, SWAP_0_AND_1 | GAZL_2_ONLY, 0	}
	, { " LEQt_vvb", NLSI_VVB,	{ VAR_TGT_R		, VAR_TGT_R		, FWD_BRANCH	}		, SWAP_0_AND_1 | GAZL_2_ONLY, 0	}
	, { " LOCA_s__", LOCA____,	{ CONST_INT_P	, 0				, 0				}		, 0				, ANY_VAR & ~TRANSIENT }
	, { " LOCf____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_FLOAT_R | VAR_FLOAT_W) & ~TRANSIENT }
	, { " LOCi____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_INT_R | VAR_INT_W) & ~TRANSIENT }
	, { " LOCp____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_PTR_R | VAR_PTR_W) & ~TRANSIENT }
	, { " LOCt____", LOCA____,	{ 0				, 0				, 0				}		, GAZL_2_ONLY	, (VAR_TGT_R | VAR_TGT_W) & ~TRANSIENT }
	, { " LSSf_ccb", LSSF_CCB,	{ CONST_FLOAT	, CONST_FLOAT	, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " LSSf_cvb", LSSF_CVB,	{ CONST_FLOAT	, VAR_FLOAT_R	, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSf_vcb", LSSF_VCB,	{ VAR_FLOAT_R	, CONST_FLOAT	, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSf_vvb", LSSF_VVB,	{ VAR_FLOAT_R	, VAR_FLOAT_R	, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSi_ccb", LSSI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " LSSi_cvb", LSSI_CVB,	{ CONST_INT		, VAR_INT_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSi_vcb", LSSI_VCB,	{ VAR_INT_R		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSi_vvb", LSSI_VVB,	{ VAR_INT_R		, VAR_INT_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSp_ccb", LSSI_CCB,	{ ANY_FREE		, ANY_FREE		, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " LSSp_cvb", LSSI_CVB,	{ ANY_FWD_FREE	, VAR_PTR_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSp_vcb", LSSI_VCB,	{ VAR_PTR_R		, ANY_FWD_FREE	, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSp_vvb", LSSI_VVB,	{ VAR_PTR_R		, VAR_PTR_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " LSSt_ccb", LSSI_CCB,	{ TGT_FREE		, TGT_FREE		, FWD_BRANCH	}		, YIELDS_GOTO | GAZL_2_ONLY, 0	}
	, { " LSSt_cvb", LSSI_CVB,	{ TGT_FWD_FREE	, VAR_TGT_R		, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " LSSt_vcb", LSSI_VCB,	{ VAR_TGT_R		, TGT_FWD_FREE	, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " LSSt_vvb", LSSI_VVB,	{ VAR_TGT_R		, VAR_TGT_R		, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
#if GAZL_2
	, { " MARK____", MARK____,	{ 0				, 0				, 0				}		, GAZL_2_ONLY	, 0				}
#endif
	, { " MODi_vcc", MODI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST|CHECK_DIV_BY_0, CONST_INT }
	, { " MODi_vcv", MODI_VCV,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, 0				, 0				}
	, { " MODi_vvc", MODI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, CHECK_DIV_BY_0, 0				}
	, { " MODi_vvv", MODI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " MOVE_vv_", MOVE_VV_,	{ ANY_VAR_W		, ANY_VAR_R		, 0				}		, 0				, 0				}
	, { " MOVf_vc_", MOVE_VC_,	{ VAR_FLOAT_W	, CONST_FLOAT	, 0				}		, 0				, 0				}
	, { " MOVf_vv_", MOVE_VV_,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, 0				}		, 0				, 0				}
	, { " MOVi_vc_", MOVE_VC_,	{ VAR_INT_W		, CONST_INT		, 0				}		, 0				, 0				}
	, { " MOVi_vv_", MOVE_VV_,	{ VAR_INT_W		, VAR_INT_R		, 0				}		, 0				, 0				}
	, { " MOVp_vc_", MOVE_VC_,	{ VAR_PTR_W		, ANY_FWD_FREE	, 0				}		, 0				, 0				}
	, { " MOVp_vv_", MOVE_VV_,	{ VAR_PTR_W		, VAR_PTR_R		, 0				}		, 0				, 0				}
	, { " MOVt_vc_", MOVE_VC_,	{ VAR_TGT_W		, TGT_FWD_FREE	, 0				}		, GAZL_2_ONLY	, 0				}
	, { " MOVt_vv_", MOVE_VV_,	{ VAR_TGT_W		, VAR_TGT_R		, 0				}		, GAZL_2_ONLY	, 0				}
	, { " MULf_vcc", MULF_CCC,	{ VAR_FLOAT_W	, CONST_FLOAT	, CONST_FLOAT	}		, YIELDS_CONST	, CONST_FLOAT	}
	, { " MULf_vcv", MULF_VVC,	{ VAR_FLOAT_W	, CONST_FLOAT	, VAR_FLOAT_R	}		, SWAP_1_AND_2	, 0				}
	, { " MULf_vvc", MULF_VVC,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, CONST_FLOAT	}		, 0				, 0				}
	, { " MULf_vvv", MULF_VVV,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, VAR_FLOAT_R	}		, 0				, 0				}
	, { " MULi_vcc", MULI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST	, CONST_FLOAT	}
	, { " MULi_vcv", MULI_VVC,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, SWAP_1_AND_2	, 0				}
	, { " MULi_vvc", MULI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, 0				, 0				}
	, { " MULi_vvv", MULI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " NEQf_ccb", NEQF_CCB,	{ CONST_FLOAT	, CONST_FLOAT	, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " NEQf_cvb", NEQF_VCB,	{ CONST_FLOAT	, VAR_FLOAT_R	, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " NEQf_vcb", NEQF_VCB,	{ VAR_FLOAT_R	, CONST_FLOAT	, FWD_BRANCH	}		, 0				, 0				}
	, { " NEQf_vvb", NEQF_VVB,	{ VAR_FLOAT_R	, VAR_FLOAT_R	, FWD_BRANCH	}		, 0				, 0				}
	, { " NEQi_ccb", NEQI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " NEQi_cvb", NEQI_VCB,	{ CONST_INT		, VAR_INT_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " NEQi_vcb", NEQI_VCB,	{ VAR_INT_R		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { " NEQi_vvb", NEQI_VVB,	{ VAR_INT_R		, VAR_INT_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " NEQp_ccb", NEQI_CCB,	{ ANY_FREE		, ANY_FREE		, FWD_BRANCH	}		, YIELDS_GOTO	, 0				}
	, { " NEQp_cvb", NEQI_VCB,	{ ANY_FWD_FREE	, VAR_PTR_R		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { " NEQp_vcb", NEQI_VCB,	{ VAR_PTR_R		, ANY_FWD_FREE	, FWD_BRANCH	}		, 0				, 0				}
	, { " NEQp_vvb", NEQI_VVB,	{ VAR_PTR_R		, VAR_PTR_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " NEQt_ccb", NEQI_CCB,	{ TGT_FREE		, TGT_FREE		, FWD_BRANCH	}		, YIELDS_GOTO | GAZL_2_ONLY, 0	}
	, { " NEQt_cvb", NEQI_VCB,	{ TGT_FWD_FREE	, VAR_TGT_R		, FWD_BRANCH	}		, SWAP_0_AND_1 | GAZL_2_ONLY, 0	}
	, { " NEQt_vcb", NEQI_VCB,	{ VAR_TGT_R		, TGT_FWD_FREE	, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " NEQt_vvb", NEQI_VVB,	{ VAR_TGT_R		, VAR_TGT_R		, FWD_BRANCH	}		, GAZL_2_ONLY	, 0				}
	, { " NOOP____", NOOP____,	{ 0				, 0				, 0				}		, 0				, 0				}
	, { " OUTf____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_FLOAT_R | VAR_FLOAT_W) & ~TRANSIENT }
	, { " OUTi____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_INT_R | VAR_INT_W) & ~TRANSIENT }
	, { " OUTp____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_PTR_R | VAR_PTR_W) & ~TRANSIENT }
	, { " OUTt____", LOCA____,	{ 0				, 0				, 0				}		, GAZL_2_ONLY	, (VAR_TGT_R | VAR_TGT_W) & ~TRANSIENT }
	, { " PARA_s__", LOCA____,	{ CONST_INT_P	, 0				, 0				}		, 0				, ANY_VAR & ~TRANSIENT }
	, { " PEEK_vc_", PEEK_VC_,	{ ANY_VAR_W		, FWD_ADDRESS_R	, 0				}		, 0				, 0				}
	, { " PEEK_vcv", PEEK_VCV,	{ ANY_VAR_W		, FWD_FREE_R	, VAR_INT_R		}		, 0				, 0				}
	, { " PEEK_vv_", PEEK_VCV,	{ ANY_VAR_W		, VAR_PTR_R		, 0				}		, SWAP_1_AND_2	, 0				}
	, { " PEEK_vvc", PEEK_VCV,	{ ANY_VAR_W		, VAR_PTR_R		, CONST_INT		}		, SWAP_1_AND_2	, 0				}
	, { " PEEK_vvv", PEEK_VVV,	{ ANY_VAR_W		, VAR_PTR_R		, VAR_INT_R		}		, 0				, 0				}
	, { " POKE_cc_", POKE_CC_,	{ FWD_ADDRESS_W	, KONST			, 0				}		, 0				, 0				}
	, { " POKE_cv_", POKE_CV_,	{ FWD_ADDRESS_W	, ANY_VAR_R		, 0				}		, 0				, 0				}
	, { " POKE_cvc", POKE_CVC,	{ FWD_FREE_W	, VAR_INT_R		, KONST			}		, 0				, 0				}
	, { " POKE_cvv", POKE_CVV,	{ FWD_FREE_W	, VAR_INT_R		, ANY_VAR_R		}		, 0				, 0				}
	, { " POKE_vc_", POKE_CVC,	{ VAR_PTR_R		, KONST			, 0				}		, SWAP_1_AND_2 | SWAP_0_AND_1	, 0				}
	, { " POKE_vcc", POKE_CVC,	{ VAR_PTR_R		, CONST_INT		, KONST			}		, SWAP_0_AND_1	, 0				}
	, { " POKE_vcv", POKE_CVV,	{ VAR_PTR_R		, CONST_INT		, ANY_VAR_R		}		, SWAP_0_AND_1	, 0				}
	, { " POKE_vv_", POKE_CVV,	{ VAR_PTR_R		, ANY_VAR_R		, 0				}		, SWAP_1_AND_2 | SWAP_0_AND_1	, 0				}
	, { " POKE_vvc", POKE_VVC,	{ VAR_PTR_R		, VAR_INT_R		, KONST			}		, 0				, 0				}
	, { " POKE_vvv", POKE_VVV,	{ VAR_PTR_R		, VAR_INT_R		, ANY_VAR_R		}		, 0				, 0				}
	, { " RETU____", RETU_C__,	{ 0				, 0				, 0				}		, 0				, 0				}
	, { " SETL_vvc", SETL_VVC,	{ ANY_VAR_FREE_W, VAR_INT_R		, KONST			}		, 0				, 0				}
	, { " SETL_vvv", SETL_VVV,	{ ANY_VAR_FREE_W, VAR_INT_R		, ANY_VAR_R		}		, 0				, 0				}
	, { " SHLi_vcc", SHLI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT_P	}		, YIELDS_CONST	, CONST_INT		}
	, { " SHLi_vcv", SHLI_VCV,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, 0				, 0				}
	, { " SHLi_vvc", SHLI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT_P	}		, 0				, 0				}
	, { " SHLi_vvv", SHLI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " SHRi_vcc", SHRI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST	, CONST_INT		}
	, { " SHRi_vcv", SHRI_VCV,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, 0				, 0				}
	, { " SHRi_vvc", SHRI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT_P	}		, 0				, 0				}
	, { " SHRi_vvv", SHRI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " SHRu_vcc", SHRU_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT_P	}		, YIELDS_CONST	, CONST_INT_P	}
	, { " SHRu_vcv", SHRU_VCV,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, 0				, 0				}
	, { " SHRu_vvc", SHRU_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT_P	}		, 0				, 0				}
	, { " SHRu_vvv", SHRU_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
#if GAZL_2
	, { " SPAN_os_", SPAN____,	{ CONST_INT_P	, CONST_INT_P	, 0				}		, GAZL_2_ONLY	, 0				}
#endif
	, { " SUBf_vcc", SUBF_CCC,	{ VAR_FLOAT_W	, CONST_FLOAT	, CONST_FLOAT	}		, YIELDS_CONST	, CONST_FLOAT	}
	, { " SUBf_vcv", SUBF_VCV,	{ VAR_FLOAT_W	, CONST_FLOAT	, VAR_FLOAT_R	}		, 0				, 0				}
	, { " SUBf_vvc", SUBF_VVC,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, CONST_FLOAT	}		, 0				, 0				}
	, { " SUBf_vvv", SUBF_VVV,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, VAR_FLOAT_R	}		, 0				, 0				}
	, { " SUBi_vcc", SUBI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST	, CONST_INT		}
	, { " SUBi_vcv", SUBI_VCV,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, 0				, 0				}
	, { " SUBi_vvc", SUBI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, 0				, 0				}
	, { " SUBi_vvv", SUBI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " SUBp_vcc", SUBI_CCC,	{ VAR_PTR_W		, FREE_ADDRESS	, CONST_INT		}		, YIELDS_CONST	, FREE_ADDRESS		}
	, { " SUBp_vcv", SUBI_VCV,	{ VAR_PTR_W		, FWD_FREE		, VAR_INT_R		}		, 0				, 0				}
	, { " SUBp_vvc", SUBI_VVC,	{ VAR_PTR_W		, VAR_PTR_R		, CONST_INT		}		, 0				, 0				}
	, { " SUBp_vvv", SUBI_VVV,	{ VAR_PTR_W		, VAR_PTR_R		, VAR_INT_R		}		, 0				, 0				}
	, { " SWCH_vsb", SWCH_VCC,	{ VAR_INT_R		, CONST_INT_P	, FWD_BRANCH	}		, BRANCH_IS_TABLE, 0			}
#if GAZL_2
	, { " TAIL_cs_", TAIL_CC_,	{ FUNC|TARGET|FORWARD, CONST_INT_P, 0			}		, GAZL_2_ONLY	, 0				}
	, { " TAIL_vs_", TAIL_VC_,	{ VAR_PTR_R|VAR_TGT_R, CONST_INT_P, 0			}		, GAZL_2_ONLY	, 0				}
#endif
	, { " TEMP_s__", GLOB____,	{ CONST_INT_P	, 0				, 0				}		, 0				, FREE_ADDRESS | TEMPORARY }
	, { " XORi_vcc", XORI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST	, CONST_INT		}
	, { " XORi_vcv", XORI_VVC,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, SWAP_1_AND_2	, 0				}
	, { " XORi_vvc", XORI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, 0				, 0				}
	, { " XORi_vvv", XORI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " fTOi_vcc", FTOI_CCC,	{ VAR_INT_W		, CONST_FLOAT	, CONST_FLOAT	}		, YIELDS_CONST	, CONST_INT		}
	, { " fTOi_vvc", FTOI_VVC,	{ VAR_INT_W		, VAR_FLOAT_R	, CONST_FLOAT	}		, 0				, 0				}
	, { " iTOf_vcc", ITOF_CCC,	{ VAR_FLOAT_W	, CONST_INT		, CONST_FLOAT	}		, YIELDS_CONST	, CONST_FLOAT	}
	, { " iTOf_vvc", ITOF_VVC,	{ VAR_FLOAT_W	, VAR_INT_R		, CONST_FLOAT	}		, 0				, 0				}

	, { "!ABSf_cc_", ABSF_CC_,	{ COMPILE_TIME	, CONST_FLOAT	, 0				}		, 0				, CONST_FLOAT	}
	, { "!ABSi_cc_", ABSI_CC_,	{ COMPILE_TIME	, CONST_INT		, 0				}		, 0				, CONST_INT_P	}
	, { "!ADDf_ccc", ADDF_CCC,	{ COMPILE_TIME	, CONST_FLOAT	, CONST_FLOAT	}		, 0				, CONST_FLOAT	}
	, { "!ADDi_ccc", ADDI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT		}
	, { "!ADDp_ccc", ADDI_CCC,	{ COMPILE_TIME	, FREE_ADDRESS	, CONST_INT		}		, 0				, FREE_ADDRESS		}
	, { "!ANDi_ccc", ANDI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT		}
	, { "!DEFf_c__", DEFI____,	{ CONST_FLOAT	, 0				, 0				}		, 0				, CONST_FLOAT	}
	, { "!DEFi_c__", DEFI____,	{ CONST_INT		, 0				, 0				}		, 0				, CONST_INT		}
	, { "!DEFp_c__", DEFI____,	{ ANY_FREE		, 0				, 0				}		, 0				, POINTER_CONSTANT	}
#if GAZL_2
	, { "!DEFt_c__", DEFI____,	{ TGT_FREE		, 0				, 0				}		, GAZL_2_ONLY	, POINTER_CONSTANT	}
#endif
	, { "!DIFp_ccc", SUBI_CCC,	{ COMPILE_TIME	, FREE_ADDRESS	, FREE_ADDRESS	}		, 0				, CONST_INT		}
	, { "!DIVf_ccc", DIVF_CCC,	{ COMPILE_TIME	, CONST_FLOAT	, CONST_FLOAT	}		, 0				, CONST_FLOAT	}
	, { "!DIVi_ccc", DIVI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, CHECK_DIV_BY_0, CONST_INT		}
	, { "!EQUi_ccb", EQUI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { "!FLOf_cc_", FLOF_CC_,	{ COMPILE_TIME	, CONST_FLOAT	, 0				}		, 0				, CONST_FLOAT	}
	, { "!GEQi_ccb", NLSI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { "!GOTO_b__", SKIP_B__,	{ FWD_BRANCH	, 0				, 0				}		, 0				, 0				}
	, { "!GRTi_ccb", LSSI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { "!IFDF_cb_", IFDF_CB_,	{ KONST			, FWD_BRANCH	, 0				}		, 0				, 0				}
	, { "!IFDF_nb_", IFDF_CB_,	{ NATIVE		, FWD_BRANCH	, 0				}		, 0				, 0				}
	, { "!IFND_cb_", IFND_CB_,	{ KONST			, FWD_BRANCH	, 0				}		, 0				, 0				}
	, { "!IFND_nb_", IFND_CB_,	{ NATIVE		, FWD_BRANCH	, 0				}		, 0				, 0				}
	, { "!IORi_ccc", IORI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT		}
	, { "!LEQi_ccb", NLSI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, SWAP_0_AND_1	, 0				}
	, { "!LSSi_ccb", LSSI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { "!MODi_ccc", MODI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, CHECK_DIV_BY_0, CONST_INT		}
	, { "!MOVf_cc_", MOVE_CC_,	{ COMPILE_TIME	, CONST_FLOAT	, 0				}		, 0				, CONST_FLOAT	}
	, { "!MOVi_cc_", MOVE_CC_,	{ COMPILE_TIME	, CONST_INT		, 0				}		, 0				, CONST_INT		}
	, { "!MOVp_cc_", MOVE_CC_,	{ COMPILE_TIME	, FREE_ADDRESS	, 0				}		, 0				, FREE_ADDRESS		}
	, { "!MULf_ccc", MULF_CCC,	{ COMPILE_TIME	, CONST_FLOAT	, CONST_FLOAT	}		, 0				, CONST_FLOAT	}
	, { "!MULi_ccc", MULI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT		}
	, { "!NEQi_ccb", NEQI_CCB,	{ CONST_INT		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { "!SHLi_ccc", SHLI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT		}
	, { "!SHRi_ccc", SHRI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT		}
	, { "!SHRu_ccc", SHRU_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT_P	}
	, { "!SUBf_ccc", SUBF_CCC,	{ COMPILE_TIME	, CONST_FLOAT	, CONST_FLOAT	}		, 0				, CONST_FLOAT	}
	, { "!SUBi_ccc", SUBI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT		}
	, { "!SUBp_ccc", SUBI_CCC,	{ COMPILE_TIME	, FREE_ADDRESS	, CONST_INT		}		, 0				, FREE_ADDRESS		}
	, { "!XORi_ccc", XORI_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_INT		}		, 0				, CONST_INT		}
	, { "!fTOi_ccc", FTOI_CCC,	{ COMPILE_TIME	, CONST_FLOAT	, CONST_FLOAT	}		, 0				, CONST_INT		}
	, { "!iTOf_ccc", ITOF_CCC,	{ COMPILE_TIME	, CONST_INT		, CONST_FLOAT	}		, 0				, CONST_FLOAT	}
};
const Int OPERATOR_COUNT = sizeof (OPERATORS) / sizeof (*OPERATORS);

static void scanOperandRoles(Int opcode, OperandRole roles[3]) {
	roles[0] = roles[1] = roles[2] = OPERAND_OTHER;
	const int READ = (VAR_INT_R | VAR_FLOAT_R | VAR_PTR_R) & ~TRANSIENT;
	const int WRITE = (VAR_INT_W | VAR_FLOAT_W | VAR_PTR_W) & ~TRANSIENT;
	for (Int i = 0; i < OPERATOR_COUNT; ++i) {
		const Operator& op = OPERATORS[i];
		if (op.opcode != opcode || (op.otherFlags & (SWAP_0_AND_1 | SWAP_1_AND_2)) != 0) {	// a swapping row is an alias of the stored order
			continue;
		}
		for (int j = 0; j < 3; ++j) {
			if ((op.accepts[j] & WRITE) != 0) {
				roles[j] = OPERAND_SLOT_WRITE;
			} else if ((op.accepts[j] & READ) != 0) {
				roles[j] = OPERAND_SLOT_READ;
			}
		}
		if (opcode == FORi_VVB || opcode == FORi_VCB) {
			roles[0] = OPERAND_SLOT_READ_WRITE;
		}
		return;
	}
}

namespace {
struct OperandRoleTable {																// built once: the JIT asks for every instruction, on every pass
	OperandRoleTable() {
		for (int i = 0; i < FINALIZED_OPCODE_COUNT; ++i) {
			scanOperandRoles(FIRST_OPCODE_VALUE + i, roles[i]);
		}
	}
	OperandRole roles[FINALIZED_OPCODE_COUNT][3];
};
}
static const OperandRoleTable OPERAND_ROLE_TABLE;

void operandRoles(Int opcode, OperandRole roles[3]) {
	const Int index = opcode - FIRST_OPCODE_VALUE;
	if (index < 0 || index >= FINALIZED_OPCODE_COUNT) {
		scanOperandRoles(opcode, roles);
		return;
	}
	const OperandRole* const cached = OPERAND_ROLE_TABLE.roles[index];
	roles[0] = cached[0];
	roles[1] = cached[1];
	roles[2] = cached[2];
}

static bool isValidIdentifierChar(Char c) {
	return (c == '_' || c == '.' || c == '$' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

static bool isValidIdentifier(const Char* b, const Char* e) {
	if (b == e) return false;
	if (*b == '_' || *b == '.' || *b == '$' || (*b >= 'A' && *b <= 'Z') || (*b >= 'a' && *b <= 'z'))
		while (++b < e && isValidIdentifierChar(*b)) ;
	return (b == e);
}

const char* Exception::what() const throw() {
	assert(0 <= error && error < ASSEMBLER_ERROR_COUNT);
	if (detail.empty()) return ASSEMBLER_ERROR_TEXTS[int(error)];
	else if (whatString.empty()) whatString = std::string(ASSEMBLER_ERROR_TEXTS[int(error)]) + ": " + detail;
	return whatString.c_str();
}

Symbols::Symbol* Symbols::define(const std::string& name, int types, Value value, UInt words) {
	const Char* nameBegin = name.data();
	const Char* nameEnd = nameBegin + name.size();
	if (!isValidIdentifier(nameBegin, nameEnd)) throw Exception(INVALID_IDENTIFIER, name);
	
	if ((types & CONST_INT_P) != 0 && value.i < 0) types &= ~CONST_INT_P;
	if ((types & CONST_INT_N) != 0 && value.i >= 0) types &= ~CONST_INT_N;
	SymbolMap::iterator it = symbols.find(name);
	if (it != symbols.end()) throw Exception(SYMBOL_ALREADY_DEFINED, name);
	return &symbols.insert(it, std::pair<std::string, Symbol>(name, Symbol(types, value, words)))->second;
}

void Symbols::registerSwitch(const Char* labelBegin, const Char* labelEnd, UInt switchSize
		, Value* storage, Int offset) {
	std::string name(labelBegin, labelEnd);
	if (!isValidIdentifier(labelBegin, labelEnd)) throw Exception(INVALID_IDENTIFIER, name);
	forwardRefs.push_back(Reference(name, storage, FWD_BRANCH, offset, switchSize));
}

void Symbols::resolve(const Reference& ref, const Symbol& symbol) {
	if ((symbol.types & ref.accepts) == 0) throw Exception(INCOMPATIBLE_TYPES, ref.label);
	assert((ref.accepts == FWD_BRANCH) == (symbol.types == BRANCH));
	assert(ref.accepts != FWD_BRANCH || ref.offset < 0);											// a branch's offset is its distance back to the jump
	// Only data and locals take an offset, `:0` included: `&main:1` would name another function, `&NULL:1` no pointer.
	if ((ref.accepts & OFFSET_GIVEN) != 0 && ((symbol.types & (ADDRESS | ANY_VAR)) == 0
			|| (symbol.types & POINTER_CONSTANT) != 0)) {
		throw Exception(OFFSET_OUT_OF_BOUNDS, ref.label);
	}
	if ((ref.accepts & UNCHECKED_ADDRESS) == 0 && (ref.offset < symbol.minOffset || ref.offset > symbol.maxOffset)) {
		throw Exception(OFFSET_OUT_OF_BOUNDS, ref.label);
	}
	if (ref.offset == 0) *ref.storage = symbol.value;
	else {
		assert((symbol.types & CONST_FLOAT) == 0);
		ref.storage->i = static_cast<Int>(static_cast<UInt>(symbol.value.i) + static_cast<UInt>(ref.offset));	// wraps: a huge `k` is caught by the range checks, never undefined
	}
}

bool Symbols::lookup(const Char* nameBegin, const Char* nameEnd, int acceptedTypes, int& types, Value& value) const {
	assert(isValidIdentifier(nameBegin, nameEnd));
	SymbolMap::const_iterator symbolIt = symbols.find(std::string(nameBegin, nameEnd));
	if (symbolIt == symbols.end() || (symbolIt->second.types & acceptedTypes) == 0) return false;
	types = symbolIt->second.types;
	value = symbolIt->second.value;
	return true;
}

const Symbols::Symbol* Symbols::link(const Char* labelBegin, const Char* labelEnd, Value* storage, int accepts
		, Int offset) {
	// Note: it would be OK to forward all forwardables directly (according to typeMask) if that would improve performance.
	std::string name(labelBegin, labelEnd);
	if (!isValidIdentifier(labelBegin, labelEnd)) throw Exception(INVALID_IDENTIFIER, name);
	Reference ref(name, storage, accepts, offset, 0);
	SymbolMap::const_iterator symbolIt = symbols.find(ref.label);
	if (symbolIt != symbols.end()) {
		resolve(ref, symbolIt->second);
		return &symbolIt->second;
	}
	if ((accepts & FORWARD) == 0) throw Exception(NON_FORWARD_SYMBOL_NOT_FOUND, name);
	forwardRefs.push_back(ref);
	return 0;
}

void Symbols::resolveForwardRefs() {
	for (std::vector<Reference>::const_iterator refIt = forwardRefs.begin(); refIt != forwardRefs.end(); ++refIt) {
		SymbolMap::const_iterator symbolIt = symbols.find(refIt->label);
		if (symbolIt == symbols.end()) throw Exception(FORWARD_SYMBOL_NOT_FOUND, refIt->label);
		resolve(*refIt, symbolIt->second);
		if (refIt->switchSize != 0) {
			refIt->storage[refIt->switchSize] = refIt->storage[0];
			Reference ref(*refIt);
			for (UInt i = 0; i < ref.switchSize; ++i) {
				refIt->storage[i] = refIt->storage[refIt->switchSize];
				Char buffer[33];
				ref.label = refIt->label + '.' + int2string(i, 10, 1, buffer);
				ref.storage = refIt->storage + i;
				SymbolMap::iterator symbolIt = symbols.find(ref.label);
				if (symbolIt != symbols.end()) resolve(ref, symbolIt->second);
			}
		}
	}
	forwardRefs.clear();	
}

void Symbols::registerNative(const Char* name, Int index) {
	Value v;
	v.i = index;
	define(name, NATIVE, v, 1);
}

Pointer Symbols::findGlobal(const Char* name, UInt& declaredWords) const {
	GlobalInfo info;
	if (!getGlobalInfo(name, info)) return NULL_POINTER;
	declaredWords = info.declaredWords;
	return info.address;
}

Target Symbols::findFunction(const Char* name) const {
	int types;
	Value value;
	const Char* nameBegin = name;
	const Char* nameEnd = name + strlen(name);
	if (!isValidIdentifier(nameBegin, nameEnd)) return NULL_TARGET;
	if (!lookup(nameBegin, nameEnd, FUNC | TARGET, types, value)) return NULL_TARGET;	// a function of either dialect, as `CALL` takes
	return value.t;
}

void Symbols::defineConstant(const Char* name, bool asFloat, const Value& value) {
	define(name, asFloat ? CONST_FLOAT : CONST_INT, value);
}

bool Symbols::lookupConstant(const Char* name, ConstantKind& kind, Value& value) const {
	const Char* const nameEnd = name + strlen(name);
	int types;
	if (!isValidIdentifier(name, nameEnd)
			|| !lookup(name, nameEnd, CONST_INT | CONST_FLOAT | POINTER_CONSTANT | NULL_PTR, types, value)) {
		return false;
	}
	kind = ((types & CONST_FLOAT) != 0 ? FLOAT_CONSTANT
			: ((types & (FUNC | TARGET)) != 0 ? TARGET_CONSTANT
			: ((types & (POINTER_CONSTANT | NULL_PTR)) != 0 ? ADDRESS_CONSTANT : INT_CONSTANT)));
	return true;
}

static bool isData(int types) {												// a `! DEFp` name has an address type too, but names no data of its own
	return (types & ADDRESS) != 0 && (types & POINTER_CONSTANT) == 0;
}

bool Symbols::findFirstGlobal(Iterator& iterator) const {
	for (iterator = symbols.begin(); iterator != symbols.end() && !isData(iterator->second.types); ++iterator);
	return (iterator != symbols.end());
}

bool Symbols::findNextGlobal(Iterator& iterator) const {
	for (++iterator; iterator != symbols.end() && !isData(iterator->second.types); ++iterator);
	return (iterator != symbols.end());
}

GlobalInfo Symbols::getGlobalInfo(const Iterator& iterator) const {
	assert(isData(iterator->second.types));
	GlobalInfo info;
	info.name = iterator->first.c_str();
	info.address = iterator->second.value.p;
	info.declaredWords = iterator->second.declaredWords;
	info.ownedWords = iterator->second.ownedWords;
	const int types = iterator->second.types;
	info.section = ((types & ADDRESS_W) == 0 ? CNST_SECTION : ((types & TEMPORARY) != 0 ? TEMP_SECTION : GLOB_SECTION));
	return info;
}

bool Symbols::getGlobalInfo(const Char* name, GlobalInfo& info) const {
	const Iterator it = symbols.find(name);
	if (it == symbols.end() || !isData(it->second.types)) {
		const GlobalInfo none = { 0, NULL_POINTER, 0, 0, GLOB_SECTION };
		info = none;
		return false;
	}
	info = getGlobalInfo(it);
	return true;
}

Assembler::Assembler(UInt maxCodeSize, Instruction* codeBase, UInt maxFunctionCount, UInt* functionTable
		, UInt maxMemorySize, Value* memoryBase, Symbols& globals)
		: codeBase(codeBase), codeEnd(codeBase + maxCodeSize), maxFunctionCount(maxFunctionCount)
		, functionTable(functionTable), memoryBase(memoryBase), memoryEnd(memoryBase + maxMemorySize)
		, ip(codeBase), functionStart(0), functionCount(0), localsSize(0), maxLocalsSize(0), markDepth(0)
		, paramsSize(0), globalsPointer(memoryBase)
		, constantsPointer(memoryEnd), dataLabelType(0), dataPointer(0), dataEnd(0), dataIsConstant(false)
		, sectionBegin(0), sectionEnd(0), regionStart(0), regionExtent(-1), dialect(1), globals(globals) {
	for (Int i = 0; i < 128; ++i) compileTimeVars[i].types = 0;
	Value v;
	v.i = 0;
	*--constantsPointer = v;	// Extra null at end of memory to stop any (native) null-terminating string scanning go wild.
	globals.define(NULL_STRING, NULL_PTR, v, 1);
	v.i = VERSION;
	globals.define(GAZL_VERSION_STRING, CONST_INT_P, v, 1);
	v.i = WORD_SIZE;
	globals.define(GAZL_WORD_SIZE_STRING, CONST_INT_P, v, 1);
	v.i = maxMemorySize;
	globals.define(GAZL_MEMORY_SIZE_STRING, CONST_INT_P, v, 1);
}

Symbols::Symbol* Assembler::declare(Symbols& where, const Char* labelBegin, const Char* labelEnd, int types, Value value
		, UInt words) {
	assert((types & FORWARD) == 0);
	if (labelBegin == labelEnd) return 0;
	const Char* p = reinterpret_cast<const Char*>(memchr(labelBegin, '#', labelEnd - labelBegin));
	if (p == 0) p = labelEnd;
	std::string name(labelBegin, p);
	if (!isValidIdentifier(labelBegin, p)) throw Exception(INVALID_IDENTIFIER, name);
	if (p < labelEnd) {
		Value v;
		parseConstant(p + 1, labelEnd, CONST_INT, &v);
		Char buffer[33];
		name += '.';
		name += int2string(v.i, 10, 1, buffer);
	}
	return where.define(name, types, value, words);
}

int Assembler::operatorCompare(const void* a, const void* b) {
	return strcmp(reinterpret_cast<const Char*>(a), reinterpret_cast<const Operator*>(b)->key);
}

const Operator* Assembler::findOperator(const Char* key, const Char* b, const Char* e) const {
	const Operator* const op = reinterpret_cast<const Operator*>(bsearch(key, OPERATORS, OPERATOR_COUNT
			, sizeof (*OPERATORS), operatorCompare));
	if (op == 0 || (dialect < 2 && (op->otherFlags & GAZL_2_ONLY) != 0)) {
		throw Exception(INVALID_INSTRUCTION, b, e);
	}
	return op;
}

Char Assembler::parseOperandType(const Char* b, const Char* e) {
	switch (b < e ? *b : 0) {
		case '#': case '<': case '&':
					return 'c';
		case 0:		return '_';
		case '@':	return 'b';
		case '^':	return 'n';
		case '*':	return 's';
		case ':':	return 'o';
		case '%':	return 'v';
		default:	return 'v';
	}
}

const Symbols::Symbol* Assembler::linkWithOffset(Symbols& defs, const char* b, const char* e, int accepts, Value* v) {
	Value o;
	o.i = 0;
	const Char* p = reinterpret_cast<const Char*>(memchr(b, ':', e - b));
	if (p != 0) {
		parseConstant(p + 1, e, CONST_INT, &o);											// either sign: the check is against the section or frame
		accepts |= OFFSET_GIVEN;
	} else {
		p = e;
	}
	return defs.link(b, p, v, accepts, o.i);
}

Assembler::Operand Assembler::parseConstant(const Char* b, const Char* e, int accepts, Value* v) {
	Operand read = { 0, 0 };
	if ((accepts & KONST) == 0) throw Exception(DID_NOT_EXPECT_CONSTANT, b, e);
	switch (b < e ? *b : 0) {
		case '\'': case '+': case '-': case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7': case '8': case '9':
					{
						const Char* p = b;
						v->i = stringToInt(p, e);
						if (p == e) {
							if ((accepts & CONST_INT) == 0) throw Exception(DID_NOT_EXPECT_CONSTANT_INT, b, e);
							if (v->i < 0 && (accepts & CONST_INT_N) == 0) throw Exception(NEGATIVE_VALUE_NOT_ACCEPTED, b, e);
						} else {
							p = b;
							v->f = parseFloat(p, e);
							if (p != e) throw Exception(INVALID_NUMERICAL_LITERAL, p, e);
							if ((accepts & CONST_FLOAT) == 0) throw Exception(DID_NOT_EXPECT_CONSTANT_FLOAT, p, e);
						}
						break;
					}
		
		case '<':	{
						if (b + 3 != e || b[2] != '>' || !isValidIdentifierChar(b[1]))
							throw Exception(INVALID_COMPILE_TIME_VARIABLE, b, e);
						const CompileTimeVar& cv = compileTimeVars[(unsigned char)b[1]];
						if ((accepts & cv.types) == 0) throw Exception(INCOMPATIBLE_COMPILE_TIME_VARIABLE_TYPE, b, e);
						if ((accepts & UNCHECKED_ADDRESS) == 0 && (accepts & cv.types & ADDRESS) != 0) {
							throw Exception(OFFSET_OUT_OF_BOUNDS, b, e);									// an access, checked only here, and the variable names no label to check it against
						}
						*v = cv.value;
						read.variableTypes = cv.types;
						break;
					}

		default:	read.symbol = globals.link(b, e, v, accepts); break;
	}
	return read;
}

Assembler::Operand Assembler::parseOperand(const Char* b, const Char* e, int accepts, Value* v) {
	Operand read = { 0, 0 };
	switch (b < e ? *b : 0) {
		case 0:		if (accepts != 0) throw Exception(MISSING_OPERAND); break;
		case '#':	if (dialect >= 2) accepts &= ~(FREE_ADDRESS | TEMPORARY | NULL_PTR | FUNC | TARGET);	// inside a GAZL 2 region `#` names only numbers
					/* continue */
		case '*':
		case ':':	++b; /* continue */
		case '<':	read = parseConstant(b, e, accepts, v); break;
		case '@':	locals.link(b + 1, e, v, accepts, -(Int)(ip - codeBase)); break;		

		case '%':	assert((accepts & TRANSIENT) != 0);
					parseConstant(++b, e, CONST_INT_P, v);
					if (v->i == 0x7FFFFFFF) throw Exception(FRAME_TOO_LARGE);
					paramsSize = maximum(paramsSize, (UInt)(v->i + 1));
					break;		

		case '^':	if ((accepts & NATIVE) == 0) throw Exception(DID_NOT_EXPECT_ADDRESS, b, e);
					read.symbol = linkWithOffset(globals, b + 1, e, accepts, v);
					break;

		case '&':	if (dialect >= 2) {
						accepts &= ~(CONST_INT | CONST_FLOAT);	// inside a GAZL 2 region `&` names only pointers
						if ((accepts & ADDRESS) != 0) {
							accepts &= ~FUNC;			// and a data pointer never names a function, not even a GAZL 1 one
						}
					}
					if ((accepts & (ADDRESS | FUNC | TARGET)) == 0) throw Exception(DID_NOT_EXPECT_ADDRESS, b, e);
					read.symbol = linkWithOffset(globals, b + 1, e, accepts, v);
					break;

		default:	assert((accepts & ANY_VAR) != 0);
					linkWithOffset(locals, b, e, accepts, v);
					if (v->i < 0 || ((accepts & UNCHECKED_ADDRESS) == 0 && v->i >= static_cast<Int>(localsSize))) {
						throw Exception(OFFSET_OUT_OF_BOUNDS, b, e);					// never below the frame: GETL / SETL check only its top at run time
					}
					v->i -= localsSize;
					break;
	}
	return read;
}

void Assembler::declareData(const Char* labelBegin, const Char* labelEnd, const Value* first, UInt declaredWords) {
	if (labelBegin == labelEnd) return;
	Value v;
	v.p = makeGAZLPointer(first, dataIsConstant);
	Symbols::Symbol* const label = declare(globals, labelBegin, labelEnd, dataLabelType, v
			, static_cast<UInt>(sectionEnd - first));
	label->declaredWords = declaredWords;
	label->minOffset = -static_cast<Int>(first - sectionBegin);
	dataLabels.push_back(label);
}

bool Assembler::lowerAddress(const Symbols::Symbol* a, const Symbols::Symbol* b) { return a->value.p < b->value.p; }

/*
	A stable sort by address keeps source order among labels on one word, so the later one owns its words. Words before
	the first label are only refused in a `GLOB` section, whose words freezing must save by name.
*/
void Assembler::closeDataSection() {
	if (sectionBegin == 0) return;																// no section yet
	std::stable_sort(dataLabels.begin(), dataLabels.end(), lowerAddress);
	const Pointer begin = makeGAZLPointer(sectionBegin, dataIsConstant);
	const Pointer end = begin + static_cast<Pointer>(sectionEnd - sectionBegin);
	const bool frozen = (!dataIsConstant && (dataLabelType & TEMPORARY) == 0);
	if (frozen && (dataLabels.empty() || dataLabels[0]->value.p != begin)) throw Exception(UNNAMED_DATA_SECTION);
	for (size_t i = 0; i < dataLabels.size(); ++i) {
		const Pointer next = (i + 1 < dataLabels.size() ? dataLabels[i + 1]->value.p : end);
		dataLabels[i]->ownedWords = static_cast<UInt>(next - dataLabels[i]->value.p);
	}
	dataLabels.clear();
}

Pointer Assembler::makeGAZLPointer(const Value* data, bool constant) const {
	if (constant) {
		assert(data >= constantsPointer && data <= memoryEnd - 1);
		return MEMORY_OFFSET - (Pointer)(memoryEnd - 1 - data);						// counted down from the zero guard
	} else {
		assert(data >= memoryBase && data <= globalsPointer);
		return MEMORY_OFFSET + (Pointer)(data - memoryBase);
	}
}

void Assembler::finalizeFunction() {
	assert(functionStart != 0);
	assert(functionStart->opcode == FUNC_CC_);
	if (ip == codeBase || (ip[-1].opcode != RETU_C__ && ip[-1].opcode != GOTO_B__
			&& ip[-1].opcode != TAIL_CC_ && ip[-1].opcode != TAIL_VC_)) {
		throw Exception(MISSING_RETURN_INSTRUCTION);
	}
	/*
		A jump or switch may never reach a `FUNC` (`FUNC` is `CALL`-only). Branch targets resolve to local `BRANCH`
		labels; a label left at the function boundary `ip` (a trailing elided `NOOP`) is where the next `FUNC` lands.
	*/
	for (Symbols::SymbolMap::const_iterator it = locals.symbols.begin(); it != locals.symbols.end(); ++it) {
		if ((it->second.types & BRANCH) != 0 && it->second.value.p == static_cast<Pointer>(ip - codeBase)) {
			throw Exception(LABEL_ON_FUNCTION);
		}
	}
	if (markDepth != 0) throw Exception(UNBALANCED_MARK);
	functionStart->p0.i = maxLocalsSize;
	functionStart->p1.i = paramsSize;
	functionStart = 0;
	localsSize = maxLocalsSize = paramsSize = 0;
	locals.resolveForwardRefs();
	locals.clear();
}

/*
	Branch threading and return duplication (design/gazl/GAZLAssemblerOptimizations.md items 4 and 5). Both rewrite an
	opcode or a displacement in place, so no address moves and nothing needs patching.
*/
void Assembler::threadBranches() {
	const int OPCODE_SPAN = DEFI____ - FIRST_OPCODE_VALUE + 1;
	int slots[OPCODE_SPAN];																		// which operand is a relative branch, from the table, or -1
	for (int i = 0; i < OPCODE_SPAN; ++i) slots[i] = -1;
	for (int i = 0; i < OPERATOR_COUNT; ++i) {
		if ((OPERATORS[i].otherFlags & BRANCH_IS_TABLE) != 0) continue;
		for (int k = 0; k < 3; ++k) {
			if ((OPERATORS[i].accepts[k] & BRANCH) != 0) {
				slots[OPERATORS[i].opcode - FIRST_OPCODE_VALUE] = k;
			}
		}
	}
	for (Instruction* p = codeBase; p < ip; ++p) {
		const int slot = slots[p->opcode - FIRST_OPCODE_VALUE];
		if (slot < 0) continue;
		Value* const disp = (slot == 0 ? &p->p0 : (slot == 1 ? &p->p1 : &p->p2));
		Instruction* target = p + disp->i;
		for (int hops = 0; hops < 64; ++hops) {              // `a: GOTO @a` must not spin
			if (target < codeBase || target >= ip || target->opcode != GOTO_B__) break;
			target += target->p0.i;
		}
		if (target < codeBase || target >= ip) continue;     // anything odd is left exactly as it was
		if (p->opcode == GOTO_B__ && target->opcode == RETU_C__) {
			p->opcode = RETU_C__;                            // RETU reads no operands and is frame-agnostic
		} else {
			disp->i = static_cast<Int>(target - p);
		}
	}
}

/*
	Runs when a region closes, because only then is its claim known: the region before the first SPAN claims just the
	words it wrote. Filling within a region only appends, so disjoint claims mean no word is written twice.
*/
void Assembler::closeDataRegion() {
	if (dataPointer == 0) return;
	const Int b = regionStart;
	const Int e = (regionExtent < 0 ? (Int)(dataPointer - sectionBegin) : regionStart + regionExtent);
	if (e == b) return;
	const std::pair<Int, Int> claim(b, e);				// kept sorted by start, so overlap is a neighbour test
	const std::vector< std::pair<Int, Int> >::iterator it
			= std::lower_bound(dataRegions.begin(), dataRegions.end(), claim);
	if ((it != dataRegions.end() && it->first < e) || (it != dataRegions.begin() && (it - 1)->second > b))
		throw Exception(OVERLAPPING_DATA_REGIONS, dataLabel);
	dataRegions.insert(it, claim);
}

void Assembler::finalize(ProgramSizes& sizes) {
	newUnit(0);
	if (dialect != 1) throw Exception(UNCLOSED_GAZL_REGION);	// so a unit concatenated after this one starts in GAZL 1
	threadBranches();
	closeDataRegion();
	closeDataSection();
	globals.resolveForwardRefs();
	/*
		Constants were placed downward from the top during assembly, with addresses counted down from the base.
		Moving them to just below the globals puts every word at its address; nothing needs patching.
	*/
	const UInt constantsSize = (UInt)(memoryEnd - 1 - constantsPointer);		// the top word is the zero guard
	if (constantsSize != 0) {
		const std::vector<Value> constants(constantsPointer, memoryEnd - 1);
		memmove(memoryBase + constantsSize, memoryBase, (globalsPointer - memoryBase) * sizeof (Value));
		memcpy(memoryBase, &constants[0], constantsSize * sizeof (Value));
	}
	// Poison the data stack, so a local read before its first write gets 0xDEADBEEF rather than a plausible value.
	Value* const stackEnd = memoryEnd - 1;
	for (Value* p = memoryBase + constantsSize + (globalsPointer - memoryBase); p < stackEnd; ++p) p->p = 0xDEADBEEF;
	sizes.codeSize = (UInt)(ip - codeBase);
	sizes.functionCount = this->functionCount;
	sizes.globalsSize = (UInt)(globalsPointer - memoryBase);
	sizes.constsSize = (UInt)(memoryEnd - constantsPointer);
}

void Assembler::finalize(UInt& codeSize, UInt& globalsSize, UInt& constsSize, UInt& functionCount) {
	ProgramSizes sizes;
	finalize(sizes);
	codeSize = sizes.codeSize;
	globalsSize = sizes.globalsSize;
	constsSize = sizes.constsSize;
	functionCount = sizes.functionCount;
}

void Assembler::finalize(AssembledProgram& program) {
	ProgramSizes sizes;
	finalize(sizes);
	program.codeSize = sizes.codeSize;
	program.functionCount = sizes.functionCount;
	program.globalsSize = sizes.globalsSize;
	program.constsSize = sizes.constsSize;
	program.code = codeBase;
	program.functionTable = functionTable;
	program.memory = memoryBase;
	program.memorySize = (UInt)(memoryEnd - memoryBase);
}

/*
	A real assembly into scratch arenas that start small and double whenever one runs out, so the sizes are exact.
	The table handed back points into none of the scratch: symbol values are offsets, not addresses.
*/
ProgramSizes Assembler::preAssemble(const Char* source, Symbols& symbols, const ProgramSizes* seed) {
	UInt codeMax = 256, memoryMax = 256, functionMax = 64;
	if (seed != 0) {
		codeMax = std::max(codeMax, seed->codeSize);
		memoryMax = std::max(memoryMax, seed->globalsSize + seed->constsSize);
		functionMax = std::max(functionMax, seed->functionCount);
	}
	while (true) {
		std::vector<Instruction> code(codeMax);
		std::vector<UInt> functions(functionMax);
		std::vector<Value> memory(memoryMax);
		Symbols scratch(symbols);						// fresh per attempt, or a retry would find the first attempt's names defined
		Assembler assem(codeMax, &code[0], functionMax, &functions[0], memoryMax, &memory[0], scratch);
		assem.newUnit(0);
		try {
			for (const Char* p = source; *p != 0; ) p = assem.feed(p);
			ProgramSizes sizes;
			assem.finalize(sizes);						// resolves and CLEARS the forward refs, so no Value* into `code` survives
			scratch.swap(symbols);						// success only: a throw leaves the caller's table untouched
			return sizes;
		}
		catch (const Exception& x) {
			if (x.error != NOT_ENOUGH_CODE_SPACE && x.error != NOT_ENOUGH_MEMORY_SPACE
					&& x.error != NOT_ENOUGH_FUNCTION_SPACE) throw;
			codeMax *= 2;								// ALL three, whichever tripped: every retry re-assembles the whole
			memoryMax *= 2;								// source, so the retry count should be the MAX of the three logs,
			functionMax *= 2;							// not their sum - transient over-allocation costs nothing here.
		}
	}
}

void Assembler::newUnit(const Char* unitName) {
	(void)unitName;
	if (!skipUntilLabel.empty()) throw Exception(MISSING_COMPILE_TIME_LABEL, skipUntilLabel);
	if (functionStart != 0) finalizeFunction();
	for (Int i = 0; i < 128; ++i) compileTimeVars[i].types = 0;
}

bool Assembler::doConstantBranch(const Operator* op, const Char* op0Begin, const Char* op0End, const Char* op1Begin
		, const Char* op1End) {
	Value v0, v1;
	parseOperand(op0Begin, op0End, op->accepts[0], ((op->otherFlags & SWAP_0_AND_1) == 0) ? &v0 : &v1);
	parseOperand(op1Begin, op1End, op->accepts[1], ((op->otherFlags & SWAP_0_AND_1) == 0) ? &v1 : &v0);
	bool doBranch = false;
	switch (op->opcode) {
		case LSSI_CCB: doBranch = (v0.i < v1.i); break;
		case EQUI_CCB: doBranch = (v0.i == v1.i); break;
		case NLSI_CCB: doBranch = !(v0.i < v1.i); break;
		case NEQI_CCB: doBranch = (v0.i != v1.i); break;
		case LSSF_CCB: doBranch = (v0.f < v1.f); break;
		case EQUF_CCB: doBranch = (v0.f == v1.f); break;
		case NLSF_CCB: doBranch = !(v0.f < v1.f); break;
		case NEQF_CCB: doBranch = (v0.f != v1.f); break;
		default: assert(0);
	}
	return doBranch;
}

Value Assembler::calcConstant(const Operator* op, const Char* op1Begin, const Char* op1End, const Char* op2Begin
		, const Char* op2End) {
	Value v1, v2;
	assert((op->otherFlags & (SWAP_0_AND_1 | SWAP_1_AND_2)) == 0);
	parseOperand(op1Begin, op1End, op->accepts[1], &v1);
	parseOperand(op2Begin, op2End, op->accepts[2], &v2);
	switch (op->opcode) {
		case MOVE_CC_: break;
		case ABSI_CC_: v1.i = absolute(v1.i); break;
		case ADDI_CCC: v1.i = iadd(v1.i, v2.i); break;
		case SUBI_CCC: v1.i = isub(v1.i, v2.i); break;
		case MULI_CCC: v1.i = imul(v1.i, v2.i); break;
		case DIVI_CCC: if (v2.i == 0) throw Exception(CONSTANT_DIVISION_BY_ZERO); v1.i = idiv(v1.i, v2.i); break;
		case MODI_CCC: if (v2.i == 0) throw Exception(CONSTANT_DIVISION_BY_ZERO); v1.i = imod(v1.i, v2.i); break;
		case ANDI_CCC: v1.i &= v2.i; break;
		case IORI_CCC: v1.i |= v2.i; break;
		case XORI_CCC: v1.i ^= v2.i; break;
		case SHLI_CCC: v1.i = ishl(v1.i, v2.i); break;
		case SHRI_CCC: v1.i = ashr(v1.i, v2.i); break;
		case SHRU_CCC: v1.i = lshr(v1.i, v2.i); break;
		case ABSF_CC_: v1.f = absolute(v1.f); break;
		case FLOF_CC_: v1.f = floorf(v1.f); break;
		case ADDF_CCC: v1.f += v2.f; break;
		case SUBF_CCC: v1.f -= v2.f; break;
		case MULF_CCC: v1.f *= v2.f; break;
		case DIVF_CCC: v1.f /= v2.f; break;																		// IEEE: a zero divisor gives an infinity or NaN
		case FTOI_CCC: v1.i = ftoi(v1.f * v2.f); break;
		case ITOF_CCC: v1.f = (Float)(v1.i) * v2.f; break;
		default: assert(0);
	}
	return v1;
}

const Char* Assembler::feed(const Char* line) {
	const Char* labelBegin = 0;
	const Char* labelEnd = 0;
	const Char* p = eatSpaceAndComment(line);
	const Char* e = eatToken(p);
	
	if (p == e && isEOL(*e)) return eatEOL(e);																			// Empty line

	if (p != e && *(e - 1) == ':') {																					// Label
		labelBegin = p;
		labelEnd = e - 1;
		p = eatSpaceAndComment(e);
		e = eatToken(p);
		if (!skipUntilLabel.empty() && skipUntilLabel == std::string(labelBegin, labelEnd))								// Found skip label, stop skipping.
			skipUntilLabel = std::string();
	}
	
	if (!skipUntilLabel.empty()) return eatEOL(e + strcspn(e, STR("\r\n")));											// Skipping active, return.

	bool isCompileTime = false;																							// Compile-time?
	if (*p == '!') {
		isCompileTime = true;
		p = eatSpaceAndComment(++p);
		e = eatToken(p);
	}

	if (isCompileTime && p == e && isEOL(*e)) return eatEOL(e);															// Labelled compile-time line with no instruction is ok.
	
	if (p == e) throw Exception(MISSING_INSTRUCTION);
	if (e != p + 4) throw Exception(INVALID_MNENOMIC, p, e);
	const Char* mnemonicBegin = p;
	
	if (!isCompileTime && memcmp(mnemonicBegin, STR("DAT"), 3) == 0) {													// DAT*
		switch (mnemonicBegin[3]) {																						// DATs
			case 's':	{
							if (dataPointer == 0) throw Exception(DATA_SECTION_MISSING);
							p = eatSpace(e);
							e = p + strcspn(p, STR("\r\n"));
							while (e > p && (*(e - 1) == ' ' || *(e - 1) == '\t')) --e;
							if ((e - p) == 0) throw Exception(MISSING_OPERAND);
							if ((e - p) > dataEnd - dataPointer) throw Exception(DATA_SECTION_FULL, dataLabel);
							for (const Char* c = p; c < e; ++c) {
								if ((*c & 0x80) != 0) throw Exception(NON_ASCII_TEXT, p, e);
							}
							declareData(labelBegin, labelEnd, dataPointer, static_cast<UInt>(e - p));
							while (p < e) (dataPointer++)->i = *p++;
							return eatEOL(eatSpace(e));
						}

			default:	{																								// DATi, DATf, DATp, DATt and DATA
							const Char key[10] = { ' ', 'D', 'A', 'T', mnemonicBegin[3], '_', 'c', '_', '_', 0 };
							const int accepts = findOperator(key, mnemonicBegin, mnemonicBegin + 4)->accepts[0];
							if (dataPointer == 0) throw Exception(DATA_SECTION_MISSING);
							Value* const first = dataPointer;
							p = eatSpaceAndComment(e);
							while (!isEOL(*p)) {
								const Char* b = p;
								e = eatToken(b);
								p = eatSpaceAndComment(e);
								if (dataPointer >= dataEnd) throw Exception(DATA_SECTION_FULL, dataLabel);
								if (parseOperandType(b, e) != 'c') throw Exception(EXPECTED_CONSTANT, b, e);
								parseOperand(b, e, accepts, dataPointer);
								++dataPointer;
							}
							declareData(labelBegin, labelEnd, first, static_cast<UInt>(dataPointer - first));
							return eatEOL(p);
						}
		}
	}
	
	if (isCompileTime && memcmp(mnemonicBegin, STR("FAIL"), 4) == 0)													// FAIL
		throw Exception(FAIL_DIRECTIVE, eatSpace(e), e + strcspn(e, "\r\n"));
	
	const Char* op0Begin = eatSpaceAndComment(e);																		// 0 to 3 operands
	const Char* op0End = eatToken(op0Begin);
	const Char* op1Begin = eatSpaceAndComment(op0End);
	const Char* op1End = eatToken(op1Begin);
	const Char* op2Begin = eatSpaceAndComment(op1End);
	const Char* op2End = eatToken(op2Begin);

	e = eatSpaceAndComment(op2End);																						// End-of-line comment
	if (!isEOL(*e)) throw Exception(EXPECTED_END_OF_LINE_OR_COMMENT, e, e + strcspn(e, "\r\n"));
	
	Char key[10];
	key[0] = (isCompileTime ? '!' : ' ');
	key[1] = mnemonicBegin[0];
	key[2] = mnemonicBegin[1];
	key[3] = mnemonicBegin[2];
	key[4] = mnemonicBegin[3];
	key[5] = '_';
	key[6] = parseOperandType(op0Begin, op0End);
	key[7] = parseOperandType(op1Begin, op1End);
	key[8] = parseOperandType(op2Begin, op2End);
	key[9] = 0;

	const Operator* op = findOperator(key, mnemonicBegin, op2End);

	Value v;
	UInt size;
	int types;
	
	if (isCompileTime) {
		if ((op->accepts[0] & COMPILE_TIME) != 0) {																		// Compile-time calculation
			if (op0End != op0Begin + 3 || *op0Begin != '<' || op0Begin[2] != '>' || !isValidIdentifierChar(op0Begin[1]))
				throw Exception(INVALID_COMPILE_TIME_VARIABLE, op0Begin, op0End);
			CompileTimeVar& cv = compileTimeVars[(unsigned char)op0Begin[1]];
			cv.value = calcConstant(op, op1Begin, op1End, op2Begin, op2End);
			cv.types = op->declareTypes;
			if ((cv.types & CONST_INT_P) != 0 && cv.value.i < 0) cv.types &= ~CONST_INT_P;
		} else if ((op->accepts[2] & BRANCH) != 0) {																	// Compile-time conditional
			assert(*op2Begin == '@');
			if (!isValidIdentifier(op2Begin + 1, op2End))
				throw Exception(INVALID_IDENTIFIER, op2Begin + 1, op2End);
			if (doConstantBranch(op, op0Begin, op0End, op1Begin, op1End))
				skipUntilLabel = std::string(op2Begin + 1, op2End);
		} else {
			switch (op->opcode) {
				case DEFI____:	{																						// ! DEFi, ! DEFf, ! DEFp
									const Operand read = parseOperand(op0Begin, op0End, op->accepts[0], &v);
									const Symbols::Symbol* const named = read.symbol;
									types = op->declareTypes;
									bool isOutside = false;															// no access through the name may pass
									if ((types & POINTER_CONSTANT) != 0 && named == 0) {								// an assembly-time address: no label to check against
										assert(read.variableTypes != 0);											// DEFp takes no literal, local or forward reference
										types |= read.variableTypes & op->accepts[0];
										isOutside = true;
									} else if ((types & POINTER_CONSTANT) != 0) {									// the kind it names: `NULL` or a function is not data
										const Int offset = static_cast<Int>(static_cast<UInt>(v.i)
												- static_cast<UInt>(named->value.i));
										types |= named->types & op->accepts[0];
										isOutside = (offset < named->minOffset || offset > named->maxOffset);
									}
									Symbols::Symbol* const symbol = declare(globals, labelBegin, labelEnd, types, v);
									if (symbol != 0 && isOutside) symbol->maxOffset = -1;							// below `minOffset`, 0: no offset is in range
								}
								break;
				
				case IFDF_CB_:
				case IFND_CB_:	if (!isValidIdentifier(op0Begin + 1, op0End))											// ! IFDF, ! IFND
									throw Exception(INVALID_IDENTIFIER, op0Begin + 1, op0End);
								if (!isValidIdentifier(op1Begin + 1, op1End))
									throw Exception(INVALID_IDENTIFIER, op1Begin + 1, op1End);
								assert(op0End > op0Begin + 1);
								if (globals.lookup(op0Begin + 1, op0End, op->accepts[0], types, v)
										== (op->opcode == IFDF_CB_)) {
									assert(*op1Begin == '@');
									assert(op1End > op1Begin + 1);
									skipUntilLabel = std::string(op1Begin + 1, op1End);
								}
								break;
								
				case SKIP_B__:	if (!isValidIdentifier(op0Begin + 1, op0End))											// ! GOTO
									throw Exception(INVALID_IDENTIFIER, op0Begin + 1, op0End);
								assert(*op0Begin == '@');
								assert(op0End > op0Begin + 1);
								skipUntilLabel = std::string(op0Begin + 1, op0End);
								break;
								
				default:		assert(0);
			}
		}
	} else {																											// Run-time
		switch (op->opcode) {
			case GLOB____:																								// GLOB, TEMP, CNST
			case CNST____:	v.i = 1;
							parseOperand(op0Begin, op0End, op->accepts[0], &v);
							size = v.i;
							closeDataRegion();									// while `dataLabel` still names the previous section
							closeDataSection();
							dataLabel = std::string(labelBegin, labelEnd);
							dataLabelType = op->declareTypes;
							if (constantsPointer - globalsPointer < size) throw Exception(NOT_ENOUGH_MEMORY_SPACE);
							if (op->opcode == GLOB____) {
								dataPointer = globalsPointer;
								globalsPointer += size;
							} else {
								constantsPointer -= size;
								dataPointer = constantsPointer;
							}
							assert(globalsPointer <= constantsPointer);
							dataEnd = dataPointer + size;
							sectionBegin = dataPointer;
							sectionEnd = dataEnd;
							memset(dataPointer, 0, size * sizeof (*dataPointer));	// up front: a SPAN can move the cursor below words already written
							dataRegions.clear();
							regionStart = 0;
							regionExtent = -1;									// the implicit region: from offset 0, claiming what it writes
							dataIsConstant = (op->opcode != GLOB____);
							if (labelBegin != labelEnd) declareData(labelBegin, labelEnd, dataPointer, size);
							break;

			case GAZL____:	parseOperand(op0Begin, op0End, op->accepts[0], &v);											// GAZL
							if (v.i < 1 || v.i > VERSION) throw Exception(UNSUPPORTED_GAZL_VERSION);
							dialect = (int)(v.i);								// a mode, not a bracket, so concatenated units compose in any order
							break;

			case SPAN____:	{																							// SPAN
								if (dataPointer == 0) throw Exception(DATA_SECTION_MISSING);
								closeDataRegion();
								parseOperand(op0Begin, op0End, op->accepts[0], &v);
								regionStart = v.i;
								parseOperand(op1Begin, op1End, op->accepts[1], &v);
								regionExtent = v.i;
								if (regionExtent > static_cast<Int>(sectionEnd - sectionBegin) - regionStart)
									throw Exception(OFFSET_OUT_OF_BOUNDS, dataLabel);
								dataPointer = sectionBegin + regionStart;
								dataEnd = dataPointer + regionExtent;
								declareData(labelBegin, labelEnd, dataPointer, static_cast<UInt>(dataEnd - dataPointer));	// an ordinary data label on the region's first word
								break;
							}
					
			case FUNC_CC_:	if (functionCount >= maxFunctionCount) throw Exception(NOT_ENOUGH_FUNCTION_SPACE);			// FUNC
							v.t = FUNCTION_OFFSET + functionCount;
							functionTable[functionCount] = (UInt)(ip - codeBase);
							++functionCount;
							declare(globals, labelBegin, labelEnd,
									(dialect >= 2 ? TARGET : FUNC), v);			// a GAZL 2 function is a `t`, so it never enters a `p` position
							if (functionStart != 0) finalizeFunction();
							if (ip >= codeEnd) throw Exception(NOT_ENOUGH_CODE_SPACE);
							ip->opcode = op->opcode;
							ip->p0.i = ip->p1.i = ip->p2.i = 0;
							functionStart = ip++;
							break;
				
			case LOCA____:	if (functionStart == 0 || ip != functionStart + 1) {										// LOCA, PARA, LOCi, LOCf, LOCp, INPi, INPf, INPp, OUTi, OUTf, OUTp
								throw Exception(MUST_DEFINE_LOCALS_FIRST);
							}
							v.i = 1;
							parseOperand(op0Begin, op0End, op->accepts[0], &v);
							size = v.i;
							if (size > 0x7FFFFFFFu - localsSize) throw Exception(FRAME_TOO_LARGE);						// slot offsets must fit an Int
							v.i = localsSize;
							if (Symbols::Symbol* const local = declare(locals, labelBegin, labelEnd, op->declareTypes, v
									, size)) {
								local->minOffset = -2147483647 - 1;													// checked against the frame where it is used
								local->maxOffset = 2147483647;
							}
							localsSize += size;
							if (localsSize > maxLocalsSize) maxLocalsSize = localsSize;
							break;

			/*
				The frame is `maxLocalsSize`, the peak over nesting chains rather than the sum. It is computed from
				resolved sizes, so `LOCA *.z.Struct` overlays correctly even when only the host knows the size.
			*/
			case MARK____:	if (functionStart == 0 || ip != functionStart + 1) {										// MARK
								throw Exception(MUST_DEFINE_LOCALS_FIRST);
							}
							if (markDepth >= MAX_MARK_DEPTH) throw Exception(UNBALANCED_MARK);
							markStack[markDepth++] = localsSize;
							break;

			case FREE____:	if (functionStart == 0 || ip != functionStart + 1) {										// FREE
								throw Exception(MUST_DEFINE_LOCALS_FIRST);
							}
							if (markDepth == 0) throw Exception(UNBALANCED_MARK);
							localsSize = markStack[--markDepth];
							break;

			default:		if (functionStart == 0) throw Exception(MISSING_FUNCTION_DECLARATION);						// The rest
							if (ip == functionStart + 1) {																// leaving the declaration region
								if (markDepth != 0) throw Exception(UNBALANCED_MARK);
								localsSize = maxLocalsSize;	// every local operand and RETU below resolves against the FINAL frame
							}
							v.p = (Int)(ip - codeBase);
							if (Symbols::Symbol* const label = declare(locals, labelBegin, labelEnd, BRANCH, v)) {		// a branch's offset is its jump distance, any value
								label->minOffset = -2147483647 - 1;
								label->maxOffset = 2147483647;
							}
							if (op->opcode == NOOP____) break;															// NOOP
							else if ((op->otherFlags & YIELDS_CONST) != 0) {											// Const calc -> MOVE
								assert((op->accepts[0] & ANY_VAR_W) != 0);
								assert((op->accepts[1] & FORWARD) == 0);
								assert((op->accepts[2] & FORWARD) == 0);
								if (ip >= codeEnd) throw Exception(NOT_ENOUGH_CODE_SPACE);
								ip->opcode = MOVE_VC_;
								ip->p0.i = ip->p1.i = ip->p2.i = 0;
								parseOperand(op0Begin, op0End, op->accepts[0], &ip->p0);
								ip->p1 = calcConstant(op, op1Begin, op1End, op2Begin, op2End);
								++ip;
							} else if ((op->otherFlags & YIELDS_GOTO) != 0) {											// Const condition -> GOTO
								assert(*op2Begin == '@');
								assert((op->accepts[0] & FORWARD) == 0);
								assert((op->accepts[1] & FORWARD) == 0);
								assert((op->accepts[2] & BRANCH) != 0);
								if (doConstantBranch(op, op0Begin, op0End, op1Begin, op1End)) {
									if (ip >= codeEnd) throw Exception(NOT_ENOUGH_CODE_SPACE);
									ip->p0.i = ip->p1.i = ip->p2.i = 0;
									parseOperand(op2Begin, op2End, op->accepts[2], &ip->p0);
									ip->opcode = GOTO_B__;
									++ip;
								}
							} else {																					// The rest
								if (ip >= codeEnd) throw Exception(NOT_ENOUGH_CODE_SPACE);
								ip->opcode = op->opcode;
								Value* p0 = &ip->p0;
								Value* p1 = &ip->p1;
								Value* p2 = &ip->p2;
								if ((op->otherFlags & SWAP_0_AND_1) != 0) std::swap(p0, p1);
								if ((op->otherFlags & SWAP_1_AND_2) != 0) std::swap(p1, p2);
								p0->i = p1->i = p2->i = 0;
								if (op->opcode == RETU_C__) p0->i = localsSize;											// RETU
								types = op->accepts[0];
								if (dialect >= 2 && (op->opcode == CALL_VVC || op->opcode == TAIL_VC_)) {
									types &= ~(VAR_PTR_R & ~TRANSIENT);			// in a GAZL 2 region the callee is a `t` local or a `%N` slot, never a `p` local
								}
								parseOperand(op0Begin, op0End, types, p0);
								parseOperand(op1Begin, op1End, op->accepts[1], p1);
								if (op->opcode == TAIL_CC_ || op->opcode == TAIL_VC_) {									// TAIL
									p2->i = maxLocalsSize;	// the frame size, which the run time rewinds before the window copy
									paramsSize = maximum(paramsSize, (UInt)(p1->i));	// the copy reads `%0` to `%m-1`, so the frame must hold them
								}
								if (op->opcode == SWCH_VCC) {															// SWCH
									assert(*op2Begin == '@');
									assert(op->accepts[2] == FWD_BRANCH);
									size = static_cast<UInt>(p1->i) + 1u;
									if (size > constantsPointer - globalsPointer) {
										throw Exception(NOT_ENOUGH_MEMORY_SPACE);
									}
									Value v;
									constantsPointer -= size;
									v.p = makeGAZLPointer(constantsPointer, true);
									p2->i = v.i;
									locals.registerSwitch(op2Begin + 1, op2End, p1->i, constantsPointer
											, -(Int)(ip - codeBase));
								} else {
									parseOperand(op2Begin, op2End, op->accepts[2], p2);
									if ((op->otherFlags & CHECK_DIV_BY_0) != 0
											&& (((op->accepts[2] & CONST_FLOAT) != 0 && p2->f == 0)
											|| ((op->accepts[2] & CONST_INT) != 0 && p2->i == 0)))
										throw Exception(CONSTANT_DIVISION_BY_ZERO);
								}
								if ((op->otherFlags & LOCAL_BOUNDS) != 0) {
									if ((UInt)p2->i > 0x7FFFFFFFu - (UInt)p1->i) throw Exception(FRAME_TOO_LARGE);
									paramsSize = maximum((Int)(paramsSize), p1->i + p2->i);
								}
								++ip;
							}
							break;
				

			case DATA____:	assert(0);
		}
	}
	
	return eatEOL(e);
}

Processor::Processor() : codeSize(0), codeBase(0), functionCount(0), functionTable(0), memorySize(0), memoryBase(0)
		, constantsSize(0), readOffset(0), rwMemorySize(0), dataStackBase(0), dataStackEnd(0), ipStackBase(0)
		, ipStackEnd(0), natives(0), clockCyclesLeft(0), ip(0), dsp(0), ipsp(0), userData(0), resume(0) {
}

Processor::Processor(UInt codeSize, const Instruction* code, UInt functionCount, const UInt* functionTable
		, UInt memorySize, Value* memory, UInt constsSize, UInt rwMemorySize, UInt dataStackOffset, UInt dataStackSize
		, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* natives, void* userData)
		: codeSize(codeSize), codeBase(code), functionCount(functionCount), functionTable(functionTable)
		, memorySize(memorySize - 1), memoryBase(memory), constantsSize(constsSize - 1)
		, readOffset(constantsSize - MEMORY_OFFSET), rwMemorySize(rwMemorySize)
		, dataStackBase(memory + constantsSize + dataStackOffset)
		, dataStackEnd(memory + constantsSize + dataStackOffset + dataStackSize)
		, ipStackBase(ipStack), ipStackEnd(ipStack + ipStackSize), natives(natives), clockCyclesLeft(0x7FFFFFFFU)
		, ip(codeBase), dsp(dataStackBase), ipsp(ipStackBase), userData(userData), resume(0) {
	assert(constsSize >= 1 && constsSize - 1 + rwMemorySize <= memorySize - 1);
	assert(dataStackOffset + dataStackSize <= rwMemorySize);
	assert(code != 0);
	assert(memory != 0);
	assert(ipStack != 0);
}

Processor::Processor(UInt codeSize, const Instruction* code, UInt functionCount, const UInt* functionTable
		, UInt memorySize, Value* memory, UInt globalsSize, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack
		, NativeFunc const* natives, void* userData)
		: codeSize(codeSize), codeBase(code), functionCount(functionCount), functionTable(functionTable)
		, memorySize(memorySize - 1), memoryBase(memory), constantsSize(constsSize - 1)
		, readOffset(constantsSize - MEMORY_OFFSET), rwMemorySize(memorySize - constsSize)
		, dataStackBase(memory + constantsSize + globalsSize)
		, dataStackEnd(memory + memorySize - 1), ipStackBase(ipStack), ipStackEnd(ipStack + ipStackSize)
		, natives(natives), clockCyclesLeft(0x7FFFFFFFU), ip(codeBase), dsp(dataStackBase), ipsp(ipStackBase)
		, userData(userData), resume(0) {
	assert(constsSize >= 1 && globalsSize + constsSize <= memorySize);			// `constsSize` includes the zero guard
	assert(code != 0);
	assert(memory != 0);
	assert(ipStack != 0);
}

// These two repeat the memory layout of the two constructors above; change them together.
Processor::Processor(const AssembledProgram& program, UInt ipStackSize, CallStackEntry* ipStack
		, NativeFunc const* natives, void* userData)
		: codeSize(program.codeSize), codeBase(program.code), functionCount(program.functionCount)
		, functionTable(program.functionTable), memorySize(program.memorySize - 1), memoryBase(program.memory)
		, constantsSize(program.constsSize - 1), readOffset(constantsSize - MEMORY_OFFSET)
		, rwMemorySize(program.memorySize - program.constsSize)
		, dataStackBase(program.memory + constantsSize + program.globalsSize)
		, dataStackEnd(program.memory + program.memorySize - 1), ipStackBase(ipStack), ipStackEnd(ipStack + ipStackSize)
		, natives(natives), clockCyclesLeft(0x7FFFFFFFU), ip(program.code), dsp(dataStackBase), ipsp(ipStackBase)
		, userData(userData), resume(0) {
	assert(program.constsSize >= 1 && program.globalsSize + program.constsSize <= program.memorySize);
	assert(program.code != 0);
	assert(program.memory != 0);
	assert(ipStack != 0);
}

Processor::Processor(const AssembledProgram& program, UInt rwMemorySize, UInt dataStackOffset, UInt dataStackSize
		, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* natives, void* userData)
		: codeSize(program.codeSize), codeBase(program.code), functionCount(program.functionCount)
		, functionTable(program.functionTable), memorySize(program.memorySize - 1), memoryBase(program.memory)
		, constantsSize(program.constsSize - 1), readOffset(constantsSize - MEMORY_OFFSET), rwMemorySize(rwMemorySize)
		, dataStackBase(program.memory + constantsSize + dataStackOffset)
		, dataStackEnd(program.memory + constantsSize + dataStackOffset + dataStackSize), ipStackBase(ipStack)
		, ipStackEnd(ipStack + ipStackSize), natives(natives), clockCyclesLeft(0x7FFFFFFFU), ip(program.code)
		, dsp(dataStackBase), ipsp(ipStackBase), userData(userData), resume(0) {
	assert(program.constsSize >= 1 && program.constsSize - 1 + rwMemorySize <= program.memorySize - 1);
	assert(dataStackOffset + dataStackSize <= rwMemorySize);
	assert(program.code != 0);
	assert(program.memory != 0);
	assert(ipStack != 0);
}

#define V0 (dsp[ip->p0.i])
#define V1 (dsp[ip->p1.i])
#define V2 (dsp[ip->p2.i])
#define C0 (ip->p0)
#define C1 (ip->p1)
#define C2 (ip->p2)
#define CHECK_INT_DIV_BY_ZERO(v) if (v == 0) { err = DIVISION_BY_ZERO; goto ret; }

Int Processor::run() {
	assert(codeBase != 0);
	
	Int clockCyclesLeft = this->clockCyclesLeft;
	const Instruction* ip = this->ip;
	Value* dsp = this->dsp;
	CallStackEntry* ipsp = this->ipsp;
	Value* const mb = memoryBase + constantsSize - MEMORY_OFFSET;														// mb[pointer] is the word at `pointer`
	UInt ui, ui2;
	Int nativeError;
	Int err = TIME_OUT;

	assert(ip != 0);
	assert(dsp != 0);
	assert(ipsp > ipStackBase && ipsp <= ipStackEnd); 	// If this blows it probably means you are trying to continue GAZL after it has returned to your native caller. You need to setup a new call with enterCall().
	
	while (--clockCyclesLeft >= 0) {
		switch (ip->opcode) {
			case FUNC_CC_:	if ((UInt)C0.i + (UInt)C1.i > (UInt)(dataStackEnd - dsp)) { err = DATA_STACK_OVERFLOW; goto ret; } dsp += (UInt)(C0.i); break;
			case CALL_VVC:	ui = V0.t - FUNCTION_OFFSET;																// ui = function ordinal
							if (ui >= functionCount) { err = BAD_CALL; goto ret; }
							ui = functionTable[ui];																		// ui = code offset
							assert((codeBase + ui)->opcode == FUNC_CC_);
							goto call;
			case CALL_CVC:	ui = C0.t - FUNCTION_OFFSET;																// ui = function ordinal (constant, validated at assembly)
							assert(ui < functionCount);
							ui = functionTable[ui];																		// ui = code offset
							assert((codeBase + ui)->opcode == FUNC_CC_);
							goto call;
			call:			if (ipsp >= ipStackEnd) { err = IP_STACK_OVERFLOW; goto ret; }
							ipsp->ip = ip;
							ipsp++->dsp = dsp;
							dsp += (UInt)(C1.i);
							ip = codeBase + ui;
							continue;
			case CALL_NVC:	this->clockCyclesLeft = clockCyclesLeft;
							this->ip = ip;
							this->dsp = dsp + (UInt)(C1.i);
							this->ipsp = ipsp;
							if ((nativeError = (*natives[C0.i])(this)) != 0) { err = nativeError; goto ret; }
							clockCyclesLeft = this->clockCyclesLeft;
							if (this->ip != ip) {																		// the native called pushCall(): continue in the last-pushed callee
								assert(this->ipsp > ipsp && this->ipsp[-1].dsp != 0);									// plain pushCall frames on top
								ipsp = this->ipsp;
								dsp = this->dsp;
								ip = this->ip;
								continue;
							}
							break;
			case RETU_C__:	ip = (--ipsp)->ip;
							dsp = ipsp->dsp;
							if (dsp == 0) { dsp = (--ipsp)->dsp; assert(ipsp->ip == ip); err = OK; goto ret; }
							break;
			case TAIL_CC_:	ui = C0.t - FUNCTION_OFFSET;						// ui = function ordinal (constant, validated at assembly)
							assert(ui < functionCount);
							goto tail;
			case TAIL_VC_:	ui = V0.t - FUNCTION_OFFSET;
							if (ui >= functionCount) { err = BAD_CALL; goto ret; }
			tail:			{																// slide the `%0` window onto the frame base; the callee returns to our caller
								const Value* sp = dsp;
								const Value* ep = sp + C1.i;
								Value* dp = dsp - (UInt)(C2.i);								// C2 = this function's own frame size, baked at assembly
								dsp = dp;
								while (sp < ep) *dp++ = *sp++;								// ascending is safe: the destination is below the source
							}
							ui = functionTable[ui];
							assert((codeBase + ui)->opcode == FUNC_CC_);
							ip = codeBase + ui;												// the callee's FUNC re-stacks its locals and re-checks, from OUR base
							continue;
			case MOVE_VV_:	V0 = V1; break;
			case MOVE_VC_:	V0 = C1; break;
			case PEEK_VC_:	V0 = mb[C1.p]; break;
			case POKE_CV_:	mb[C0.p] = V1; break;
			case POKE_CC_:	mb[C0.p] = C1; break;
			case PEEK_VVV:	if ((ui = (UInt)V1.i + (UInt)V2.i) + readOffset < memorySize) { V0 = mb[ui]; break; } else { err = BAD_PEEK; goto ret; }
			case PEEK_VCV:	if ((ui = (UInt)C1.i + (UInt)V2.i) + readOffset < memorySize) { V0 = mb[ui]; break; } else { err = BAD_PEEK; goto ret; }
			case POKE_VVV:	if ((ui = (UInt)V0.i + (UInt)V1.i) - MEMORY_OFFSET < rwMemorySize) { mb[ui] = V2; break; } else { err = BAD_POKE; goto ret; }
			case POKE_CVV:	if ((ui = (UInt)C0.i + (UInt)V1.i) - MEMORY_OFFSET < rwMemorySize) { mb[ui] = V2; break; } else { err = BAD_POKE; goto ret; }
			case POKE_VVC:	if ((ui = (UInt)V0.i + (UInt)V1.i) - MEMORY_OFFSET < rwMemorySize) { mb[ui] = C2; break; } else { err = BAD_POKE; goto ret; }
			case POKE_CVC:	if ((ui = (UInt)C0.i + (UInt)V1.i) - MEMORY_OFFSET < rwMemorySize) { mb[ui] = C2; break; } else { err = BAD_POKE; goto ret; }
			case GETL_VVV:	if ((Int)(ui2 = (UInt)(dataStackEnd - dsp - C1.i)) > 0 && (ui = V2.i) < ui2) { V0 = (dsp + C1.i)[ui]; break; } else { err = BAD_PEEK; goto ret; };
			case SETL_VVV:	if ((Int)(ui2 = (UInt)(dataStackEnd - dsp - C0.i)) > 0 && (ui = V1.i) < ui2) { (dsp + C0.i)[ui] = V2; break; } else { err = BAD_POKE; goto ret; };
			case SETL_VVC:	if ((Int)(ui2 = (UInt)(dataStackEnd - dsp - C0.i)) > 0 && (ui = V1.i) < ui2) { (dsp + C0.i)[ui] = C2; break; } else { err = BAD_POKE; goto ret; };
			case ADRL_VV_:	V0.p = Pointer(&dsp[C1.i] - mb); break;
			case ABSI_VV_:	V0.i = absolute(V1.i); break;
			case ADDI_VVV:	V0.i = iadd(V1.i, V2.i); break;
			case ADDI_VVC:	V0.i = iadd(V1.i, C2.i); break;
			case SUBI_VVV:	V0.i = isub(V1.i, V2.i); break;
			case SUBI_VVC:	V0.i = isub(V1.i, C2.i); break;
			case SUBI_VCV:	V0.i = isub(C1.i, V2.i); break;
			case MULI_VVV:	V0.i = imul(V1.i, V2.i); break;
			case MULI_VVC:	V0.i = imul(V1.i, C2.i); break;
			case DIVI_VVV:	CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = idiv(V1.i, V2.i); break;
			case DIVI_VVC:	V0.i = idiv(V1.i, C2.i); break;
			case DIVI_VCV:	CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = idiv(C1.i, V2.i); break;
			case MODI_VVV:	CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = imod(V1.i, V2.i); break;
			case MODI_VVC:	V0.i = imod(V1.i, C2.i); break;
			case MODI_VCV:	CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = imod(C1.i, V2.i); break;
			case ANDI_VVV:	V0.i = V1.i & V2.i; break;
			case ANDI_VVC:	V0.i = V1.i & C2.i; break;
			case IORI_VVV:	V0.i = V1.i | V2.i; break;
			case IORI_VVC:	V0.i = V1.i | C2.i; break;
			case XORI_VVV:	V0.i = V1.i ^ V2.i; break;
			case XORI_VVC:	V0.i = V1.i ^ C2.i; break;
			case SHLI_VVV:	V0.i = ishl(V1.i, V2.i); break;
			case SHLI_VVC:	V0.i = ishl(V1.i, C2.i); break;
			case SHLI_VCV:	V0.i = ishl(C1.i, V2.i); break;
			case SHRI_VVV:	V0.i = ashr(V1.i, V2.i); break;
			case SHRI_VVC:	V0.i = ashr(V1.i, C2.i); break;
			case SHRI_VCV:	V0.i = ashr(C1.i, V2.i); break;
			case SHRU_VVV:	V0.i = lshr(V1.i, V2.i); break;
			case SHRU_VVC:	V0.i = lshr(V1.i, C2.i); break;
			case SHRU_VCV:	V0.i = lshr(C1.i, V2.i); break;
			case ABSF_VV_:	V0.f = absolute(V1.f); break;					// clears the sign bit only (same op in interp + JIT); no canonicalization needed
			case FLOF_VV_:	V0.f = floorf(V1.f); break;
			case ADDF_VVV:	V0.f = CANON_F(V1.f + V2.f); break;
			case ADDF_VVC:	V0.f = CANON_F(V1.f + C2.f); break;
			case SUBF_VVV:	V0.f = CANON_F(V1.f - V2.f); break;
			case SUBF_VVC:	V0.f = CANON_F(V1.f - C2.f); break;
			case SUBF_VCV:	V0.f = CANON_F(C1.f - V2.f); break;
			case MULF_VVV:	V0.f = CANON_F(V1.f * V2.f); break;
			case MULF_VVC:	V0.f = CANON_F(V1.f * C2.f); break;
			case DIVF_VVV:	V0.f = CANON_F(V1.f / V2.f); break;
			case DIVF_VVC:	V0.f = CANON_F(V1.f / C2.f); break;
			case DIVF_VCV:	V0.f = CANON_F(C1.f / V2.f); break;
			case FTOI_VVC:	V0.i = ftoi(V1.f * C2.f); break;
			case ITOF_VVC:	V0.f = (Float)(V1.i) * C2.f; break;				// int->float * finite const: never NaN
			case COPY_VVC:	ui = V0.i; ui2 = V1.i; goto copy;
			case COPY_VCC:	ui = V0.i; ui2 = C1.i; goto copy;
			case COPY_CVC:	ui = C0.i; ui2 = V1.i; goto copy;
			case COPY_CCC:	ui = C0.i; ui2 = C1.i; goto copy;
			copy:			if ((UInt)C2.i <= rwMemorySize && ui - MEMORY_OFFSET <= rwMemorySize - (UInt)C2.i
									&& (UInt)C2.i <= memorySize
									&& ui2 + readOffset <= memorySize - (UInt)C2.i) {
								const Value* sp = &mb[ui2];
								Value* dp = &mb[ui];
								Value* ep = dp + C2.i;
								while (dp < ep) *dp++= *sp++;
								break;
							} else {
								err = ACCESS_VIOLATION;
								goto ret;
							}
			case FORi_VVB:	V0.i = iadd(V0.i, 1); if (V0.i < V1.i) { ip += C2.i; continue; }; break;					// iadd, not ++: a counter already at INT_MAX made `++` signed-overflow UB. Wrap is unchanged, and FORp maps here too
			case FORi_VCB:	V0.i = iadd(V0.i, 1); if (V0.i < C1.i) { ip += C2.i; continue; }; break;
			case LSSI_VVB:	if (V0.i < V1.i) { ip += C2.i; continue; }; break;
			case LSSI_VCB:	if (V0.i < C1.i) { ip += C2.i; continue; }; break;
			case LSSI_CVB:	if (C0.i < V1.i) { ip += C2.i; continue; }; break;
			case EQUI_VVB:	if (V0.i == V1.i) { ip += C2.i; continue; }; break;
			case EQUI_VCB:	if (V0.i == C1.i) { ip += C2.i; continue; }; break;
			case NLSI_VVB:	if (!(V0.i < V1.i)) { ip += C2.i; continue; }; break;
			case NLSI_VCB:	if (!(V0.i < C1.i)) { ip += C2.i; continue; }; break;
			case NLSI_CVB:	if (!(C0.i < V1.i)) { ip += C2.i; continue; }; break;
			case NEQI_VVB:	if (V0.i != V1.i) { ip += C2.i; continue; }; break;
			case NEQI_VCB:	if (V0.i != C1.i) { ip += C2.i; continue; }; break;
			case LSSF_VVB:	if (V0.f < V1.f) { ip += C2.i; continue; }; break;
			case LSSF_VCB:	if (V0.f < C1.f) { ip += C2.i; continue; }; break;
			case LSSF_CVB:	if (C0.f < V1.f) { ip += C2.i; continue; }; break;
			case EQUF_VVB:	if (V0.f == V1.f) { ip += C2.i; continue; }; break;
			case EQUF_VCB:	if (V0.f == C1.f) { ip += C2.i; continue; }; break;
			case NLSF_VVB:	if (!(V0.f < V1.f)) { ip += C2.i; continue; }; break;
			case NLSF_VCB:	if (!(V0.f < C1.f)) { ip += C2.i; continue; }; break;
			case NLSF_CVB:	if (!(C0.f < V1.f)) { ip += C2.i; continue; }; break;
			case NEQF_VVB:	if (V0.f != V1.f) { ip += C2.i; continue; }; break;
			case NEQF_VCB:	if (V0.f != C1.f) { ip += C2.i; continue; }; break;
			case GOTO_B__:	{ (ip += C0.i); continue; };
			case SWCH_VCC:	ip += mb[C2.p + minimum((UInt)(V0.i), (UInt)(C1.i))].i; continue;
			default:		assert(0);
		}
		++ip;
	}
ret:
	this->clockCyclesLeft = clockCyclesLeft;
	this->ip = ip;
	this->dsp = dsp;
	this->ipsp = ipsp;
	assert(ipsp >= ipStackBase && ipsp <= ipStackEnd);
	return err;
}

#undef V0
#undef V1
#undef V2
#undef C0
#undef C1
#undef C2
#undef CHECK_INT_DIV_BY_ZERO

Status Processor::enterCall(Target target) {
	assert(codeBase != 0);
	UInt ui = target - FUNCTION_OFFSET;																					// ui = function ordinal
	if (ui >= functionCount) return BAD_CALL;
	ui = functionTable[ui];																								// ui = code offset
	assert((codeBase + ui)->opcode == FUNC_CC_);
	if (ipsp + 2 > ipStackEnd) return IP_STACK_OVERFLOW;
	ipsp->ip = this->ip;
	ipsp++->dsp = this->dsp;
	ipsp->ip = this->ip;
	ipsp++->dsp = 0;																									// Mark return to native caller.
	this->ip = codeBase + ui;
	return OK;
}

/*
	A frame stores its resume point minus one, because `RETU` restores it and the dispatch loop then increments `ip`.
	The first push resumes the `^call`'s continuation; each later one resumes the previously pushed function.
*/
Value* Processor::pushCall(Target target) {
	assert(codeBase != 0);
	UInt ui = target - FUNCTION_OFFSET;
	if (ui >= functionCount) return 0;
	const Instruction* func = codeBase + functionTable[ui];
	assert(func->opcode == FUNC_CC_);
	const bool atNativeCall = (ipsp != ipStackBase && this->ip->opcode == CALL_NVC);
	const bool chainedPush = (ipsp != ipStackBase && this->ip->opcode == FUNC_CC_);	// a previous pushCall in this native call
	if (!atNativeCall && !chainedPush) return 0;				// only valid from inside a native callback
	if (ipsp >= ipStackEnd) return 0;
	if ((UInt)func->p0.i + (UInt)func->p1.i > (UInt)(dataStackEnd - this->dsp)) return 0;	// the callee's own FUNC check, done early
	ipsp->ip = atNativeCall ? this->ip : this->ip - 1;
	ipsp++->dsp = atNativeCall ? this->dsp - this->ip->p1.i : this->dsp;
	this->ip = func;											// this->dsp stays: the ^call's window is the argument window
	return this->dsp;
}

// Frozen memory: docs/gazl/Specification.md section 11.
const UInt MEMORY_FORMAT_VERSION = 1;
const UInt MEMORY_HEADER_SIZE = 20;									// magic, format version, GAZL version, word size, entry count
const UInt MEMORY_CHECK_SIZE = 28;									// the check entry: "$gazl" and three words
static const char MEMORY_MAGIC[4] = { 'G', 'Z', 'M', 'M' };

static unsigned char* putMemoryWord(unsigned char* p, UInt v) {		// Writes a 32-bit little-endian word.
	p[0] = static_cast<unsigned char>(v);
	p[1] = static_cast<unsigned char>(v >> 8);
	p[2] = static_cast<unsigned char>(v >> 16);
	p[3] = static_cast<unsigned char>(v >> 24);
	return p + 4;
}

static UInt getMemoryWord(const unsigned char* p) {					// Reads a 32-bit little-endian word, byte by byte.
	return static_cast<UInt>(p[0]) | (static_cast<UInt>(p[1]) << 8) | (static_cast<UInt>(p[2]) << 16)
			| (static_cast<UInt>(p[3]) << 24);
}

static UInt memoryPadding(UInt nameLength) { return (4 - nameLength % 4) % 4; }	// zero bytes after a name

static unsigned char* putMemoryEntry(unsigned char* p, const char* name, UInt nameLength, const Value* words
		, UInt wordCount) {
	p = putMemoryWord(p, nameLength);
	memcpy(p, name, nameLength);
	p += nameLength;
	for (UInt i = memoryPadding(nameLength); i > 0; --i) *p++ = 0;
	p = putMemoryWord(p, wordCount);
	for (UInt i = 0; i < wordCount; ++i) p = putMemoryWord(p, words[i].p);
	return p;
}

static unsigned char* putMemoryCheck(unsigned char* p) {			// writes the check entry, exactly as any other entry
	Value check[3];
	check[0].p = 0x01020304;
	check[1].f = 1.0f;
	check[2].p = 0x7F800001;										// a signaling NaN, which a float load could quieten
	unsigned char* const next = putMemoryEntry(p, "$gazl", 5, check, 3);
	assert(next == p + MEMORY_CHECK_SIZE);
	return next;
}

struct MemoryEntry {														// One entry of frozen memory, pointing into the bytes it was read from.
	const unsigned char* name;												// not terminated: `nameLength` bytes
	UInt nameLength;
	UInt wordCount;
	const unsigned char* words;												// still encoded: `wordCount` little-endian words
	const unsigned char* next;												// the entry after this one
};

// Reads the entry at `p`, or returns false when its lengths run past `end`.
static bool getMemoryEntry(const unsigned char* p, const unsigned char* end, MemoryEntry& entry) {
	if (end - p < 4) return false;
	entry.nameLength = getMemoryWord(p);
	entry.name = p + 4;
	const size_t room = static_cast<size_t>(end - entry.name);
	if (entry.nameLength > room || room - entry.nameLength < memoryPadding(entry.nameLength) + 4) {
		return false;																	// no sum that can wrap
	}
	entry.words = entry.name + entry.nameLength + memoryPadding(entry.nameLength) + 4;
	entry.wordCount = getMemoryWord(entry.words - 4);
	if (static_cast<size_t>(end - entry.words) / 4 < entry.wordCount) return false;
	entry.next = entry.words + static_cast<size_t>(entry.wordCount) * 4;
	return true;
}

static int compareMemoryNames(const void* a, size_t aLength, const void* b, size_t bLength) {	// byte order, then length
	const int order = memcmp(a, b, minimum(aLength, bLength));
	return (order != 0 ? order : (aLength < bLength ? -1 : (aLength > bLength ? 1 : 0)));
}

struct MemoryOwner {
	GlobalInfo info;
	UInt nameLength;
};

// The owners in `GLOB` sections that own at least one word, in ascending name order.
static void findMemoryOwners(const Symbols& symbols, std::vector<MemoryOwner>& owners) {
	owners.clear();
	Symbols::Iterator it;
	for (bool ok = symbols.findFirstGlobal(it); ok; ok = symbols.findNextGlobal(it)) {
		const MemoryOwner owner = { symbols.getGlobalInfo(it), static_cast<UInt>(it->first.size()) };
		if (owner.info.section == GLOB_SECTION && owner.info.ownedWords != 0) owners.push_back(owner);
	}
}

static UInt frozenSize(const std::vector<MemoryOwner>& owners) {
	UInt size = MEMORY_HEADER_SIZE + MEMORY_CHECK_SIZE;
	for (size_t i = 0; i < owners.size(); ++i) {
		size += 4 + owners[i].nameLength + memoryPadding(owners[i].nameLength) + 4 + owners[i].info.ownedWords * 4;
	}
	return size;
}

UInt freezeMemorySize(const Processor& processor, const Symbols& symbols) {
	(void)processor;
	std::vector<MemoryOwner> owners;
	findMemoryOwners(symbols, owners);
	return frozenSize(owners);
}

UInt freezeMemory(const Processor& processor, const Symbols& symbols, void* buffer, UInt bufferSize) {
	std::vector<MemoryOwner> owners;
	findMemoryOwners(symbols, owners);
	const UInt needed = frozenSize(owners);
	if (buffer == 0 || bufferSize < needed) return needed;			// Too small: write nothing, tell the caller the required size.

	unsigned char* const start = static_cast<unsigned char*>(buffer);
	memcpy(start, MEMORY_MAGIC, 4);
	unsigned char* p = putMemoryWord(start + 4, MEMORY_FORMAT_VERSION);
	p = putMemoryWord(p, static_cast<UInt>(VERSION));
	p = putMemoryWord(p, static_cast<UInt>(WORD_SIZE));
	p = putMemoryWord(p, static_cast<UInt>(owners.size()) + 1);
	p = putMemoryCheck(p);
	for (size_t i = 0; i < owners.size(); ++i) {
		const GlobalInfo& owner = owners[i].info;
		const Value* words = processor.accessConstMemory(owner.address, owner.ownedWords);
		assert(words != 0);
		p = putMemoryEntry(p, owner.name, owners[i].nameLength, words, owner.ownedWords);
	}
	assert(static_cast<UInt>(p - start) == needed);					// Byte count must match `frozenSize` exactly.
	return needed;
}

// Checks run in the order of the specification's validation table; restoring starts only once all have passed.
MemoryLoad thawMemory(Processor& processor, const Symbols& symbols, const void* buffer, UInt bufferSize
		, bool prefix) {
	const unsigned char* const start = static_cast<const unsigned char*>(buffer);
	const unsigned char* const end = start + bufferSize;
	if (bufferSize < 4 || memcmp(start, MEMORY_MAGIC, 4) != 0) return MEMORY_BAD_MAGIC;
	if (bufferSize < MEMORY_HEADER_SIZE) return MEMORY_TRUNCATED;
	const UInt gazlVersion = getMemoryWord(start + 8);
	if (getMemoryWord(start + 4) != MEMORY_FORMAT_VERSION || gazlVersion == 0
			|| gazlVersion > static_cast<UInt>(VERSION)) {
		return MEMORY_BAD_VERSION;
	}
	if (getMemoryWord(start + 12) != static_cast<UInt>(WORD_SIZE)) return MEMORY_BAD_WORDSIZE;

	std::vector<MemoryEntry> entries;
	const unsigned char* p = start + MEMORY_HEADER_SIZE;
	for (UInt e = getMemoryWord(start + 16); e > 0; --e) {
		MemoryEntry entry;
		if (!getMemoryEntry(p, end, entry)) return MEMORY_TRUNCATED;
		entries.push_back(entry);
		p = entry.next;
	}

	unsigned char check[MEMORY_CHECK_SIZE];
	putMemoryCheck(check);
	const unsigned char* const first = start + MEMORY_HEADER_SIZE;
	if (entries.empty() || entries[0].next != first + MEMORY_CHECK_SIZE
			|| memcmp(first, check, MEMORY_CHECK_SIZE) != 0) {
		return MEMORY_BAD_CANARY;
	}

	for (size_t e = 1; e < entries.size(); ++e) {
		const MemoryEntry& entry = entries[e];
		const Char* const name = reinterpret_cast<const Char*>(entry.name);
		if (!isValidIdentifier(name, name + entry.nameLength)) return MEMORY_MALFORMED;
		for (UInt i = 0; i < memoryPadding(entry.nameLength); ++i) {
			if (entry.name[entry.nameLength + i] != 0) return MEMORY_MALFORMED;
		}
		if (e > 1 && compareMemoryNames(entries[e - 1].name, entries[e - 1].nameLength, entry.name, entry.nameLength)
				>= 0) {
			return MEMORY_MALFORMED;
		}
	}
	if (p != end) return MEMORY_MALFORMED;

	std::vector<MemoryOwner> owners;
	findMemoryOwners(symbols, owners);
	bool exact = true;
	size_t e = 1;
	size_t o = 0;
	while (e < entries.size() || o < owners.size()) {											// one merged walk: step the lower name, or both
		const int order = (e == entries.size() ? 1 : (o == owners.size() ? -1 : compareMemoryNames(entries[e].name
				, entries[e].nameLength, owners[o].info.name, owners[o].nameLength)));
		if (order != 0) {
			exact = false;
			if (order < 0) ++e;																	// an entry with no owner: dropped
			else ++o;																			// an owner with no entry: left unchanged
			continue;
		}
		const MemoryEntry& entry = entries[e++];												// the same name: restored
		const GlobalInfo& owner = owners[o++].info;
		exact = exact && entry.wordCount == owner.ownedWords;
		const UInt restored = (entry.wordCount == owner.ownedWords || prefix ? minimum(entry.wordCount, owner.ownedWords)
				: 0);
		Value* const destination = processor.accessMemory(owner.address, owner.ownedWords);
		assert(destination != 0);
		for (UInt i = 0; i < restored; ++i) destination[i].p = getMemoryWord(entry.words + i * 4);
	}
	return (exact ? MEMORY_OK : MEMORY_MISMATCH);
}

#if !defined(NDEBUG)

#include "UnitTest.inc"

struct TestCallbackData {
	Pointer globalPointer;
	Target callBack;
};

Status unitTestAssertFail(Processor*);
int testMul(Processor* p);
int testCallback(Processor* p);

Status unitTestAssertFail(Processor*) {
	assert(0);
	return ABORTED;
}

int testMul(Processor* p) {
	Value* params = p->accessParams(3);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].f = params[1].f * params[2].f;
	return 0;
}

int testCallback(Processor* p) {
	const TestCallbackData* data = reinterpret_cast<const TestCallbackData*>(p->getUserData());
	const Value* memory = p->accessConstMemory(data->globalPointer, 1);
	assert(memory != 0);
	if (memory == 0) return ACCESS_VIOLATION;
	assert(memory->p == 0);
	Value* params = p->accessParams(3);
	assert(params != 0);
	if (params == 0) return DATA_STACK_OVERFLOW;
	params[0].p = 0;
	params[1].t = data->callBack;
	int err = p->enterCall(data->callBack);
	assert(err == 0);
	if (err != 0) return err;
	err = p->run();
	assert(memory->t == data->callBack);
	return 0;
}

// The one list of unit-test natives: preAssemble's self-check and the freeze and thaw round trip need it identical.
static void seedTestNatives(Symbols& g) { g.registerNative("assertFail", 0); g.registerNative("testMul", 1); g.registerNative("testCallback", 2); }

bool unitTest() {
	assert(sizeof (Int) == sizeof (Pointer));
	assert(sizeof (UInt) == sizeof (Int));
	
	for (int i = 0; i < OPERATOR_COUNT; ++i) {
		const Operator& op = OPERATORS[i];
		assert(i == 0 || strcmp(op.key, OPERATORS[i - 1].key) > 0);
		assert(i == 0 || strncmp(op.key, OPERATORS[i - 1].key, 5) != 0													// every row of a word is GAZL 2 or none is
				|| ((op.otherFlags ^ OPERATORS[i - 1].otherFlags) & GAZL_2_ONLY) == 0);
		// std::cout << op.key << std::endl;
		for (int j = 0; j < 3; ++j) {
			assert(strchr("_bcnosv", op.key[6 + j]) != 0);
			assert(op.key[6 + j] != 'o' || ((op.accepts[j] & CONST_INT_P) != 0 && (op.accepts[j] & (ANY_VAR | BRANCH)) == 0));
			assert(op.key[6 + j] != '_' || op.accepts[j] == 0);
			assert(op.key[6 + j] != 'b' || (op.accepts[j] & (ANY_VAR | CONST_INT_P | CONST_FLOAT | BRANCH | ADDRESS | FUNC | NATIVE | COMPILE_TIME)) == BRANCH);
			assert(op.key[6 + j] != 'v' || (op.accepts[j] & ANY_VAR) != 0);
			assert(op.key[6 + j] != 'v' || (op.accepts[j] & (CONST_INT_P | CONST_FLOAT | BRANCH | ADDRESS | FUNC | NATIVE | COMPILE_TIME)) == 0);
			assert(op.key[6 + j] != 'c' || (op.accepts[j] & (CONST_INT_P | CONST_FLOAT | ADDRESS | FUNC | TARGET | NATIVE | COMPILE_TIME)) != 0);
			assert(op.key[6 + j] != 'c' || (op.accepts[j] & (ANY_VAR | BRANCH)) == 0);
			assert(op.opcode == FTOI_CCC || op.opcode == FTOI_VVC || op.opcode == IFDF_CB_ || op.key[4] != 'I' || (op.accepts[j] & (VAR_FLOAT_W | CONST_FLOAT)) == 0);
			assert(op.opcode == ITOF_CCC || op.opcode == ITOF_VVC || op.opcode == IFDF_CB_ || op.key[4] != 'F' || (op.accepts[j] & (VAR_INT_W | CONST_INT_P)) == 0);
			assert(op.accepts[j] == 0 || (op.accepts[j] & (ANY_VAR | CONST_INT_P | CONST_FLOAT | BRANCH | ADDRESS | FUNC | TARGET | NATIVE | COMPILE_TIME)) != 0);
			assert((op.accepts[j] & ANY_VAR) == 0 || (op.accepts[j] & (CONST_INT_P | CONST_FLOAT | BRANCH | ADDRESS | FUNC | NATIVE | COMPILE_TIME)) == 0);
			assert((op.accepts[j] & ANY_VAR) == 0 || (op.accepts[j] & FORWARD) == 0);
			assert(op.accepts[0] != COMPILE_TIME || (((op.accepts[1] & FORWARD) == 0 || op.accepts[1] == 0) && (((op.accepts[2] & FORWARD) == 0) || op.accepts[2] == 0)));
			assert((op.accepts[j] & CONST_INT_P) != 0 || (op.accepts[j] & CONST_INT_N) == 0);
			assert(op.accepts[j] == KONST																// `#const` may name a forward address
					|| (op.accepts[j] & (CONST_INT_P | CONST_FLOAT)) == 0 || (op.accepts[j] & FORWARD) == 0);
			assert((op.accepts[j] & BRANCH) == 0 || (op.accepts[j] & FORWARD) != 0);
		}
		assert(((op.otherFlags & (YIELDS_CONST | YIELDS_GOTO)) != 0 || op.key[0] == '!')
				== (op.opcode >= FIRST_COMPILE_TIME_OPCODE));
		assert((op.otherFlags & CHECK_DIV_BY_0) == 0 || (op.accepts[2] & (CONST_FLOAT | CONST_INT)) == CONST_FLOAT || (op.accepts[2] & (CONST_FLOAT | CONST_INT)) == CONST_INT);
		assert((op.declareTypes & FORWARD) == 0);
	}
	
	Value* memory = 0;
	Instruction* cody = 0;
	UInt* functionTable = 0;
	CallStackEntry* callStack = 0;

	static const NativeFunc nativeTable[] = {
		unitTestAssertFail, testMul, testCallback
	};
	
	try {
		const int MAX_CODE_SIZE = 1000;
		const int MAX_FUNCTION_COUNT = MAX_CODE_SIZE;	// A function is at least one instruction, so this can never overflow.
		const int MEMORY_SIZE = 2000;
		const int CATCH_ZONE_SIZE = MEMORY_SIZE;
		const int CALL_STACK_SIZE = 100;

		memory = new Value[MEMORY_SIZE + CATCH_ZONE_SIZE];
		cody = new Instruction[MAX_CODE_SIZE];
		functionTable = new UInt[MAX_FUNCTION_COUNT];
		callStack = new CallStackEntry[CALL_STACK_SIZE];

		Value v;
		v.i = 0xAACC5599;
		std::fill_n(&memory[0], MEMORY_SIZE + CATCH_ZONE_SIZE, v);
		
		Symbols globals;
		seedTestNatives(globals);

		ProgramSizes sizes = { 0, 0, 0, 0 };
		{
			Assembler assem(MAX_CODE_SIZE, cody, MAX_FUNCTION_COUNT, functionTable, MEMORY_SIZE, memory, globals);
			assem.newUnit("UnitTest");
			const Char* cp = UNITTEST;
			while (*cp != 0) {
				try {
					cp = assem.feed(cp);
					if (*cp == 0) assem.finalize(sizes);
				}
				catch (const Exception& e) {
					(void)e;
					assert(0);
				}
			}
		}

		// preAssemble() matches the real assembly above, retries from its small arenas, and hands back its table.
		{
			Symbols seed;
			seedTestNatives(seed);
			ProgramSizes measured = { 0, 0, 0, 0 };
			try {
				measured = Assembler::preAssemble(UNITTEST, seed);
			}
			catch (const Exception& e) {
				(void)e;
				assert(0);
			}
			assert(measured.codeSize == sizes.codeSize && measured.globalsSize == sizes.globalsSize
					&& measured.constsSize == sizes.constsSize && measured.functionCount == sizes.functionCount);
			{
				ConstantKind kind = FLOAT_CONSTANT;
				Value constant;
				constant.i = 0;
				const bool found = seed.lookupConstant("CR", kind, constant);			// `CR: ! DEFi #13` in UnitTest.gazl
				assert(found && kind == INT_CONSTANT && constant.i == 13);				// the table really came back, not just the four sizes
				(void)found;
			}
		}

		TestCallbackData callbackData;
		
		UInt size;
		callbackData.globalPointer = globals.findGlobal("global.pointer", size);
		assert(callbackData.globalPointer != 0);
		callbackData.callBack = globals.findFunction("CallBack");
		assert(callbackData.callBack != NULL_TARGET);

		std::vector<unsigned char> memoryBlob;
		{
			Processor pmachine(sizes.codeSize, cody, sizes.functionCount, functionTable, MEMORY_SIZE, memory
					, sizes.globalsSize, sizes.constsSize, CALL_STACK_SIZE, callStack, nativeTable, &callbackData);
			Target funcy = globals.findFunction("test");
			assert(funcy != NULL_TARGET);
			Status status = pmachine.enterCall(funcy);
			assert(status == OK);
			status = pmachine.run();
			assert(status == OK);
			const Value* memory = pmachine.accessConstMemory(callbackData.globalPointer, 1);
			assert(memory != 0);
			assert(memory->p != 0);
			memory = pmachine.accessConstMemory(memory->p, 4);
			assert(memory != 0);
			assert(memory[0].i == 'd' && memory[1].i == 'o' && memory[2].i == 'n' && memory[3].i == 'e');

			// Store a function pointer into a global so the freeze must round-trip an ordinal, then freeze.
			Value* globalPointer = pmachine.accessMemory(callbackData.globalPointer, 1);
			assert(globalPointer != 0);
			globalPointer->t = globals.findFunction("CallBack");
			memoryBlob.resize(freezeMemorySize(pmachine, globals));
			UInt written = freezeMemory(pmachine, globals, memoryBlob.empty() ? 0 : &memoryBlob[0], (UInt)memoryBlob.size());
			assert(written == memoryBlob.size());
		}

		// Thaw into a re-assembled machine: every `GLOB` word comes back, the stored function pointer included.
		{
			std::vector<Value> memory2(MEMORY_SIZE);
			std::vector<Instruction> cody2(MAX_CODE_SIZE);
			std::vector<UInt> functionTable2(MAX_FUNCTION_COUNT);
			std::vector<CallStackEntry> callStack2(CALL_STACK_SIZE);
			Symbols globals2;
			seedTestNatives(globals2);

			ProgramSizes sizes2 = { 0, 0, 0, 0 };
			{
				Assembler assem(MAX_CODE_SIZE, &cody2[0], MAX_FUNCTION_COUNT, &functionTable2[0], MEMORY_SIZE, &memory2[0]
						, globals2);
				assem.newUnit("UnitTest");
				const Char* cp = UNITTEST;
				while (*cp != 0) {
					cp = assem.feed(cp);
					if (*cp == 0) assem.finalize(sizes2);
				}
			}

			Processor pmachine2(sizes2.codeSize, &cody2[0], sizes2.functionCount, &functionTable2[0], MEMORY_SIZE, &memory2[0]
					, sizes2.globalsSize, sizes2.constsSize, CALL_STACK_SIZE, &callStack2[0], nativeTable, &callbackData);
			MemoryLoad loaded = thawMemory(pmachine2, globals2, memoryBlob.empty() ? 0 : &memoryBlob[0]
					, (UInt)memoryBlob.size(), false);
			assert(loaded == MEMORY_OK);

			// Every non-TEMP global must match the frozen original (identical assemblies share global addresses).
			Symbols::Iterator it;
			for (bool ok = globals.findFirstGlobal(it); ok; ok = globals.findNextGlobal(it)) {
				const GlobalInfo info = globals.getGlobalInfo(it);
				if (info.section == TEMP_SECTION) continue;
				const UInt index = info.address - MEMORY_OFFSET + (sizes.constsSize - 1);			// the buffer starts with the constants
				for (UInt i = 0; i < info.ownedWords; ++i) assert(memory2[index + i].i == memory[index + i].i);
			}
			// The restored function pointer resolves to CallBack in the fresh assembly (and equals the original ordinal).
			UInt gpSize;
			const Pointer gp = globals2.findGlobal("global.pointer", gpSize);
			const Value* restored = pmachine2.accessConstMemory(gp, 1);
			assert(restored != 0);
			assert(restored->t == globals2.findFunction("CallBack"));
			assert(restored->t == globals.findFunction("CallBack"));
		}

		for (int i = MEMORY_SIZE; i < MEMORY_SIZE + CATCH_ZONE_SIZE; ++i) assert(memory[i].i == static_cast<Int>(0xAACC5599));
	}
	catch (...) {
		delete [] memory;
		delete [] cody;
		delete [] functionTable;
		delete [] callStack;
		memory = 0;
		cody = 0;
		functionTable = 0;
		callStack = 0;

		assert(0);
	}

	delete [] memory;
	delete [] cody;
	delete [] functionTable;
	delete [] callStack;

	return true;
}

#endif

}

#undef STR

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC pop_options
#endif
#endif

#ifdef _MSC_VER
#pragma float_control(pop)
#endif

#if !defined(NDEBUG)
#ifdef REGISTER_UNIT_TEST
REGISTER_UNIT_TEST(GAZL::unitTest)
#endif
#endif

