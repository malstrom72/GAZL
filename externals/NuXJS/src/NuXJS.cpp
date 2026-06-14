/**
	NuXJS is released under the BSD 2-Clause License.

	Copyright (c) 2018-2025, Magnus Lidström

	Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
	following conditions are met:

	1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
	disclaimer.

	2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
	following disclaimer in the documentation and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
	INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
	ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
	SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
	THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
	IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**/

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC push_options
#pragma GCC optimize ("no-finite-math-only")
// older gcc might need this too:
// #pragma GCC optimize ("float-store")
#endif
#endif

#ifdef _MSC_VER
#pragma float_control(precise, on, push)  
#endif

#ifdef __FAST_MATH__
#error This code requires IEEE compliant floating point handling. Avoid -Ofast / -ffast-math etc (at least for this source file).
#endif

#include "assert.h"
#include <stdint.h>
#include <cmath>
#include "NuXJS.h"
#ifdef _MSC_VER
#include <float.h>
#endif

// Since these can be called during static initialization we allow heap == 0 to use standard new/delete.
void* operator new(size_t size, NuXJS::Heap* heap) {
	return (heap != 0 ? heap->allocate(size) : ::operator new(size));
}

void* operator new[](size_t size, NuXJS::Heap* heap) {
	return (heap != 0 ? heap->allocate(size) : ::operator new[](size));
}

void operator delete(void* ptr, NuXJS::Heap* heap) {
	if (ptr != 0) {
		if (heap != 0) {
			heap->free(ptr);
		} else {
			::operator delete(ptr);
		}
	}
}

void operator delete[](void* ptr, NuXJS::Heap* heap) {
	if (ptr != 0) {
		if (heap != 0) {
			heap->free(ptr);
		} else {
			::operator delete[](ptr);
		}
	}
}

namespace NuXJS {

// MSVC (at least 2010) doesn't do fmod correctly with infinite divisor
static double NaN() { return std::numeric_limits<double>::quiet_NaN(); }
#ifdef _MSC_VER
bool isNaN(double d) { return (_isnan(d) != 0); }
bool isFinite(double d) { return (_finite(d) != 0); }

/*
	ECMA rules
			-inf	-y		0		+y		+inf	NaN
			-------------------------------------------
	-inf   |NaN		NaN		NaN		NaN		NaN		NaN
	-x	   |-x		-x%y	NaN		-x%y	-x		NaN
	0	   |0		0		NaN		0		0		NaN
	+x	   |+x		x%y		NaN		x%y		+x		NaN
	+inf   |NaN		NaN		NaN		NaN		NaN		NaN
	NaN	   |NaN		NaN		NaN		NaN		NaN		NaN

	MSVC fmod
			-inf	-y		0		+y		+inf	NaN
			-------------------------------------------
	-inf   |NaN		NaN		NaN		NaN		NaN		NaN		!_finite
	-x	   |NaN*	-x%y	NaN		-x%y	NaN*	NaN		_finite
	0	   |NaN*	0		NaN		0		NaN*	NaN		_finite
	+x	   |NaN*	x%y		NaN		x%y		NaN*	NaN		_finite
	+inf   |NaN		NaN		NaN		NaN		NaN		NaN		!_finite
	NaN	   |NaN		NaN		NaN		NaN		NaN		NaN		!_finite
			!_f		_f		_f		_f		!_f		!_f
*/
static double modulo(double x, double y) { return (!_finite(y) && !_isnan(y) && _finite(x) ? x : fmod(x, y)); }
#else
static double modulo(double x, double y) { return fmod(x, y); }
// FIX : if c++11 use std::isnan and std::isfinite
bool isNaN(double d) { return d != d; }
bool isFinite(double d) { return !isNaN(d) && fabs(d) != std::numeric_limits<double>::infinity(); }
#endif

// Lossy conversion from unsigned to signed value is undefined behavior in C, which means that the optimizer may not
// assume that the value wraps.
static Int32 wrapToInt32(UInt32 i) {
	return (i >= 0x80000000U ? static_cast<Int32>(i - 0x80000000U) - 0x7FFFFFFF - 1 : static_cast<Int32>(i));
}
static double nanToZero(double d) { return (isNaN(d) ? 0.0 : d); }

/* --- Unicode stuff --- */

const Int32 UNICODE_MASKS[378] = {
	0,16,-2013265922,134217726,0,69207040,-8388609,-8388609,-1,-1,-1,-1,-1,-1,-1,-62914561,16777215,0,-65536,-1,-1,
	-100728321,196611,31,0,0,0,67108864,-10432,-5,1417641983,1048573,-8194,-1,-536936449,-1,-65533,-1,-58977,54513663,
	0,-131072,41943039,-2,255,0,-65536,460799,0,134217726,2047,-131072,-1,2097151999,3112959,96,0,0,0,0,0,0,0,0,-32,
	603979775,-16711680,3,-417824,63307263,-1342177280,196611,-423968,57540095,1577058304,1835008,-282656,602799615,
	65536,1,-417824,600702463,-1342177280,3,-700594208,62899992,0,0,-139296,66059775,0,3,-139296,66059775,1073741824,
	3,-139296,67108351,0,3,0,0,0,0,-2,917503,127,0,-17816170,537783470,805306463,0,1,0,-257,1023,3840,0,0,0,0,0,-1,
	-65473,8388607,-1,-1,-2080374785,-1,-1,-249,-1,67108863,-1,-1,-1,-1,268435455,-1,-1,67108863,1061158911,-1,
	-1426112705,1073741823,-1,1608515583,265232348,534519807,0,0,0,-2147483647-1,0,0,0,0,1060109444,33291600,0,-1,7,0,0,0,
	224,4064254,-2,-1,1612709887,-2,-1,2013265919,-32,-122881,-1,-1,32767,0,0,0,1,0,0,0,0,0,0,0,32,0,0,0,0,0,8,0,0,-1,
	-1,-1,-1,-1,-1,-1,-1,16383,0,0,0,0,0,0,-2131230593,1602223615,-37,-1,-1,262143,-524288,-1,1073741823,-65536,-1,
	-196609,-1,255,268369920,0,0,0,-2686976,-1,-1,-1,536870911,0,134217726,134217726,-64,-1,2147483647,486341884,0,
	67043344,-2013265922,134217726,0,69207040,-8388609,-8388609,-1,-1,63,67108867,-10432,-5,1417641983,1048573,-8194,
	-1,-536936449,-1,-65413,-1,-58977,54513663,0,-131072,41943039,-2,-130817,-1140850693,-65514,460799,0,134217726,
	524287,-64513,-1,2097151999,-1611694081,67059199,-18,-201326593,-14729217,65487,-417810,-741999105,-1333773921,
	262095,-423964,-747766273,1577073031,2097088,-282642,-202506753,80831,65473,-417810,-204603905,-1329579633,65475,
	-700594196,-1010841832,8404423,65408,-139282,-1007682049,6307295,65475,-139284,-1007682049,1080049119,65475,-139284,
	-1006633473,8404431,65475,0,0,0,0,-2,134217727,67076095,0,-17816170,1006628014,872365919,0,50331649,-1029700609,
	-257,-130049,-21032993,50216959,0,0,-2147483647-1,1,-2147483647-1,0,0,536805376,2,224,4128766,-2,-1,1713373183,-2,-1,
	2147483647,-1057488769,1602223615,-37,-1,-1,262143,-524288,-1,0,1572879,57344,-2686976,-1,-1,-1,536870911,67043328,
	-2013265922,134217726,-32,-1,2147483647,486341884,0
};

const UInt16 IDENTIFIER_START_OFFSETS[256] = {
	0,8,16,24,32,40,48,56,56,64,72,80,88,96,104,112,117,125,56,56,56,56,56,56,56,56,56,56,56,56,133,141,149,157,56,56,
	56,56,56,56,56,56,56,56,56,56,56,56,165,173,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,181,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,56,56,56,56,56,184,56,56,56,56,56,56,56,56,56,56,56,56,181,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,190,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,198,205,213,198,220,228,236
};

const UInt16 IDENTIFIER_PART_OFFSETS[256] = {
	243,8,16,251,259,267,275,56,56,283,291,299,307,315,323,331,117,125,56,56,56,56,56,56,56,56,56,56,56,56,133,141,338,
	157,56,56,56,56,56,56,56,56,56,56,56,56,56,56,346,173,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,56,56,56,56,181,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,56,56,56,56,56,56,56,56,56,184,56,56,56,56,56,56,56,56,56,56,56,56,181,56,56,56,56,56,56,56,56,56,56,
	56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,190,56,56,56,56,56,
	56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,56,198,205,354,198,220,362,370
};

static inline bool testUnicodeChar(Char c, const UInt16* offsets) {
	return (UNICODE_MASKS[offsets[c >> 8] + ((c & 255) >> 5)] & (1 << (c & 31))) != 0;
}

/* --- String constants --- */

const String BOOLEAN_STRING("boolean"), FUNCTION_STRING("function"), NUMBER_STRING("number"), OBJECT_STRING("object")
		, STRING_STRING("string");

const String EMPTY_STRING, LENGTH_STRING("length"), NULL_STRING("null"), UNDEFINED_STRING("undefined");

const String A_RGUMENTS_STRING("Arguments"), A_RRAY_STRING("Array"), B_OOLEAN_STRING("Boolean"), D_ATE_STRING("Date")
		, E_RROR_STRING("Error"), F_UNCTION_STRING("Function"), N_UMBER_STRING("Number"), O_BJECT_STRING("Object")
		, S_TRING_STRING("String");

static const String ANONYMOUS_SCRIPT_STRING("<anonymous>"), ANONYMOUS_STRING("anonymous")
		, ARGUMENTS_STRING("arguments"), BRACKET_OBJECT_STRING("[object "), CALLEE_STRING("callee")
		, CANNOT_CONVERT_TO_OBJECT_STRING("Cannot convert undefined or null to object")
		, CANNOT_USE_IN_OPERATOR_STRING("Cannot use 'in' operator on "), CLASS_STRING("class"), COLON_SPACE(": ")
		, CONSTRUCTOR_STRING("constructor"), END_BRACKET_STRING("]"), EVAL_CODE_STRING("<eval>"), EVAL_STRING("eval")
		, FALSE_STRING("false"), FUNCTION_SPACE("function "), INFINITY_STRING("Infinity")
		, IS_NOT_A_FUNCTION_STRING(" is not a function"), IS_NOT_DEFINED_STRING(" is not defined")
		, MESSAGE_STRING("message"), MINUS_INFINITY_STRING("-Infinity"), NAME_STRING("name"), NAN_STRING("NaN")
		, NATIVE_FUNCTION_STRING("function() { [native code] }"), PROTOTYPE_CHAIN_TOO_LONG("Prototype chain too long")
		, PROTOTYPE_STRING("prototype"), STACK_STRING("stack"), STACK_OVERFLOW_STRING("Stack overflow")
		, TRUE_STRING("true"), VALUE_STRING("value");

static const String ERROR_NAMES[ERROR_TYPE_COUNT] = {
	"Error", "EvalError", "RangeError", "ReferenceError", "SyntaxError", "TypeError", "URIError"
};

static const String* PROTOTYPE_NAMES[Runtime::PROTOTYPE_COUNT] = {
	&O_BJECT_STRING, &F_UNCTION_STRING, &S_TRING_STRING, &B_OOLEAN_STRING, &N_UMBER_STRING, &D_ATE_STRING
	, &A_RRAY_STRING, &ERROR_NAMES[0], &ERROR_NAMES[1], &ERROR_NAMES[2], &ERROR_NAMES[3], &ERROR_NAMES[4]
	, &ERROR_NAMES[5], &ERROR_NAMES[6]
};

static const String PROTOTYPES_STRING("prototypes"), IS_NAN_STRING("isNaN"), IS_FINITE_STRING("isFinite")
		, MAX_NUMBER_STRING("maxNumber"), MIN_NUMBER_STRING("minNumber");

/* --- Utilities --- */

// Returns pointer to beginning of string. End of string is always buffer + 32 (*not* zero-terminated by this routine).
static Char* intToString(Char buffer[32], Int32 i) {
	Char* p = buffer + 32;
	Int32 x = i;
	do {
		*--p = "9876543210123456789"[9 + x % 10];		// Mirrored string to handle negative x.
	} while ((x /= 10) != 0);
	if (i < 0) {
		*--p = '-';
	}
	assert(p >= buffer);
	return p;
}

const int MIN_EXPONENT = -324;
const int MAX_EXPONENT = 308;
const int NEGATIVE_E_NOTATION_START = -6;
const int POSITIVE_E_NOTATION_START = 21;

// FIX : shouldn't need this, used in quick-hash-finders and they should be optimized somehow i think
static int strncmp(const Char* a, const char* b, size_t n) {
	size_t i = 0;
	while (i < n && a[i] == b[i] && a[i] != 0 && b[i] != 0) {
		++i;
	}
	return (i == n ? 0 : a[i] - b[i]);
}
/* Built with QuickHashMaker.pika */
static int findStatementKeyword(size_t n /* string length */, const Char* s /* string (zero-termination not required) */) {
	static const char* STRINGS[13] = {
		"var", "if", "while", "do", "for", "return", "continue", "break", "try", 
		"throw", "switch", "function", "with"
	};
	static const int HASH_TABLE[64] = {
		-1, -1, -1, -1, 10, -1, -1, -1, -1, 4, -1, 3, 6, -1, -1, 11, 
		7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, -1, -1, -1, -1, 
		-1, 2, 12, 5, 9, -1, -1, -1, -1, -1, -1, -1, -1, 1, 8, -1
	};
	if (n < 2 || n > 8) return -1;
	int stringIndex = HASH_TABLE[(s[1] - s[0]) & 63];
	return (stringIndex >= 0 && strncmp(s, STRINGS[stringIndex], n) == 0 && STRINGS[stringIndex][n] == 0) ? stringIndex : -1;
}
/* Built with QuickHashMaker.pika */
static int findReservedKeyword(size_t n /* string length */, const Char* s) {
	static const char* STRINGS[36] = {
		"break", "case", "catch", "continue", "debugger", "default", "delete", "do", 
		"else", "finally", "for", "function", "if", "in", "instanceof", "new", 
		"return", "switch", "this", "throw", "try", "typeof", "var", "void", "while", 
		"with", "class", "const", "enum", "export", "extends", "import", "super", 
		"null", "false", "true"
	};
	static const int QUICK_HASH_TABLE[256] = {
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, 33, -1, -1, -1, -1, -1, -1, 2, 1, -1, -1, -1, 
		-1, -1, -1, -1, 23, -1, -1, -1, -1, -1, -1, -1, 35, 20, -1, -1, 
		-1, -1, -1, 34, -1, -1, -1, -1, -1, -1, -1, 32, -1, -1, -1, -1, 
		4, 5, 6, -1, -1, -1, -1, -1, -1, -1, 17, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, 21, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, 9, -1, 26, -1, -1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, 8, -1, -1, -1, 3, -1, -1, 27, -1, -1, -1, -1, 
		-1, -1, -1, -1, 28, 15, 7, -1, -1, -1, -1, 0, -1, -1, -1, -1, 
		-1, -1, -1, -1, -1, 10, -1, -1, -1, -1, 31, -1, -1, -1, 14, -1, 
		-1, -1, 16, -1, -1, 22, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 19, 18, -1, -1, -1, 
		-1, 30, 29, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
		-1, -1, -1, 24, -1, -1, -1, -1, -1, -1, -1, -1, 25, -1, -1, -1
	};
	if (n < 2 || n > 10) return -1;
	int stringIndex = QUICK_HASH_TABLE[(((s[1] + s[0]) << 3) - n) & 0xFF];
	return (stringIndex >= 0 && strncmp(s, STRINGS[stringIndex], n) == 0 && STRINGS[stringIndex][n] == 0) ? stringIndex : -1;
}
/* Built with QuickHashMaker.pika */
static int findLiteralKeyword(size_t n /* string length */, const Char* s /* zero-terminated string */) {
	static const char* STRINGS[5] = {
		"null", "false", "true", "function", "this"
	};
	static const int QUICK_HASH_TABLE[8] = {
		0, 2, -1, 3, -1, -1, 1, 4
	};
	if (n < 4 || n > 8) return -1;
	int stringIndex = QUICK_HASH_TABLE[((n ^ s[3])) & 7];
	return (stringIndex >= 0 && strncmp(s, STRINGS[stringIndex], n) == 0 && STRINGS[stringIndex][n] == 0) ? stringIndex : -1;
}

static const Char* parseHex(const Char* p, const Char* e, UInt32& i) {
	for (i = 0; p != e && ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f')); ++p) {
		i = (i << 4) + (*p <= '9' ? *p - '0' : (*p & ~0x20) - ('A' - 10));
	}
	return p;
}

static const Char* parseUnsignedInt(const Char* p, const Char* e, UInt32& i) {
	for (i = 0; p != e && *p >= '0' && *p <= '9'; ++p) {
		i = i * 10 + (*p - '0');
	}
	return p;
}

/*
	Helper class for high-precision double <=> string conversion routines. 52*2 bits of two doubles allows accurate
	representation of integers between 0 and 81129638414606681695789005144064.
*/
struct DoubleDouble {
	DoubleDouble() { }
	DoubleDouble(double d) : high(floor(d)), low(d - high) { }
	DoubleDouble(double high, double low) : high(high), low(low) {
		assert(high < ldexp(1.0, 53));
		assert(low < 1.0);
	}
	DoubleDouble operator+(const DoubleDouble& other) {
		const double lowSum = low + other.low;
		const double overflow = floor(lowSum);
		return DoubleDouble((high + other.high) + overflow, lowSum - overflow);
	}
	DoubleDouble operator*(int factor) const {
		const double lowTimesFactor = low * factor;
		const double overflow = floor(lowTimesFactor);
		return DoubleDouble((high * factor) + overflow, lowTimesFactor - overflow);
	}
	DoubleDouble operator/(int divisor) const {
		const double floored = floor(high / divisor);
		const double remainder = high - floored * divisor;
		return DoubleDouble(floored, (low + remainder) / divisor);
	}
	bool operator<(const DoubleDouble& other) const {
		return high < other.high || (high == other.high && low < other.low);
	}
	operator double() const {
		return high + low;
	}
	double high;
	double low;
};

static DoubleDouble multiplyAndAdd(const DoubleDouble& term, const DoubleDouble& factorA, double factorB) {
	const double fmaLow = factorA.low * factorB + term.low;
	const double overflow = floor(fmaLow);
	return DoubleDouble(factorA.high * factorB + term.high + overflow, fmaLow - overflow);
}

/**
	If we just do (high + low) first, that sum is rounded to 53 bits once, possibly nudging the result slightly upward.
	Then when we scale down into the subnormal range (right-shift the mantissa) we hit what looks like an exact halfway
	case — and since the current mantissa is odd, IEEE-754 rounds up again. In reality, the exact (high+low) value was
	just below that halfway point, so it should have rounded down to the even mantissa. This is a classic "double
	rounding" problem.

	scaleAndRound avoids this by combining high and low at full precision under the final exponent window and
	performing a *single* correct round-to-nearest-even step. This matches the Decimal oracle and fixes all denormal
	boundary mismatches.

	Assumptions:
	- 'factor' is an exact power-of-two (normal or subnormal) from the table.
	- 'acc.high' is integral in [0, 2^53) and 'acc.low' ∈ [0,1).
	- Table ensures factorExponent >= -1073 so T = factorExponent + 1073 >= 0.
**/
static double scaleAndRound(const DoubleDouble& acc, double factor) {
	if (acc.high == 0.0 && acc.low == 0.0) {
		return 0.0;
	}
	
	const double fastResult = (acc.high + acc.low) * factor;
	if (fastResult >= 2.2250738585072014e-308) {
		return fastResult;												// normal result; fast path is exact here
	}
	
	int factorExponent;													// slow path: denormal/transition region
	frexp(factor, &factorExponent);										// assemble payload then single rounding
	
	const int t = factorExponent + 1073;								// guaranteed by table construction
	assert(t >= 0);														// (no right-shift branch needed)	
	const double bf = ldexp(acc.low, t);								// align (high, low) into the 52-bit subnormal payload scale
	const double bi = floor(bf);
	const double fraction = bf - bi;									// fractional contribution
	
	double ni = ldexp(acc.high, t) + bi;								// integer payload (exact in double)
	if (fraction > 0.5 || (fraction == 0.5 && fmod(ni, 2.0) != 0.0)) {
		ni += 1.0;														// round to nearest, ties-to-even
	}
	
	return ldexp(ni, -1074);											// subnormal construction (or DBL_MIN when ni == 2^52)
}

const int QUICK_CONSTANTS_INTEGERS_RANGE = 1000;
struct QuickConstants {
	QuickConstants() {
		for (int i = 0; i < 127; ++i) {
			Char buf[1] = { static_cast<Char>(i & 0xFF) };
			asciiChars[i] = String(buf, buf + 1);
			ascii[i] = &asciiChars[i];
		}
		for (int i = -QUICK_CONSTANTS_INTEGERS_RANGE; i <= QUICK_CONSTANTS_INTEGERS_RANGE; ++i) {
			Char buffer[32];
			integers[i + QUICK_CONSTANTS_INTEGERS_RANGE] = String(intToString(buffer, i), buffer + 32); // FIX : newHashedString
			integers[i + QUICK_CONSTANTS_INTEGERS_RANGE].createBloomCode();
		}

		/*
			Generate a table of `DoubleDoubles` for all powers of 10 from -324 to 308. The `DoubleDoubles` are
			normalized to take up as many bits as possible while leaving enough headroom to allow multiplications of up
			to 10 without overflowing. The exp10Factors array will contain the multiplication factors required to
			revert the normalization. I.e. `static_cast<double>(normals[1 - (-324)]) * factors[1 - (-324)] == 10.0`.
			Notice that for the very lowest exponents we refrain from normalizing to correctly handle denormal values.
		*/
		const double WIDTH = ldexp(1.0, 53 - 4);

		DoubleDouble normal(WIDTH, 0.0);
		double factor = 1.0 / WIDTH;
		for (int i = 0; i <= MAX_EXPONENT; ++i) {
			if (normal.high >= WIDTH) {
				factor *= 16.0;
				normal = normal / 16;
			}
			assert(factor < std::numeric_limits<double>::infinity());
			exp10Normals[i - MIN_EXPONENT] = normal;
			exp10Factors[i - MIN_EXPONENT] = factor;
			normal = normal * 10;
		}
	
		normal = DoubleDouble(WIDTH, 0.0);
		factor = 1.0 / WIDTH;
		for (int i = -1; i >= MIN_EXPONENT; --i) {
			// Check factor / 16.0 > 0.0 to avoid normalizing denormal exponents.
			if (normal.high < WIDTH && factor / 16.0 > 0.0) {
				factor /= 16.0;
				normal = normal * 16;
			}
			normal = normal / 10;
			exp10Normals[i - MIN_EXPONENT] = normal;
			exp10Factors[i - MIN_EXPONENT] = factor;
		}
	}
	Value ascii[127];
	String asciiChars[127];
	String integers[QUICK_CONSTANTS_INTEGERS_RANGE * 2 + 1];
	DoubleDouble exp10Normals[MAX_EXPONENT + 1 - MIN_EXPONENT];
	double exp10Factors[MAX_EXPONENT + 1 - MIN_EXPONENT];
} QUICK_CONSTANTS;

static Char* doubleToString(Char buffer[32], const double value) {
	Char* p = buffer;

	double absValue = value;
	if (value < 0) {
		*p++ = '-';
		absValue = -value;
	}
	
	if (absValue == 0.0) {
		*p++ = '0';
		return p;
	}

	// frexp is fast and precise and gives log2(x), log10(x) = log2(x) / log2(10)
	int base2Exponent;
	(void) frexp(absValue, &base2Exponent);
	int exponent = std::max(static_cast<int>(ceil(0.30102999566398119521 * (base2Exponent - 1))) - 1, MIN_EXPONENT);
	if (exponent < MAX_EXPONENT) {
		assert(MIN_EXPONENT <= exponent + 1 && exponent + 1 <= MAX_EXPONENT);
		const double factor = QUICK_CONSTANTS.exp10Factors[exponent + 1 - MIN_EXPONENT];

		// Notice that in theory we could have a value that is considered equal to next magnitude but should be rounded
		// downwards (to a lower exponential) and not upwards. However in reality, only the first denormal power of 10
		// would be a candidate for this, and for both double and single precision floats, they round upwards.
		if (absValue >= static_cast<double>(QUICK_CONSTANTS.exp10Normals[exponent + 1 - MIN_EXPONENT]) * factor) {
			++exponent;
		}
	}
	
	const bool eNotation = (exponent < NEGATIVE_E_NOTATION_START || exponent >= POSITIVE_E_NOTATION_START);
	Char* periodPosition = p + (eNotation || exponent < 0 ? 0 : exponent) + 1;
	if (!eNotation && exponent < 0) {
		*p++ = '0';
		*p++ = '.';
		while (p < periodPosition - exponent) {
			*p++ = '0';
		}
	}

	assert(MIN_EXPONENT <= exponent && exponent <= MAX_EXPONENT);
	const double factor = QUICK_CONSTANTS.exp10Factors[exponent - MIN_EXPONENT];
	DoubleDouble magnitude = QUICK_CONSTANTS.exp10Normals[exponent - MIN_EXPONENT];
	const DoubleDouble normalized = absValue / factor;
	DoubleDouble accumulator = 0.0;
	double reconstructed;
	do {
		if (p == periodPosition) {
			*p++ = '.';
		}
		
		// Incrementally find the max digit that keeps accumulator < normalized target (instead of using division).
		DoubleDouble next = accumulator + magnitude;
		int digit = 0;
		while (next < normalized && digit < 9) {
			accumulator = next;
			next = next + magnitude;
			++digit;
		}
		assert(next >= normalized); // Correct behavior is to never reach higher than digit 9.

		// Do we hit goal with digit or digit + 1? If so, is next digit >= 5 (magnitude / 2) then increment it.
		reconstructed = scaleAndRound(accumulator, factor);
		if (reconstructed != absValue) {
			reconstructed = scaleAndRound(accumulator + magnitude, factor);
		}
		if (reconstructed == absValue && accumulator + magnitude / 2 < normalized) {
			++digit;
			assert(digit < 10); // If this happens we have failed to calculate the correct exponent above.
		} else {
			assert(accumulator > 0.0); // If this happens we have failed to calculate the correct exponent above.
		}

		*p++ = '0' + digit;
		magnitude = magnitude / 10;
		
		// p < buffer + 27 is an extra precaution if the correct value is never reached (e.g. because of too aggressive
		// optimizations). 27 leaves room for longest exponent.
	} while (p < buffer + 27 && reconstructed != absValue);

	while (p < periodPosition) {
		*p++ = '0';
	}
	
	if (eNotation) {
		*p++ = 'e';
		*p++ = (exponent < 0 ? '-' : '+');

		// intToString fills the buffer from right (buffer + 32) to left and there should always be enough space
		p = std::copy(intToString(buffer, exponent < 0 ? -exponent : exponent), buffer + 32, p);
	}
	assert(p <= buffer + 32);
	return p;
}

static const Char* parseDouble(const Char* const b, const Char* const e, double& value) {
	int exponent = -1;
	double sign = 1.0;
	const Char* significandBegin = b;
	const Char* numberEnd;

	const Char* p = b;
	if (p != e && (*p == '-' || *p == '+')) {
		sign = (*p == '-' ? -1.0 : 1.0);
		++p;
		significandBegin = p;
	}
	if (e - p >= 8 && strncmp(p, "Infinity", 8) == 0) {
		p += 8;
		value = std::numeric_limits<double>::infinity();
		numberEnd = p;
	} else {
		while (p != e && *p >= '0' && *p <= '9') {
			++exponent;
			++p;
		}
		if (p != e && *p == '.') {
			if (p == significandBegin) {
				++significandBegin;
			}
			++p;
			while (p != e && *p >= '0' && *p <= '9') {
				++p;
			}
		}

		if (p == significandBegin) {
			value = 0.0;
			return b;
		}

		const Char* significandEnd = p;
		numberEnd = p;

		if (e - p >= 2 && (*p == 'e' || *p == 'E')) {
			++p;
			Int32 sign = (*p == '-' ? -1 : 1);
			if (*p == '+' || *p == '-') {
				++p;
			}
			UInt32 ui;
			const Char* q = parseUnsignedInt(p, e, ui);
			if (q != p) {
				exponent += sign * wrapToInt32(ui);
				numberEnd = q;
			}
		}
		
		p = significandBegin;
		while (p != significandEnd && (*p == '0' || *p == '.')) {
			if (*p == '0') {
				--exponent;
			}
			++p;
		}
		
		if (p == significandEnd || exponent < MIN_EXPONENT) {
			value = 0.0;
		} else if (exponent > MAX_EXPONENT) {
			value = std::numeric_limits<double>::infinity();
		} else {
			DoubleDouble magnitude = QUICK_CONSTANTS.exp10Normals[exponent - MIN_EXPONENT];
			DoubleDouble accumulator(0.0, 0.0);
			while (p != significandEnd) {
				if (*p != '.') {
					accumulator = multiplyAndAdd(accumulator, magnitude, (*p - '0'));
					magnitude = magnitude / 10;
				}
				++p;
			}
			const double factor = QUICK_CONSTANTS.exp10Factors[exponent - MIN_EXPONENT];
			value = scaleAndRound(accumulator, factor);
		}
	}
	value *= sign;
	return numberEnd;
}

static const Char* eatStringWhite(const Char* p, const Char* e) {
	while (p != e) {
		switch (*p) {
			case ' ': case '\f': case '\n': case '\r': case '\t': case '\v': case 0xA0: case 0x2028: case 0x2029: break;
			default: return p;
		}
		++p;
	}
	return p;
}

static double stringToDouble(const String& s) {
	double v = 0.0;
	const Char* p = s.begin();
	const Char* e = s.end();
	p = eatStringWhite(p, e);
	if (p != e && *p == '0' && p + 1 != e && (p[1] == 'x' || p[1] == 'X')) {
		UInt32 u;
		const Char* b = p + 2;
		p = parseHex(b, e, u);
		if (p == b) {
			return NaN();
		}
		v = u;
	} else {
		p = parseDouble(p, e, v);
	}
	p = eatStringWhite(p, e);
	return (p == e ? v : NaN());
}

class GenericWrapper : public JSObject {
	public:
		typedef JSObject super;
		GenericWrapper(GCList& gcList, const String* className, const Value& value, Runtime::PrototypeId prototypeId
				, Object* prototype = 0)
				: super(gcList, prototype), className(className), wrapped(value), prototypeId(prototypeId) {
			assert(prototypeId != Runtime::ARBITRARY_PROTOTYPE || prototype != 0);
		}
		virtual const String* getClassName() const { return className; }
		virtual Value getInternalValue(Heap&) const { return wrapped; }
		virtual Object* getPrototype(Runtime& rt) const {
			return (prototypeId == Runtime::ARBITRARY_PROTOTYPE
					? super::getPrototype(rt) : rt.getPrototypeObject(prototypeId));
		}
		void setInternalValue(const Value& v) { wrapped = v; }

	protected:
		const String* const className;
		Runtime::PrototypeId prototypeId;	// for wrappers created without Runtime
		Value wrapped;
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, className);
			gcMark(heap, wrapped);
			super::gcMarkReferences(heap);
		}
};

/* --- StringWrapper --- */

class StringWrapper : public JSObject {
	public:
		typedef JSObject super;
		StringWrapper(GCList& gcList, const String* string) : super(gcList, 0), wrapped(string) { assert(string != 0); }
		virtual const String* getClassName() const { return &S_TRING_STRING; }
		virtual Value getInternalValue(Heap&) const { return wrapped; }
		virtual Object* getPrototype(Runtime& rt) const {
			Object* stringPrototype = rt.getPrototypeObject(Runtime::STRING_PROTOTYPE);
			return (this == stringPrototype ? rt.getObjectPrototype() : stringPrototype);
		}
		virtual Flags getOwnProperty(Runtime& rt, const Value& key, Value* v) const {
			Flags flags = wrapped->getOwnProperty(rt, key, v);
			return (flags != NONEXISTENT ? flags : super::getOwnProperty(rt, key, v));
		}
		virtual bool setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags = STANDARD_FLAGS) {
			return (wrapped->hasOwnProperty(rt, key) ? false : super::setOwnProperty(rt, key, v, flags));
		}
		virtual bool deleteOwnProperty(Runtime& rt, const Value& key) {
			return (wrapped->hasOwnProperty(rt, key) ? false : super::deleteOwnProperty(rt, key));
		}
		virtual Enumerator* getOwnPropertyEnumerator(Runtime& rt) const {
			Heap& heap = rt.getHeap();
			return new(heap) JoiningEnumerator(heap.managed(), rt, wrapped, super::getOwnPropertyEnumerator(rt));
		}

	protected:
		const String* wrapped;
		virtual void gcMarkReferences(Heap& heap) const {
			gcMark(heap, wrapped);
			super::gcMarkReferences(heap);
		}
};

/* --- Value --- */

const String* Value::TYPE_STRINGS[5] = {
	&UNDEFINED_STRING, &OBJECT_STRING, &BOOLEAN_STRING, &NUMBER_STRING, &STRING_STRING
};

Function* Value::toFunction(Heap& heap) const {
	Function* f = asFunction();
	if (f == 0) {
		throw ScriptException(heap, new(heap) Error(heap.managed(), TYPE_ERROR
				, new(heap) String(heap.managed(), *toString(heap), IS_NOT_A_FUNCTION_STRING)));
	}
	return f;
}

const String* Value::typeOfString() const {
	return (type == OBJECT_TYPE ? var.object->typeOfString() : TYPE_STRINGS[static_cast<int>(type)]);
}

bool Value::toBool() const {
	switch (type) {
		default: assert(0);
		case UNDEFINED_TYPE:
		case NULL_TYPE: return false;
		case BOOLEAN_TYPE: return var.boolean;
		case NUMBER_TYPE: return nanToZero(var.number) != 0.0;
		case STRING_TYPE: return (!var.string->empty());
		case OBJECT_TYPE: return true;
	}
}

double Value::toDouble() const {
	switch (type) {
		default: assert(0);
		case UNDEFINED_TYPE:
		case OBJECT_TYPE: return NaN();  // Notice, you shouldn't normally call toDouble on an object as proper ToNumber requires conversion to a primitive type
		case NULL_TYPE: return 0.0;
		case BOOLEAN_TYPE: return var.boolean ? 1.0 : 0.0;
		case NUMBER_TYPE: return var.number;
		case STRING_TYPE: return stringToDouble(*var.string);
	}
}

Int32 Value::toInt() const {
	const double v = toDouble();
	if (v > -2147483649.0 && v < 2147483648.0) {
		return static_cast<Int32>(v);
	} else if (!isFinite(v)) {
		return 0;
	} else {
		const UInt32 ui = static_cast<UInt32>(fmod(fabs(v), 4294967296.0));
		return (v >= 0 ? wrapToInt32(ui) : -wrapToInt32(ui));
	}
}

bool Value::toArrayIndex(UInt32& index) const {
	switch (type) {
		case NUMBER_TYPE: {
			const double n = var.number;
			if (n < 0.0 || n >= 4294967295.0) {
				return false;
			}
			index = static_cast<UInt32>(n);
			return index == n;
		}
		case STRING_TYPE: {
			const Char* p = var.string->begin();
			const Char* const e = var.string->end();
			index = 0;
			if (p == e || (*p == '0' && p + 1 != e)) {
				return false;
			}
			while (p != e && *p >= '0' && *p <= '9') {
				const UInt32 digit = static_cast<UInt32>(*p - '0');
				if (index > 429496729U || (index == 429496729U && digit >= 5U)) {
					return false;
				}
				index = index * 10 + digit;
				++p;
			}
			return (p == e);
		}
		default: return false;
	}
}

const String* Value::toString(Heap& heap) const {
	switch (type) {
		default: assert(0);
		case UNDEFINED_TYPE: return &UNDEFINED_STRING;
		case NULL_TYPE: return &NULL_STRING;
		case BOOLEAN_TYPE: return var.boolean ? &TRUE_STRING : &FALSE_STRING;
		case NUMBER_TYPE: return String::fromDouble(heap, var.number);
		case STRING_TYPE: return var.string;
		case OBJECT_TYPE: return var.object->toString(heap);
	}
}

Object* Value::toObjectOrNull(Heap& heap, bool requireExtensible) const {
	switch (type) {
		default: assert(0);
		case UNDEFINED_TYPE: return 0;
		case NULL_TYPE: return 0;
		case BOOLEAN_TYPE: return new(heap) GenericWrapper(heap.managed(), &B_OOLEAN_STRING, *this, Runtime::BOOLEAN_PROTOTYPE);
		case NUMBER_TYPE: return new(heap) GenericWrapper(heap.managed(), &N_UMBER_STRING, *this, Runtime::NUMBER_PROTOTYPE);
		// I think const_cast can be accepted here, the string is always immutable when accessed as an Object
		case STRING_TYPE: return (!requireExtensible ? static_cast<Object*>(const_cast<String*>(var.string)) : new(heap) StringWrapper(heap.managed(), var.string));
		case OBJECT_TYPE: return var.object;
	}
}

Object* Value::toObject(Heap& heap, bool requireExtensible) const {
	Object* object = toObjectOrNull(heap, requireExtensible);
	if (object == 0) {
		throw ScriptException(heap, new(heap) Error(heap.managed(), TYPE_ERROR, &CANNOT_CONVERT_TO_OBJECT_STRING));
	}
	return object;
}

bool Value::compareStrictly(const Value& r) const {
	assert(r.type == type);
	switch (type) {
		default: assert(0);
		case UNDEFINED_TYPE:
		case NULL_TYPE: return true;
		case BOOLEAN_TYPE: return var.boolean == r.var.boolean;
		case NUMBER_TYPE: return var.number == r.var.number;
		case STRING_TYPE: return var.string->isEqualTo(*r.var.string);
		case OBJECT_TYPE: return var.object == r.var.object;
	}
}

/*
				undefined	null	boolean				number			string				object
	undefined	TRUE		TRUE	FALSE				FALSE			FALSE				FALSE
	null		TRUE		TRUE	FALSE				FALSE			FALSE				FALSE
	boolean		FALSE		FALSE	l === r				num(l) === r	num(l) === num(r)	num(l) === num(r)
	number		FALSE		FALSE	l === num(r)		l === r			l === num(r)		l == prim(r)
	string		FALSE		FALSE	num(l) === num(r)	num(l) === r	l === r				l == prim(r)
	object		FALSE		FALSE	num(l) === num(r)	prim(l) == r	prim(l) == r		l === r
*/
bool Value::isEqualTo(const Value& r) const {
	if (type == r.type) {
		return compareStrictly(r);
	} else if (type < r.type) {	// Symmetrical operation, flip for simpler logic.
		return r.isEqualTo(*this);
	} else {
		switch (type) {
			case NULL_TYPE: return true;
			case BOOLEAN_TYPE: return false;
			case NUMBER_TYPE: return (r.type == BOOLEAN_TYPE && var.number == (r.var.boolean ? 1.0 : 0.0));
			case STRING_TYPE: return (r.type >= BOOLEAN_TYPE && stringToDouble(*var.string)
					== (r.type == BOOLEAN_TYPE ? (r.var.boolean ? 1.0 : 0.0) : r.var.number));
			default: return false;
		}
	}
}

bool Value::isLessThan(const Value& y) const {
	if (type == STRING_TYPE && y.type == STRING_TYPE) {
		return var.string->isLessThan(*y.var.string);
	}
	return toDouble() < y.toDouble();
}

bool Value::isLessThanOrEqualTo(const Value& y) const {
	if (type == STRING_TYPE && y.type == STRING_TYPE) {
		return !y.var.string->isLessThan(*var.string);
	}
	return toDouble() <= y.toDouble();
}

Value Value::add(Heap& heap, const Value& y) const {
	if (type == STRING_TYPE || y.type == STRING_TYPE) {
		const String* ls = toString(heap);
		const String* rs = y.toString(heap);
		return rs->empty() ? ls : (ls->empty() ? rs : String::concatenate(heap, *ls, *rs));
	}
	return toDouble() + y.toDouble();
}

const Value Value::UNDEFINED(Value::UNDEFINED_TYPE), Value::NUL(Value::NULL_TYPE), Value::NOT_A_NUMBER(NaN())
		, Value::INFINITE_NUMBER(std::numeric_limits<double>::infinity())
		, Value::NEG_INFINITE_NUMBER(-std::numeric_limits<double>::infinity());

const Value UNDEFINED_VALUE(Value::UNDEFINED), NULL_VALUE(Value::NUL), NAN_VALUE(Value::NOT_A_NUMBER)
		, INFINITY_VALUE(Value::INFINITE_NUMBER), NEG_INFINITY_VALUE(Value::NEG_INFINITE_NUMBER)
		, FALSE_VALUE(false), TRUE_VALUE(true);

std::wostream& operator<<(std::wostream& out, const Value& v) {
	Heap heap; // ok to use a temporary heap here for the string conversion as we discard the string immediately
	out << v.toWideString(heap);
	return out;
}

void gcMark(Heap& heap, const Value& v) {
	switch (v.type) {
		case Value::STRING_TYPE: gcMark(heap, v.var.string); break;
		case Value::OBJECT_TYPE: gcMark(heap, v.var.object); break;
		default: break;
	}
}

/* --- String --- */

static UInt32 utf16Length(size_t l, const wchar_t* s) {
	UInt32 n = static_cast<UInt32>(l);
	assert(n == l);
	if (sizeof (wchar_t) == 4) {
		const wchar_t* e = s + l;
		for (const wchar_t* p = s; p != e; ++p) {
			const UInt32 c = static_cast<UInt32>(*p);
			assert(c <= 0x10FFFF);
			n += (c >= 0x10000 ? 1 : 0);
		}
	}
	return n;
}

static void toUTF16Chars(const wchar_t* s, UInt32 n, Char* d) {
	if (sizeof (wchar_t) == sizeof (Char)) {
		std::copy(s, s + n, d);
	} else {
		assert(sizeof (wchar_t) == 4);
		const Char* const e = d + n;
		while (d != e) {
			const UInt32 c = static_cast<UInt32>(*s++);
			if ((c >> 16) == 0) {
				*d++ = static_cast<Char>(c);
			} else {
				*d++ = static_cast<Char>(0xD800 | ((c - 0x10000) >> 10));
				assert(d != e);
				*d++ = static_cast<Char>(0xDC00 | ((c - 0x10000) & 0x3FF));
			}
		}
	}
}

String::String() : chars(0, 0), hash(2166136261U), bloom(0x10000020) {
	assert(String("").calcHash() == hash && String("").createBloomCode() == bloom);
}

String::String(const Char* b, const Char* e) : chars(b, e, 0), hash(0), bloom(0) { createBloomCode(); }

String::String(const char* s) : chars(static_cast<UInt32>(std::strlen(s)), 0), hash(0), bloom(0) {
	assert(size() == std::strlen(s));
	std::copy(s, s + size(), begin());
	createBloomCode();
}

String::String(const wchar_t* s) : chars(utf16Length(std::wcslen(s), s), 0), hash(0), bloom(0) {
	toUTF16Chars(s, size(), begin());
	createBloomCode();
}

String::String(GCList& gcList, const char* b, const char* e)
		: super(gcList), chars(static_cast<UInt32>(e - b), &gcList.getHeap()), hash(0), bloom(0) {
	assert(size() == e - b);
	std::copy(b, e, begin());
	createBloomCode();
}

String::String(GCList& gcList, const Char* b, const Char* e)
		: super(gcList), chars(b, e, &gcList.getHeap()), hash(0), bloom(0) {
}

String::String(GCList& gcList, const String& l, const String& r)
		: super(gcList), chars(l.size() + r.size(), &gcList.getHeap()), hash(0), bloom(0) {
	assert(size() == (l.end() - l.begin() + (r.end() - r.begin())));
	Char* e = std::copy(l.begin(), l.end(), begin());
	std::copy(r.begin(), r.end(), e);
}

String::String(GCList& gcList, const char* s)
		: super(gcList), chars(static_cast<UInt32>(std::strlen(s)), &gcList.getHeap()), hash(0), bloom(0) {
	assert(size() == std::strlen(s));
	std::copy(s, s + size(), begin());
}

String::String(GCList& gcList, const wchar_t* s)
		: super(gcList), chars(utf16Length(std::wcslen(s), s), &gcList.getHeap()), hash(0), bloom(0) {
	toUTF16Chars(s, size(), begin());
}

String::String(GCList& gcList, const std::string& s)
		: super(gcList), chars(static_cast<UInt32>(s.size()), &gcList.getHeap()), hash(0), bloom(0) {
	assert(size() == s.size());
	std::copy(s.begin(), s.end(), begin());
}

String::String(GCList& gcList, const std::wstring& s)
		: super(gcList), chars(utf16Length(s.size(), s.data()), &gcList.getHeap()), hash(0), bloom(0) {
	toUTF16Chars(s.data(), size(), begin());
}

const String* String::typeOfString() const { return &STRING_STRING; }
const String* String::getClassName() const { return &S_TRING_STRING; }
Object* String::getPrototype(Runtime& rt) const { return rt.getPrototypeObject(Runtime::STRING_PROTOTYPE); }

Flags String::getOwnProperty(Runtime& rt, const Value& key, Value* v) const {
	if (key.equalsString(LENGTH_STRING)) {
		*v = static_cast<UInt32>(size());
		return HIDDEN_CONST_FLAGS;
	}
	UInt32 index;
	if (key.toArrayIndex(index) && index < size()) {
		const Char* p = begin() + index;
		Heap& heap = rt.getHeap();
		*v = (*p < 127 ? QUICK_CONSTANTS.ascii[*p] : new(heap) String(heap.managed(), p, p + 1));
		return (READ_ONLY_FLAG | DONT_DELETE_FLAG | EXISTS_FLAG);
	}
	return NONEXISTENT;
}

// Modified FNV Hash
UInt32 String::calcHash() const {
	if (hash == 0) {
		UInt32 x = 2166136261U;
		for (const Char* p = begin(), *e = end(); p != e; ++p) {
			x = (x << 3) | (x >> (32 - 3));
			x = (x + *p) * 16777619;
		}
		hash = (x + (x == 0 ? 1 : 0));
	}
	return hash;
}

bool String::isLessThan(const String& o) const {
	if (this == &o) {
		return false;
	}
	UInt32 l = std::min(size(), o.size());
	const Char* a = begin();
	const Char* b = o.begin();
	for (UInt32 i = 0; i < l; ++i) {
		if (a[i] != b[i]) {
			return (a[i] < b[i]);
		}
	}
	return size() < o.size();
}

UInt32 String::createBloomCode() const {
	if (bloom == 0) {
		const UInt32 hash = calcHash();
		bloom = (static_cast<UInt32>(1) << (hash & 31)) | (static_cast<UInt32>(1) << ((hash >> 16) & 31));
	}
	return bloom;
}

Enumerator* String::getOwnPropertyEnumerator(Runtime& rt) const {
	Heap& heap = rt.getHeap();
	return new(heap) RangeEnumerator(heap.managed(), 0, size());
}

const String* String::fromInt(Heap& heap, Int32 i) {
	Char buffer[32];
	return (i >= -QUICK_CONSTANTS_INTEGERS_RANGE && i <= QUICK_CONSTANTS_INTEGERS_RANGE)
		 	? &QUICK_CONSTANTS.integers[i + QUICK_CONSTANTS_INTEGERS_RANGE]
		 	: new(heap) String(heap.managed(), intToString(buffer, i), buffer + 32);
}

const String* String::fromDouble(Heap& heap, double d) {
	const String* s;
	Int32 i;
	if (d >= -2147483648.0 && d <= 2147483647.0 && (i = static_cast<Int32>(d)) == d) {
		s = fromInt(heap, i);
	} else if (isNaN(d)) {
		s = &NAN_STRING;
	} else if (d == std::numeric_limits<double>::infinity()) {
		s = &INFINITY_STRING;
	} else if (d == -std::numeric_limits<double>::infinity()) {
		s = &MINUS_INFINITY_STRING;
	} else {
		Char buffer[32];
		s = new(heap) String(heap.managed(), buffer, doubleToString(buffer, d));
	}
	return s;
}

std::string String::toUTF8String() const {
	std::string s;
	s.reserve(size());
	for (const Char* p = begin(); p != end(); ++p) {
		const Char c = *p;
		if (c < 0x80) {
			s.push_back(static_cast<char>(c));
		} else if (c < 0x800) {
			s.push_back(static_cast<char>(0xC0 | (c >> 6)));
			s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		} else if (c >= 0xD800 && c <= 0xDBFF && p + 1 != end() && *(p + 1) >= 0xDC00 && *(p + 1) <= 0xDFFF) {
			++p;
			const UInt32 codePoint = 0x10000
					+ ((static_cast<UInt32>(c) - 0xD800) << 10)
					+ (static_cast<UInt32>(*p) - 0xDC00);
			s.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
			s.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
			s.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
			s.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
		} else {
			s.push_back(static_cast<char>(0xE0 | (c >> 12)));
			s.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			s.push_back(static_cast<char>(0x80 | (c & 0x3F)));
		}
	}
	return s;
}

std::wstring String::toWideString() const {
	const Char* b = begin();
	const Char* e = end();
	if (sizeof (std::wstring::value_type) != 2) {
		assert(sizeof (std::wstring::value_type) == 4); 
		const Char* p = b;
		while (p != e && (*p < 0xD800 || *p > 0xDFFF)) {
			++p;
		}
		if (p != e) {
			std::wstring s;
			s.reserve(size());
			for (p = b; p != e; ++p) {
				UInt32 c = *p;
				if (c >= 0xD800 && c <= 0xDBFF && p + 1 != e && *(p + 1) >= 0xDC00 && *(p + 1) <= 0xDFFF) {
					++p;
					c = 0x10000 + ((static_cast<UInt32>(c) - 0xD800) << 10) + (static_cast<UInt32>(*p) - 0xDC00);
				}
				s.push_back(static_cast<std::wstring::value_type>(c));
			}
			return s;
		}
	}
	return std::wstring(b, e);
}

/* --- GCItem --- */

void* GCItem::operator new(size_t n, Heap& heap) {
	Heap** p = reinterpret_cast<Heap**>(::operator new(sizeof (Heap*) + n, &heap));
	*p = &heap;
	return p + 1;
}

GCItem::GCItem(const GCItem& copy) throw() : _gcList(0) {
	_gcPrev = _gcNext = this;
	if (copy._gcList != 0) {
		copy._gcList->claim(this);
	}
}

/* --- GCList --- */

void GCList::relinquish(GCItem* item) throw() {
	assert(item->_gcList == this);
	assert(count > 0);	// If this assert triggers it could mean that you've forgot to root a GCItem and it has been both garbage collected and destructed (e.g. by going out of scope).
	--count;
	item->_gcList = 0;
	item->_gcPrev->_gcNext = item->_gcNext;
	item->_gcNext->_gcPrev = item->_gcPrev;
}

void GCList::claim(GCItem* item) throw() {
	if (item->_gcList != 0) {
		item->_gcList->relinquish(item);
	}
	item->_gcPrev = this;
	item->_gcNext = _gcNext;
	_gcNext->_gcPrev = item;
	_gcNext = item;
	item->_gcList = this;
	++count;
}

void GCList::deleteAll() throw() {
	for (const GCItem* i = _gcNext->_gcNext; i != _gcNext; i = i->_gcNext) {
		assert(i->_gcPrev->_gcList == this);
		delete i->_gcPrev;
	}
	assert(_gcPrev == _gcNext && _gcPrev == this);
}

/* --- Heap --- */

Heap::Heap() : allocatedCount(0), allocatedSize(0), pooledSize(0), managedListA(*this), managedListB(*this)
		, rootList(*this), currentList(&managedListA), newList(&managedListB) {
	std::fill(pools + 0, pools + MAX_POOLED_SIZE / POOL_SIZE_GRANULARITY, (void*)(0));
}

int Heap::calcPoolIndex(const size_t size) {
	return (0 < size && size <= MAX_POOLED_SIZE - POOL_SIZE_GRANULARITY)
			? static_cast<int>((size + (POOL_SIZE_GRANULARITY - 1)) / POOL_SIZE_GRANULARITY)
			: -1;
}

void* Heap::acquireMemory(size_t size) {
	if (size >= MAX_SINGLE_ALLOCATION_SIZE) {
		throw ConstStringException("Memory allocation failure (size too large)");
	}
	void* ptr = ::operator new(size, std::nothrow);
	if (ptr == 0) {
		throw ConstStringException("Memory allocation failure");
	}
	++allocatedCount;
	allocatedSize += size;
	return ptr;
}

void Heap::releaseMemory(void* ptr, size_t size) {
	assert(allocatedCount >= 1);
	assert(allocatedSize >= size);
	::operator delete(ptr);
	--allocatedCount;
	allocatedSize -= size;
}

void* Heap::allocate(size_t size) {
	assert(size < ~(size_t)(0) - sizeof (size_t));
	assert(sizeof (size_t) >= sizeof (void*));	// we presume we can fit a pointer in the same memory as a size_t
	void* block = 0;
	const int poolIndex = calcPoolIndex(size);
	if (poolIndex >= 0) {
		size = poolIndex * POOL_SIZE_GRANULARITY;
		assert(0 <= poolIndex && poolIndex < MAX_POOLED_SIZE / POOL_SIZE_GRANULARITY);
		block = pools[poolIndex];
		if (block != 0) {
			pools[poolIndex] = *(static_cast<void**>(block));
			pooledSize -= size;
		}
	}
	if (block == 0) {
		block = acquireMemory(size + sizeof (size_t));
	}
	size_t* p = reinterpret_cast<size_t*>(block);
	*p = size;
	assert((std::fill_n(reinterpret_cast<Byte*>(p + 1), size, (Byte)0xC9), true)); // try to make things crash if we use uninitialized data
	return p + 1;
}

void Heap::free(void* ptr) {
	assert(ptr != 0);
	assert(sizeof (size_t) >= sizeof (void*));	// we presume we can fit a pointer in the same memory as a size_t
	size_t* p = reinterpret_cast<size_t*>(ptr) - 1;
	size_t size = *p;
	const int poolIndex = calcPoolIndex(size);
	if (poolIndex >= 0) {
		*(void**)(p) = pools[poolIndex];
		assert(0 <= poolIndex && poolIndex < MAX_POOLED_SIZE / POOL_SIZE_GRANULARITY);
		pools[poolIndex] = p;
		pooledSize += size;
		assert((std::fill_n(reinterpret_cast<Byte*>(ptr), size, (Byte)0xA9), true)); // try to make things crash if we use freed data
	} else {
		assert((std::fill_n(reinterpret_cast<Byte*>(p), size + sizeof (size_t), (Byte)0xA9), true)); // try to make things crash if we use freed data
		releaseMemory(p, size + sizeof (size_t));
	}
}

void Heap::gc() {
	for (const GCItem* item = rootList._gcNext; item != &rootList; item = item->_gcNext) {
		assert((item->_gcReferenceMarkingComplete = false, true));
		item->gcMarkReferences(*this);
		assert(item->_gcReferenceMarkingComplete);
	}
	for (const GCItem* item = newList->_gcPrev; item != newList; item = item->_gcPrev) {
		assert((item->_gcReferenceMarkingComplete = false, true));
		item->gcMarkReferences(*this);
		assert(item->_gcReferenceMarkingComplete);
	}
	std::swap(currentList, newList);
	newList->deleteAll();
}

void Heap::drain() {
	for (int poolIndex = 0; poolIndex < MAX_POOLED_SIZE / POOL_SIZE_GRANULARITY; ++poolIndex) {
		void* p = pools[poolIndex];
		while (p != 0) {
			void* n = *(void**)(p);
			releaseMemory(p, (poolIndex * POOL_SIZE_GRANULARITY + sizeof (size_t)));
			p = n;
		}
		pools[poolIndex] = 0;
	}
	pooledSize = 0;
}

Heap::~Heap() {
	managedListA.deleteAll();
	managedListB.deleteAll();
	drain();
	assert(allocatedCount == 0);
	assert(allocatedSize == 0);
}

/* --- Object --- */

static struct EmptyEnumerator : public Enumerator { const String* nextPropertyName() { return 0; } } EMPTY_ENUMERATOR;

Function* Object::asFunction() { return 0; }
JSArray* Object::asArray() { return 0; }
Error* Object::asError() { return 0; }
const String* Object::typeOfString() const { return &OBJECT_STRING; }
const String* Object::getClassName() const { return &O_BJECT_STRING; }
Object* Object::getPrototype(Runtime& rt) const { return rt.getObjectPrototype(); }
Value Object::getInternalValue(Heap&) const { return UNDEFINED_VALUE; }
Flags Object::getOwnProperty(Runtime&, const Value&, Value*) const { return NONEXISTENT; }
bool Object::setOwnProperty(Runtime&, const Value&, const Value&, Flags) { return false; }
bool Object::deleteOwnProperty(Runtime&, const Value&) { return false; }
Enumerator* Object::getOwnPropertyEnumerator(Runtime&) const { return &EMPTY_ENUMERATOR; }

bool Object::updateOwnProperty(Runtime& rt, const Value& key, const Value& v) {
	return (hasOwnProperty(rt, key) && ((void)(setOwnProperty(rt, key, v)), true));
}

bool Object::isOwnPropertyEnumerable(Runtime& rt, const Value& key) const {
	Value dummy;
	return ((getOwnProperty(rt, key, &dummy) & (DONT_ENUM_FLAG | EXISTS_FLAG)) == EXISTS_FLAG);
}

const String* Object::toString(Heap& heap) const {
	return String::concatenate(heap, String(heap.roots(), BRACKET_OBJECT_STRING, *getClassName()), END_BRACKET_STRING);
}

bool Object::hasOwnProperty(Runtime& rt, const Value& key) const {
	Value dummy;
	return (getOwnProperty(rt, key, &dummy) != NONEXISTENT);
}

bool Object::hasProperty(Runtime& rt, const Value& key) const {
	Value dummy;
	return (getProperty(rt, key, &dummy) != NONEXISTENT);
}

Flags Object::getProperty(Runtime& rt, const Value& key, Value* v) const {
	const Object* o = this;
	do {
		Flags flags = o->getOwnProperty(rt, key, v);
		if (flags != NONEXISTENT) {
			return flags;
		}
		o = o->getPrototype(rt);
	} while (o != 0);
	return NONEXISTENT;
}

bool Object::setProperty(Runtime& rt, const Value& key, const Value& v) {
	if (updateOwnProperty(rt, key, v)) {
		return true;
	}
	const Object* o = this;
	while ((o = o->getPrototype(rt)) != 0) {
		Value dummy;
		if ((o->getOwnProperty(rt, key, &dummy) & READ_ONLY_FLAG) != 0) {
			return false;
		}
	}
	return setOwnProperty(rt, key, v);
}

Enumerator* Object::getPropertyEnumerator(Runtime& rt) const {
	Heap& heap = rt.getHeap();
	Enumerator* enumerator = getOwnPropertyEnumerator(rt);
	const Object* o = getPrototype(rt);
	while (o != 0) {
		enumerator = new(heap) JoiningEnumerator(heap.managed(), rt, o, enumerator);
		o = o->getPrototype(rt);
	}
	return enumerator;
}

/* --- Table --- */

UInt32 Table::calcMaxLoad(UInt32 bucketCount) { return (bucketCount - (bucketCount >> 4) - 1); }
Table::Table(Heap* heap) : buckets(1U << TABLE_BUILT_IN_N, heap), loadCount(0) { }
UInt32 Table::getLoadCount() const { return loadCount; }
Table::Bucket* Table::getFirst() const { return getNext(buckets.begin() - 1); }
const Table::Bucket* Table::lookup(const String* key) const { return const_cast<Table*>(this)->lookup(key); }	// OK because lookup does not modify, only exposes non-const pointer

Table::Bucket* Table::getNext(Bucket* bucket) const {
	assert(bucket != 0);
	Bucket* e = buckets.end();
	while (++bucket != e && !bucket->valueExists()) { }
	return (bucket == e ? 0 : bucket);
}

Table::Bucket* Table::lookup(const String* key) {
	Bucket* bucket = find(key, key->calcHash());
	return (bucket->valueExists() ? bucket : 0);
}

Table::Bucket* Table::insert(const String* key) {
	UInt32 hash = key->calcHash();
	Bucket* bucket = find(key, hash);
	if (!bucket->keyExists()) {
		if (loadCount >= calcMaxLoad(buckets.size())) {
			rebuild(buckets.size() << 2);	// expand 4 times on overload
			assert(loadCount < calcMaxLoad(buckets.size()));
			bucket = find(key, hash);
			assert(!bucket->keyExists());
		}
		++loadCount;
		bucket->flags = 0;
		bucket->hash16 = static_cast<UInt16>(hash & 0xFFFF);
		bucket->key = key;
	}
	return bucket;
}

bool Table::update(Bucket* bucket, const Value& value, Flags flags) {
	assert(bucket->keyExists());
	if ((bucket->flags & READ_ONLY_FLAG) != 0) {
		return false;
	} else {
		bucket->flags |= EXISTS_FLAG | flags;
		bucket->type = static_cast<Byte>(value.type & 0xFF);
		bucket->var = value.var;
	}
	return true;
}

void Table::update(Bucket* bucket, const Int32 index) {
	assert(bucket->keyExists());
	bucket->flags = EXISTS_FLAG | INDEX_TYPE_FLAG;
	bucket->index = index;
}

bool Table::erase(Bucket* bucket) {
	assert(bucket->keyExists());
	if (bucket->valueExists()) {
		if ((bucket->flags & DONT_DELETE_FLAG) != 0) {
			return false;
		}
		bucket->flags = 0;
	}
	return true;
}

void Table::gcMarkReferences(Heap& heap) const {
	UInt32 rebuildLoadCount = 0;
	const UInt32 n = buckets.size();
	for (UInt32 i = 0; i < n; ++i) {
		const Bucket& bucket = buckets[i];
		if (bucket.keyExists()) {
			gcMark(heap, bucket.key);
			if (bucket.valueExists()) {
				switch (((bucket.flags & INDEX_TYPE_FLAG) != 0) ? Value::NUMBER_TYPE : bucket.type) {
					case Value::STRING_TYPE: gcMark(heap, bucket.var.string); break;
					case Value::OBJECT_TYPE: gcMark(heap, bucket.var.object); break;
					default: break;
				}
				++rebuildLoadCount;
			}
		}
	}
	assert(rebuildLoadCount <= loadCount);
	if (rebuildLoadCount != loadCount) {
		UInt32 check = const_cast<Table*>(this)->rebuild(rebuildLoadCount < calcMaxLoad(n >> 1) ? (n >> 1) : n);
		(void)check;
		assert(check == rebuildLoadCount);
	}
}

Table::Bucket* Table::find(const String* key, UInt32 hash) {
	UInt32 mask = (buckets.size() - 1);
	UInt32 i = hash & mask;
	UInt16 hash16 = static_cast<UInt16>(hash & 0xFFFF);
	Bucket* bucket;
	while ((bucket = &buckets[i])->keyExists()) {
		if (bucket->key == key || (bucket->hash16 == hash16 && bucket->key->isEqualTo(*key))) {
			if (i != (hash & mask)) {
				std::swap(buckets[i], buckets[(i - 1) & mask]);
				i = (i - 1) & mask;
			}
			break;
		}
		i = (i + 1) & mask;
	}
	return &buckets[i];
}

UInt32 Table::rebuild(UInt32 newSize) {
	Vector<Bucket, 1U << TABLE_BUILT_IN_N> newBuckets(newSize, buckets.getAssociatedHeap());
	UInt32 newLoadCount = 0;
	UInt32 mask = newSize - 1;
	for (UInt32 i = 0; i < buckets.size(); ++i) {
		const Bucket& bucket = buckets[i];
		if (bucket.valueExists()) {
			UInt32 j = bucket.key->calcHash() & mask;
			while (newBuckets[j].keyExists()) {
				j = (j + 1) & mask;
			}
			newBuckets[j] = bucket;
			++newLoadCount;
		}
	}
	buckets = newBuckets;
	loadCount = newLoadCount;
	return newLoadCount;
}

/* --- JSObject --- */

JSObject::JSObject(GCList& gcList, Object* prototype)
		: super(gcList), Table(&gcList.getHeap()), prototype(prototype) { }

Object* JSObject::getPrototype(Runtime&) const { return prototype; }

bool JSObject::setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags) {
	return update(insert(key.toString(rt.getHeap())), v, flags);
}

bool JSObject::updateOwnProperty(Runtime& rt, const Value& key, const Value& v) {
	Table::Bucket* bucket = lookup(key.toString(rt.getHeap()));
	return (bucket != 0 && update(bucket, v));
}

Flags JSObject::getOwnProperty(Runtime& rt, const Value& key, Value* v) const {
	const Table::Bucket* bucket = lookup(key.toString(rt.getHeap()));
	if (bucket != 0) {
		*v = bucket->getValue();
		return bucket->getFlags();
	}
	return NONEXISTENT;
}

bool JSObject::deleteOwnProperty(Runtime& rt, const Value& key) {
	Table::Bucket* bucket = lookup(key.toString(rt.getHeap()));
	return (bucket == 0 || erase(bucket));
}

Enumerator* JSObject::getOwnPropertyEnumerator(Runtime& rt) const {
	Heap& heap = rt.getHeap();
	StringListEnumerator* list = new(heap) StringListEnumerator(heap.managed(), getLoadCount());
	for (Table::Bucket* bucket = getFirst(); bucket != 0; bucket = getNext(bucket)) {
		if (bucket->doEnumerate()) {
			list->add(bucket->getKey());
		}
	}
	return list;
}

/* --- RangeEnumerator --- */

RangeEnumerator::RangeEnumerator(GCList& gcList, Int32 from, Int32 count) : super(gcList), heap(gcList.getHeap())
		, index(from), to(from + count) { }
const String* RangeEnumerator::nextPropertyName() { return (index >= to ? 0 : String::fromInt(heap, index++)); }

/* --- StringListEnumerator --- */

StringListEnumerator::StringListEnumerator(GCList& gcList, UInt32 initialCapacity)
		: super(gcList), stringList(&gcList.getHeap()), nextIndex(0) {
	stringList.reserve(initialCapacity);
}
void StringListEnumerator::add(const String* s) { stringList.push(s); }
const String* StringListEnumerator::nextPropertyName() { return (nextIndex < stringList.size() ? stringList[nextIndex++] : 0); }

/* --- JoiningEnumerator --- */

JoiningEnumerator::JoiningEnumerator(GCList& gcList, Runtime& rt, const Object* objectA, Enumerator* enumeratorB)
		: super(gcList), rt(rt), objectA(objectA), enumeratorA(objectA->getOwnPropertyEnumerator(rt))
		, enumeratorB(enumeratorB) {
}

const String* JoiningEnumerator::nextPropertyName() {
	const String* name = enumeratorA->nextPropertyName();
	if (name == 0) {
		do {
			name = enumeratorB->nextPropertyName();
			if (name != 0 && !objectA->isOwnPropertyEnumerable(rt, name)) {
				return name;
			}
		} while (name != 0);
	}
	return name;
}

static const Char LINE_TERMINATORS[] = { '\n', 0x2028, 0x2029, '\r' }; // FIX : '\r' must be last for line count to work, but I think we should change line counting algo in the future, a single '\r' without '\n' should count too really

static bool isLineTerminator(Char c) {
	return (std::find(LINE_TERMINATORS, LINE_TERMINATORS + 4, c) != LINE_TERMINATORS + 4);
}

static bool lineTerminatorInRange(const Char* b, const Char* e) {
	return (std::find_first_of(b, e, LINE_TERMINATORS, LINE_TERMINATORS + 4) != e);
}

/* --- SourceCodeUnit --- */

SourceCodeUnit::SourceCodeUnit(GCList& gcList, const String* sourceCode, const String* fileName)
	: super(gcList), source(sourceCode), fileName(fileName), lineOffsets(&gcList.getHeap())
{
	assert(sourceCode != 0);
	assert(fileName != 0);
	lineOffsets.push(0);
	const Char* const e = source->end();
	for (const Char* p = source->begin(); p != e; ++p) {
		if (isLineTerminator(*p)) {
			if (*p == '\r' && p + 1 != e && *(p + 1) == '\n') {
				++p;
			}
			lineOffsets.push(static_cast<UInt32>(p + 1 - source->begin()));
		}
	}
	lineOffsets.shrink();
}

void SourceCodeUnit::computeLineColumn(UInt32 offset, UInt32& line, UInt32& column) const {
	assert(offset <= source->size());
	const UInt32* begin = lineOffsets.begin();
	const UInt32* it = std::upper_bound(begin, const_cast<const UInt32*>(lineOffsets.end()), offset);
	assert(it != begin);
	line = static_cast<UInt32>(it - begin);
	column = static_cast<UInt32>(offset - *(it - 1) + 1);
}

/* --- Code --- */

Code::Code(GCList& gcList, Constants* sharedConstants, SourceCodeUnit* sourceUnit)
	: super(gcList), codeWords(0, &gcList.getHeap())
	, constants(sharedConstants ? sharedConstants : new(gcList.getHeap()) Constants(gcList.getHeap().managed()))
	, sourceUnit(sourceUnit), codeOffsets(&gcList.getHeap()), sourceOffsets(&gcList.getHeap())
	, nameIndexes(&gcList.getHeap()), varNames(&gcList.getHeap()), argumentNames(&gcList.getHeap()), name(0)
	, selfName(0), source(0), bloomSet(0), maxStackDepth(0)
{
	assert(constants != 0);
	assert(sourceUnit != 0);
}

bool Code::lookupNameIndex(const String* name, Int32& index) const {
	const Table::Bucket* bucket = nameIndexes.lookup(name);
	if (bucket == 0) {
		return false;
	}
	index = bucket->getIndexValue();
	return true;
}

Code::SourceLocation Code::lookupSourceLocation(const CodeWord* instructionPointer) const {
	assert(instructionPointer >= codeWords.begin() && instructionPointer < codeWords.end());
	const UInt32 instructionIndex = static_cast<UInt32>(instructionPointer - codeWords.begin());
	const UInt32* begin = codeOffsets.begin();
	const UInt32* it = std::upper_bound(begin, const_cast<const UInt32*>(codeOffsets.end()), instructionIndex);
	SourceLocation location;
	assert(it != begin);
	location.offset = sourceOffsets[it - 1 - begin];
	location.fileName = sourceUnit->getFileName();
	location.functionName = name;
	sourceUnit->computeLineColumn(location.offset, location.line, location.column);
	return location;
}

/* --- Function --- */

Function* Function::asFunction() { return this; }
const String* Function::typeOfString() const { return &FUNCTION_STRING; }
const String* Function::getClassName() const { return &F_UNCTION_STRING; }
const String* Function::toString(Heap&) const { return &NATIVE_FUNCTION_STRING; }
Value Function::getInternalValue(Heap&) const { return &NATIVE_FUNCTION_STRING; }
Object* Function::getPrototype(Runtime& rt) const { return rt.getPrototypeObject(Runtime::FUNCTION_PROTOTYPE); }

Value Function::construct(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
	return invoke(rt, processor, argc, argv, thisObject);
}

bool Function::hasInstance(Runtime& rt, Object* object) const {
	if (object == 0) {
		return false;
	}
	Value v(UNDEFINED_VALUE);
	getProperty(rt, &PROTOTYPE_STRING, &v);
	Object* prototypeObject = v.asObject();
	if (prototypeObject == 0) {
		ScriptException::throwError(rt.getHeap(), TYPE_ERROR, "Non-object prototype in instanceof check");
		return false;
	}
	while ((object = object->getPrototype(rt)) != 0) {
		if (object == prototypeObject) {
			return true;
		}
	}
	return false;
}

/* --- JSArray --- */

JSArray::JSArray(GCList& gcList) : super(gcList), length(0), denseVector(&gcList.getHeap()) { }
JSArray::JSArray(GCList& gcList, UInt32 initialLength)
		: super(gcList), length(initialLength), denseVector(initialLength, &gcList.getHeap()) {
	std::fill(denseVector.begin(), denseVector.end(), UNDEFINED_VALUE);
}
JSArray::JSArray(GCList& gcList, UInt32 initialLength, const Value* initialElements) : super(gcList)
		, length(initialLength), denseVector(initialElements, initialElements + initialLength, &gcList.getHeap()) { }

const String* JSArray::getClassName() const { return &A_RRAY_STRING; }
JSArray* JSArray::asArray() { return this; }

Object* JSArray::getPrototype(Runtime& rt) const { // FIX : have a sep. ArrayPrototype object like we have for Function?
	Object* arrayPrototype = rt.getPrototypeObject(Runtime::ARRAY_PROTOTYPE);
	return (this == arrayPrototype ? rt.getObjectPrototype() : arrayPrototype);
}

bool JSArray::updateLength(UInt32 newLength) {
	if (newLength < denseVector.size()) {
		denseVector.resize(newLength);
	}
	if (completeObject != 0) {
		for (Table::Bucket* bucket = completeObject->getFirst(); bucket != 0; bucket = completeObject->getNext(bucket)) {
			UInt32 i;
			if (Value(bucket->getKey()).toArrayIndex(i) && i >= newLength && !completeObject->erase(bucket)) {
				return false;
			}
		}
	}
	length = newLength;
	return true;
}

Value JSArray::getElement(Runtime& rt, UInt32 index) const {
	if (index < denseVector.size()) {
		return denseVector[index];
	}
	Value v = UNDEFINED_VALUE;
	super::getOwnProperty(rt, index, &v);
	return v;
}

Flags JSArray::getOwnProperty(Runtime& rt, const Value& key, Value* v) const {
	UInt32 index;
	if (key.toArrayIndex(index) && index < denseVector.size()) {
		*v = denseVector[index];
		return STANDARD_FLAGS;
	}
	if (key.equalsString(LENGTH_STRING)) {
		*v = length;
		return HIDDEN_CONST_FLAGS;
	}
	return super::getOwnProperty(rt, key, v);
}

void JSArray::sliceDenseVector(Runtime& rt, const Value& key) {
	UInt32 index;
	if (key.toArrayIndex(index) && index < denseVector.size()) {
		Table& props = *getCompleteObject(rt);
		Heap& heap = rt.getHeap();
		for (UInt32 moveIndex = index + 1; moveIndex < denseVector.size(); ++moveIndex) {
			props.update(props.insert(String::fromInt(heap, moveIndex)), denseVector[moveIndex]);
		}
		denseVector.resize(index);
	}
}

bool JSArray::setOwnPropertyInternal(Runtime& rt, const Value& key, const Value& v, Flags flags, bool& result) {
	result = false;
	UInt32 index;
	if (key.toArrayIndex(index)) {
		if ((flags & (READ_ONLY_FLAG | DONT_ENUM_FLAG | DONT_DELETE_FLAG)) == 0) {
			assert(flags == STANDARD_FLAGS);
			result = setElement(rt, index, v);
			return true;
		}
		sliceDenseVector(rt, key);
		return false;
	}
	if (key.equalsString(LENGTH_STRING)) {
		const double rawLength = v.toDouble();
		const UInt32 coercedLength = static_cast<UInt32>(rawLength);
		if (isNaN(rawLength) || rawLength < 0.0 || rawLength > 4294967295.0
				|| rawLength != static_cast<double>(coercedLength)) {
			ScriptException::throwError(rt.getHeap(), RANGE_ERROR, "Invalid array length");
		}
		result = updateLength(coercedLength);
		return true;
	}
	return false;
}

bool JSArray::setElement(Runtime& rt, UInt32 index, const Value& v) {
	if (index >= length) {
		length = index + 1;
	}
	if (index < denseVector.size()) {
		denseVector[index] = v;
		return true;
	}
	if (index == denseVector.size()) {
		if (completeObject == 0) {
			denseVector.push(v);
		} else {
			Heap& heap = rt.getHeap();
			Table::Bucket* bucket = completeObject->lookup(String::fromInt(heap, index));
			if (bucket == 0 || bucket->hasStandardFlags()) {
				UInt32 moveIndex = index + 1;
				denseVector.push(v);
				while ((bucket = completeObject->lookup(String::fromInt(heap, moveIndex))) != 0
						&& bucket->hasStandardFlags()) {
					denseVector.push(bucket->getValue());
					completeObject->erase(bucket);
					++moveIndex;
				}
			}
		}
		return true;
	}
	return super::setOwnProperty(rt, index, v, STANDARD_FLAGS);
}

bool JSArray::setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags) {
	bool result;
	return (setOwnPropertyInternal(rt, key, v, flags, result) ? result : super::setOwnProperty(rt, key, v, flags));
}

bool JSArray::updateOwnProperty(Runtime& rt, const Value& key, const Value& v) {
	bool result;
	return (setOwnPropertyInternal(rt, key, v, STANDARD_FLAGS, result) ? result : super::updateOwnProperty(rt, key, v));
}

bool JSArray::deleteOwnProperty(Runtime& rt, const Value& key) {
	return (key.equalsString(LENGTH_STRING) ? false : ((void)(sliceDenseVector(rt, key)), super::deleteOwnProperty(rt, key)));
}

void JSArray::constructCompleteObject(Runtime&) const { }

Enumerator* JSArray::getOwnPropertyEnumerator(Runtime& rt) const {
	Heap& heap = rt.getHeap();
	Enumerator* rangeEnumerator = new(heap) RangeEnumerator(heap.managed(), 0, denseVector.size());
	return (completeObject == 0 ? rangeEnumerator : new(heap) JoiningEnumerator(heap.managed(), rt, completeObject, rangeEnumerator));
}

void JSArray::pushElements(Runtime& rt, Int32 count, const Value* elements) {
	if (length == denseVector.size()) {
		denseVector.insert(denseVector.end(), elements, elements + count);
	} else {
		for (int i = 0; i < count; ++i) {
			super::setOwnProperty(rt, length + i, elements[i]);
		}
	}
	length += count;
}

/* --- LazyJSObject --- */

template<class SUPER> Flags LazyJSObject<SUPER>::getOwnProperty(Runtime& rt, const Value& key, Value* v) const {
	return getCompleteObject(rt)->getOwnProperty(rt, key, v);
}

template<class SUPER> bool LazyJSObject<SUPER>::setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags) {
	return getCompleteObject(rt)->setOwnProperty(rt, key, v, flags);
}

template<class SUPER> bool LazyJSObject<SUPER>::deleteOwnProperty(Runtime& rt, const Value& key) {
	return getCompleteObject(rt)->deleteOwnProperty(rt, key);
}

template<class SUPER> Enumerator* LazyJSObject<SUPER>::getOwnPropertyEnumerator(Runtime& rt) const {
	return getCompleteObject(rt)->getOwnPropertyEnumerator(rt);
}

template<class SUPER> JSObject* LazyJSObject<SUPER>::getCompleteObject(Runtime& rt) const {
	if (completeObject == 0) {
		Heap& heap = rt.getHeap();
		completeObject = new(heap) JSObject(heap.managed(), SUPER::getPrototype(rt));
		constructCompleteObject(rt);
	}
	return completeObject;
}

/* --- ExtensibleFunction --- */

void ExtensibleFunction::createPrototypeObject(Runtime& rt, Object* forObject, bool dontEnumPrototype) const {
	Heap& heap = rt.getHeap();
	JSObject* prototype = new(heap) JSObject(heap.managed(), rt.getObjectPrototype());
	prototype->setOwnProperty(rt, &CONSTRUCTOR_STRING, const_cast<ExtensibleFunction*>(this), DONT_ENUM_FLAG);
	forObject->setOwnProperty(rt, &PROTOTYPE_STRING, prototype, (dontEnumPrototype ? (DONT_ENUM_FLAG | DONT_DELETE_FLAG) : DONT_DELETE_FLAG));
}

/* --- JSFunction --- */

JSFunction::JSFunction(GCList& gcList, const Code* code, Scope* closure)
		: super(gcList), code(code), closure(closure) { }

Value JSFunction::getInternalValue(Heap& heap) const { return toString(heap); }

const String* JSFunction::toString(Heap& heap) const {
	const String* s = code->getSource();
	return (s != 0 ? s : Object::toString(heap));
}

Value JSFunction::invoke(Runtime&, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
	processor.enterFunctionCode(this, argc, argv, thisObject);
	return UNDEFINED_VALUE;
}

void JSFunction::constructCompleteObject(Runtime& rt) const {
	createPrototypeObject(rt, completeObject, false);
	completeObject->setOwnProperty(rt, &NAME_STRING, code->getName(), DONT_ENUM_FLAG);
	completeObject->setOwnProperty(rt, &LENGTH_STRING, code->getArgumentsCount(), HIDDEN_CONST_FLAGS);
}

/* --- Error --- */

Error::Error(GCList& gcList, ErrorType type, const String* message)
	: super(gcList), errorType(type), name(&ERROR_NAMES[errorType]), message(message), stack(0)
{
	assert(0 <= errorType && errorType < ERROR_TYPE_COUNT);
}

Value Error::getInternalValue(Heap&) const { return &ERROR_NAMES[errorType]; }
Object* Error::getPrototype(Runtime& rt) const { return rt.getErrorPrototype(errorType); }

const String* Error::toString(Heap& heap) const {
	return (message == 0 ? name : String::concatenate(heap, String(heap.roots(), *name, COLON_SPACE), *message));
}

void Error::updateReflection(Runtime& rt) {
	Value v;
	name = (getProperty(rt, &NAME_STRING, &v) != NONEXISTENT ? v.toString(rt.getHeap()) : &ERROR_NAMES[errorType]);
	message = (getProperty(rt, &MESSAGE_STRING, &v) != NONEXISTENT ? v.toString(rt.getHeap()) : 0);
	stack = (getProperty(rt, &STACK_STRING, &v) != NONEXISTENT ? v.toString(rt.getHeap()) : 0);
}

bool Error::setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags) {
	const bool result = super::setOwnProperty(rt, key, v, flags);
	updateReflection(rt);
	return result;
}

bool Error::deleteOwnProperty(Runtime& rt, const Value& key) {
	const bool result = super::deleteOwnProperty(rt, key);
	updateReflection(rt);
	return result;
}

void Error::constructCompleteObject(Runtime& rt) const {
	if (message != 0) {
		completeObject->setOwnProperty(rt, &MESSAGE_STRING, message, DONT_ENUM_FLAG);
	}
	if (stack != 0) {
		completeObject->setOwnProperty(rt, &STACK_STRING, stack, DONT_ENUM_FLAG);
	}
}

/* --- Arguments --- */

Arguments::Arguments(GCList& gcList, const FunctionScope* scope, UInt32 argumentsCount) : super(gcList)
	   , scope(scope), function(scope->function), argumentsCount(argumentsCount)
	   , deletedArguments(argumentsCount, &gcList.getHeap()), values(0, &gcList.getHeap()) {
	std::fill(deletedArguments.begin(), deletedArguments.end(), false);
}

const String* Arguments::getClassName() const { return &A_RGUMENTS_STRING; }

const String* Arguments::toString(Heap& heap) const {
	return new(heap) String(heap.managed(), String(heap.roots(), BRACKET_OBJECT_STRING, O_BJECT_STRING)
			, END_BRACKET_STRING);
}

Object* Arguments::getPrototype(Runtime& rt) const { return rt.getObjectPrototype(); }

void Arguments::detach() {
   if (scope != 0) {
	   values.resize(argumentsCount);
	   std::copy(scope->getLocalsPointer(), scope->getLocalsPointer() + argumentsCount, values.begin());
	   scope = 0;
   }
}

Value* Arguments::findProperty(const Value& key) const {
	UInt32 i;
	if (key.toArrayIndex(i) && i < argumentsCount && !deletedArguments[i]) {
		return (scope != 0 ? scope->getLocalsPointer() + i : const_cast<Value*>(&values[i]));
	}
	return 0;
}

Flags Arguments::getOwnProperty(Runtime& rt, const Value& key, Value* v) const {
	const Value* p = findProperty(key);
	return (p == 0 ? super::getOwnProperty(rt, key, v) : ((void)(*v = *p), (DONT_ENUM_FLAG | EXISTS_FLAG)));
}

bool Arguments::setOwnProperty(Runtime& rt, const Value& key, const Value& v, Flags flags) {
	Value* p = findProperty(key);
	if (p != 0 && (flags & (READ_ONLY_FLAG | DONT_ENUM_FLAG | DONT_DELETE_FLAG)) != 0) {
		const UInt32 index = static_cast<UInt32>(p - (scope != 0 ? scope->getLocalsPointer() : values.begin()));
		deletedArguments[index] = true;
		p = 0;
	}
	return (p == 0 ? super::setOwnProperty(rt, key, v, flags) : ((void)(*p = v), true));
}

bool Arguments::deleteOwnProperty(Runtime& rt, const Value& key) {
	const Value* p = findProperty(key);
	return (p == 0 ? super::deleteOwnProperty(rt, key)
			: (deletedArguments[p - (scope != 0 ? scope->getLocalsPointer() : values.begin())] = true));
}

Enumerator* Arguments::getOwnPropertyEnumerator(Runtime& rt) const {
	return (completeObject == 0 ? super::getOwnPropertyEnumerator(rt) : completeObject->getOwnPropertyEnumerator(rt));
}

void Arguments::constructCompleteObject(Runtime& rt) const {
	completeObject->setOwnProperty(rt, &LENGTH_STRING, argumentsCount, DONT_ENUM_FLAG);
	completeObject->setOwnProperty(rt, &CALLEE_STRING, function, DONT_ENUM_FLAG);
}

Arguments::~Arguments() {
	if (scope != 0) {
		scope->arguments = 0;
	}
}

/* --- Scope --- */

Scope::Scope(GCList& gcList, Scope* parentScope)
		: super(gcList), parentScope(parentScope), localsPointer(parentScope != 0 ? parentScope->localsPointer : 0)
		, deleteOnPop(true) { }

Flags Scope::readVar(Runtime& rt, const String* name, Value* v) const {
	assert(parentScope != 0);
	return parentScope->readVar(rt, name, v);
}

void Scope::writeVar(Runtime& rt, const String* name, const Value& v) {
	assert(parentScope != 0);
	return parentScope->writeVar(rt, name, v);
}

bool Scope::deleteVar(Runtime& rt, const String* name) {
	assert(parentScope != 0);
	return parentScope->deleteVar(rt, name);
}

void Scope::declareVar(Runtime& rt, const String* name, const Value& initValue, bool dontDelete) {
	assert(parentScope != 0);
	return parentScope->declareVar(rt, name, initValue, dontDelete);
}

void Scope::makeClosure() const {
	for (const Scope* s = this; s->deleteOnPop; s = s->parentScope) {
		s->deleteOnPop = false;
		assert(s->parentScope != 0);
	}
}

/* --- FunctionScope --- */

FunctionScope::FunctionScope(GCList& gcList, JSFunction* function, UInt32 argc, const Value* argv)
		: super(gcList, function->closure), function(function), passedArgumentsCount(argc)
		, locals(function->code->calcLocalsSize(argc), &gcList.getHeap()), dynamicVars(0)
		, arguments(0), bloomSet(function->code->bloomSet) {
	const Code* code = function->code;
	localsPointer = locals.begin() + code->getVarsCount();
	std::fill(locals.begin(), localsPointer, UNDEFINED_VALUE);
	Value* e = std::copy(argv, argv + argc, localsPointer);
	if (code->getArgumentsCount() > argc) {
		std::fill(e, locals.end(), UNDEFINED_VALUE);
	}
}

JSObject* FunctionScope::getDynamicVars(Runtime& rt) const {
	if (dynamicVars == 0) {
		Heap& heap = rt.getHeap();
		dynamicVars = new(heap) JSObject(heap.managed(), 0);
		arguments = new(heap) Arguments(heap.managed(), this, passedArgumentsCount);
		dynamicVars->setOwnProperty(rt, &ARGUMENTS_STRING, arguments, DONT_DELETE_FLAG);
	}
	return dynamicVars;
}

Flags FunctionScope::readVar(Runtime& rt, const String* name, Value* v) const {
	const UInt32 bloomCode = name->createBloomCode();
	if ((bloomSet & bloomCode) == bloomCode) {
		Int32 index;
		const Code* code = function->code;
		if (code->lookupNameIndex(name, index)) {
			assert(locals.begin() <= localsPointer + index && localsPointer + index < locals.end());
			*v = localsPointer[index];
			return DONT_DELETE_FLAG | EXISTS_FLAG;
		}
		if (dynamicVars != 0 || name->isEqualTo(ARGUMENTS_STRING)) {
			const Flags flags = getDynamicVars(rt)->getOwnProperty(rt, name, v);
			if (flags != NONEXISTENT) {
				return flags;
			}
		}
		if (code->selfName != 0 && name->isEqualTo(*code->selfName)) {
			*v = function;
			return DONT_DELETE_FLAG | READ_ONLY_FLAG | EXISTS_FLAG;
		}
	}
	return parentScope->readVar(rt, name, v);
}

void FunctionScope::writeVar(Runtime& rt, const String* name, const Value& v) {
	const UInt32 bloomCode = name->createBloomCode();
	if ((bloomSet & bloomCode) == bloomCode) {
		Int32 index;
		const Code* code = function->code;
		if (code->lookupNameIndex(name, index)) {
			assert(locals.begin() <= localsPointer + index && localsPointer + index < locals.end());
			localsPointer[index] = v;
			return;
		}
		// FIX : make sub that lookup's the bucket, also use in getProperty
		if (dynamicVars != 0 || name->isEqualTo(ARGUMENTS_STRING)) {
			Table& props = *getDynamicVars(rt);
			Table::Bucket* bucket = props.lookup(name);
			if (bucket != 0) {
				props.update(bucket, v);
				return;
			}
		}
		if (code->selfName != 0 && name->isEqualTo(*code->selfName)) {
			return;
		}
	}
	parentScope->writeVar(rt, name, v); // FIX : recursion
}

bool FunctionScope::deleteVar(Runtime& rt, const String* name) {
	const UInt32 bloomCode = name->createBloomCode();
	if ((bloomSet & bloomCode) == bloomCode) {
		Int32 index;
		const Code* code = function->code;
		if (code->lookupNameIndex(name, index)) {
			return false;
		}
		if (dynamicVars != 0 || name->isEqualTo(ARGUMENTS_STRING)) {
			Table& props = *getDynamicVars(rt);
			Table::Bucket* bucket = props.lookup(name);
			if (bucket != 0) {
				return props.erase(bucket);
			}
		}
		if (code->selfName != 0 && name->isEqualTo(*code->selfName)) {
			return false;
		}
	}
	return parentScope->deleteVar(rt, name); // FIX : recursion
}

void FunctionScope::declareVar(Runtime& rt, const String* name, const Value& initValue, bool dontDelete) {
	Int32 index;
	if (!function->code->lookupNameIndex(name, index)) {
		Table& props = *getDynamicVars(rt);
		Table::Bucket* bucket = props.insert(name);
		if (!bucket->valueExists()) {
			bloomSet |= name->createBloomCode();
			props.update(bucket, UNDEFINED_VALUE, dontDelete ? DONT_DELETE_FLAG : 0);
		}
	}
	if (!initValue.isUndefined()) {
		writeVar(rt, name, initValue);
	}
}

FunctionScope::~FunctionScope() {
	if (arguments != 0) {
		arguments->detach();
		arguments = 0;
	}
}

/* --- ScriptException --- */
	
void ScriptException::throwError(Heap& heap, ErrorType type, const String* message) {
	throw ScriptException(heap, new(heap) Error(heap.managed(), type, message));
}

void ScriptException::throwError(Heap& heap, ErrorType type, const char* message) {
	throwError(heap, type, String::allocate(heap, message));
}

ScriptException::ScriptException(Heap& heap, const Value& value) throw()
	: value(value), utf8String(value.toString(heap)->toUTF8String())
{
}

const char* ScriptException::getStackTrace() const {
	if (stackTrace.empty()) {
		Error* errorObject = value.asError();
		const String* stackString = (errorObject != 0 ? errorObject->getStackString() : 0);
		if (stackString != 0) {
			stackTrace = stackString->toUTF8String();
		}
	}
	return stackTrace.c_str();
}

/* --- EvalFunction --- */

static struct EvalFunction : public Function {
	typedef Function super;
	EvalFunction(bool direct) : direct(direct) { }
	virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object*) {
		if (argc < 1) {
			return UNDEFINED_VALUE;
		}
		if (!argv[0].isString()) {
			return argv[0];
		}

		Heap& heap = rt.getHeap();
		const String* expression = argv[0].toString(heap);
		processor.enterEvalCode(rt.compileEvalCode(expression), direct);
		return UNDEFINED_VALUE;
	}
	bool direct;
} EVAL_FUNCTION(false), DIRECT_EVAL_FUNCTION(true);

/* --- Processor --- */

const Int32 MIN_OPERAND_VALUE = -(1 << 23);
const Int32 MAX_OPERAND_VALUE = (1 << 23) - 1;

// This list must be in enum order
const Processor::OpcodeInfo Processor::opcodeInfo[Processor::OP_COUNT] = {
	{ CONST_OP                   , "CONST"                   , +1     , 0 },
	{ READ_LOCAL_OP              , "READ_LOCAL"              , +1     , 0 },
	{ WRITE_LOCAL_OP             , "WRITE_LOCAL"             , 0      , 0 },
	{ WRITE_LOCAL_POP_OP         , "WRITE_LOCAL_POP"         , -1     , 0 },
	{ READ_NAMED_OP              , "READ_NAMED"              , 1      , 0 },
	{ WRITE_NAMED_OP             , "WRITE_NAMED"             , 0      , 0 },
	{ WRITE_NAMED_POP_OP         , "WRITE_NAMED_POP"         , -1     , 0 },
	{ CHECK_OBJECT_COERCIBLE_OP	 , "CHECK_OBJECT_COERCIBLE"  , 0	  , 0 },
	{ CHECK_RESOLVE_PROPERTY_OP	 , "CHECK_RESOLVE_PROPERTY"	 , 0	  , 0 },
	{ GET_PROPERTY_OP            , "GET_PROPERTY"            , -1     , 0 },
	{ SET_PROPERTY_OP            , "SET_PROPERTY"            , -2     , 0 },
	{ SET_PROPERTY_POP_OP        , "SET_PROPERTY_POP"        , -3     , 0 },
	{ ADD_PROPERTY_OP            , "ADD_PROPERTY"            , -1     , 0 },
	{ PUSH_ELEMENTS_OP           , "PUSH_ELEMENTS_OP"        , 0      , OpcodeInfo::POP_OPERAND },
	{ OBJ_TO_PRIMITIVE_OP        , "OBJ_TO_PRIMITIVE"        , 0      , 0 },
	{ OBJ_TO_NUMBER_OP           , "OBJ_TO_NUMBER"           , 0      , 0 },
	{ OBJ_TO_STRING_OP           , "OBJ_TO_STRING"           , 0      , 0 },
	{ PRE_EQ_OP                  , "PRE_EQ"                  , 0      , 0 },
	{ INC_OP                     , "INC"                     , 0      , 0 },
	{ DEC_OP                     , "DEC"                     , 0      , 0 },
	{ ADD_OP                     , "ADD"                     , -1     , 0 },
	{ SUB_OP                     , "SUB"                     , -1     , 0 },
	{ MUL_OP                     , "MUL"                     , -1     , 0 },
	{ DIV_OP                     , "DIV"                     , -1     , 0 },
	{ MOD_OP                     , "MOD"                     , -1     , 0 },
	{ OR_OP                      , "OR"                      , -1     , 0 },
	{ XOR_OP                     , "XOR"                     , -1     , 0 },
	{ AND_OP                     , "AND"                     , -1     , 0 },
	{ SHL_OP                     , "SHL"                     , -1     , 0 },
	{ SHR_OP                     , "SHR"                     , -1     , 0 },
	{ USHR_OP                    , "USHR"                    , -1     , 0 },
	{ PLUS_OP                    , "PLUS"                    , 0      , 0 },
	{ MINUS_OP                   , "MINUS"                   , 0      , 0 },
	{ INV_OP                     , "INV"                     , 0      , 0 },
	{ NOT_OP                     , "NOT"                     , 0      , 0 },
	{ X_EQ_OP                    , "X_EQ"                    , -1     , 0 },
	{ X_NEQ_OP                   , "X_NEQ"                   , -1     , 0 },
	{ EQ_OP                      , "EQ"                      , -1     , 0 },
	{ NEQ_OP                     , "NEQ"                     , -1     , 0 },
	{ LT_OP                      , "LT"                      , -1     , 0 },
	{ LEQ_OP                     , "LEQ"                     , -1     , 0 },
	{ GT_OP                      , "GT"                      , -1     , 0 },
	{ GEQ_OP                     , "GEQ"                     , -1     , 0 },
	{ JMP_OP                     , "JMP"                     , 0      , OpcodeInfo::TERMINAL },
	{ JSR_OP                     , "JSR"                     , 0      , 0 },
	{ JT_OP                      , "JT"                      , -1     , 0 },
	{ JF_OP                      , "JF"                      , -1     , 0 },
	{ JT_OR_POP_OP               , "JT_OR_POP"               , -1     , OpcodeInfo::NO_POP_ON_BRANCH },
	{ JF_OR_POP_OP               , "JF_OR_POP"               , -1     , OpcodeInfo::NO_POP_ON_BRANCH },
	{ POP_OP                     , "POP"                     , 0      , OpcodeInfo::POP_OPERAND },
	{ PUSH_BACK_OP               , "PUSH_BACK"               , 0      , OpcodeInfo::POP_OPERAND },
	{ REPUSH_OP                  , "REPUSH"                  , 1      , 0 },
	{ SWAP_OP                    , "SWAP"                    , 0      , 0 },
	{ REPUSH_2_OP                , "REPUSH_2"                , +2     , 0 },
	{ POST_SHUFFLE_OP            , "POST_SHUFFLE"            , +1     , 0 },
	{ CALL_OP                    , "CALL"                    , 0      , OpcodeInfo::POP_OPERAND },
	{ CALL_METHOD_OP             , "CALL_METHOD"             , -1     , OpcodeInfo::POP_OPERAND },
	{ CALL_EVAL_OP               , "CALL_EVAL"               , 0      , OpcodeInfo::POP_OPERAND },
	{ NEW_OP                     , "NEW"                     , +1     , OpcodeInfo::POP_OPERAND },
	{ NEW_RESULT_OP              , "NEW_RESULT"              , -1     , 0 },
	{ NEW_OBJECT_OP              , "NEW_OBJECT"              , +1     , 0 },
	{ NEW_ARRAY_OP               , "NEW_ARRAY"               , +1     , 0 },
	{ NEW_REG_EXP_OP             , "NEW_REG_EXP"             , -1     , 0 },
	{ RETURN_OP                  , "RETURN"                  , -1     , OpcodeInfo::TERMINAL },
	{ THIS_OP                    , "THIS"                    , +1     , 0 },
	{ VOID_OP                    , "VOID"                    , +1     , 0 },
	{ DELETE_OP                  , "DELETE"                  , -1     , 0 },
	{ DELETE_NAMED_OP            , "DELETE_NAMED"            , 1      , 0 },
	{ GEN_FUNC_OP                , "GEN_FUNC"                , +1     , 0 },
	{ DECLARE_OP                 , "DECLARE"                 , -1     , 0 },
	{ CATCH_SCOPE_OP             , "CATCH_SCOPE"             , -1     , 0 },
	{ WITH_SCOPE_OP              , "WITH_SCOPE"              , -1     , 0 },
	{ POP_FRAME_OP               , "POP_FRAME"               , 0      , 0 },
	{ TRY_OP                     , "TRY"                     , 0      , OpcodeInfo::NO_POP_ON_BRANCH },
	{ TRIED_OP                   , "TRIED"                   , 0      , 0 },
	{ THROW_OP                   , "THROW"                   , -1     , OpcodeInfo::TERMINAL },
	{ IN_OP                      , "IN"                      , -1     , 0 },
	{ INSTANCE_OF_OP             , "INSTANCE_OF"             , -1     , 0 },
	{ TYPEOF_OP                  , "TYPEOF"                  , 0      , 0 },
	{ TYPEOF_NAMED_OP            , "TYPEOF_NAMED"            , 1      , 0 },
	{ GET_ENUMERATOR_OP          , "GET_ENUMERATOR"          , 0      , 0 },
	{ NEXT_PROPERTY_OP           , "NEXT_PROPERTY"           , 0      , OpcodeInfo::POP_ON_BRANCH }
};

const Processor::OpcodeInfo& Processor::getOpcodeInfo(const Opcode opcode) {
	assert(0 <= opcode && opcode < OP_COUNT);
	assert(opcodeInfo[opcode].opcode == opcode); // source code check
	return opcodeInfo[opcode];
}

/*
	This is the direct scope of an eval(), it only bridges all virtuals to the parent FunctionScope, but has a different
	code pointer (for constants etc) and it declares deletable vars.
*/
struct Processor::EvalScope : public Scope {
	typedef Scope super;
	EvalScope(GCList& gcList, Scope* parentScope) : super(gcList, parentScope) { }
	virtual void declareVar(Runtime& rt, const String* name, const Value& initValue, bool) {
		parentScope->declareVar(rt, name, initValue, false);
	}
};
	
/*
	The CatchScope is necessary because within a catch block there is a single extra variable that is local to the catch
	block. It is created dynamically and pushed on the call stack when entering a catch block and removed at any exit
	point. Remember you can access the catch variable via evals, otherwise we could have just placed it among the locals
	variables.
*/
struct Processor::CatchScope : public Scope {
	typedef Scope super;

	CatchScope(GCList& gcList, Scope* parentScope, const String* exceptionName, const Value& exceptionValue)
			: super(gcList, parentScope), exceptionName(exceptionName), exceptionValue(exceptionValue) { }
	virtual Flags readVar(Runtime& rt, const String* name, Value* v) const  {
		if (name->isEqualTo(*exceptionName)) {
			*v = exceptionValue;
			return DONT_DELETE_FLAG | EXISTS_FLAG;
		} else {
			return parentScope->readVar(rt, name, v);
		}
	}
	virtual void writeVar(Runtime& rt, const String* name, const Value& v) {
		if (name->isEqualTo(*exceptionName)) {
			exceptionValue = v;
		} else {
			parentScope->writeVar(rt, name, v);
		}
	}
	virtual bool deleteVar(Runtime& rt, const String* name) {
		if (name->isEqualTo(*exceptionName)) {
			return false;
		} else {
			return parentScope->deleteVar(rt, name);
		}
	}

	const String* exceptionName;
	Value exceptionValue;

	virtual void gcMarkReferences(Heap& heap) const {
		gcMark(heap, exceptionName);
		gcMark(heap, exceptionValue);
		super::gcMarkReferences(heap);
	}
};

struct Processor::WithScope : public Scope {
	typedef Scope super;
	WithScope(GCList& gcList, Scope* parentScope, Object* withObject)
	 		: super(gcList, parentScope), withObject(withObject) { }
	virtual Flags readVar(Runtime& rt, const String* name, Value* v) const {
		Flags flags = withObject->getProperty(rt, name, v);
		return (flags != NONEXISTENT ? flags : parentScope->readVar(rt, name, v));
	}
	virtual void writeVar(Runtime& rt, const String* name, const Value& v) {
		const Value key(name);
		for (Object* o = withObject; o != 0; o = o->getPrototype(rt)) {
			Value dummy;
			const Flags flags = o->getOwnProperty(rt, key, &dummy);
			if (flags != NONEXISTENT) {
				if ((flags & READ_ONLY_FLAG) == 0) {
					withObject->setOwnProperty(rt, key, v);
				}
				return;
			}
		}
		parentScope->writeVar(rt, name, v);
	}
	virtual bool deleteVar(Runtime& rt, const String* name) {
		Value dummy;
		Flags findFlags = withObject->getProperty(rt, name, &dummy);
		return (findFlags != NONEXISTENT ? withObject->deleteOwnProperty(rt, name) : parentScope->deleteVar(rt, name));
	}

	Object* withObject;
	virtual void gcMarkReferences(Heap& heap) const {
		gcMark(heap, withObject);
		super::gcMarkReferences(heap);
	}
};

Processor::Processor(Runtime& rt) : super(rt.heap.roots()), rt(rt), heap(rt.heap), currentFrame(0), firstCatcher(0)
		, stack(rt.stackSize, &heap) {
	stack[0] = UNDEFINED_VALUE;
	reset();
}

CodeWord Processor::packInstruction(const Opcode opcode, const Int32 operand) {
	assert(0 <= opcode && static_cast<int>(opcode) < 256);
	assert(MIN_OPERAND_VALUE < operand && operand < MAX_OPERAND_VALUE);
	return ((operand & 0xFFFFFF) << 8) | opcode;
}

std::pair<Processor::Opcode, Int32> Processor::unpackInstruction(const CodeWord codeWord) {
	return std::make_pair(static_cast<Opcode>(codeWord & 0xFF), codeWord >> 8);
}

void Processor::push(const Value& v) { assert(sp + 1 < stack.end()); *++sp = v; }
void Processor::pop2push1(const Value& v) { assert(sp >= stack.begin()); *--sp = v; }
void Processor::pop(Int32 count) { assert(count >= 0); assert(sp - count + 1 >= stack.begin()); sp -= count; }

void Processor::enter(const Code* code, Scope* scope, Object* thisObject) {
	// We could grow stack here *but* then existing pointers into the stack might become invalid and we would need to
	// copy arguments for native calls somewhere (e.g. to C++ stack).
	if (sp + code->getMaxStackDepth() > stack.end()) {
		ScriptException::throwError(heap, RANGE_ERROR, &STACK_OVERFLOW_STRING); // Notice: we can't use virtual throw here, cause we need to abort any sp changes etc that could happen if we continued execution beyond this point.
	} else {
		pushFrame(code, scope, (thisObject == 0 ? rt.getGlobalObject() : thisObject));
		ip = code->getCodeWords();
	}
}

void Processor::invokeFunction(Function* f, Int32 argc, const Value* argv, Object* thisObject) {
	sp[0] = f->invoke(rt, *this, argc, argv, thisObject);
}

void Processor::enterGlobalCode(const Code* code) {
	enter(code, rt.getGlobalScope(), rt.getGlobalObject());
}

void Processor::enterEvalCode(const Code* code, bool local) {
	if (local && currentFrame != 0) {
		enter(code, new(heap) Processor::EvalScope(heap.managed(), currentFrame->scope), currentFrame->thisObject);
	} else {
		enter(code, new(heap) Processor::EvalScope(heap.managed(), rt.getGlobalScope()), rt.getGlobalObject());
	}
}

void Processor::enterFunctionCode(JSFunction* func, UInt32 argc, const Value* argv, Object* thisObject) {
	enter(func->code, new(heap) FunctionScope(heap.managed(), func, argc, argv), thisObject);
}

void Processor::reset() {
	currentFrame = 0;
	firstCatcher = 0;
	ip = 0;
	sp = stack.begin();
}

void Processor::pushFrame(const Code* code, Scope* scope, Object* thisObject) {
	currentFrame = new(heap) Frame(heap.managed(), ip, code, scope, thisObject, currentFrame);
}

void Processor::popFrame() {
	assert(currentFrame != 0);
	Frame* killMe = currentFrame;
	currentFrame->scope->leave();
	currentFrame = currentFrame->previousFrame;
	delete killMe;
}

void Processor::popCatcher() {
	assert(firstCatcher != 0);
	Catcher* killMe = firstCatcher;
	firstCatcher = firstCatcher->nextCatcher;
	delete killMe;
}

static void appendString(Vector<Char>& buffer, const String* value) {
	buffer.insert(buffer.end(), value->begin(), value->end());
}

static void appendASCII(Vector<Char>& buffer, const char* literal) {
	while (*literal != 0) {
		buffer.push(static_cast<Char>(*literal));
		++literal;
	}
}

void Processor::addStackTrace(const Value& exception) const {
	Error* errorObject = exception.asError();
	if (errorObject != 0) {
		const String* stackString = errorObject->getStackString();
		if (stackString == 0) {
			Vector<Char> buffer(&heap);
			appendString(buffer, errorObject->getErrorName());
			const String* message = errorObject->getErrorMessage();
			if (message != 0 && !message->empty()) {
				appendASCII(buffer, ": ");
				appendString(buffer, message);
			}

			Char digits[32];
			const Frame* frameWalker = currentFrame;
			const CodeWord* nextIP = ip;
			while (frameWalker != 0 && nextIP != 0) {
				const Code::SourceLocation location = frameWalker->code->lookupSourceLocation(nextIP - 1);
				
				appendASCII(buffer, "\n    at ");
				if (location.functionName != 0 && !location.functionName->empty()) {
					appendString(buffer, location.functionName);
					appendASCII(buffer, " (");
				}
				appendString(buffer, location.fileName);
				if (location.line > 0) {
					buffer.push(static_cast<Char>(':'));
					const Char* digitsBegin = intToString(digits, location.line);
					buffer.insert(buffer.end(), digitsBegin, digits + 32);
					if (location.column > 0) {
						buffer.push(static_cast<Char>(':'));
						digitsBegin = intToString(digits, location.column);
						buffer.insert(buffer.end(), digitsBegin, digits + 32);
					}
				}
				if (location.functionName != 0 && !location.functionName->empty()) {
					buffer.push(static_cast<Char>(')'));
				}

				nextIP = frameWalker->returnIP;
				frameWalker = frameWalker->previousFrame;
			}

			stackString = new(heap) String(heap.managed(), buffer.begin(), buffer.end());
			errorObject->setOwnProperty(rt, Value(&STACK_STRING), Value(stackString), DONT_ENUM_FLAG | DONT_DELETE_FLAG);
		}
	}
}

void Processor::throwVirtualException(const Value& exception) {
	addStackTrace(exception);
	if (firstCatcher == 0) { // No JS catcher: throw a ScriptException
		reset();
		throw ScriptException(heap, exception);
	}
	ip = firstCatcher->ip;
	assert(ip != 0);
	currentFrame = firstCatcher->frame;
	sp = firstCatcher->sp;
	push(exception);
	popCatcher();
}

void Processor::error(ErrorType errorType, const String* message) {
	throwVirtualException(new(heap) Error(heap.managed(), errorType, message));
}

Object* Processor::convertToObject(const Value& v, const bool requireExtensible) {
	Object* const object = v.toObjectOrNull(heap, requireExtensible);
	if (object == 0) {
		error(TYPE_ERROR, &CANNOT_CONVERT_TO_OBJECT_STRING);
	}
	return object;
}

// FIX : should we continue this non-throwing approach or simply use toFunction everywhere? If keeping, we should share error creation with toFunction()
Function* Processor::asFunction(const Value& v) {
	Function* const f = v.asFunction();
	if (f == 0) {
		error(TYPE_ERROR, new(heap) String(heap.managed(), *v.toString(heap), IS_NOT_A_FUNCTION_STRING));
	}
	return f;
}

void Processor::invokeFunction(Function* f, Int32 popCount, Int32 argc, Object* thisObject) {
	sp[-popCount] = f->invoke(rt, *this, argc, sp - argc + 1, thisObject);
	pop(popCount);
}

void Processor::newOperation(const Int32 argc) {
	Function* f = asFunction(sp[-argc]);
	if (f != 0) { // FIX : sub
		Value v(UNDEFINED_VALUE);
		f->getProperty(rt, &PROTOTYPE_STRING, &v);
		Object* prototype = v.asObject();
		Int32 counter = 0;
		for (Object* p = prototype; p != 0; p = p->getPrototype(rt)) {
			if (++counter > MAX_PROTOTYPE_CHAIN_LENGTH) {
				error(RANGE_ERROR, &PROTOTYPE_CHAIN_TOO_LONG);
				return;
			}
		}
		Object* newObject = new(heap) JSObject(heap.managed(), prototype != 0 ? prototype : rt.getObjectPrototype());
		++sp;	// if we have 0 args we make room for the new object
		sp[-argc] = f->construct(rt, *this, argc, sp - argc, newObject);
		sp[-argc - 1] = newObject;
		pop(argc);
	}
}

void Processor::innerRun() {
	assert(currentFrame != 0);	// you must enter something first
	Scope* scope = currentFrame->scope;
	assert(scope != 0);
	const Code* code = currentFrame->code;
	assert(ip >= code->getCodeWords() && ip < code->getCodeWords() + code->getCodeSize());
	const Value* constants = currentFrame->code->getConstants()->begin();
	Value* locals = scope->getLocalsPointer();
	Object* thisObject = currentFrame->thisObject;

	while (--cyclesLeft >= 0) {
		const CodeWord instruction = *ip++;
		const Opcode opcode = static_cast<Opcode>(instruction & 0xFF);
		const Int32 im = instruction >> 8;
		switch (opcode) {
			case CONST_OP:				push(constants[im]); break;
			case READ_LOCAL_OP:			assert(locals != 0); push(locals[im]); break;
			case WRITE_LOCAL_OP:		assert(locals != 0); locals[im] = sp[0]; break;
			case WRITE_LOCAL_POP_OP:	assert(locals != 0); locals[im] = sp[0]; pop(1); break;
			case READ_NAMED_OP: {
				const String* name = constants[im].getString();
				if (scope->readVar(rt, name, ++sp) == NONEXISTENT) {
					error(REFERENCE_ERROR, new(heap) String(heap.managed(), *name, IS_NOT_DEFINED_STRING));
					return;
				}
				break;
			}
			case WRITE_NAMED_OP:		scope->writeVar(rt, constants[im].getString(), sp[0]); break;
			case WRITE_NAMED_POP_OP:	scope->writeVar(rt, constants[im].getString(), sp[0]); pop(1); break;

			case CHECK_OBJECT_COERCIBLE_OP: {
				if (sp[0].isUndefined() || sp[0].isNull()) {
					error(TYPE_ERROR, &CANNOT_CONVERT_TO_OBJECT_STRING);
					return;
				}
				break;
			}

			case CHECK_RESOLVE_PROPERTY_OP: {
				if (sp[-1].isUndefined() || sp[-1].isNull()) {
					error(TYPE_ERROR, &CANNOT_CONVERT_TO_OBJECT_STRING);
					return;
				}
				sp[-1] = convertToObject(sp[-1], false);
				break;
			}

			case GET_PROPERTY_OP: {
				const Object* o = convertToObject(sp[-1], false);
				if (o == 0) {
					return;
				}
				if (o->getProperty(rt, sp[0], sp - 1) == NONEXISTENT) {
					sp[-1] = UNDEFINED_VALUE;
				}
				pop(1);
				break;
			}
			
			case SET_PROPERTY_OP: {
				sp[-2].getObject()->setProperty(rt, sp[-1], sp[0]);
				sp[-2] = sp[0];
				pop(2);
				break;
			}
			
			case SET_PROPERTY_POP_OP: {
				sp[-2].getObject()->setProperty(rt, sp[-1], sp[0]);
				pop(3);
				break;
			}

			case OBJ_TO_PRIMITIVE_OP:
			case OBJ_TO_NUMBER_OP:
			case OBJ_TO_STRING_OP: {
				const Object* o = sp[0].asObject();
				if (o != 0) {
					assert(0 <= opcode - OBJ_TO_PRIMITIVE_OP && opcode - OBJ_TO_PRIMITIVE_OP < 3);
					invokeFunction(rt.toPrimitiveFunctions[opcode - OBJ_TO_PRIMITIVE_OP], 0, 1);
					return;
				}
				break;
			}
				
			case PRE_EQ_OP: {
				// FIX : doesn't feel totally efficient this...
				if (sp[-1].isObject() != sp[0].isObject()) {
					if (sp[-1].isObject()) {
						std::swap(sp[-1], sp[0]);
					}
					if (sp[-1].isString() || sp[-1].isNumber() || sp[-1].isBoolean()) {
						invokeFunction(rt.toPrimitiveFunctions[OBJ_TO_PRIMITIVE_OP - OBJ_TO_PRIMITIVE_OP], 0, 1);
						return;
					}
				}
				break;
			}

			case INC_OP:	sp[0] = sp[0].toDouble() + 1.0; break;
			case DEC_OP:	sp[0] = sp[0].toDouble() - 1.0; break;
			case ADD_OP:	pop2push1(sp[-1].add(heap, sp[0])); break;
			case SUB_OP:	pop2push1(sp[-1].toDouble() - sp[0].toDouble()); break;
			case MUL_OP:	pop2push1(sp[-1].toDouble() * sp[0].toDouble()); break;
			case DIV_OP:	pop2push1(sp[-1].toDouble() / sp[0].toDouble()); break;
			case MOD_OP:	pop2push1(modulo(sp[-1].toDouble(), sp[0].toDouble())); break;
			case OR_OP:		pop2push1(sp[-1].toInt() | sp[0].toInt()); break;
			case XOR_OP:	pop2push1(sp[-1].toInt() ^ sp[0].toInt()); break;
			case AND_OP:	pop2push1(sp[-1].toInt() & sp[0].toInt()); break;
			case SHL_OP:	pop2push1(wrapToInt32(static_cast<UInt32>(sp[-1].toInt()) << (sp[0].toInt() & 0x1F))); break;
			case SHR_OP:	pop2push1(sp[-1].toInt() >> (sp[0].toInt() & 0x1F)); break;
			case USHR_OP:	pop2push1(static_cast<UInt32>(sp[-1].toInt()) >> (sp[0].toInt() & 0x1F)); break;
			case PLUS_OP:	sp[0] = sp[0].toDouble(); break;
			case MINUS_OP:	sp[0] = -sp[0].toDouble(); break;
			case INV_OP:	sp[0] = ~sp[0].toInt(); break;
			case NOT_OP:	sp[0] = !sp[0].toBool(); break;
			case X_EQ_OP:	pop2push1(sp[-1].isStrictlyEqualTo(sp[0])); break;
			case X_NEQ_OP:	pop2push1(!sp[-1].isStrictlyEqualTo(sp[0])); break;
			case EQ_OP:		pop2push1(sp[-1].isEqualTo(sp[0])); break;
			case NEQ_OP:	pop2push1(!sp[-1].isEqualTo(sp[0])); break;
			case LT_OP:		pop2push1(sp[-1].isLessThan(sp[0])); break;
			case LEQ_OP:	pop2push1(sp[-1].isLessThanOrEqualTo(sp[0])); break;
			case GT_OP:		pop2push1(sp[0].isLessThan(sp[-1])); break;
			case GEQ_OP:	pop2push1(sp[0].isLessThanOrEqualTo(sp[-1])); break;
			case JMP_OP:	ip += im; break;

			case JSR_OP: {
				scope->makeClosure(); // FIX : only to prevent popping it since it is shared by two frames, feels wrong
				pushFrame(code, scope, thisObject);
				ip += im;
				return;
			}

			case JT_OP:				ip += (sp[0].toBool() ? im : 0); pop(1); break;
			case JF_OP:				ip += (!sp[0].toBool() ? im : 0); pop(1); break;
			case JT_OR_POP_OP:		if (sp[0].toBool()) ip += im; else pop(1); break;
			case JF_OR_POP_OP:		if (!sp[0].toBool()) ip += im; else pop(1); break;

			case POP_OP:			assert(im >= 0); pop(im); break;
			case PUSH_BACK_OP:		assert(im >= 0); sp[-im] = sp[0]; pop(im); break;
			case REPUSH_OP:			assert(im <= 0); push(sp[im]); break;
			case SWAP_OP:			std::swap(sp[-1], sp[0]); break;
			case REPUSH_2_OP:		push(sp[-1]); push(sp[-1]); break;
			case POST_SHUFFLE_OP:	push(sp[0]); sp[-1] = sp[-2]; sp[-2] = sp[-3]; sp[-3] = sp[0]; break;

			case CALL_OP: {
				Function* const f = asFunction(sp[-im]);
				if (f != 0) {
					invokeFunction(f, im, im);
				}
				return;
			}
			
			case CALL_METHOD_OP: {
				Object* const o = convertToObject(sp[-im - 1], true);
				if (o != 0) {
					Value v(UNDEFINED_VALUE);
					Function* f;
					const Value& name = sp[-im];
					if (o->getProperty(rt, name, &v) == NONEXISTENT || (f = v.asFunction()) == 0) {
						error(TYPE_ERROR, new(heap) String(heap.managed(), *name.toString(heap), IS_NOT_A_FUNCTION_STRING));
					} else {
						invokeFunction(f, im + 1, im, o);
					}
				}
				return;
			}
			
			case CALL_EVAL_OP: {
				Function* f = asFunction(sp[-im]);
				if (f != 0) {
					invokeFunction((f == rt.evalFunction ? &DIRECT_EVAL_FUNCTION : f), im, im);
				}
				return;
			}

			case NEW_OP: newOperation(im); return;
			case NEW_RESULT_OP: pop2push1(sp[0].isObject() ? sp[0] : sp[-1]); break;
			case NEW_OBJECT_OP: push(new(heap) JSObject(heap.managed(), rt.getObjectPrototype())); break;
			case NEW_ARRAY_OP: push(new(heap) JSArray(heap.managed())); break;
			case NEW_REG_EXP_OP: invokeFunction(rt.createRegExpFunction, 1, 2); return;
			case RETURN_OP:	ip = currentFrame->returnIP; popFrame(); return;
			case THIS_OP: push(thisObject); break;
			case VOID_OP: push(UNDEFINED_VALUE); break;
			
			case GEN_FUNC_OP: {
				const Object* o = constants[im].getObject();
				assert(dynamic_cast<const Code*>(o) != 0);
				scope->makeClosure();
				push(new(heap) JSFunction(heap.managed(), reinterpret_cast<const Code*>(o), scope));
				break;
			}
			
			case DECLARE_OP: scope->declareVar(rt, constants[im].getString(), sp[0], true); pop(1); break;
			
			case ADD_PROPERTY_OP: {
				Object* o = sp[-1].getObject();
				o->setOwnProperty(rt, constants[im], sp[0]);
				pop(1);
				break;
			}
			
			case PUSH_ELEMENTS_OP: {
				Object* o = sp[-im].getObject();
				assert(dynamic_cast<JSArray*>(o) != 0);
				reinterpret_cast<JSArray*>(o)->pushElements(rt, im, sp - im + 1);
				pop(im);
				break;
			}
			
			case DELETE_NAMED_OP: push(scope->deleteVar(rt, constants[im].getString())); break;
			
			case DELETE_OP: {
				Object* o = convertToObject(sp[-1], true);
				if (o == 0) {
					return;
				}
				pop2push1(o->deleteOwnProperty(rt, sp[0]));
				break;
			}
			
			case CATCH_SCOPE_OP: {
				pushFrame(code, new(heap) CatchScope(heap.managed(), scope, constants[im].getString(), sp[0]), thisObject);
				pop(1);
				return;
			}
			
			case WITH_SCOPE_OP: {
				Object* o = convertToObject(sp[0], false);
				if (o != 0) {
					pushFrame(code, new(heap) WithScope(heap.managed(), scope, o), thisObject);
					pop(1);
				}
				return;
			}
			
			case POP_FRAME_OP: popFrame(); return;
			case TRY_OP: firstCatcher = new(heap) Catcher(heap.managed(), ip + im, sp, currentFrame, firstCatcher); break;
			case TRIED_OP: popCatcher(); break;
			case THROW_OP: throwVirtualException(sp[0]); return;
			
			case IN_OP: {
				const Object* o = sp[0].asObject();
				if (o == 0) {
					error(TYPE_ERROR, new(heap) String(heap.managed(), CANNOT_USE_IN_OPERATOR_STRING, *sp[0].toString(heap)));
					return;
				}
				Value dummy;
				sp[-1] = (o->getProperty(rt, sp[-1], &dummy) != NONEXISTENT);
				pop(1);
				break;
			}
			case INSTANCE_OF_OP: {
				Function* f = asFunction(sp[0]);
				if (f == 0) {
					return;
				}
				sp[-1] = (f->hasInstance(rt, sp[-1].asObject()));
				pop(1);
				break;
			}
			case TYPEOF_OP: sp[0] = sp[0].typeOfString(); break;
			case TYPEOF_NAMED_OP: {
				Value v(UNDEFINED_VALUE);
				scope->readVar(rt, constants[im].getString(), &v);
				push(v.typeOfString());
				break;
			}
			
			case GET_ENUMERATOR_OP: {
				const Object* o = convertToObject(sp[0], false);
				if (o == 0) {
					return;
				}
				sp[0] = o->getPropertyEnumerator(rt);
				break;
			}
			
			case NEXT_PROPERTY_OP: {
				Object* o = sp[0].getObject();
				assert(dynamic_cast<Enumerator*>(o) != 0);
				const String* name = reinterpret_cast<Enumerator*>(o)->nextPropertyName();
 				if (name != 0) {
					sp[0] = name;
				} else {
					ip += im;
					pop(1);
				}
				break;
			}
			
			default: assert(0);
		}
	}
}

Value Processor::getResult() const {
	assert(ip == 0 && sp == stack.begin()); // make sure you've called run() until it returns false
	return sp[0];
}

bool Processor::run(Int32 maxCycles) {
	cyclesLeft = maxCycles;
	while (ip != 0 && cyclesLeft >= 0) {
		try {
			innerRun();
		}
		catch (const ScriptException& x) {
			throwVirtualException(x.value);
		}
	}
	return (ip != 0);
}

/* --- Compiler --- */

enum OperatorType {
	GROUP, UNARY, BINARY, IN_OPERATOR, ABSTRACT_EQUAL, ASSIGNMENT, COMPOUND_ASSIGNMENT, PRE_INC_DEC, POST_INC_DEC
	, LOGICAL_AND_OR, PROPERTY_BRACKETS, PROPERTY_DOT, CONDITIONAL, FUNCTION_CALL, COMMA, VOID_OPERATOR
	, DELETE_OPERATOR, NEW_OPERATOR, TYPE_OF
};
enum Associativity { LEFT_TO_RIGHT, RIGHT_TO_LEFT };

struct OperatorInfo {
	const char* token;
	const char* closeToken;
	Compiler::Precedence prec;
	Associativity assoc;
	OperatorType type;
	Processor::Opcode vmOp;
	bool primitiveInput;
	bool primitiveOutput;
};

const OperatorInfo PRE_OPS[] = {
	{ "(", ")", Compiler::GROUP_PREC, RIGHT_TO_LEFT, GROUP, Processor::INVALID_OP, false, false },
	{ "++", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, PRE_INC_DEC, Processor::INC_OP, true, true },
	{ "--", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, PRE_INC_DEC, Processor::DEC_OP, true, true },
	{ "+", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, UNARY, Processor::PLUS_OP, true, true },
	{ "-", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, UNARY, Processor::MINUS_OP, true, true },
	{ "~", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, UNARY, Processor::INV_OP, true, true },
	{ "!", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, UNARY, Processor::NOT_OP, false, true },
	{ "void", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, VOID_OPERATOR, Processor::VOID_OP, false, true },
	{ "delete", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, DELETE_OPERATOR, Processor::VOID_OP, false, true },
	{ "new", 0, Compiler::NEW_PREC, RIGHT_TO_LEFT, NEW_OPERATOR, Processor::VOID_OP, false, false },
	{ "typeof", 0, Compiler::PREFIX_PREC, RIGHT_TO_LEFT, TYPE_OF, Processor::TYPEOF_OP, false, true }
};

const OperatorInfo POST_OPS[] = {
	{ "===", 0, Compiler::EQUALITY_PREC, LEFT_TO_RIGHT, BINARY, Processor::X_EQ_OP, false, true },
	{ "==", 0, Compiler::EQUALITY_PREC, LEFT_TO_RIGHT, ABSTRACT_EQUAL, Processor::EQ_OP, false, true },
	{ "=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, ASSIGNMENT, Processor::INVALID_OP, false, false },
	{ ">>>=", 0, Compiler::SHIFT_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::USHR_OP, true, true },
	{ ">>=", 0, Compiler::SHIFT_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::SHR_OP, true, true },
	{ "<<=", 0, Compiler::SHIFT_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::SHL_OP, true, true },
	{ "+=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::ADD_OP, true, true },
	{ "-=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::SUB_OP, true, true },
	{ "*=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::MUL_OP, true, true },
	{ "/=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::DIV_OP, true, true },
	{ "%=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::MOD_OP, true, true },
	{ "&=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::AND_OP, true, true },
	{ "^=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::XOR_OP, true, true },
	{ "|=", 0, Compiler::ASSIGN_PREC, RIGHT_TO_LEFT, COMPOUND_ASSIGNMENT, Processor::OR_OP, true, true },
	{ ">>>", 0, Compiler::SHIFT_PREC, LEFT_TO_RIGHT, BINARY, Processor::USHR_OP, true, true },
	{ ">>", 0, Compiler::SHIFT_PREC, LEFT_TO_RIGHT, BINARY, Processor::SHR_OP, true, true },
	{ "<<", 0, Compiler::SHIFT_PREC, LEFT_TO_RIGHT, BINARY, Processor::SHL_OP, true, true },
	{ "++", 0, Compiler::POST_INC_DEC_PREC, LEFT_TO_RIGHT, POST_INC_DEC, Processor::INC_OP, true, true },
	{ "--", 0, Compiler::POST_INC_DEC_PREC, LEFT_TO_RIGHT, POST_INC_DEC, Processor::DEC_OP, true, true },
	{ "&&", 0, Compiler::LOGICAL_AND_PREC, RIGHT_TO_LEFT, LOGICAL_AND_OR, Processor::JF_OR_POP_OP, false, false },
	{ "||", 0, Compiler::LOGICAL_OR_PREC, RIGHT_TO_LEFT, LOGICAL_AND_OR, Processor::JT_OR_POP_OP, false, false },
	{ "+", 0, Compiler::ADD_SUB_PREC, LEFT_TO_RIGHT, BINARY, Processor::ADD_OP, true, true },
	{ "-", 0, Compiler::ADD_SUB_PREC, LEFT_TO_RIGHT, BINARY, Processor::SUB_OP, true, true },
	{ "*", 0, Compiler::MUL_DIV_PREC, LEFT_TO_RIGHT, BINARY, Processor::MUL_OP, true, true },
	{ "/", 0, Compiler::MUL_DIV_PREC, LEFT_TO_RIGHT, BINARY, Processor::DIV_OP, true, true },
	{ "%", 0, Compiler::MUL_DIV_PREC, LEFT_TO_RIGHT, BINARY, Processor::MOD_OP, true, true },
	{ "&", 0, Compiler::BIT_AND_PREC, LEFT_TO_RIGHT, BINARY, Processor::AND_OP, true, true },
	{ "^", 0, Compiler::BIT_XOR_PREC, LEFT_TO_RIGHT, BINARY, Processor::XOR_OP, true, true },
	{ "|", 0, Compiler::BIT_OR_PREC, LEFT_TO_RIGHT, BINARY, Processor::OR_OP, true, true },
	{ "!==", 0, Compiler::EQUALITY_PREC, LEFT_TO_RIGHT, BINARY, Processor::X_NEQ_OP, false, true },
	{ "!=", 0, Compiler::EQUALITY_PREC, LEFT_TO_RIGHT, ABSTRACT_EQUAL, Processor::NEQ_OP, false, true },
	{ "<=", 0, Compiler::RELATIONAL_PREC, LEFT_TO_RIGHT, BINARY, Processor::LEQ_OP, true, true },
	{ "<", 0, Compiler::RELATIONAL_PREC, LEFT_TO_RIGHT, BINARY, Processor::LT_OP, true, true },
	{ ">=", 0, Compiler::RELATIONAL_PREC, LEFT_TO_RIGHT, BINARY, Processor::GEQ_OP, true, true },
	{ ">", 0, Compiler::RELATIONAL_PREC, LEFT_TO_RIGHT, BINARY, Processor::GT_OP, true, true },
	{ ".", 0, Compiler::PROPERTY_PREC, LEFT_TO_RIGHT, PROPERTY_DOT, Processor::INVALID_OP, false, false },
	{ "[", "]", Compiler::PROPERTY_PREC, LEFT_TO_RIGHT, PROPERTY_BRACKETS, Processor::INVALID_OP, false, false },
	{ "?", ":", Compiler::CONDITIONAL_PREC, RIGHT_TO_LEFT, CONDITIONAL, Processor::INVALID_OP, false, false },
	{ "(", ")", Compiler::FUNCTION_CALL_PREC, LEFT_TO_RIGHT, FUNCTION_CALL, Processor::INVALID_OP, false, false },
	{ ",", 0, Compiler::COMMA_PREC, LEFT_TO_RIGHT, COMMA, Processor::INVALID_OP, false, false },
	{ "instanceof", 0, Compiler::RELATIONAL_PREC, LEFT_TO_RIGHT, BINARY, Processor::INSTANCE_OF_OP, false, true },
	{ "in", 0, Compiler::RELATIONAL_PREC, LEFT_TO_RIGHT, IN_OPERATOR, Processor::IN_OP, false, true }
};

enum Statement {
	VAR_STATEMENT, IF_STATEMENT, WHILE_STATEMENT, DO_WHILE_STATEMENT, FOR_STATEMENT, RETURN_STATEMENT
	, CONTINUE_STATEMENT, BREAK_STATEMENT, TRY_STATEMENT, THROW_STATEMENT, SWITCH_STATEMENT, FUNCTION_STATEMENT
	, WITH_STATEMENT
};
enum Literal { NULL_LITERAL, FALSE_LITERAL, TRUE_LITERAL, FUNCTION_LITERAL, THIS_LITERAL };

struct Compiler::ExpressionResult {
	enum Type {
		NONE				// void
		, PUSHED			// arbitrary value on stack
		, PUSHED_PRIMITIVE	// primitive value on stack
		, CONSTANT			// constant (and primitive) value on stack
		, LOCAL				// arbitrary value in local variable
		, NAMED				// arbitrary value in named variable
		, PROPERTY			// arbitrary value in object property
		, SAFEKEPT			// further up the value stack (value = position relative to safe-kept-counter)
	};
	ExpressionResult() : t(NONE), v(UNDEFINED_VALUE) { }
	ExpressionResult(Type type, const Value& value = UNDEFINED_VALUE) : t(type), v(value) { }
	Type t;
	Value v;
};
	
struct Compiler::SemanticScope {
	enum Type { ROOT_TYPE, LABEL_TYPE, ITERATOR_LABEL_TYPE, TRY_TYPE, CATCH_TYPE, FINALLY_TYPE, WITH_TYPE };
	
	SemanticScope(Heap& heap, Type type, Int32 stackDepthOnEntry, SemanticScope* next)
			: breaks(&heap), continues(&heap), finallys(&heap), type(type), stackDepthOnEntry(stackDepthOnEntry)
			, next(next) { }
	
	SemanticScope(Heap& heap, const String& label, Int32 stackDepthOnEntry, SemanticScope* next)
			: breaks(&heap), continues(&heap), finallys(&heap), type(LABEL_TYPE), label(label)
			, stackDepthOnEntry(stackDepthOnEntry), next(next) { }
	
	void makeIteratorScopes(SemanticScope* untilScope) {
		for (SemanticScope* s = this; s != untilScope; s = s->next) {
			s->type = ITERATOR_LABEL_TYPE;
		}
	}
	
	Int32 popStack(Compiler& compiler, Int32 currentDepth, Processor::Opcode popOpcode) {
		if (!compiler.currentSection->inDeadCode()) {
			const Int32 stackDistance = currentDepth - stackDepthOnEntry;
			if (stackDistance > 0) {
				compiler.emit(popOpcode, stackDistance);
				currentDepth -= stackDistance;
			}
		}
		return currentDepth;
	}
	
	Int32 rollback(Compiler& compiler, Int32 rollbackStackDepth, Processor::Opcode popOpcode) {
		switch (type) {
			case CATCH_TYPE:
			case WITH_TYPE:
			case FINALLY_TYPE: compiler.emit(Processor::POP_FRAME_OP); break;
			default: break;
		}
		switch (type) {
			case TRY_TYPE:
			case CATCH_TYPE:
			case ROOT_TYPE: rollbackStackDepth = popStack(compiler, rollbackStackDepth, popOpcode); break;
			default: break;
		}
		switch (type) {
			case TRY_TYPE:
			case CATCH_TYPE: finallys.push(compiler.emitForwardBranch(Processor::JSR_OP)); break;
			default: break;
		}
		return rollbackStackDepth;
	}
	
	Type type;
	const String label; 				// empty for automatic while, for, case labels
	SemanticScope* const next;
	Int32 stackDepthOnEntry;
	Vector<BranchPoint> breaks; 		// source points for break jmp's
	Vector<BranchPoint> continues; 		// source points for continue jmp's
	Vector<BranchPoint> finallys; 		// source points for finally jsr's
};

Compiler::Compiler(GCList& gcList, Code* code, Target compileFor, int initialNestCounter)
	: super(gcList), heap(gcList.getHeap()), code(code), compilingFor(compileFor), setupSection(heap, 1)
	, mainSection(heap, 1), b(0), p(0), e(0), sourceUnitBase(code->getSourceUnit()->getSource()->begin())
	, currentSection(0), acceptInOperator(true), withScopeCounter(0), nestCounter(initialNestCounter)
{
}

const String* Compiler::newHashedString(Heap& heap, const Char* b, const Char* e) {
	if (b == e) {
		return &EMPTY_STRING;
	}
	String* s = new(heap) String(heap.managed(), b, e);
	s->createBloomCode();
	return s;
}

void Compiler::error(ErrorType type, const char* message) { ScriptException::throwError(heap, type, message); }

void Compiler::CodeSection::pushSourceMapping(UInt32 offset) {
	if (sourceOffsets.empty() || sourceOffsets[sourceOffsets.size() - 1] != offset) {
		codeOffsets.push(static_cast<UInt32>(code.size()));
		sourceOffsets.push(offset);
	}
	assert(sourceOffsets.size() == codeOffsets.size());
}

UInt32 Compiler::CodeSection::popSourceMapping() {
	assert(!sourceOffsets.empty() && !codeOffsets.empty());
	const UInt32 popped = sourceOffsets[sourceOffsets.size() - 1];
	if (codeOffsets[codeOffsets.size() - 1] == static_cast<UInt32>(code.size())) {
		codeOffsets.pop();
		sourceOffsets.pop();
	}
	return popped;
}

void Compiler::CodeSection::exportSourceMapping(Vector<UInt32>& toCodeOffsets, Vector<UInt32>& toSourceOffsets
		, UInt32 codeBase) const {
	assert(codeOffsets.size() == sourceOffsets.size() && toCodeOffsets.size() == toSourceOffsets.size());
	if (!sourceOffsets.empty()) {
		const UInt32 start = (!toSourceOffsets.empty() && toSourceOffsets[toSourceOffsets.size() - 1] == sourceOffsets[0] ? 1 : 0);
		for (UInt32 i = start; i < codeOffsets.size(); ++i) {
			toCodeOffsets.push(codeOffsets[i] + codeBase);
			toSourceOffsets.push(sourceOffsets[i]);
		}
	}
}

void Compiler::CodeSection::emit(Processor::Opcode opcode, Int32 operand, UInt32 sourceOffset) {
	if (inDeadCode()) {	// unknown stack depth = dead code
		return;
	}
	const Processor::OpcodeInfo& opcodeInfo = Processor::getOpcodeInfo(opcode);
	stackDepth += opcodeInfo.stackUse + (((opcodeInfo.flags & Processor::OpcodeInfo::POP_OPERAND) != 0) ? -operand : 0);
	assert(stackDepth >= 0);
	maxStackDepth = std::max(maxStackDepth, stackDepth);		// Yeah, if we eliminate a (void/const/repush, pop) pair this might be one more than necessary, but it never seems to happen from emperical tests and it doesn't really matter in the first place.
	if ((opcodeInfo.flags & Processor::OpcodeInfo::TERMINAL) != 0) {
		stackDepth = DEAD_CODE_STACK_DEPTH;
	}
	if (opcode == Processor::POP_OP && operand == 1) {
		Processor::Opcode replacementOpcode = Processor::INVALID_OP;
		switch (lastEmitted) {
			case Processor::WRITE_LOCAL_OP: replacementOpcode = Processor::WRITE_LOCAL_POP_OP; break;
			case Processor::WRITE_NAMED_OP: replacementOpcode = Processor::WRITE_NAMED_POP_OP; break;
			case Processor::SET_PROPERTY_OP: replacementOpcode = Processor::SET_PROPERTY_POP_OP; break;
			case Processor::REPUSH_OP:
			case Processor::CONST_OP:
			case Processor::VOID_OP: {
				code.pop();
				popSourceMapping();
				lastEmitted = Processor::INVALID_OP;
				return;
			}
			default: break;
		}
		if (replacementOpcode != Processor::INVALID_OP) {
			const Int32 oldOperand = Processor::unpackInstruction(code.end()[-1]).second;
			code.pop();
			pushSourceMapping(popSourceMapping());
			code.push(Processor::packInstruction(replacementOpcode, oldOperand));
			lastEmitted = replacementOpcode;
			return;
		}
	}
	pushSourceMapping(sourceOffset);
	code.push(Processor::packInstruction(opcode, operand));
	lastEmitted = opcode;
}

void Compiler::CodeSection::insertSection(const CodeSection& section) {
	const Int32 stackAdjust = stackDepth - section.initialStackDepth;
	lastEmitted = Processor::INVALID_OP;
	section.exportSourceMapping(codeOffsets, sourceOffsets, static_cast<UInt32>(code.size()));
	code.insert(code.end(), section.code.begin(), section.code.end());
	stackDepth = section.stackDepth + stackAdjust;
	maxStackDepth = std::max(maxStackDepth, section.maxStackDepth + stackAdjust);
}

void Compiler::emit(Processor::Opcode opcode, Int32 operand) {
	if (operand < MIN_OPERAND_VALUE || operand > MAX_OPERAND_VALUE) { // Highly hypothetical, but correctness!
		error(RANGE_ERROR, "Internal compiler limitations reached. Reduce code complexity.");
	}
	currentSection->emit(opcode, operand, static_cast<UInt32>(p - sourceUnitBase));
}

Compiler::CodeSection* Compiler::changeSection(CodeSection* newOutputSection) {
	std::swap(newOutputSection, currentSection);
	return newOutputSection;
}

Compiler::BranchPoint Compiler::emitForwardBranch(Processor::Opcode opcode) {
// FIX : share code somehow
	const Processor::OpcodeInfo& opcodeInfo = Processor::getOpcodeInfo(opcode);
	const Int32 operand = 0;
	const Int32 targetStackDepth = (currentSection->inDeadCode() ? DEAD_CODE_STACK_DEPTH
			: currentSection->stackDepth
			+ opcodeInfo.stackUse + (((opcodeInfo.flags & Processor::OpcodeInfo::POP_OPERAND) != 0) ? -operand : 0)
			+ (((opcodeInfo.flags & Processor::OpcodeInfo::NO_POP_ON_BRANCH) != 0) ? 1 : 0)
			+ (((opcodeInfo.flags & Processor::OpcodeInfo::POP_ON_BRANCH) != 0) ? -1 : 0));
	emit(opcode);
	return BranchPoint(currentSection->code.size() - 1, targetStackDepth);
}

void Compiler::completeForwardBranch(const BranchPoint& point) {
	assert(currentSection->inDeadCode() || point.inDeadCode() || point.stackDepth == currentSection->stackDepth);
	if (!point.inDeadCode()) {
		currentSection->lastEmitted = Processor::INVALID_OP;
		currentSection->stackDepth = point.stackDepth;
		currentSection->maxStackDepth = std::max(currentSection->maxStackDepth, point.stackDepth);
		const std::pair<Processor::Opcode, Int32> instruction = Processor::unpackInstruction(currentSection->code[point.codeOffset]);
		currentSection->code[point.codeOffset] = Processor::packInstruction(instruction.first, currentSection->code.size() - point.codeOffset - 1);
	}
}

void Compiler::completeForwardBranches(const BranchPoint* begin, const BranchPoint* end) {
	for (const BranchPoint* it = begin; it != end; ++it) {
		completeForwardBranch(*it);
	}
}

Compiler::BranchPoint Compiler::markBackwardBranch() {
	currentSection->lastEmitted = Processor::INVALID_OP;
	return BranchPoint(currentSection->code.size(), currentSection->stackDepth);
}

void Compiler::emitBackwardBranch(Processor::Opcode opcode, const BranchPoint& point) {
	if (!currentSection->inDeadCode()) {
		assert(!point.inDeadCode());
		assert(0 <= point.codeOffset && static_cast<UInt32>(point.codeOffset) <= currentSection->code.size());
		emit(opcode, point.codeOffset - 1 - currentSection->code.size());
	}
}

UInt32 Compiler::addConstant(const Value& constant) {
	Constants* constants = code->constants;
	for (UInt32 index = constants->size(); index > 0;) {
		if ((*constants)[--index].isStrictlyEqualTo(constant)) {
			return index;
		}
	}
	constants->push(constant);
	return constants->size() - 1;
}

void Compiler::emitWithConstant(Processor::Opcode opcode, const Value& constant) {
	emit(opcode, addConstant(constant));
}

void Compiler::white() {
	while (!eof()) {
		switch (*p) {
			case ' ': case '\f': case '\n': case '\r': case '\t': case '\v': case 0xA0: case 0x2028: case 0x2029: ++p; break;
			case '/':	if (p + 1 != e && p[1] == '/') {
							p = std::find_first_of(p += 2, e, LINE_TERMINATORS, LINE_TERMINATORS + 4);
							break;
						} else if (p + 1 != e && p[1] == '*') {
							static const Char END_CHARS[] = { '*', '/' };
							p += 2;
							p = std::search(p, e, END_CHARS, END_CHARS + 2);
							if (eof()) {
								error(SYNTAX_ERROR, "Missing */");
							}
							p += 2;
							break;
						}
			default:	return;
		}
	}
}

bool Compiler::whiteNoLF() {
	const Char* b = p;
	white();
	if (lineTerminatorInRange(b, p)) {
		p = b;
		return false;
	}
	return true;
}

bool Compiler::token(const char* t, bool eatLeadingWhite) {
	assert(*t != 0);
	const Char* b = p;
	if (eatLeadingWhite) {
		white();
	}
	while (*t != 0 && !eof() && *p == *t) {
		++p;
		++t;
	}
	if (*t == 0) {
		const bool isPunctuator = (t[-1] < 'a' || t[-1] > 'z') && (t[-1] < 'A' || t[-1] > 'Z');
		if (isPunctuator || eof()
				|| (!(*p == '\\' && (e - p) >= 2 && p[1] == 'u') && !testUnicodeChar(*p, IDENTIFIER_PART_OFFSETS))) {
			return true;
		}
	}
	p = b;
	return false;
}

void Compiler::expectToken(const char* t, bool eatLeadingWhite) {
	if (!token(t, eatLeadingWhite)) {
		ScriptException::throwError(heap, SYNTAX_ERROR
				, String::concatenate(heap, *String::concatenate(heap, "Expected '", t), "'"));
	}
}

Vector<Char, 64> Compiler::parseIdentifier(bool limitLeadingChar) {
	Vector<Char, 64> parsed(0);
	const UInt16* uniOffsets = limitLeadingChar ? IDENTIFIER_START_OFFSETS : IDENTIFIER_PART_OFFSETS;
	while (!eof() && (testUnicodeChar(*p, uniOffsets) || (*p == '\\' && (e - p) >= 6 && p[1] == 'u'))) {
		Char c = *p;
		++p;
		if (c == '\\') {
			assert(e - p >= 5 && *p == 'u');
			UInt32 i;
			if (parseHex(p + 1, p + 5, i) != p + 5) {
				error(SYNTAX_ERROR, "Invalid escape sequence");
			}
			c = static_cast<Char>(i);
			if (!testUnicodeChar(c, uniOffsets)) {
				error(SYNTAX_ERROR, "Illegal Unicode in identifier");
			}
			p += 5;
		}
		parsed.push(c);
		uniOffsets = IDENTIFIER_PART_OFFSETS;
	}
	return parsed;
}

const String* Compiler::identifier(bool required, bool allowKeywords) {
	Vector<Char, 64> parsed = parseIdentifier(true);
	if (parsed.size() == 0 && required) {
		error(SYNTAX_ERROR, "Expected identifier");
	}
	if (!allowKeywords && findReservedKeyword(parsed.size(), parsed.begin()) >= 0) {
		error(SYNTAX_ERROR, "Illegal use of keyword");
	}
	return newHashedString(heap, parsed.begin(), parsed.end());
}

static UInt32 unescapedMaxLength(const Char* p, const Char* e) {
	assert(p != e && (*p == '"' || *p == '\''));
	Char endChar = *p;
	UInt32 l = 0;
	while (++p != e && *p != endChar && !isLineTerminator(*p)) {
		if (*p == '\\' && p + 1 != e) {
			++p;
		}
		++l;
	}
	return l;
}

static const Char ESCAPE_CHARS[] = { '\\', '\"', '\'', 'b',  'f',  'n',  'r',  't',  'v', '0' };
static const Char ESCAPE_CODES[] = { '\\', '\"', '\'', '\b', '\f', '\n', '\r', '\t', '\v', '\0' };
const int ESCAPE_CODE_COUNT = sizeof (ESCAPE_CHARS) / sizeof (*ESCAPE_CHARS);

Char* Compiler::unescape(Char* buffer, const Char* e) {	
	assert(!eof() && (*p == '"' || *p == '\''));
	Char endChar = *p;
	++p;
	const Char* b = p;
	Char* d = buffer;
	while (!eof() && *p != endChar && !isLineTerminator(*p)) {
		if (*p == '\\' && p + 1 != e) {
			UInt32 l = 0;
			d = std::copy(b, p, d);
			++p;
			const Char* f = std::find(ESCAPE_CHARS, ESCAPE_CHARS + ESCAPE_CODE_COUNT, *p);
			if (f != ESCAPE_CHARS + ESCAPE_CODE_COUNT) {
				l = ESCAPE_CODES[f - ESCAPE_CHARS];
			} else if (*p == 'x' || *p == 'u') {
				const int n = (*p == 'x' ? 2 : 4);
				if (e - (p + 1) < n || parseHex(p + 1, p + 1 + n, l) != p + 1 + n) {
					error(SYNTAX_ERROR, "Invalid escape sequence");
				}
				p += n;
			} else if (isLineTerminator(*p)) {
				error(SYNTAX_ERROR, "\\ continuation is not supported");
			} else {
				l = *p;
			}
			b = ++p;
			*d++ = Char(l);
		} else {
			++p;
		}
	}
	if (eof() || *p != endChar) {
		error(SYNTAX_ERROR, "Unterminated string");
	}
	d = std::copy(b, p, d);
	++p;
	return d;
}

Compiler::ExpressionResult Compiler::makeRValue(const ExpressionResult& xr, bool toPrimitive, Processor::Opcode toPrimitiveOp) {
	switch (xr.t) {
		case ExpressionResult::PUSHED: break;
		case ExpressionResult::PUSHED_PRIMITIVE: return ExpressionResult(ExpressionResult::PUSHED_PRIMITIVE);
		case ExpressionResult::NONE: emit(Processor::VOID_OP); return ExpressionResult(ExpressionResult::PUSHED_PRIMITIVE);
		case ExpressionResult::CONSTANT: emitWithConstant(Processor::CONST_OP, xr.v); return ExpressionResult(ExpressionResult::PUSHED_PRIMITIVE);
		case ExpressionResult::LOCAL: emit(Processor::READ_LOCAL_OP, xr.v.toInt()); break;
		case ExpressionResult::NAMED: emitWithConstant(Processor::READ_NAMED_OP, xr.v); break;
		case ExpressionResult::PROPERTY: emit(Processor::GET_PROPERTY_OP); break;
		case ExpressionResult::SAFEKEPT: emit(Processor::REPUSH_OP
				, (currentSection->inDeadCode() ? 0 : xr.v.toInt() - currentSection->stackDepth + 1)); break;
		default: assert(0);
	}
	if (toPrimitive) {
		emit(toPrimitiveOp);
		return ExpressionResult(ExpressionResult::PUSHED_PRIMITIVE);
	} else {
		return ExpressionResult(ExpressionResult::PUSHED);
	}
}

Processor::Opcode Compiler::evalPopOpcode() const {
	return compilingFor == FOR_EVAL ? Processor::PUSH_BACK_OP : Processor::POP_OP;
}

Compiler::ExpressionResult Compiler::safeKeep() {
	Int32 stackOffset = 1;
	if (compilingFor == FOR_EVAL) {
		emit(Processor::SWAP_OP);				// evaluation result should be up front always
		stackOffset = 2;
	}
	assert(currentSection->inDeadCode() || currentSection->stackDepth >= stackOffset);
	return ExpressionResult(ExpressionResult::SAFEKEPT
			, (currentSection->inDeadCode() ? 0 : currentSection->stackDepth - stackOffset));
}

void Compiler::returnSafeKept(const ExpressionResult& xr) {
	(void)xr;
	assert(xr.t == ExpressionResult::SAFEKEPT);
	assert(currentSection->inDeadCode() || xr.v.toInt() == currentSection->stackDepth - (compilingFor == FOR_EVAL ? 2 : 1));
	emit(evalPopOpcode(), 1);
}

Compiler::ExpressionResult Compiler::makeAssignment(const ExpressionResult& xr) {
	switch (xr.t) {
		default: error(REFERENCE_ERROR, "Illegal l-value"); // FIX : ECMA like the word "reference"
		case ExpressionResult::LOCAL: emit(Processor::WRITE_LOCAL_OP, xr.v.toInt()); break;
		case ExpressionResult::NAMED: emitWithConstant(Processor::WRITE_NAMED_OP, xr.v); break;
		case ExpressionResult::PROPERTY: emit(Processor::SET_PROPERTY_OP); break;
	}
	return ExpressionResult(ExpressionResult::PUSHED);
}

Compiler::ExpressionResult Compiler::discard(const ExpressionResult& xr) {
	switch (xr.t) {
		case ExpressionResult::PUSHED:
		case ExpressionResult::PUSHED_PRIMITIVE: emit(Processor::POP_OP, 1); break;
		case ExpressionResult::NAMED: emitWithConstant(Processor::READ_NAMED_OP, xr.v); emit(Processor::POP_OP, 1); break;
		case ExpressionResult::PROPERTY: emit(Processor::GET_PROPERTY_OP); emit(Processor::POP_OP, 1); break;
		default: break;
	}
	return ExpressionResult(ExpressionResult::NONE);
}

Compiler::ExpressionResult Compiler::operand(const OperatorInfo& op) {
	ExpressionResult rr(expression(op.closeToken != 0 ? LOWEST_PREC : op.prec));
	if (op.closeToken != 0) {
		expectToken(op.closeToken, true);
	}
	return rr;
}

Compiler::ExpressionResult Compiler::functionCall(Processor::Opcode opcode) {
	const bool didAcceptInOperator = acceptInOperator;
	acceptInOperator = true;
	Int32 count = 0;
	white();
	while (!token(")", false)) {
		if (eof()) {
			error(SYNTAX_ERROR, count == 0 ? "Expected ')'" : "Expected ',' or ')'");
		}
		if (count != 0) {
			if (!token(",", false)) {
				error(SYNTAX_ERROR, "Expected ',' or ')'");
			}
			white();
		}
		rvalueExpression(COMMA_PREC);
		white();
		++count;
	}
	acceptInOperator = didAcceptInOperator;
	emit(opcode, count);
	return ExpressionResult(ExpressionResult::PUSHED);
}

Value Compiler::stringOrNumberConstant() {
	if (!eof()) {
		switch (*p) {
			case '0': {
				if (p + 1 == e || (p[1] != '.' && !testUnicodeChar(p[1], IDENTIFIER_START_OFFSETS))) {
					++p;
					return 0;
				} else if (p[1] == 'x' || p[1] == 'X') {
					UInt32 u;
					const Char* q = parseHex(p + 2, e, u);
					if (q != p + 2) {
						p = q;
						return u;
					}
					break;
				} else if (p[1] != '.' && p[1] != 'e' && p[1] != 'E') {
					break;
				}
				/* fall through */
			}
			case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': case '.': {
				double d;
				const Char* q = parseDouble(p, e, d);
				if (q != p && (q == e || !testUnicodeChar(*q, IDENTIFIER_START_OFFSETS))) {
					p = q;
					return d;
				}
				break;
			}
			case '\'': case '"': {
				UInt32 maxLength = unescapedMaxLength(p, e);
				Vector<Char, 64> buffer(maxLength, &heap);
				Char* unescapeEnd = unescape(buffer, e);
				assert(unescapeEnd <= buffer + maxLength);
				return newHashedString(heap, buffer, unescapeEnd);
			}
		}
	}
	return Value();
}

Compiler::ExpressionResult Compiler::arrayInitialiser() { // FIX : share stuff with functionCall and arrayInitialiser
	emit(Processor::NEW_ARRAY_OP);
	
	Int32 index = 0;
	Int32 pushedCount = 0;
	Int32 currentLength = 0;
	while (!token("]", true)) {
		++index;
		if (token(",", true)) {
			if (pushedCount > 0) {
				emit(Processor::PUSH_ELEMENTS_OP, pushedCount);
				pushedCount = 0;
			}
		} else {
			if (index != currentLength + 1) {
				assert(pushedCount == 0);
				emitWithConstant(Processor::CONST_OP, index - 1);
				emitWithConstant(Processor::ADD_PROPERTY_OP, &LENGTH_STRING);
			}
			rvalueExpression(COMMA_PREC);
			++pushedCount;
			if (pushedCount >= 16) {
				emit(Processor::PUSH_ELEMENTS_OP, pushedCount);
				pushedCount = 0;
			}
			currentLength = index;
			if (token("]", true)) {
				break;
			}
			if (!token(",", true)) {
				error(SYNTAX_ERROR, "Expected ',' or ']'");
			}
		}
	}
	if (pushedCount > 0) {
		emit(Processor::PUSH_ELEMENTS_OP, pushedCount);
	} else if (index != currentLength) {
		emitWithConstant(Processor::CONST_OP, index);
		emitWithConstant(Processor::ADD_PROPERTY_OP, &LENGTH_STRING);
	}
	return ExpressionResult(ExpressionResult::PUSHED);
}

Compiler::ExpressionResult Compiler::objectInitialiser() { // FIX : share stuff with functionCall and arrayInitialiser
	emit(Processor::NEW_OBJECT_OP);
	
	white();
	while (!token("}", false)) {
		const Char* b = p;
		Value key = stringOrNumberConstant();
		if (p == b) {
			key = identifier(false, true);
			if (key.equalsString(EMPTY_STRING)) {
				error(SYNTAX_ERROR, "Expected property name");
			}
		}
		expectToken(":", true);
		rvalueExpression(COMMA_PREC);
		emitWithConstant(Processor::ADD_PROPERTY_OP, key);
		if (token("}", true)) {
			break;
		}
		if (!token(",", true)) {
			error(SYNTAX_ERROR, "Expected ',' or '}'");
		}
		white();
	}
	return ExpressionResult(ExpressionResult::PUSHED);
}

int Compiler::parseOperator(Precedence precedence, int opCount, const OperatorInfo* opList) {
	white();
	if (eof()) {
		return -1;
	}
	int i = 0;
	while (i < opCount && !token(opList[i].token, false)) {
		++i;
	}
	if (i >= opCount) {
		return -1;
	}
	const OperatorInfo& op = opList[i];
	if (precedence < op.prec + (op.assoc == RIGHT_TO_LEFT ? 0 : 1)) {
		return -1;
	}
	return i;
}

bool Compiler::preOperate(ExpressionResult& xr, Precedence precedence) {
	const int opIndex = parseOperator(precedence, sizeof (PRE_OPS) / sizeof (*PRE_OPS), PRE_OPS);
	if (opIndex < 0) {
		return false;
	}
	xr = discard(xr);
	const OperatorInfo& op = PRE_OPS[opIndex];
	switch (op.type) {
		case VOID_OPERATOR: {
			xr = discard(operand(op));
			break;
		}
		
		case GROUP: {
			const bool didAcceptInOperator = acceptInOperator;
			acceptInOperator = true;
			xr = operand(op);
			acceptInOperator = didAcceptInOperator;
			break;
		}
		
		case UNARY: {
			makeRValue(operand(op), op.primitiveInput);
			emit(op.vmOp);
			xr = (op.primitiveOutput ? ExpressionResult::PUSHED_PRIMITIVE : ExpressionResult::PUSHED);
			break;
		}

		case NEW_OPERATOR: {
			makeRValue(operand(op), false);
			if (!token("(", true)) {
				emit(Processor::NEW_OP, 0);
			} else {
				white();
				functionCall(Processor::NEW_OP);
			}
			emit(Processor::NEW_RESULT_OP); // FIX : make a chain for script constructors and require returning object for native constructors instead?
			assert(!op.primitiveOutput);
			xr = ExpressionResult::PUSHED;
			break;
		}
		
		case DELETE_OPERATOR: {
			assert(!op.primitiveInput);
			assert(op.primitiveOutput);
			xr = operand(op);
			switch (xr.t) {
				case ExpressionResult::PUSHED:
				case ExpressionResult::PUSHED_PRIMITIVE: emit(Processor::POP_OP, 1); /* fall through */
				case ExpressionResult::NONE:
				case ExpressionResult::CONSTANT: xr = ExpressionResult(ExpressionResult::CONSTANT, true); break;
				case ExpressionResult::LOCAL: xr = ExpressionResult(ExpressionResult::CONSTANT, false); break;
				case ExpressionResult::NAMED: emitWithConstant(Processor::DELETE_NAMED_OP, xr.v); xr = ExpressionResult(ExpressionResult::PUSHED_PRIMITIVE); break;
				case ExpressionResult::PROPERTY: emit(Processor::DELETE_OP); xr = ExpressionResult(ExpressionResult::PUSHED_PRIMITIVE); break;
				default: assert(0);
			}
			break;
		}
		
		case PRE_INC_DEC: {
			assert(op.primitiveInput);
			assert(op.primitiveOutput);
			xr = operand(op);
			if (xr.t == ExpressionResult::PROPERTY) {
				emit(Processor::CHECK_RESOLVE_PROPERTY_OP);
				emit(Processor::REPUSH_2_OP);
			}
			makeRValue(xr, true);
			emit(op.vmOp);
			makeAssignment(xr);
			xr = ExpressionResult::PUSHED_PRIMITIVE;
			break;
		}
		
		case TYPE_OF: {
			assert(!op.primitiveInput);
			assert(op.primitiveOutput);
			xr = operand(op);
			if (xr.t == ExpressionResult::NAMED) {
				emitWithConstant(Processor::TYPEOF_NAMED_OP, xr.v);
			} else {
				makeRValue(xr, false);
				emit(op.vmOp);
			}
			xr = ExpressionResult::PUSHED_PRIMITIVE;
			break;
		}
		
		default: assert(0);
	}
	return true;
}

bool Compiler::postOperate(ExpressionResult& xr, Precedence precedence) {
	const Char* b = p;
	const int opIndex = parseOperator(precedence, sizeof (POST_OPS) / sizeof (*POST_OPS), POST_OPS);
	if (opIndex < 0) {
		return false;
	}
	const OperatorInfo& op = POST_OPS[opIndex];
	switch (op.type) {
		case COMMA: {
			xr = discard(xr);
			xr = operand(op);
			break;
		}
			
		case BINARY: {
			const Processor::Opcode primitiveOp = (op.vmOp == Processor::ADD_OP
					? Processor::OBJ_TO_PRIMITIVE_OP : Processor::OBJ_TO_NUMBER_OP);
			makeRValue(xr, op.primitiveInput, primitiveOp);
			makeRValue(operand(op), op.primitiveInput, primitiveOp);
			emit(op.vmOp);
			xr = (op.primitiveOutput ? ExpressionResult::PUSHED_PRIMITIVE : ExpressionResult::PUSHED);
			break;
		}
		
		case ABSTRACT_EQUAL: {
			assert(!op.primitiveInput);
			assert(op.primitiveOutput);
			const ExpressionResult lxr = makeRValue(xr, false);
			xr = makeRValue(operand(op), op.primitiveInput);
			if (lxr.t != ExpressionResult::PUSHED_PRIMITIVE || xr.t != ExpressionResult::PUSHED_PRIMITIVE) {
				emit(Processor::PRE_EQ_OP);
			}
			emit(op.vmOp);
			xr = ExpressionResult::PUSHED_PRIMITIVE;
			break;
		}

		case IN_OPERATOR: {
			if (!acceptInOperator) {
				return false;
			}
			assert(!op.primitiveInput);
			assert(op.primitiveOutput);
			makeRValue(xr, true, Processor::OBJ_TO_STRING_OP); // left should be primitive, right doesn't have to
			makeRValue(operand(op), false);
			emit(op.vmOp);
			xr = ExpressionResult::PUSHED_PRIMITIVE;
			break;
		}

		case POST_INC_DEC: {
			assert(op.primitiveInput);
			assert(op.primitiveOutput);
			if (lineTerminatorInRange(b, p)) {
				p = b;
				return false;
			}
			if (xr.t == ExpressionResult::PROPERTY) {
				emit(Processor::CHECK_RESOLVE_PROPERTY_OP);
				emit(Processor::REPUSH_2_OP);
			}
			makeRValue(xr, true);
			emit(Processor::PLUS_OP);
			emit(xr.t == ExpressionResult::PROPERTY ? Processor::POST_SHUFFLE_OP : Processor::REPUSH_OP);
			emit(op.vmOp);
			makeAssignment(xr);
			emit(Processor::POP_OP, 1);
			xr = ExpressionResult::PUSHED_PRIMITIVE;
			break;
		}
		
		case LOGICAL_AND_OR: {
			assert(!op.primitiveInput);
			assert(!op.primitiveOutput);
			makeRValue(xr, false);
			const BranchPoint point = emitForwardBranch(op.vmOp);
			makeRValue(operand(op), false);
			completeForwardBranch(point);
			xr = ExpressionResult::PUSHED;
			break;
		}
		
		case CONDITIONAL: {
			assert(!op.primitiveInput);
			assert(!op.primitiveOutput);
			makeRValue(xr, false);
			const BranchPoint falsePoint = emitForwardBranch(Processor::JF_OP);
			makeRValue(operand(op), false);
			const BranchPoint endPoint = emitForwardBranch(Processor::JMP_OP);
			completeForwardBranch(falsePoint);
			rvalueExpression(ASSIGN_PREC);
			completeForwardBranch(endPoint);
			xr = ExpressionResult::PUSHED;
			break;
		}
		
		case PROPERTY_DOT: {
			assert(!op.primitiveInput);
			makeRValue(xr, op.primitiveInput);
			emit(Processor::CHECK_OBJECT_COERCIBLE_OP);
			white();
			emitWithConstant(Processor::CONST_OP, identifier(true, true));
			xr = ExpressionResult(ExpressionResult::PROPERTY);
			break;
		}
		
		case FUNCTION_CALL: {
			assert(!op.primitiveInput);
			assert(!op.primitiveOutput);
			Processor::Opcode callOp = Processor::CALL_OP;
			if (xr.t == ExpressionResult::NAMED && xr.v.equalsString(EVAL_STRING)) {
				callOp = Processor::CALL_EVAL_OP;
			}
			if (xr.t == ExpressionResult::PROPERTY) {
				callOp = Processor::CALL_METHOD_OP;
			} else {
				makeRValue(xr, false);
			}
			functionCall(callOp);
			xr = ExpressionResult::PUSHED;
			break;
		}
		
		case ASSIGNMENT: {
			assert(!op.primitiveInput);
			assert(!op.primitiveOutput);
			if (xr.t == ExpressionResult::PROPERTY) {
				emit(Processor::CHECK_RESOLVE_PROPERTY_OP);
			}
			const ExpressionResult rxr = makeRValue(operand(op), false);
			makeAssignment(xr);
			xr = rxr;
			break;
		}
			
		case COMPOUND_ASSIGNMENT: {
			assert(op.primitiveInput);
			assert(op.primitiveOutput);
			const Processor::Opcode primitiveOp = (op.vmOp == Processor::ADD_OP
					? Processor::OBJ_TO_PRIMITIVE_OP : Processor::OBJ_TO_NUMBER_OP);
			if (xr.t == ExpressionResult::PROPERTY) {
				emit(Processor::CHECK_RESOLVE_PROPERTY_OP);
				emit(Processor::REPUSH_2_OP);
			}
			makeRValue(xr, true, primitiveOp);
			makeRValue(operand(op), true, primitiveOp);
			emit(op.vmOp);
			makeAssignment(xr);
			xr = ExpressionResult::PUSHED_PRIMITIVE;
			break;
		}
			
		case PROPERTY_BRACKETS: {
			assert(!op.primitiveInput);
			makeRValue(xr, false);
			emit(Processor::CHECK_OBJECT_COERCIBLE_OP);
			const bool didAcceptInOperator = acceptInOperator;
			acceptInOperator = true;
			makeRValue(operand(op), true, Processor::OBJ_TO_STRING_OP); // left doesn't need to be primitive, but right does (and preferred string!)
			acceptInOperator = didAcceptInOperator;
			xr = ExpressionResult(ExpressionResult::PROPERTY);
			break;
		}
		
		default: assert(0);
	}
	return true;
}

void Compiler::functionDefinition(const String* functionName, const String* selfName) {
	assert(functionName != 0);
	Code* func = new(heap) Code(heap.managed(), code->constants, code->getSourceUnit());
	Compiler funcCompiler(heap.roots(), func, Compiler::FOR_FUNCTION, nestCounter);
	try {
		p = funcCompiler.compileFunction(p, e, functionName, selfName);
	}
	catch (const Exception&) { // FIX : a bit Q & D ish
		p = funcCompiler.p;
		throw;
	}
	emitWithConstant(Processor::GEN_FUNC_OP, func);
}

const Int32 CATCH_PARAMETER = 0x7FFFFFFF;
const Int32 MAX_NESTED_EXPRESSION_DEPTH = 64;

bool Compiler::optionalExpression(ExpressionResult& xr, Precedence precedence) {
	if (nestCounter >= MAX_NESTED_EXPRESSION_DEPTH) {
		error(RANGE_ERROR, "Internal compiler limitations reached. Reduce code complexity.");
	}
	struct NestCounter {
		NestCounter(Compiler& c) : c(c) { ++c.nestCounter; };
		~NestCounter() { --c.nestCounter; };
		Compiler& c;
	} nestCounter(*this);
	
	if (!preOperate(xr, precedence)) {
		if (eof()) {
			return false;
		}
		const Char* b = p;
		parseIdentifier(true);
		int i = findLiteralKeyword(p - b, &*b);
		if (i >= 0) {
			xr = discard(xr);
			switch (static_cast<Literal>(i)) {
				case NULL_LITERAL: xr = ExpressionResult(ExpressionResult::CONSTANT, NULL_VALUE); break;
				case FALSE_LITERAL: xr = ExpressionResult(ExpressionResult::CONSTANT, false); break;
				case TRUE_LITERAL: xr = ExpressionResult(ExpressionResult::CONSTANT, true); break;
				case THIS_LITERAL: {
					emit(Processor::THIS_OP);
					xr = ExpressionResult(ExpressionResult::PUSHED);
					break;
				}
				case FUNCTION_LITERAL: {
					white();
					const String* name = identifier(false, false);
					functionDefinition(name, name);
					xr = ExpressionResult(ExpressionResult::PUSHED);
					break;
				}
				default: assert(0);
			}
		} else {
			p = b;
			assert(!eof());
			Value v = stringOrNumberConstant();
			if (p != b) {
				xr = discard(xr);
				xr = ExpressionResult(ExpressionResult::CONSTANT, v);
			} else {
				switch (*p) {
					case '{': {
						++p;
						xr = discard(xr);
						xr = objectInitialiser();
						break;
					}
					
					case '[': {
						++p;
						xr = discard(xr);
						xr = arrayInitialiser();
						break;
					}
					
					case '/': {
						bool inClass = false;
						while (++p != e && !isLineTerminator(*p) && (*p != '/' || inClass)) {
							if (*p == '\\' && p + 1 != e && !isLineTerminator(p[1])) {
								++p;
							} else if (*p == '[') {
								inClass = true;
							} else if (*p == ']' && inClass) {
								inClass = false;
							}
						}
						if (p == e || *p != '/') {
							error(SYNTAX_ERROR, "Unterminated regular expression");
						} else {
							xr = discard(xr);
							const String* regExpSource = newHashedString(heap, b + 1, p);
							emitWithConstant(Processor::CONST_OP, regExpSource);
							++p;
							const Char* b = p;
							parseIdentifier(false);
							const String* regExpFlags = newHashedString(heap, b, p);
							emitWithConstant(Processor::CONST_OP, regExpFlags);
							emit(Processor::NEW_REG_EXP_OP);
							xr = ExpressionResult(ExpressionResult::PUSHED);
						}
						break;
					}
					
					default: {
						const String* name = identifier(false, false);
						if (name->empty()) {
							return false;
						}
						xr = discard(xr);
						xr = ExpressionResult(ExpressionResult::NAMED, name);
						// Notice: compilingFor != FOR_EVAL and checking for "arguments" is only for non-strict mode (if we ever support strict-mode in the future)
						if (!name->isEqualTo(ARGUMENTS_STRING) && compilingFor == FOR_FUNCTION && withScopeCounter == 0) {
							const Table::Bucket* bucket = code->nameIndexes.lookup(name);
							if (bucket != 0) {
								const Int32 index = bucket->getIndexValue();
								if (index != CATCH_PARAMETER) {
									xr = ExpressionResult(ExpressionResult::LOCAL, index);
									break;
								}
							}
						}
						break;
					}
				}
			}
		}
	}
	const Char* b = p;
	while (postOperate(xr, precedence)) {
		b = p;
	}
	p = b;
	return true;
}

Compiler::ExpressionResult Compiler::expression(Precedence precedence) {
	ExpressionResult xr;
	if (!optionalExpression(xr, precedence)) {
		error(SYNTAX_ERROR, "Missing / invalid expression");
	}
	return xr;
}

Compiler::ExpressionResult Compiler::rvalueExpression(Precedence precedence) {
	return makeRValue(expression(precedence));
}

void Compiler::rvalueGroup() {
	expectToken("(", true);
	rvalueExpression();
	expectToken(")", true);
}

// FIX : ok, this is serious mess
Compiler::ExpressionResult Compiler::declareIdentifier(const String* name, bool func) {
	ExpressionResult lxr(ExpressionResult::NAMED, name);
	if (compilingFor != FOR_FUNCTION) {
		CodeSection* previousSection = changeSection(&setupSection);
		if (!func) {
			emit(Processor::VOID_OP);
		}
		emitWithConstant(Processor::DECLARE_OP, name);
		changeSection(previousSection);
		return lxr;
	} else if (!name->isEqualTo(ARGUMENTS_STRING)) {
		Int32 index;
		Table& nameIndexes = code->nameIndexes;
		Table::Bucket* bucket = nameIndexes.lookup(name);
		if (bucket != 0) {
			index = bucket->getIndexValue();
		} else {
			code->varNames.push(name);
			index = -static_cast<Int32>(code->varNames.size());
			nameIndexes.update(nameIndexes.insert(name), index);
			code->bloomSet |= name->createBloomCode();
		}
		if (withScopeCounter == 0 && index != CATCH_PARAMETER) {
			lxr = ExpressionResult(ExpressionResult::LOCAL, index);
		}
	}
	if (func) {
		makeAssignment(lxr);
		emit(Processor::POP_OP, 1);
	}
	return lxr;
}

Compiler::ExpressionResult Compiler::varDeclaration() {
	ExpressionResult lxr(declareIdentifier(identifier(true, false), false));
	if (token("=", true)) {
		rvalueExpression(COMMA_PREC);
		makeAssignment(lxr);
		emit(Processor::POP_OP, 1);
	}
	return lxr;
}

void Compiler::varDeclarations() {
	while (true) {
		varDeclaration();
		if (!token(",", true)) {
			break;
		}
		white();
	}
}

void Compiler::completeIteratorContinues(const SemanticScope* fromScope, const SemanticScope* untilScope) {
	for (const SemanticScope* s = fromScope; s != untilScope; s = s->next) {
		assert(s->type == SemanticScope::ITERATOR_LABEL_TYPE);
		completeForwardBranches(s->continues.begin(), s->continues.end());
	}
}

void Compiler::completeIteratorBreaks(const SemanticScope* fromScope, const SemanticScope* untilScope) {
	for (const SemanticScope* s = fromScope; s != untilScope; s = s->next) {
		assert(s->type == SemanticScope::ITERATOR_LABEL_TYPE);
		completeForwardBranches(s->breaks.begin(), s->breaks.end());
	}
}

void Compiler::completeBreaks(const SemanticScope* ofScope) {
	completeForwardBranches(ofScope->breaks.begin(), ofScope->breaks.end());
}

void Compiler::forInStatement(SemanticScope* emptyLabelScope, SemanticScope* scopeLabelsEnd
		, const CodeSection& iterationSection, const ExpressionResult& iterationXR) {
	ExpressionResult enumXR(rvalueExpression());
 	emit(Processor::GET_ENUMERATOR_OP);
	enumXR = safeKeep();
	expectToken(")", true);
	for (SemanticScope* s = emptyLabelScope; s != scopeLabelsEnd; s = s->next) {	// we must change stack depth of all labels that point to this scope, otherwise we'll pop the enumerator on continue
		s->stackDepthOnEntry = currentSection->stackDepth;
	}
	const BranchPoint loopPoint = markBackwardBranch();
	makeRValue(enumXR);
	const BranchPoint exitLoopPoint = emitForwardBranch(Processor::NEXT_PROPERTY_OP);
	ExpressionResult xr(ExpressionResult::PUSHED);
	if (!iterationSection.code.empty()) { // This is probably extremely rare (left hand side being anything but a directly assignable variable), so I don't mind the extra code of safe keeping here
		xr = safeKeep();
		currentSection->insertSection(iterationSection);
		makeRValue(xr);
		discard(makeAssignment(iterationXR));
		returnSafeKept(xr);
	} else {
		discard(makeAssignment(iterationXR));
	}
	statement(emptyLabelScope, emptyLabelScope);
	completeIteratorContinues(emptyLabelScope, scopeLabelsEnd);
	emitBackwardBranch(Processor::JMP_OP, loopPoint);
	completeForwardBranch(exitLoopPoint);
	completeIteratorBreaks(emptyLabelScope, scopeLabelsEnd);
	returnSafeKept(enumXR);
}

void Compiler::forStatement(SemanticScope* emptyLabelScope, SemanticScope* scopeLabelsEnd) {
	expectToken(";", true);
	const BranchPoint loopPoint = markBackwardBranch();
	BranchPoint exitLoopPoint;
	ExpressionResult testXR;
	if (optionalExpression(testXR, LOWEST_PREC)) {
		makeRValue(testXR);
		exitLoopPoint = emitForwardBranch(Processor::JF_OP);
	}
	expectToken(";", true);
	CodeSection incrementSection(heap, currentSection->stackDepth);
	{
		CodeSection* previousSection = changeSection(&incrementSection);
		ExpressionResult incXR;
		if (optionalExpression(incXR, LOWEST_PREC)) {
			discard(incXR);
		}
		expectToken(")", true);
		changeSection(previousSection);
	}
	statement(emptyLabelScope, emptyLabelScope);
	completeIteratorContinues(emptyLabelScope, scopeLabelsEnd);
	currentSection->insertSection(incrementSection);
	emitBackwardBranch(Processor::JMP_OP, loopPoint);
	completeForwardBranch(exitLoopPoint);
	completeIteratorBreaks(emptyLabelScope, scopeLabelsEnd);
}

void Compiler::semicolon(bool acceptLF) {
	const Char* b = p;
	white();
	if (!eof() && *p != '}' && !token(";", false) && (!acceptLF || !lineTerminatorInRange(b, p))) {
		p = b;
		error(SYNTAX_ERROR, "Syntax error");
	}
}

// FIX : sort all statement funcs

void Compiler::varStatement() {
	white();
	varDeclarations();
	semicolon();
}

void Compiler::functionStatement() {
	white();
	const String* name = identifier(true, false);
	CodeSection* previousSection = changeSection(&setupSection);
	functionDefinition(name, 0);
	declareIdentifier(name, true);
	changeSection(previousSection);
}

void Compiler::withStatement(SemanticScope* currentScope) {
	rvalueGroup();
	emit(Processor::WITH_SCOPE_OP);
	{
		++withScopeCounter;
		SemanticScope withScope(heap, SemanticScope::WITH_TYPE, currentSection->stackDepth, currentScope);
		statement(&withScope, &withScope);
		--withScopeCounter;
	}
	emit(Processor::POP_FRAME_OP);
}

void Compiler::breakStatement(SemanticScope* currentScope) {
	const String* label = (whiteNoLF() ? identifier(false, false) : &EMPTY_STRING);
	Int32 rollbackStackDepth = currentSection->stackDepth;
	for (SemanticScope* s = currentScope; s != 0; s = s->next) {
		if (s->label.isEqualTo(*label)
				&& (s->type == SemanticScope::ITERATOR_LABEL_TYPE || s->type == SemanticScope::LABEL_TYPE)) {
			s->popStack(*this, rollbackStackDepth, evalPopOpcode());
			s->breaks.push(emitForwardBranch(Processor::JMP_OP));
			semicolon();
			return;
		}
		rollbackStackDepth = s->rollback(*this, rollbackStackDepth, evalPopOpcode());
	}
	error(SYNTAX_ERROR, !label->empty() ? "Undefined label" : "Illegal break");
}

void Compiler::continueStatement(SemanticScope* currentScope) {
	const String* label = (whiteNoLF() ? identifier(false, false) : &EMPTY_STRING);
	Int32 rollbackStackDepth = currentSection->stackDepth;
	for (SemanticScope* s = currentScope; s != 0; s = s->next) {
		if (s->label.isEqualTo(*label) && (s->type == SemanticScope::ITERATOR_LABEL_TYPE
				|| (s->type == SemanticScope::LABEL_TYPE && !label->empty()))) {
			if (s->type != SemanticScope::ITERATOR_LABEL_TYPE) {
				error(SYNTAX_ERROR, "Illegal label for continue");
			}
			s->popStack(*this, rollbackStackDepth, evalPopOpcode());
			s->continues.push(emitForwardBranch(Processor::JMP_OP));
			semicolon();
			return;
		}
		rollbackStackDepth = s->rollback(*this, rollbackStackDepth, evalPopOpcode());
	}
	error(SYNTAX_ERROR, !label->empty() ? "Undefined label" : "Illegal continue");
}

void Compiler::forOrForInStatement(SemanticScope* currentScope, SemanticScope* scopeLabelsEnd) {
	SemanticScope emptyLabelScope(heap, EMPTY_STRING, currentSection->stackDepth, currentScope);
	emptyLabelScope.makeIteratorScopes(scopeLabelsEnd);
	expectToken("(", true);
	CodeSection initSection(heap, currentSection->stackDepth);
	if (token("var", true)) {
		white();
		acceptInOperator = false;
		ExpressionResult iterationXR = varDeclaration();
		acceptInOperator = true;
		if (token("in", true)) {
			forInStatement(&emptyLabelScope, scopeLabelsEnd, initSection, iterationXR);
		} else {
			if (token(",", true)) {
				white();
				acceptInOperator = false;
				varDeclarations();
				acceptInOperator = true;
			}
			forStatement(&emptyLabelScope, scopeLabelsEnd);
		}
	} else {
		ExpressionResult iterationXR;
		bool isForIn = false;
		{
			CodeSection* previousSection = changeSection(&initSection);
			acceptInOperator = false;
			bool gotExpression = optionalExpression(iterationXR, LOWEST_PREC);
			acceptInOperator = true;
			isForIn = (gotExpression && token("in", true));
			if (!isForIn) {
				discard(iterationXR);
			}
			changeSection(previousSection);
		}
		if (isForIn) {
			forInStatement(&emptyLabelScope, scopeLabelsEnd, initSection, iterationXR);
		} else {
			currentSection->insertSection(initSection);
			forStatement(&emptyLabelScope, scopeLabelsEnd);
		}
	}
}

void Compiler::ifStatement(SemanticScope* currentScope) {
	rvalueGroup();
	const BranchPoint falsePoint = emitForwardBranch(Processor::JF_OP);
	statement(currentScope, currentScope);
	if (token("else", true)) {
		const BranchPoint endPoint = emitForwardBranch(Processor::JMP_OP);
		completeForwardBranch(falsePoint);
		statement(currentScope, currentScope);
		completeForwardBranch(endPoint);
	} else {
		completeForwardBranch(falsePoint);
	}				
}

void Compiler::whileStatement(SemanticScope* currentScope, SemanticScope* scopeLabelsEnd) {
	const BranchPoint loopPoint = markBackwardBranch();
	SemanticScope emptyLabelScope(heap, EMPTY_STRING, currentSection->stackDepth, currentScope);
	emptyLabelScope.makeIteratorScopes(scopeLabelsEnd);
	rvalueGroup();
	const BranchPoint exitLoopPoint = emitForwardBranch(Processor::JF_OP);
	statement(&emptyLabelScope, &emptyLabelScope);
	completeIteratorContinues(&emptyLabelScope, scopeLabelsEnd);
	emitBackwardBranch(Processor::JMP_OP, loopPoint);
	completeForwardBranch(exitLoopPoint);
	completeIteratorBreaks(&emptyLabelScope, scopeLabelsEnd);
}

void Compiler::doWhileStatement(SemanticScope* currentScope, SemanticScope* scopeLabelsEnd) {
	white();
	const BranchPoint loopPoint = markBackwardBranch();
	SemanticScope emptyLabelScope(heap, EMPTY_STRING, currentSection->stackDepth, currentScope);
	emptyLabelScope.makeIteratorScopes(scopeLabelsEnd);
	statement(&emptyLabelScope, &emptyLabelScope);
	completeIteratorContinues(&emptyLabelScope, scopeLabelsEnd);
	expectToken("while", true);
	rvalueGroup();
	emitBackwardBranch(Processor::JT_OP, loopPoint);
	semicolon();
	completeIteratorBreaks(&emptyLabelScope, scopeLabelsEnd);
}

void Compiler::returnStatement(SemanticScope* currentScope) {
	if (compilingFor != FOR_FUNCTION) {
		error(SYNTAX_ERROR, "Illegal return");
	}
	ExpressionResult returnXR;
	if (whiteNoLF()) {
		optionalExpression(returnXR, LOWEST_PREC);
	}
	returnXR = makeRValue(returnXR);

	Int32 rollbackStackDepth = currentSection->stackDepth;
	for (SemanticScope* s = currentScope; s != 0; s = s->next) {
		rollbackStackDepth = s->rollback(*this, rollbackStackDepth, Processor::PUSH_BACK_OP);
	}
	emit(Processor::RETURN_OP);
	semicolon();
}

void Compiler::throwStatement() {
	if (!whiteNoLF()) {
		error(SYNTAX_ERROR, "Illegal newline after throw");
	}
	rvalueExpression();
	emit(Processor::THROW_OP);
	semicolon();
}

void Compiler::block(SemanticScope* scope) {
	expectToken("{", true);
	statementList(scope);
	expectToken("}", false);
}

/*
	Return statement needs a stack entry. All calls to finally() must have identical stack pointer. In combination this
	means we must preallocate an entry on the stack for any potential return statements ("space holder"). Also in eval
	mode we need to always flush eval results to stack before each call to finally() and when setting up try scopes.
	There is also a rule that eval result from try scope should be rolled back in case of a throw.
	
							// NO EVAL						// EVAL
							// ========						// ========
 
							// spaceHolder					// (eval)
	try {
							// spaceHolder					// rollback (eval)
		<statements>
							// spaceHolder					// (eval)
		finally()
	}
							// spaceHolder thrownA			// (eval) thrownA
	catch (x) {
		*try {
							// spaceHolder					// (eval)
			<statements>
			finally()
		*}
							// spaceHolder thrownA thrownB	// (eval) thrownA thrownB
		*catch {
							// thrownB						// thrownB
			*finally()
							// thrownB						// thrownB
			*throw
		*}
	}
							// spaceHolder					// (eval)
	finally {
							// spaceHolder					// rollback (eval)
		<statements>
							// spaceHolder					// rollback
	}
							//								// (eval)



							// NO EVAL						// EVAL
							// ========						// ========
 
							// spaceHolder					// (eval)
	try {
							// spaceHolder					// rollback (eval)
		<statements>
							// spaceHolder					// (eval)
		finally()
	}
							// spaceHolder thrownA			// (eval) thrownA
	*catch {
							// thrownA						// thrownA
		*finally()
							// thrownA						// thrownA
		*throw
	*}
							// spaceHolder					// (eval)
	finally {
							// spaceHolder					// rollback (eval)
		<statements>
							// spaceHolder					// rollback
	}
							//								// (eval)
*/
void Compiler::tryStatement(SemanticScope* currentScope) {
	white();

/* FIX : I thought we could drop if we keep the reserve-one-element-for-return-on-stack-solution, but rethrower needs a place to safe-keep its throw value and this is here*/
	if (compilingFor != FOR_EVAL) { // space holder for return value so that we can break out of finallys (with the same stack pop count) regardless if try scope returns a value or not
		emit(Processor::VOID_OP, 0);
	}

	Vector<BranchPoint> allFinallys(&heap);
	BranchPoint catchPoint;
	const Int32 trySectionEntryStackDepth = currentSection->stackDepth;
	{
		SemanticScope tryScope(heap, SemanticScope::TRY_TYPE, trySectionEntryStackDepth, currentScope);
		catchPoint = emitForwardBranch(Processor::TRY_OP);
		if (compilingFor == FOR_EVAL) { // Eval result from try scope should be rolled back in case of a throw
			emit(Processor::REPUSH_OP, 0);
		}
		block(&tryScope);
		if (compilingFor == FOR_EVAL) {
			emit(Processor::PUSH_BACK_OP, 1);
		}
		tryScope.finallys.push(emitForwardBranch(Processor::JSR_OP));
		allFinallys.insert(allFinallys.end(), tryScope.finallys.begin(), tryScope.finallys.end());
	}
	const BranchPoint tryEndPoint = emitForwardBranch(Processor::JMP_OP);

	// Catcher
	
	BranchPoint catchEndPoint;
	bool gotCatch = false;
	if (token("catch", true)) {
		gotCatch = true;
		expectToken("(", true);
		white();
		const String* exceptionVarName = identifier(true, false);
		Int32 previousLocalIndex = 0;
		Table& nameIndexes = code->nameIndexes;
		bool didExist = false; // FIX : can use NON_EXISTENT type if we choose to keep it
		Table::Bucket* indexBucket = nameIndexes.lookup(exceptionVarName);
		if (indexBucket != 0) {
			didExist = true;
			previousLocalIndex = indexBucket->getIndexValue();
		} else {
			indexBucket = nameIndexes.insert(exceptionVarName);
		}
		nameIndexes.update(indexBucket, CATCH_PARAMETER); // prohibit further var accesses or declarations to the capture parameter via fast index binding.
		completeForwardBranch(catchPoint);
		catchPoint = emitForwardBranch(Processor::TRY_OP);
		emitWithConstant(Processor::CATCH_SCOPE_OP, exceptionVarName);
		expectToken(")", true);
		{
			SemanticScope catchScope(heap, SemanticScope::CATCH_TYPE, currentSection->stackDepth, currentScope);
			block(&catchScope);
			emit(Processor::POP_FRAME_OP);
			catchScope.finallys.push(emitForwardBranch(Processor::JSR_OP));
			catchEndPoint = emitForwardBranch(Processor::JMP_OP);
			allFinallys.insert(allFinallys.end(), catchScope.finallys.begin(), catchScope.finallys.end());
		}
		if (didExist) {
			nameIndexes.update(nameIndexes.insert(exceptionVarName), previousLocalIndex);
		} else {
			nameIndexes.erase(nameIndexes.lookup(exceptionVarName));
		}
	}
	
	// Re-thrower (could actually be eliminated if we only have catch and no finally, but we don't know yet)
	
	completeForwardBranch(catchPoint);
	emit(Processor::PUSH_BACK_OP, currentSection->stackDepth - trySectionEntryStackDepth);
	BranchPoint finallyRethrowPoint = emitForwardBranch(Processor::JSR_OP);
	emit(Processor::THROW_OP);

	// Finally
	
	completeForwardBranches(allFinallys.begin(), allFinallys.end());
	emit(Processor::TRIED_OP);
	completeForwardBranch(finallyRethrowPoint);
	if (token("finally", true)) {
		SemanticScope finallyScope(heap, SemanticScope::FINALLY_TYPE, currentSection->stackDepth, currentScope);
		if (compilingFor == FOR_EVAL) {	// Ecmascript dictates that finally block should never change completion value.
			emit(Processor::VOID_OP);
		}
		block(&finallyScope);
		if (compilingFor == FOR_EVAL) {
			emit(Processor::POP_OP, 1);
		}
	} else {
		if (!gotCatch) {
			error(SYNTAX_ERROR, "Expected 'catch' or 'finally'");
		}
	}
	emit(Processor::RETURN_OP);
	
	completeForwardBranch(tryEndPoint);
	completeForwardBranch(catchEndPoint);

/* FIX : I thought we could drop if we keep the reserve-one-element-for-return-on-stack-solution */
	if (compilingFor != FOR_EVAL) { // pop return space holder
		emit(Processor::POP_OP, 1);
	}
}

/*
	Switch statement places the test block *after* the cases block. This is to simplify forward branches (breaks etc) as
	they would be marked at incorrect addresses otherwise.
*/
void Compiler::switchStatement(SemanticScope* currentScope) {
	rvalueGroup();
	ExpressionResult controlXR(safeKeep());
	expectToken("{", true);
	const Int32 testsSectionEntryStackDepth = currentSection->stackDepth;
	const BranchPoint testsPoint = emitForwardBranch(Processor::JMP_OP);
	CodeSection testsSection(heap, testsSectionEntryStackDepth);
	Vector<Int32> jumpOffsets(&heap);
	BranchPoint defaultPoint;
	SemanticScope emptyLabelScope(heap, EMPTY_STRING, testsSectionEntryStackDepth, currentScope);
	bool firstStatement = true;
	white();
	while (!token("}", false)) {
		if (eof()) {
			error(SYNTAX_ERROR, "Expected '}'");
		}
		else if (token("case", false)) {
			assert(currentSection == &mainSection);
			currentSection->stackDepth = testsSection.stackDepth;
			const BranchPoint casePoint = markBackwardBranch();

			CodeSection* previousSection = changeSection(&testsSection);
			makeRValue(controlXR);
			rvalueExpression();
			expectToken(":", true);
			if (!currentSection->inDeadCode()) {
				emit(Processor::X_EQ_OP);
				jumpOffsets.push(testsSection.code.size());
				emit(Processor::JT_OP, casePoint.codeOffset - 1 - currentSection->code.size());
			}
			changeSection(previousSection);
		} else if (token("default", false)) {
			expectToken(":", true);
			if (defaultPoint.isValid()) {
				error(SYNTAX_ERROR, "More than one 'default:'");
			}
			assert(currentSection == &mainSection);
			currentSection->stackDepth = testsSectionEntryStackDepth;
			defaultPoint = markBackwardBranch();
		} else if (firstStatement) {
			error(SYNTAX_ERROR, "Expected 'case', 'default' or '}'");
		} else {
			const Char* b = p;
			statement(&emptyLabelScope, &emptyLabelScope);
			if (b == p) {
				error(SYNTAX_ERROR, "Expected '}'");
			}
		}
		firstStatement = false;
		white();
	}
	
	assert(currentSection == &mainSection);
	const BranchPoint donePoint = emitForwardBranch(Processor::JMP_OP);
	completeForwardBranch(testsPoint);
	for (const Int32* it = jumpOffsets.begin(); it != jumpOffsets.end(); ++it) {
		const std::pair<Processor::Opcode, Int32> instruction = Processor::unpackInstruction(testsSection.code[*it]);
		testsSection.code[*it] = Processor::packInstruction(instruction.first, instruction.second - mainSection.code.size());
	}
	currentSection->insertSection(testsSection);
	assert(currentSection->stackDepth == testsSectionEntryStackDepth); // since we assume this in the default case code
	if (defaultPoint.isValid()) {
		emitBackwardBranch(Processor::JMP_OP, defaultPoint);
	}
	completeForwardBranch(donePoint);
	completeBreaks(&emptyLabelScope);
	returnSafeKept(controlXR);
}

/*
	'scopeLabelsEnd' points to the end of a range of scopes that are all labelled scopes. This is required
	because it is legal to give while loops etc multiple alternative labels to be used with 'continue'.
	(These should all be converted to 'ITERATOR_LABEL_TYPE' upon entry.)
*/
void Compiler::statement(SemanticScope* currentScope, SemanticScope* scopeLabelsEnd) {
	assert(currentSection == &mainSection); // statements must produce into main-section because of breaks etc...
	
	white();
	
	if (token("{", false)) {
		statementList(currentScope);
		expectToken("}", false);
		return;
	}
	
	const Char* b = p;
	Vector<Char, 64> parsed = parseIdentifier(true);
	int statementTokenIndex = findStatementKeyword(p - b, &*b);
	if (statementTokenIndex < 0) {
		if (p != b && token(":", true)) {
			const String label = String(parsed.begin(), parsed.end());
			assert(!label.empty());
			if (findReservedKeyword(label.size(), label.begin()) >= 0) {
				error(SYNTAX_ERROR, "Illegal use of keyword");
			}
			for (SemanticScope* s = currentScope; s != 0; s = s->next) {
				if (!s->label.empty() && s->label.isEqualTo(label)) {
					error(SYNTAX_ERROR, "Duplicate label");
				}
			}
			SemanticScope newLabelScope(heap, label, currentSection->stackDepth, currentScope);
			statement(&newLabelScope, scopeLabelsEnd);
			if (newLabelScope.type != SemanticScope::ITERATOR_LABEL_TYPE) {
				completeBreaks(&newLabelScope);
			}
		} else {
			p = b;
			ExpressionResult evalXR = (compilingFor == FOR_EVAL ? ExpressionResult::PUSHED : ExpressionResult::NONE);
			optionalExpression(evalXR, LOWEST_PREC);
			if (compilingFor == FOR_EVAL) {
				evalXR = makeRValue(evalXR);
			} else {
				evalXR = discard(evalXR);
			}
			semicolon();
		}
	} else {
		switch (static_cast<Statement>(statementTokenIndex)) {
			case BREAK_STATEMENT: breakStatement(currentScope); break;
			case CONTINUE_STATEMENT: continueStatement(currentScope); break;
			case VAR_STATEMENT: varStatement(); break;
			case IF_STATEMENT: ifStatement(currentScope); break;
			case WHILE_STATEMENT: whileStatement(currentScope, scopeLabelsEnd); break;
			case DO_WHILE_STATEMENT: doWhileStatement(currentScope, scopeLabelsEnd); break;
			case FOR_STATEMENT: forOrForInStatement(currentScope, scopeLabelsEnd); break;
			case RETURN_STATEMENT: returnStatement(currentScope); break;
			case THROW_STATEMENT: throwStatement(); break;
			case TRY_STATEMENT: tryStatement(currentScope); break;
			case SWITCH_STATEMENT: switchStatement(currentScope); break;
			case FUNCTION_STATEMENT: functionStatement(); break;
			case WITH_STATEMENT: withStatement(currentScope); break;

			default: assert(0);
		}
	}
}

void Compiler::statementList(SemanticScope* currentScope) {
	const Char* b;
	do {
		b = p;
		statement(currentScope, currentScope);
	} while (!eof() && b != p);
}

const Char* Compiler::compile(const Char* b, const Char* e) {
	changeSection(&mainSection);
	this->b = b;
	p = b;
	this->e = e;
	acceptInOperator = true;
	
	// FIX : not 100% necessary now because we should always start with undefined on top of stack
	if (compilingFor == FOR_EVAL) {
		emit(Processor::POP_OP, 1);	// FIX : only if we reserve one element for return like we do now
		emit(Processor::VOID_OP);
	}
	SemanticScope rootScope(heap, SemanticScope::ROOT_TYPE, 1, 0);
	statementList(&rootScope);
 	// FIX : sometimes necessary even if we start with undefined on top of stack, because try/catch rethrower might need to safe-keep its exception there
	if (compilingFor != FOR_EVAL) {
		// FIX : if RETURN_OP took a push back count we could just do void_op here, or even have another RETURN_VOID_OP
		emit(Processor::POP_OP, 1);	// FIX : only if we reserve one element for return like we do now
		emit(Processor::VOID_OP);
	}
	emit(Processor::RETURN_OP);

	assert(currentSection == &mainSection);
	code->codeWords.resize(setupSection.code.size() + mainSection.code.size());
	std::copy(setupSection.code.begin(), setupSection.code.end(), code->codeWords.begin());
	std::copy(mainSection.code.begin(), mainSection.code.end(), code->codeWords.begin() + setupSection.code.size());
	code->constants->shrink();
	code->maxStackDepth = std::max(mainSection.maxStackDepth, setupSection.maxStackDepth);
	setupSection.exportSourceMapping(code->codeOffsets, code->sourceOffsets, 0);
	mainSection.exportSourceMapping(code->codeOffsets, code->sourceOffsets, static_cast<UInt32>(setupSection.code.size()));
	code->codeOffsets.shrink();
	code->sourceOffsets.shrink();
	
	return p;
}

const Char* Compiler::compileFunction(const Char* b, const Char* e, const String* functionName, const String* selfName) {
	assert(compilingFor == FOR_FUNCTION);  // FIX : messy, why do we have compilerFor if we separate this anyhow? Maybe subclass Compiler instead?
	assert(functionName != 0);
	p = b;
	this->e = e;
	expectToken("(", true);
	white();
	Table& nameIndexes = code->nameIndexes;
	Vector<const String*>& argumentNames = code->argumentNames;
	while (!token(")", false)) {
		if (eof()) {
			error(SYNTAX_ERROR, argumentNames.size() == 0 ? "Expected ')'" : "Expected ',' or ')'");
		}
		if (argumentNames.size() != 0) {
			if (!token(",", false)) {
				error(SYNTAX_ERROR, "Expected ',' or ')'");
			}
			white();
		}
		const String* name = identifier(true, false);
		nameIndexes.update(nameIndexes.insert(name), static_cast<Int32>(argumentNames.size()));
		argumentNames.push(name);
		code->bloomSet |= name->createBloomCode();
		white();
	}
	expectToken("{", true);
	compile(p, e); // FIX: ugly as it sets p and e again, although it doesn't hurt
	expectToken("}", false);
	code->name = functionName;
	code->selfName = selfName;
	code->source = String::concatenate(heap, String(heap.roots(), FUNCTION_SPACE, *functionName), String(heap.roots(), b, p));
	code->bloomSet |= (selfName != 0 ? selfName->createBloomCode() : 0) | ARGUMENTS_STRING.createBloomCode();
	return p;
}

void Compiler::compile(const String& source) {
	compile(source.begin(), source.end());
	white();
	if (!eof()) {
		error(SYNTAX_ERROR, "Syntax error"); // FIX : better error?
	}
}

void Compiler::getStopPosition(UInt32& offset, UInt32& lineNumber, UInt32& columnNumber) const {
	offset = static_cast<UInt32>(p - b);
	code->getSourceUnit()->computeLineColumn(offset, lineNumber, columnNumber);
}

/* --- Runtime --- */

struct Runtime::ErrorPrototype : public Error {
	typedef Error super;
	ErrorPrototype(GCList& gcList, ErrorType type);
	virtual Object* getPrototype(Runtime& rt) const;
};


Runtime::ErrorPrototype::ErrorPrototype(GCList& gcList, ErrorType type) : super(gcList, type) { }

Object* Runtime::ErrorPrototype::getPrototype(Runtime& rt) const {
	return (errorType == GENERIC_ERROR ? rt.getObjectPrototype() : rt.getErrorPrototype(GENERIC_ERROR));
}

Runtime::GlobalScope::GlobalScope(GCList& gcList) : super(gcList, 0) { deleteOnPop = false; }

Flags Runtime::GlobalScope::readVar(Runtime& rt, const String* name, Value* v) const {
	return rt.getGlobalObject()->getProperty(rt, name, v);
}

void Runtime::GlobalScope::writeVar(Runtime& rt, const String* name, const Value& v) {
	rt.getGlobalObject()->setProperty(rt, name, v);
}

bool Runtime::GlobalScope::deleteVar(Runtime& rt, const String* name) {
	return rt.getGlobalObject()->deleteOwnProperty(rt, name);
}

void Runtime::GlobalScope::declareVar(Runtime& rt, const String* name, const Value& initValue, bool dontDelete) {
	Value v;
	// FIX : deep or not?!
	const Flags flags = rt.getGlobalObject()->getOwnProperty(rt, name, &v);
	if (flags == NONEXISTENT || !initValue.isUndefined()) {
		rt.getGlobalObject()->setOwnProperty(rt, name, initValue, dontDelete ? DONT_DELETE_FLAG : 0);
	}
}

struct Runtime::FunctionPrototypeFunction : public ExtensibleFunction {
	typedef ExtensibleFunction super;
	FunctionPrototypeFunction(GCList& gcList) : super(gcList) { }
	virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject);
	virtual Value construct(Runtime& rt, Processor&, UInt32, const Value*, Object*);
	virtual Object* getPrototype(Runtime& rt) const;
	virtual void constructCompleteObject(Runtime& rt) const;
};

Value Runtime::FunctionPrototypeFunction::invoke(Runtime&, Processor&, UInt32, const Value*, Object*) {
	return UNDEFINED_VALUE;
}

Value Runtime::FunctionPrototypeFunction::construct(Runtime& rt, Processor&, UInt32, const Value*, Object*) {
	ScriptException::throwError(rt.getHeap(), TYPE_ERROR, "Function.prototype is not a constructor");
	return UNDEFINED_VALUE;
}

Object* Runtime::FunctionPrototypeFunction::getPrototype(Runtime& rt) const { return rt.getObjectPrototype(); }

void Runtime::FunctionPrototypeFunction::constructCompleteObject(Runtime& rt) const {
	completeObject->setOwnProperty(rt, &NAME_STRING, &EMPTY_STRING, DONT_ENUM_FLAG);
	completeObject->setOwnProperty(rt, &LENGTH_STRING, Value(0.0), HIDDEN_CONST_FLAGS);
}

struct SeparateConstructorFunction : public ExtensibleFunction {
	typedef ExtensibleFunction super;
	SeparateConstructorFunction(GCList& gcList, Function* regularFunction, Function* constructorFunction);
	virtual Value invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject);
	virtual Value construct(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject);
	virtual const Code* getScriptCode() const { return regularFunction->getScriptCode(); }

	virtual void constructCompleteObject(Runtime& rt) const;
	Function* const regularFunction;
	Function* const constructorFunction;

	virtual void gcMarkReferences(Heap& heap) const {
		gcMark(heap, regularFunction);
		gcMark(heap, constructorFunction);
		super::gcMarkReferences(heap);
	}
};

SeparateConstructorFunction::SeparateConstructorFunction(GCList& gcList, Function* regularFunction, Function* constructorFunction)
		: super(gcList), regularFunction(regularFunction), constructorFunction(constructorFunction) {
	assert(regularFunction != 0);
}

Value SeparateConstructorFunction::invoke(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
	return regularFunction->invoke(rt, processor, argc, argv, thisObject);
}

Value SeparateConstructorFunction::construct(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object* thisObject) {
	if (constructorFunction == 0) {
		Value v;
		regularFunction->getProperty(rt, &NAME_STRING, &v);
		Heap& heap = rt.getHeap();
		ScriptException::throwError(heap, TYPE_ERROR, String::concatenate(heap, *v.toString(heap), " is not a constructor"));
	}
	return constructorFunction->invoke(rt, processor, argc, argv, thisObject);
}

void SeparateConstructorFunction::constructCompleteObject(Runtime& rt) const {
	Object* templateFunction = regularFunction;
	if (constructorFunction != 0) {
		createPrototypeObject(rt, completeObject, true);
		templateFunction = constructorFunction;
	}
	Value v;
	Flags flags = templateFunction->getProperty(rt, &NAME_STRING, &v);
	if (flags != NONEXISTENT) { // FIX : getName?
		completeObject->setOwnProperty(rt, &NAME_STRING, v, flags);
	}
	flags = templateFunction->getProperty(rt, &LENGTH_STRING, &v);
	if (flags != NONEXISTENT) { // FIX : in the future I think we should have a getLength
		completeObject->setOwnProperty(rt, &LENGTH_STRING, v, flags);
	}
}

template<class F> struct UnaryMathFunction : public Function {
	UnaryMathFunction(F& f) : f(f) { }
	virtual Value invoke(Runtime&, Processor&, UInt32 argc, const Value* argv, Object*) {
		return (argc >= 1 ? Value(f(argv[0].toDouble())) : NAN_VALUE);
	}
	F& f;
};

struct Support {
	static Value getInternalProperty(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 2) {
			Object* o = argv[0].asObject();
			if (o != 0) {
				const String* s = argv[1].toString(rt.getHeap());
				if (s->isEqualTo(PROTOTYPE_STRING)) {
					Object* p = o->getPrototype(rt);
					return (p == 0 ? NULL_VALUE : Value(p));
				} else if (s->isEqualTo(CLASS_STRING)) {
					return o->getClassName();
				} else if (s->isEqualTo(VALUE_STRING)) {
					return o->getInternalValue(rt.getHeap());
				}
			}
		}
		return UNDEFINED_VALUE;
	}
	
	static Value updateDateValue(Runtime&, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 2) {
			Object* o = argv[0].asObject();
			if (o != 0 && o->getClassName()->isEqualTo(D_ATE_STRING)) {
				assert(dynamic_cast<GenericWrapper*>(o) != 0);
				reinterpret_cast<GenericWrapper*>(o)->setInternalValue(argv[1]);
			}
		}
		return UNDEFINED_VALUE;
	}

	static Value createWrapper(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 2) {
			Heap& heap = rt.getHeap();
			const String* const className = argv[0].toString(heap);
			const Value internalValue = argv[1];
			Object* prototype = (argc >= 3 ? argv[2].toObjectOrNull(heap, false) : 0);
			if (prototype == 0) {
				prototype = rt.getObjectPrototype();
			}
			if (className->isEqualTo(S_TRING_STRING)) {
				assert(prototype == rt.getPrototypeObject(Runtime::STRING_PROTOTYPE));
				return new(heap) StringWrapper(heap.managed(), internalValue.toString(heap));
			} else if (className->isEqualTo(E_RROR_STRING)) {
				for (int i = 0; i < ERROR_TYPE_COUNT; ++i) {
					if (prototype == rt.getErrorPrototype(static_cast<ErrorType>(i))) {
						return new(heap) Error(heap.managed(), static_cast<ErrorType>(i));
					}
				}
			}
			return new(heap) GenericWrapper(heap.managed(), className, internalValue, Runtime::ARBITRARY_PROTOTYPE, prototype);
		}
		return UNDEFINED_VALUE;
	}

	static Value distinctConstructor(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 1) {
			Function* regularFunction = argv[0].asFunction();
			if (regularFunction != 0) {
				Function* constructorFunction = (argc >= 2 ? argv[1].asFunction() : 0);
				Heap& heap = rt.getHeap();
				return new(heap) SeparateConstructorFunction(heap.managed(), regularFunction, constructorFunction);
			}
		}
		return UNDEFINED_VALUE;
	}

	static Value defineProperty(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		bool success = false;
		if (argc >= 2) {
			Object* o = argv[0].asObject();
			if (o != 0) {
				success = o->setOwnProperty(rt, argv[1], (argc >= 3 ? argv[2] : UNDEFINED_VALUE)
						, (argc >= 4 && argv[3].toBool() ? READ_ONLY_FLAG : 0)
						| (argc >= 5 && argv[4].toBool() ? DONT_ENUM_FLAG : 0)
						| (argc >= 6 && argv[5].toBool() ? DONT_DELETE_FLAG : 0)
						| EXISTS_FLAG);
			}
		}
		return success;
	}

	static Value compileFunction(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 1) {
			Heap& heap = rt.getHeap();
			const String* source = argv[0].toString(heap);
			SourceCodeUnit* unit = new(heap) SourceCodeUnit(heap.managed(), source, &ANONYMOUS_SCRIPT_STRING);
			Code* code = new(heap) Code(heap.managed(), 0, unit);
			Compiler compiler(heap.roots(), code, Compiler::FOR_FUNCTION);
			compiler.compileFunction(source->begin(), source->end()
					, (argc >= 2 ? argv[1].toString(heap) : &ANONYMOUS_STRING), 0);
			return new(heap) JSFunction(heap.managed(), code, rt.getGlobalScope());
		}
		return UNDEFINED_VALUE;
	}

	static Value callWithArgs(Runtime& rt, Processor& processor, UInt32 argc, const Value* argv, Object*) {
		Heap& heap = rt.getHeap();
		Function* callFunction = (argc > 0 ? argv[0].asFunction() : 0);
		if (callFunction == 0) {
			ScriptException::throwError(heap, TYPE_ERROR, "apply / call used on non-function");
			return Value();
		} else {
			Object* newThis = (argc > 1 ? argv[1].toObjectOrNull(heap, true) : 0);
			Vector<Value> args(0, &heap);
			Object* arrayObject = (argc > 2 ? argv[2].toObjectOrNull(heap, false) : 0);
			if (arrayObject != 0) {
				Value v;
				if (arrayObject->getProperty(rt, &LENGTH_STRING, &v) != NONEXISTENT) { // FIX : in the future I think we should have a virtual getLength
					Int32 offset = (argc > 3 ? argv[3].toInt() : 0);
					UInt32 length = static_cast<UInt32>(std::max(v.toInt() - offset, 0));
					args = Vector<Value>(length, &heap);
					for (UInt32 i = 0; i < length; ++i) {
						args[i] = UNDEFINED_VALUE;
						arrayObject->getProperty(rt, i + offset, &args[i]);
					}
				}
			}
			// FIX : we copy all arguments once to argv, and then chain will copy them again to a scope object, couldn't we short-cut that somehow?
			// FIX : since only arrays and arguments are really valid here, perhaps even have a new virtual in object for implementing efficient apply with these?
			return callFunction->invoke(rt, processor, args.size(), args.begin(), newThis);
		}
	}
	
	static Value hasOwnProperty(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		Object* object = (argc >= 2 ? argv[0].toObjectOrNull(rt.getHeap(), false) : 0);
		return (object != 0 ? object->hasOwnProperty(rt, argv[1]) : false);
	}

	static Value isPropertyEnumerable(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		Object* object = (argc >= 2 ? argv[0].toObjectOrNull(rt.getHeap(), false) : 0);
		return (object != 0 ? object->isOwnPropertyEnumerable(rt, argv[1]) : false);
	}

	static Value fromCharCode(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		Char c = static_cast<Char>(argc > 0 ? argv[0].toInt() & 0xFFFF : 0);
		Heap& heap = rt.getHeap();
		return ((0 <= c && c < 127) ? QUICK_CONSTANTS.ascii[c] : Value(new(heap) String(heap.managed(), &c, &c + 1)));
	}

	static Value atan2(Runtime&, Processor&, UInt32 argc, const Value* argv, Object*) {
		return (argc >= 2 ? Value(std::atan2(argv[0].toDouble(), argv[1].toDouble())) : NAN_VALUE);
	}

	static Value pow(Runtime&, Processor&, UInt32 argc, const Value* argv, Object*) {
		return (argc >= 2 ? Value(std::pow(argv[0].toDouble(), argv[1].toDouble())) : NAN_VALUE);
	}

	static Value parseFloat(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 1) {
			const String* s = argv[0].toString(rt.getHeap());
			const Char* e = s->end();
			const Char* b = eatStringWhite(s->begin(), e);
			double d = 0.0;
			if (parseDouble(b, e, d) != b) {
				return d;
			}
		}
		return NAN_VALUE;
	}

	static Value charCodeAt(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 2) {
			const String* s = argv[0].toString(rt.getHeap());
			const double d = nanToZero(argv[1].toDouble());
			if (-1.0 < d && d < s->size()) {
				return Value((*s)[static_cast<UInt32>(d)]);
			}
		}
		return NAN_VALUE;
	}

	static Value substring(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 3) {
			const String* s = argv[0].toString(rt.getHeap());
			const double l = s->size();
			const double from = std::max(nanToZero(argv[1].toDouble()), 0.0);
			const double to = std::min(nanToZero(argv[2].toDouble()), l);
			if (from < to && from < l && to >= 1.0) {
				const Char* b = s->begin();
				Heap& heap = rt.getHeap();
				return (from < 1.0 && to >= l ? s
						: new(heap) String(heap.managed(), b + static_cast<UInt32>(from), b + static_cast<UInt32>(to)));
			}
		}
		return &EMPTY_STRING;
	}

	static Value submatch(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		if (argc >= 3) {
			const String* const text = argv[0].toString(rt.getHeap());
			const Int32 offset = argv[1].toInt();
			const String* const match = argv[2].toString(rt.getHeap());
			const UInt32 matchSize = match->size();
			if (offset + matchSize <= text->size()
					&& std::equal(text->begin() + offset, text->begin() + offset + matchSize, match->begin())) {
				return true;
			}
		}
		return false;
	}

	static Value getCurrentTime(Runtime& rt, Processor&, UInt32, const Value*, Object*) {
		return rt.getCurrentEpochTime();
	}

	static Value localTimeDifference(Runtime& rt, Processor&, UInt32 argc, const Value* argv, Object*) {
		(void)argc;
		std::time_t t;
		const double dt = (argv[0].toDouble() - rt.unixEpochTimeDiff) / 1000.0 + 0.5;
		if (dt < 0.0) { // MSVC can crash on conversion although documentation says it should return null.
			return NAN_VALUE;
		}
		t = static_cast<std::time_t>(dt);
		const struct std::tm localTM = *std::localtime(&t);
		const struct std::tm utcTM = *std::gmtime(&t);
		struct std::tm newTM;
		std::memset(&newTM, 0, sizeof (newTM));
		newTM.tm_year = utcTM.tm_year;
		newTM.tm_mon = utcTM.tm_mon;
		newTM.tm_mday = utcTM.tm_mday;
		newTM.tm_hour = utcTM.tm_hour;
		newTM.tm_min = utcTM.tm_min;
		newTM.tm_sec = utcTM.tm_sec;
		newTM.tm_isdst = localTM.tm_isdst;
		const std::time_t newTime = std::mktime(&newTM);
		return (newTime == -1 ? NAN_VALUE : Value(t * 1000.0 - newTime * 1000.0));
	}
	
	static Value random(Runtime&, Processor&, UInt32, const Value*, Object*) {
		return rand() / (RAND_MAX + 1.0);
	}
};

static struct {
	String name;
	UnaryMathFunction<double (double)> func;
} NATIVE_MATH_FUNCTIONS[] = {
	{ "acos", std::acos }, { "asin", std::asin }, { "atan", std::atan }, { "cos", std::cos }, { "exp", std::exp }
	, { "floor", std::floor }, { "log", std::log }, { "sin", std::sin }, { "sqrt", std::sqrt }, { "tan", std::tan }
};

static struct {
	String name;
	FunctorAdapter<NativeFunction> func;
} SUPPORT_FUNCTIONS[] = {
	{ "getInternalProperty", Support::getInternalProperty }, { "createWrapper", Support::createWrapper },
	{ "defineProperty", Support::defineProperty }, { "compileFunction", Support::compileFunction },
	{ "distinctConstructor", Support::distinctConstructor }, { "callWithArgs", Support::callWithArgs },
	{ "hasOwnProperty", Support::hasOwnProperty }, { "fromCharCode", Support::fromCharCode },
	{ "isPropertyEnumerable", Support::isPropertyEnumerable }, { "atan2", Support::atan2 },
	{ "pow", Support::pow }, { "parseFloat", Support::parseFloat }, { "charCodeAt", Support::charCodeAt },
	{ "substring", Support::substring }, { "submatch", Support::submatch },
	{ "getCurrentTime", Support::getCurrentTime }, { "localTimeDifference", Support::localTimeDifference },
	{ "random", Support::random }, { "updateDateValue", Support::updateDateValue }
};

static UnaryMathFunction<bool (double)> IS_NAN_FUNCTION(isNaN);
static UnaryMathFunction<bool (double)> IS_FINITE_FUNCTION(isFinite);

static struct DefaultConversion : public Function {
	virtual Value invoke(Runtime&, Processor&, UInt32, const Value* argv, Object*) {
		return argv[0];
	}
} DEFAULT_CONVERSION;

static struct NoRegExpSupport : public Function {
	virtual Value invoke(Runtime& rt, Processor&, UInt32, const Value*, Object*) {
		ScriptException::throwError(rt.getHeap(), REFERENCE_ERROR, "No RegExp support");
		return UNDEFINED_VALUE;
	}
} NO_REG_EXP_SUPPORT;

Runtime::Runtime(Heap& heap) : super(heap.roots()), heap(heap), globalScope(heap.roots()), globalObject(0)
		, stackSize(STANDARD_JS_STACK_SIZE), callNestCounter(0), checkTimeOutCounter(0)
		, timeOut(0), memoryCap(MAX_MEMORY_CAP), gcThreshold(AUTO_GC_MIN_SIZE), createRegExpFunction(&NO_REG_EXP_SUPPORT)
		, evalFunction(&EVAL_FUNCTION), unixEpochTimeDiff(0.0), evalCodeCache(&heap) {
	std::fill(stringConstantsCache, stringConstantsCache + (1 << STRING_CONSTANTS_CACHE_SIZE_N), (const String*)(0));
	std::fill(prototypes, prototypes + PROTOTYPE_COUNT, (Object*)(0));
	std::fill(toPrimitiveFunctions + 0, toPrimitiveFunctions + 3, &DEFAULT_CONVERSION);
	Object* objectProto = new(heap) JSObject(heap.managed(), 0);
	prototypes[OBJECT_PROTOTYPE] = objectProto;
	prototypes[FUNCTION_PROTOTYPE] = new(heap) FunctionPrototypeFunction(heap.managed());
	prototypes[BOOLEAN_PROTOTYPE] = new(heap) GenericWrapper(heap.managed(), &B_OOLEAN_STRING, false, OBJECT_PROTOTYPE);
	prototypes[NUMBER_PROTOTYPE] = new(heap) GenericWrapper(heap.managed(), &N_UMBER_STRING, 0.0, OBJECT_PROTOTYPE);
	prototypes[STRING_PROTOTYPE] = new(heap) StringWrapper(heap.managed(), &EMPTY_STRING);
	prototypes[DATE_PROTOTYPE] = new(heap) GenericWrapper(heap.managed(), &D_ATE_STRING, NaN(), OBJECT_PROTOTYPE);
	prototypes[ARRAY_PROTOTYPE] = new(heap) JSArray(heap.managed());
	for (int i = 0; i < ERROR_TYPE_COUNT; ++i) {
		prototypes[FIRST_ERROR_PROTOTYPE + i] = new(heap) ErrorPrototype(heap.managed(), static_cast<ErrorType>(i));
	}
	globalObject = new(heap) JSObject(heap.managed(), objectProto);
}

void Runtime::autoGC(bool checkOutOfMemory) {
	if (heap.size() >= gcThreshold) {
		heap.drain();
		heap.gc();
		const size_t inUse = heap.size() - heap.pooled();
		gcThreshold = std::min(std::max(inUse * AUTO_GC_GROWTH_FACTOR, AUTO_GC_MIN_SIZE), memoryCap);
		checkTimeOutCounter = std::min(checkTimeOutCounter, 1U);
		if (checkOutOfMemory && heap.size() >= memoryCap) {
			heap.drain(); // release pool first and check again
			if (heap.size() >= memoryCap) {
				throw ConstStringException("Out of memory");
			}
		}
	}
}

// Handle wrapping if clock_t is an integer type but not if it is a double.
template<typename T> static bool clockExceeds(T a, T b) { return wrapToInt32(static_cast<UInt32>(a) - static_cast<UInt32>(b)) >= 0; }
static bool clockExceeds(double a, double b) { return a >= b; }

void Runtime::checkTimeOut() {
	if (checkTimeOutCounter != 0 && --checkTimeOutCounter == 0) {
		checkTimeOutCounter = CHECK_TIMEOUT_INTERVAL;
		if (clockExceeds(clock(), timeOut)) {
			throw ConstStringException("Time out");
		}
	}
}

Var Runtime::runUntilReturn(Processor& processor) {
	while (processor.run(STANDARD_CYCLES_BETWEEN_AUTO_GC)) {
		autoGC(true);
		checkTimeOut();
	}
	return Var(*this, processor.getResult());
}

Object* Runtime::getPrototypeObject(PrototypeId prototype) const {
	assert(0 <= prototype && prototype < PROTOTYPE_COUNT);
	return prototypes[prototype];
}

Object* Runtime::getErrorPrototype(ErrorType error) const {
	assert(0 <= error && error < ERROR_TYPE_COUNT);
	return prototypes[static_cast<PrototypeId>(FIRST_ERROR_PROTOTYPE + error)];
}

JSObject* Runtime::newJSObject() const { return new(heap) JSObject(heap.managed(), getObjectPrototype()); }
JSArray* Runtime::newJSArray(UInt32 initialLength) const { return new(heap) JSArray(heap.managed(), initialLength); }
Var Runtime::getGlobalsVar() { return Var(*this, globalObject); }
Var Runtime::newObjectVar() { return Var(*this, newJSObject()); }
Var Runtime::newArrayVar(UInt32 initialLength) { return Var(*this, newJSArray(initialLength)); }

const String* Runtime::newStringConstantWithHash(UInt32 hash, const char* s) {
	const String* cached = stringConstantsCache[hash];
	size_t length;
	if (cached != 0) {
		const char* p0 = s;
		const Char* p1 = cached->begin();
		const Char* e = cached->end();
		while (*p0 != 0 && p1 != e && *p0 == *p1) {
			++p0;
			++p1;
		}
		if (*p0 == 0 && p1 == e) {
			return cached;
		}
		length = p0 - s + strlen(p0);
	} else {
		length = strlen(s);
	}
	const String* string = new(heap) String(heap.managed(), s, s + length);
	if (length < STRING_CONSTANTS_CACHE_MAX_LENGTH) {
		stringConstantsCache[hash] = string;
	}
	return string;
}

Var Runtime::call(Function* function, UInt32 argc, const Value* argv, Object* thisObject) {
	if (callNestCounter >= MAX_CROSS_CALL_RECURSION) {
		ScriptException::throwError(heap, RANGE_ERROR, &STACK_OVERFLOW_STRING); // FIX : other string?
	}
	struct NestCounter {
		NestCounter(Runtime& rt) : rt(rt) { ++rt.callNestCounter; };
		~NestCounter() { --rt.callNestCounter; };
		Runtime& rt;
	} nestCounter(*this);
	Processor processor(*this);
	processor.invokeFunction(function, argc, argv, thisObject);
	return runUntilReturn(processor);
}

void Runtime::run(const String& source, const String* filename) {
	Processor processor(*this);
	processor.enterGlobalCode(compileGlobalCode(source, filename));
	runUntilReturn(processor);
}

Code* Runtime::compileEvalCode(const String* expression) {
	const Table::Bucket* bucket = evalCodeCache.lookup(expression);
	if (bucket != 0) {
		Object* o = bucket->getValue().getObject();
		assert(dynamic_cast<Code*>(o) != 0);
		return reinterpret_cast<Code*>(o);
	} else {
		SourceCodeUnit* unit = new(heap) SourceCodeUnit(heap.managed(), expression, &EVAL_CODE_STRING);
		Code* code = new(heap) Code(heap.managed(), 0, unit);
		Compiler compiler(heap.roots(), code, Compiler::FOR_EVAL);
		compiler.compile(*expression);
		evalCodeCache.update(evalCodeCache.insert(expression), code);
		return code;
	}
}

Var Runtime::eval(const String& expression) {
	Processor processor(*this);
	const String* retainedExpression = (heap.managed().owns(&expression)
			? &expression : new(heap) String(heap.managed(), expression.begin(), expression.end()));
	processor.enterEvalCode(compileEvalCode(retainedExpression));
	return runUntilReturn(processor);
}

Code* Runtime::compileGlobalCode(const String& source, const String* filename) {
	const String* effectiveFileName = (filename != 0 ? filename : &ANONYMOUS_SCRIPT_STRING);
	const String* retainedSource = (heap.managed().owns(&source)
			? &source : new(heap) String(heap.managed(), source.begin(), source.end()));
	SourceCodeUnit* unit = new(heap) SourceCodeUnit(heap.managed(), retainedSource, effectiveFileName);
	Code* code = new(heap) Code(heap.managed(), 0, unit);
	Compiler compiler(heap.roots(), code, Compiler::FOR_GLOBAL);
	try {
		compiler.compile(*retainedSource);
	}
	catch (const ScriptException& x) {
		throw CompilationError(x, effectiveFileName, compiler);
	}
	return code;
}

void Runtime::fetchFunction(const Object* supportObject, const char* name, Function** f) {
	Value v;
	if (supportObject->getProperty(*this, newStringConstant(name), &v) != NONEXISTENT
			&& v.asFunction() != 0) {
		*f = v.asFunction();
	}
}

extern const char* STDLIB_JS;

double Runtime::getCurrentEpochTime() {
	std::time_t t;
	std::time(&t);
	return t * 1000.0 + unixEpochTimeDiff;
}

void Runtime::setupStandardLibrary() {
	{	// time() is not guaranteed to return utc time, so we perform some conversions to find the actual implementation
		struct std::tm refTM;
		std::memset(&refTM, 0, sizeof (refTM));
		refTM.tm_year = 80;
		refTM.tm_mday = 1;
		const std::time_t refTime = std::mktime(&refTM);
		const std::time_t refTimeAsUTC = std::mktime(std::gmtime(&refTime));
		assert(refTime != -1 && refTimeAsUTC != -1);
		unixEpochTimeDiff = 315532800000.0 - refTime * 2000.0 + refTimeAsUTC * 1000.0;
	}
	
	JSObject* supportObject = new(heap) JSObject(heap.managed(), getObjectPrototype());
	JSObject* prototypesObject = new(heap) JSObject(heap.managed(), getObjectPrototype());
	const Var protectedSupportObject(*this, supportObject);
	supportObject->setOwnProperty(*this, &PROTOTYPES_STRING, prototypesObject);
	for (size_t i = 0; i < sizeof (SUPPORT_FUNCTIONS) / sizeof (*SUPPORT_FUNCTIONS); ++i) {
		supportObject->setOwnProperty(*this, &SUPPORT_FUNCTIONS[i].name, &SUPPORT_FUNCTIONS[i].func);
	}
	supportObject->setOwnProperty(*this, &IS_NAN_STRING, &IS_NAN_FUNCTION);
	supportObject->setOwnProperty(*this, &IS_FINITE_STRING, &IS_FINITE_FUNCTION);
	supportObject->setOwnProperty(*this, &EVAL_STRING, &EVAL_FUNCTION);
	supportObject->setOwnProperty(*this, &UNDEFINED_STRING, UNDEFINED_VALUE);
	supportObject->setOwnProperty(*this, &NAN_STRING, NAN_VALUE);
	supportObject->setOwnProperty(*this, &INFINITY_STRING, INFINITY_VALUE);
	supportObject->setOwnProperty(*this, &MAX_NUMBER_STRING, std::numeric_limits<double>::max());
	supportObject->setOwnProperty(*this, &MIN_NUMBER_STRING, std::numeric_limits<double>::denorm_min());

	for (size_t i = 0; i < sizeof (NATIVE_MATH_FUNCTIONS) / sizeof (*NATIVE_MATH_FUNCTIONS); ++i) {
		supportObject->setOwnProperty(*this, &NATIVE_MATH_FUNCTIONS[i].name, &NATIVE_MATH_FUNCTIONS[i].func);
	}
	
	for (int i = 0; i < PROTOTYPE_COUNT; ++i) {
		prototypesObject->setOwnProperty(*this, PROTOTYPE_NAMES[i], prototypes[i]);
	}
	
	const Var func = eval(*String::allocate(heap, STDLIB_JS));
	Value argv[1] = { protectedSupportObject };
	call(func, 1, argv);
	
	fetchFunction(supportObject, "toPrimitive", toPrimitiveFunctions + 0);
	fetchFunction(supportObject, "toPrimitiveNumber", toPrimitiveFunctions + 1);
	fetchFunction(supportObject, "toPrimitiveString", toPrimitiveFunctions + 2);
	fetchFunction(supportObject, "createRegExp", &createRegExpFunction);
	fetchFunction(supportObject, "evalFunction", &evalFunction);

	heap.gc();
}

void Runtime::setGlobalObject(Object* newGlobals) { globalObject = newGlobals; }
void Runtime::setStackSize(UInt32 maxValuesOnStack) { stackSize = maxValuesOnStack; }
void Runtime::setMemoryCap(size_t maxBytesUsed) { memoryCap = maxBytesUsed; }
void Runtime::noTimeOut() { checkTimeOutCounter = 0; }

void Runtime::resetTimeOut(Int32 timeOutSeconds) {
	timeOut = clock() + timeOutSeconds * CLOCKS_PER_SEC;
	checkTimeOutCounter = CHECK_TIMEOUT_INTERVAL;
}

} /* namespace NuXJS */

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC pop_options
#endif
#endif

#ifdef _MSC_VER
#pragma float_control(pop)  
#endif
