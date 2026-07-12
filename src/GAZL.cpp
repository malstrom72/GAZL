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

// TODO : persistant data storage by creating a new source by feeding the assembler code and replacing all globals with the current data, alternatively just outputting the globals and make it possible to merge them with code... need to think about this....

#include "GAZL.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <iostream>

#define STR(x) x

#ifdef __GNUC__
#ifndef __clang__
#pragma GCC push_options
#pragma GCC optimize ("no-finite-math-only")
#endif
// older gcc might need this too:
// #pragma GCC optimize ("float-store")
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
};

// --- defined integer / FTOI semantics, shared by Processor::run() and calcConstant() so the
// run-time and constant-folded paths can never diverge. These are the chosen normative results
// (two's-complement wrap / count-mod-32 shifts / saturating FTOI -- the AArch64 & WebAssembly
// choices), identical on every target. add/sub/mul and the shifts lower to a single native
// instruction; idiv/imod/ftoi keep a real edge-case branch on purpose (see docs/PortabilityAudit.md).
inline int absolute(int i) { Int x = i >> (Int)(sizeof (Int) * 8 - 1); return (Int)(((UInt)i ^ (UInt)x) - (UInt)x); } // INT_MIN -> INT_MIN
inline float absolute(float f) { return fabsf(f); }
inline double absolute(double f) { return fabs(f); }
inline Int idiv(Int a, Int b) { return (b == -1) ? (Int)(0u - (UInt)a) : a / b; }	// caller guarantees b != 0; INT_MIN / -1 -> INT_MIN
inline Int imod(Int a, Int b) { return (b == -1) ? 0 : a % b; }						// caller guarantees b != 0; INT_MIN % -1 -> 0
inline Int iadd(Int a, Int b) { return (Int)((UInt)a + (UInt)b); }					// two's-complement wrap
inline Int isub(Int a, Int b) { return (Int)((UInt)a - (UInt)b); }					// two's-complement wrap
inline Int imul(Int a, Int b) { return (Int)((UInt)a * (UInt)b); }					// two's-complement wrap
inline Int ishl(Int a, Int n) { return (Int)((UInt)a << (n & 31)); }				// count mod 32; no neg-shift / overflow UB
inline Int ashr(Int a, Int n) { return a >> (n & 31); }								// count mod 32; arithmetic (sign-extending)
inline Int lshr(Int a, Int n) { return (Int)((UInt)a >> (n & 31)); }				// count mod 32; logical (zero-fill)
inline Int ftoi(Float v) {															// v is the ALREADY-SCALED float
	// Bit-pattern classification (no float comparisons) so the saturation/NaN handling survives
	// /fp:fast & -ffast-math -- those imply finite-math-only, under which a `v != v` / `v >= 2^31`
	// test is assumed unreachable and deleted, letting the out-of-range cast UB reappear.
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
		case '\'':	while (++p < e && *p != '\'') i = (i << 8) | (*p & 0xFFU); if (p < e) ++p; break;
		case '\"':	while (++p < e && *p != '\"') i = (i << 8) | (*p & 0xFFU); if (p < e) ++p; break;
		case '0':	if (*++p == 'x') {
						while (++p < e && ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'F') || (*p >= 'a' && *p <= 'f')))
							i = (i << 4) + (*p <= '9' ? *p - '0' : (*p & ~0x20) - ('A' - 10));
						break;
					} /* else continue */
		default:	for (; p < e && *p >= '0' && *p <= '9'; ++p) i = i * 10 + (*p - '0'); break;
	}
	return (Int)(i) * sign;
}

template<class F> F pow10(F x) { return pow(10, x); }
static float pow10(float x) { return powf(10.0f, x); }

static Float stringToFloat(const Char* &p, const Char* e) {
	/*
		Accumulate in double, convert to Float once at the end. Accumulating directly in float32
		diverges across compilers: `d * 10 + digit` loses precision at each step near the float
		range limit, and clang contracts it to a single-rounded fmadd while MSVC emits mul+add
		(two roundings) -- so e.g. "2147483647.0" parsed to 2^31 on one and 2^31-128 on the other.
		A <=15-digit integer is exact in double (so contraction cannot change it), and the single
		closing double->float conversion is correctly rounded and identical on every target.
	*/
	double d = 0;
	double sign = 1;
	switch (p < e ? *p : 0) {
		case '+': ++p; sign = 1; break;
		case '-': ++p; sign = -1; break;
	}
	if (p < e && *p >= '0' && *p <= '9') {
		do {
			d = d * 10 + (*p - '0');
		} while (++p < e && *p >= '0' && *p <= '9');
		if (p + 1 < e && *p == '.' && p[1] >= '0' && p[1] <= '9') {
			++p;
			double f = 1;
			do {
				d += (*p - '0') * (f *= 0.1);
			} while (++p < e && *p >= '0' && *p <= '9');
		}
		if (p + 1 < e && (*p == 'E' || *p == 'e')) {
			d *= pow10((double)(stringToInt(++p, e)));
		}
	}
	return static_cast<Float>(d * sign);
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

const Int FIRST_OPCODE_VALUE = 0x2345;
const Char* NULL_STRING = STR("NULL");
const Char* GAZL_VERSION_STRING = STR("GAZL_VERSION");
const Char* GAZL_WORD_SIZE_STRING = STR("GAZL_WORD_SIZE");
const Char* GAZL_MEMORY_SIZE_STRING = STR("GAZL_MEMORY_SIZE");

enum Opcode {
	FUNC_CC_ = FIRST_OPCODE_VALUE, CALL_VVC, CALL_CVC, CALL_NVC, RETU_C__
	, MOVE_VV_, MOVE_VC_
	, PEEK_VC_, POKE_CV_, POKE_CC_
	, PEEK_VVV, PEEK_VCV, POKE_VVV, POKE_CVV, POKE_VVC, POKE_CVC
	, GETL_VVV, SETL_VVV, SETL_VVC, ADRL_VV_
	, ABSI_VV_
	, ADDI_VVV, ADDI_VVC, SUBI_VVV, SUBI_VVC, SUBI_VCV
	, MULI_VVV, MULI_VVC, DIVI_VVV, DIVI_VVC, DIVI_VCV, MODI_VVV, MODI_VVC, MODI_VCV
	, ANDI_VVV, ANDI_VVC, IORI_VVV, IORI_VVC, XORI_VVV, XORI_VVC
	, SHLI_VVV, SHLI_VVC, SHLI_VCV, SHRI_VVV, SHRI_VVC, SHRI_VCV, SHRU_VVV, SHRU_VVC, SHRU_VCV
	, ABSF_VV_
	, FLOF_VV_
	, ADDF_VVV, ADDF_VVC, SUBF_VVV, SUBF_VVC, SUBF_VCV, MULF_VVV, MULF_VVC, DIVF_VVV, DIVF_VVC, DIVF_VCV
	, FTOI_VVC, ITOF_VVC
	, COPY_VVC, COPY_VCC, COPY_CVC, COPY_CCC
	, FORi_VVB, FORi_VCB
	, LSSI_VVB, LSSI_VCB, LSSI_CVB, EQUI_VVB, EQUI_VCB
	, NLSI_VVB, NLSI_VCB, NLSI_CVB, NEQI_VVB, NEQI_VCB
	, LSSF_VVB, LSSF_VCB, LSSF_CVB, EQUF_VVB, EQUF_VCB
	, NLSF_VVB, NLSF_VCB, NLSF_CVB, NEQF_VVB, NEQF_VCB
	, GOTO_B__, SWCH_VCC

	, NOOP____, GLOB____, CNST____, DATA____, LOCA____, OUTP____
	
	, MOVE_CC_
	, ABSI_CC_
	, ADDI_CCC, SUBI_CCC, MULI_CCC, DIVI_CCC, MODI_CCC, ANDI_CCC, IORI_CCC, XORI_CCC, SHLI_CCC, SHRI_CCC, SHRU_CCC
	, ABSF_CC_
	, FLOF_CC_
	, ADDF_CCC, SUBF_CCC, MULF_CCC, DIVF_CCC
	, FTOI_CCC, ITOF_CCC
	, LSSI_CCB, EQUI_CCB, NLSI_CCB, NEQI_CCB
	, LSSF_CCB, EQUF_CCB, NLSF_CCB, NEQF_CCB
	, SKIP_B__, IFDF_CB_, IFND_CB_
	, DEFI____
};
const int FIRST_COMPILE_TIME_OPCODE = MOVE_CC_;

// FIX : sort in some meaningful order?
const int TRANSIENT			= 0x00001;					// FIX : name ?
const int VAR_INT_R			= 0x00002 | TRANSIENT;		// Readable int variable.
const int VAR_INT_W			= 0x00004 | TRANSIENT;		// Writable int variable.
const int VAR_FLOAT_R		= 0x00008 | TRANSIENT;		// Readable float variable.
const int VAR_FLOAT_W		= 0x00010 | TRANSIENT;		// Writable float variable.
const int VAR_PTR_R			= 0x00020 | TRANSIENT;		// Readable pointer variable.
const int VAR_PTR_W			= 0x00040 | TRANSIENT;		// Writable pointer variable.
const int ANY_VAR_R			= VAR_INT_R | VAR_FLOAT_R | VAR_PTR_R;
const int ANY_VAR_W			= VAR_INT_W | VAR_FLOAT_W | VAR_PTR_W;
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
const int UNCHECKED_ADDRESS	= 0x80000;					// Do *not* verify that the offset is < size of the symbol. (Some instructions shouldn't do this, like ADDp for example.)
const int FWD_ADDRESS_W		= ADDRESS_W | FORWARD;
const int FWD_ADDRESS_R		= ADDRESS_R | FORWARD;
const int FWD_BRANCH		= BRANCH | FORWARD;
const int FREE_ADDRESS		= ADDRESS | UNCHECKED_ADDRESS;
const int FWD_FREE			= FREE_ADDRESS | FORWARD;
const int FWD_FREE_W		= ADDRESS_W | UNCHECKED_ADDRESS | FORWARD;
const int FWD_FREE_R		= ADDRESS_R | UNCHECKED_ADDRESS | FORWARD;
const int ANY_FREE			= NULL_PTR | FREE_ADDRESS | FUNC;
const int ANY_FWD_FREE		= ANY_FREE | FORWARD;
const int ANY_VAR_FREE_W	= ANY_VAR_W | UNCHECKED_ADDRESS;
const int ANY_VAR_FREE_R	= ANY_VAR_R | UNCHECKED_ADDRESS;
const int ANY_VAR_FREE		= ANY_VAR | UNCHECKED_ADDRESS;
const int KONST				= CONST_INT | CONST_FLOAT | ANY_FWD_FREE; // FIX : called KONST because windows defines a CONST macro, which messes up CONST if you force include windows.h

const int SWAP_0_AND_1		= 0x01; // Used for commutative operations where operand 0 and operand 1 can be swapped in order to minimize the effective instruction set when operands have different addressing modes.
const int SWAP_1_AND_2		= 0x02; // Used for commutative operations where operand 1 and operand 2 can be swapped in order to minimize the effective instruction set when operands have different addressing modes.
const int YIELDS_CONST		= 0x04; // Result of instruction is a constant (e.g. all source operands are constants).
const int YIELDS_GOTO		= 0x08; // Instruction can be resolved to either a GOTO or a NOOP (e.g. comparison of two constants).
const int LOCAL_BOUNDS		= 0x10;	// Operand 1 is a variable (local or transient), operand 2 is a size. Make sure frame bounds >= &variable + size.
const int CHECK_DIV_BY_0	= 0x20; // Operand 1 is a constant used in a division or modulo operation, must check for division by zero. Can only be used if operand 2 is either a CONST_FLOAT or CONST_INT

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
	, { " CALL_c__", CALL_CVC,	{ FUNC | FORWARD, 0				, 0				}		, 0				, 0				}
	, { " CALL_cvs", CALL_CVC,	{ FUNC | FORWARD, TRANSIENT		, CONST_INT_P	}		, LOCAL_BOUNDS	, 0				}
	, { " CALL_n__", CALL_NVC,	{ NATIVE|FORWARD, 0			, 0				}		, 0				, 0				}
	, { " CALL_nvs", CALL_NVC,	{ NATIVE|FORWARD, TRANSIENT	, CONST_INT_P	}		, LOCAL_BOUNDS	, 0				}
	, { " CALL_v__", CALL_VVC,	{ VAR_PTR_R		, 0				, 0				}		, 0				, 0				}
	, { " CALL_vvs", CALL_VVC,	{ VAR_PTR_R		, TRANSIENT		, CONST_INT_P	}		, LOCAL_BOUNDS	, 0				}
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
	, { " DIFp_vcc", SUBI_CCC,	{ VAR_INT_W		, FREE_ADDRESS		, FREE_ADDRESS		}		, YIELDS_CONST	, CONST_INT		}
	, { " DIFp_vcv", SUBI_VCV,	{ VAR_INT_W		, FWD_FREE		, VAR_PTR_R		}		, 0				, 0				}
	, { " DIFp_vvc", SUBI_VVC,	{ VAR_INT_W		, VAR_PTR_R		, FWD_FREE		}		, 0				, 0				}
	, { " DIFp_vvv", SUBI_VVV,	{ VAR_INT_W		, VAR_PTR_R		, VAR_PTR_R		}		, 0				, 0				}
	, { " DIVf_vcc", DIVF_CCC,	{ VAR_FLOAT_W	, CONST_FLOAT	, CONST_FLOAT	}		, YIELDS_CONST|CHECK_DIV_BY_0, CONST_FLOAT }
	, { " DIVf_vcv", DIVF_VCV,	{ VAR_FLOAT_W	, CONST_FLOAT	, VAR_FLOAT_R	}		, 0				, 0				}
	, { " DIVf_vvc", DIVF_VVC,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, CONST_FLOAT	}		, CHECK_DIV_BY_0, 0				}
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
	, { " FLOf_vc_", FLOF_CC_,	{ VAR_FLOAT_W	, CONST_FLOAT	, 0				}		, YIELDS_CONST	, CONST_FLOAT	}
	, { " FLOf_vv_", FLOF_VV_,	{ VAR_FLOAT_W	, VAR_FLOAT_R	, 0				}		, 0				, 0				}
	, { " FORi_vcb", FORi_VCB,	{ VAR_INT_W		, CONST_INT		, FWD_BRANCH	}		, 0				, 0				}
	, { " FORi_vvb", FORi_VVB,	{ VAR_INT_W		, VAR_INT_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " FORp_vcb", FORi_VCB,	{ VAR_PTR_W		, FWD_FREE		, FWD_BRANCH	}		, 0				, 0				}
	, { " FORp_vvb", FORi_VVB,	{ VAR_PTR_W		, VAR_PTR_R		, FWD_BRANCH	}		, 0				, 0				}
	, { " FUNC____", FUNC_CC_,	{ 0				, 0				, 0				}		, 0				, FUNC			}
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
	, { " INPf____", LOCA____,	{ 0				, 0				, 0				}		, 0				, VAR_FLOAT_R & ~TRANSIENT }
	, { " INPi____", LOCA____,	{ 0				, 0				, 0				}		, 0				, VAR_INT_R & ~TRANSIENT }
	, { " INPp____", LOCA____,	{ 0				, 0				, 0				}		, 0				, VAR_PTR_R & ~TRANSIENT }
	, { " IORi_vcc", IORI_CCC,	{ VAR_INT_W		, CONST_INT		, CONST_INT		}		, YIELDS_CONST	, CONST_INT		}
	, { " IORi_vcv", IORI_VVC,	{ VAR_INT_W		, CONST_INT		, VAR_INT_R		}		, SWAP_1_AND_2	, 0				}
	, { " IORi_vvc", IORI_VVC,	{ VAR_INT_W		, VAR_INT_R		, CONST_INT		}		, 0				, 0				}
	, { " IORi_vvv", IORI_VVV,	{ VAR_INT_W		, VAR_INT_R		, VAR_INT_R		}		, 0				, 0				}
	, { " LEQf_ccb", NLSI_CCB,	{ CONST_FLOAT	, CONST_FLOAT	, FWD_BRANCH	}		, SWAP_0_AND_1 | YIELDS_GOTO, 0	}
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
	, { " LOCA_s__", LOCA____,	{ CONST_INT_P	, 0				, 0				}		, 0				, ANY_VAR & ~TRANSIENT }
	, { " LOCf____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_FLOAT_R | VAR_FLOAT_W) & ~TRANSIENT }
	, { " LOCi____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_INT_R | VAR_INT_W) & ~TRANSIENT }
	, { " LOCp____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_PTR_R | VAR_PTR_W) & ~TRANSIENT }
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
	, { " NOOP____", NOOP____,	{ 0				, 0				, 0				}		, 0				, 0				}
	, { " OUTf____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_FLOAT_R | VAR_FLOAT_W) & ~TRANSIENT }
	, { " OUTi____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_INT_R | VAR_INT_W) & ~TRANSIENT }
	, { " OUTp____", LOCA____,	{ 0				, 0				, 0				}		, 0				, (VAR_PTR_R | VAR_PTR_W) & ~TRANSIENT }
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
	, { " SWCH_vsb", SWCH_VCC,	{ VAR_INT_R		, CONST_INT_P	, FWD_BRANCH	}		, 0				, 0				}
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
	, { "!DEFp_c__", DEFI____,	{ FREE_ADDRESS		, 0			, 0				}		, 0				, FREE_ADDRESS		}
	, { "!DIFp_ccc", SUBI_CCC,	{ COMPILE_TIME	, FREE_ADDRESS	, FREE_ADDRESS	}		, 0				, CONST_INT		}
	, { "!DIVf_ccc", DIVF_CCC,	{ COMPILE_TIME	, CONST_FLOAT	, CONST_FLOAT	}		, CHECK_DIV_BY_0, CONST_FLOAT	}
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

void Symbols::define(const std::string& name, int types, Value value, UInt size) {
	const Char* nameBegin = name.data();
	const Char* nameEnd = nameBegin + name.size();
	if (!isValidIdentifier(nameBegin, nameEnd)) throw Exception(INVALID_IDENTIFIER, name);
	
	if ((types & CONST_INT_P) != 0 && value.i < 0) types &= ~CONST_INT_P;
	if ((types & CONST_INT_N) != 0 && value.i >= 0) types &= ~CONST_INT_N;
	SymbolMap::iterator it = symbols.find(name);
	if (it != symbols.end()) throw Exception(SYMBOL_ALREADY_DEFINED, name);
	symbols.insert(it, std::pair<std::string, Symbol>(name, Symbol(types, value, size)));
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
	assert((ref.offset < 0) == (ref.accepts == FWD_BRANCH));
	if ((ref.accepts & UNCHECKED_ADDRESS) == 0 && ref.offset >= static_cast<Int>(symbol.size))
		throw Exception(OFFSET_OUT_OF_BOUNDS, ref.label);
	if (ref.offset == 0) *ref.storage = symbol.value;
	else {
		assert((symbol.types & CONST_FLOAT) == 0);
		ref.storage->i = symbol.value.i + ref.offset;
	}
}

bool Symbols::lookup(const Char* nameBegin, const Char* nameEnd, int acceptedTypes, int& types, Value& value, UInt& size) const {
	assert(isValidIdentifier(nameBegin, nameEnd));
	SymbolMap::const_iterator symbolIt = symbols.find(std::string(nameBegin, nameEnd));
	if (symbolIt == symbols.end() || (symbolIt->second.types & acceptedTypes) == 0) return false;
	types = symbolIt->second.types;
	size = symbolIt->second.size;
	value = symbolIt->second.value;
	return true;
}

void Symbols::link(const Char* labelBegin, const Char* labelEnd, Value* storage, int accepts, Int offset) {
	// Note: it would be OK to forward all forwardables directly (according to typeMask) if that would improve performance.
	std::string name(labelBegin, labelEnd);
	if (!isValidIdentifier(labelBegin, labelEnd)) throw Exception(INVALID_IDENTIFIER, name);
	Reference ref(name, storage, accepts, offset, 0);
	SymbolMap::const_iterator symbolIt = symbols.find(ref.label);
	if (symbolIt != symbols.end()) resolve(ref, symbolIt->second);
	else {
		if ((accepts & FORWARD) == 0) throw Exception(NON_FORWARD_SYMBOL_NOT_FOUND, name);
		forwardRefs.push_back(ref);
	}	
}

// TODO : sort the two containers instead and do line by line lookup (a bit tricky with the switch though, but it should be doable)
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

Pointer Symbols::findGlobal(const Char* name, UInt& size) const {
	int types;
	Value value;
	const Char* nameBegin = name;
	const Char* nameEnd = name + strlen(name);
	if (!isValidIdentifier(nameBegin, nameEnd)) return NULL_POINTER;
	if (!lookup(nameBegin, nameEnd, ADDRESS, types, value, size)) return NULL_POINTER;
	return value.p;
}

Pointer Symbols::findFunction(const Char* name) const {
	int types;
	Value value;
	UInt size;
	const Char* nameBegin = name;
	const Char* nameEnd = name + strlen(name);
	if (!isValidIdentifier(nameBegin, nameEnd)) return NULL_POINTER;
	if (!lookup(nameBegin, nameEnd, FUNC, types, value, size)) return NULL_POINTER;
	return value.p;
}

void Symbols::defineConstant(const Char* name, bool asFloat, const Value& value) {
	define(name, asFloat ? CONST_FLOAT : CONST_INT, value);
}

bool Symbols::lookupConstant(const Char* name, bool* isFloat, Value* value) const {
	int types;
	UInt size;
	if (!lookup(name, name + strlen(name), KONST, types, *value, size)) return false;
	*isFloat = ((types & CONST_FLOAT) != 0);
	return true;
}

bool Symbols::findFirstGlobal(Iterator& iterator, bool includeTemps) const {
	int mask = (includeTemps ? ADDRESS : ADDRESS | TEMPORARY);
	for (iterator = symbols.begin(); iterator != symbols.end() && (iterator->second.types & mask) != ADDRESS; ++iterator);
	return (iterator != symbols.end());
}

bool Symbols::findNextGlobal(Iterator& iterator, bool includeTemps) const {
	int mask = (includeTemps ? ADDRESS : ADDRESS | TEMPORARY);
	for (++iterator; iterator != symbols.end() && (iterator->second.types & mask) != ADDRESS; ++iterator);
	return (iterator != symbols.end());
}

const char* Symbols::getGlobalInfo(const Iterator& iterator, bool& isTemp, Pointer& address, UInt& size) const {
	assert((iterator->second.types & ADDRESS) == ADDRESS);
	isTemp = ((iterator->second.types & TEMPORARY) != 0);
	address = iterator->second.value.p;
	size = iterator->second.size;
	return iterator->first.c_str();
}

Assembler::Assembler(UInt maxCodeSize, Instruction* codeBase, UInt maxFunctionCount, UInt* functionTable
		, UInt maxMemorySize, Value* memoryBase, Symbols& globals)
		: codeBase(codeBase), codeEnd(codeBase + maxCodeSize), functionTable(functionTable)
		, maxFunctionCount(maxFunctionCount), memoryBase(memoryBase), memoryEnd(memoryBase + maxMemorySize)
		, ip(codeBase), functionStart(0), functionCount(0), localsSize(0), paramsSize(0), globalsPointer(memoryBase)
		, constantsPointer(memoryEnd), dataLabelType(0), dataPointer(0), dataEnd(0), globals(globals) {
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

void Assembler::declare(Symbols& where, const Char* labelBegin, const Char* labelEnd, int types, Value value, UInt size) {
	assert((types & FORWARD) == 0);
	if (labelBegin == labelEnd) return;
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
	where.define(name, types, value, size);
}

int Assembler::operatorCompare(const void* a, const void* b) {
	return strcmp(reinterpret_cast<const Char*>(a), reinterpret_cast<const Operator*>(b)->key);
}

Char Assembler::parseOperandType(const Char* b, const Char* e) {
	switch (b < e ? *b : 0) {
		case '#': case '<': case '&':
					return 'c';
		case 0:		return '_';
		case '@':	return 'b';
		case '^':	return 'n';
		case '*':	return 's';
		case '%':	return 'v';
		default:	return 'v';
	}
}

void Assembler::linkWithOffset(Symbols& defs, const char* b, const char* e, int accepts, Value* v) {
	Value o;
	o.i = 0;
	const Char* p = reinterpret_cast<const Char*>(memchr(b, ':', e - b));
	if (p != 0) parseConstant(p + 1, e, CONST_INT_P, &o);
	else p = e;
	defs.link(b, p, v, accepts, o.i);
}

void Assembler::parseConstant(const Char* b, const Char* e, int accepts, Value* v) {
	if ((accepts & KONST) == 0) throw Exception(DID_NOT_EXPECT_CONSTANT, b, e);
// FIX : why?	assert((accepts & FORWARD) == 0);
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
							v->f = stringToFloat(p, e);
							if (p != e) throw Exception(INVALID_NUMERICAL_LITERAL, p, e);
							if ((accepts & CONST_FLOAT) == 0) throw Exception(DID_NOT_EXPECT_CONSTANT_FLOAT, p, e);
						}
						break;
					}
		
		case '<':	{
						if (b + 3 != e || b[2] != '>' || !isValidIdentifierChar(b[1]))
							throw Exception(INVALID_COMPILE_TIME_VARIABLE, b, e);
						const CompileTimeVar& cv = compileTimeVars[b[1]];
						if ((accepts & cv.types) == 0) throw Exception(INCOMPATIBLE_COMPILE_TIME_VARIABLE_TYPE, b, e);
						*v = cv.value;
						break;
					}
				
		default:	globals.link(b, e, v, accepts); break;
	}
}

void Assembler::parseOperand(const Char* b, const Char* e, int accepts, Value* v) {
	switch (b < e ? *b : 0) {
		case 0:		if (accepts != 0) throw Exception(MISSING_OPERAND); break;
		case '*':
		case '#':	++b; /* continue */
		case '<':	parseConstant(b, e, accepts, v); break;
		case '@':	locals.link(b + 1, e, v, accepts, -(Int)(ip - codeBase)); break;		

		case '%':	assert((accepts & TRANSIENT) != 0);
					parseConstant(++b, e, CONST_INT_P, v);
					paramsSize = maximum(paramsSize, (UInt)(v->i + 1));
					break;		

		case '^':	if ((accepts & NATIVE) == 0) throw Exception(DID_NOT_EXPECT_ADDRESS, b, e);
					linkWithOffset(globals, b + 1, e, accepts, v);
					break;

		case '&':	if ((accepts & (ADDRESS | FUNC)) == 0) throw Exception(DID_NOT_EXPECT_ADDRESS, b, e);
					linkWithOffset(globals, b + 1, e, accepts, v);
					break;

		default:	assert((accepts & ANY_VAR) != 0);
					linkWithOffset(locals, b, e, accepts, v);
					v->i -= localsSize;
					break;
	}
}

void Assembler::finalizeFunction() {
	assert(functionStart != 0);
	assert(functionStart->opcode == FUNC_CC_);
	if (ip == codeBase || (ip[-1].opcode != RETU_C__ && ip[-1].opcode != GOTO_B__))
		throw Exception(MISSING_RETURN_INSTRUCTION);
	functionStart->p0.i = localsSize;
	functionStart->p1.i = paramsSize;
	functionStart = 0;
	localsSize = paramsSize = 0;
	locals.resolveForwardRefs();
	locals.clear();
}

void Assembler::finalize(UInt& codeSize, UInt& globalsSize, UInt& constsSize, UInt& functionCount) {
	newUnit(0);
	if (dataPointer != 0) memset(dataPointer, 0, (dataEnd - dataPointer) * sizeof (*dataPointer));
	globals.resolveForwardRefs();
	codeSize = (UInt)(ip - codeBase);
	globalsSize = (UInt)(globalsPointer - memoryBase);
	constsSize = (UInt)(memoryEnd - constantsPointer);
	functionCount = this->functionCount;
}

void Assembler::newUnit(const Char* unitName) { // FIX : use unitName (or not?)
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
		case DIVF_CCC: if (v2.f == 0) throw Exception(CONSTANT_DIVISION_BY_ZERO); v1.f /= v2.f; break;
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
							if (dataPointer + (e - p) > dataEnd) throw Exception(DATA_SECTION_FULL, dataLabel);
							Value v;
							v.p = (Int)(dataPointer - memoryBase + MEMORY_OFFSET);
							declare(globals, labelBegin, labelEnd, dataLabelType, v, static_cast<Int>(e - p));
							while (p < e) (dataPointer++)->i = *p++;
							return eatEOL(eatSpace(e));
						}

			case 'i':
			case 'f':
			case 'p':
			case 'A':	{																								// DATi, DATf, DATp and DATA
							if (dataPointer == 0) throw Exception(DATA_SECTION_MISSING);
							Value v;
							v.p = (Int)(dataPointer - memoryBase + MEMORY_OFFSET);
							declare(globals, labelBegin, labelEnd, dataLabelType, v);
							int accepts = KONST;
							switch (mnemonicBegin[3]) {
								case 'i': accepts = CONST_INT; break;
								case 'f': accepts = CONST_FLOAT; break;
								case 'p': accepts = ANY_FWD_FREE; break;
							}
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
							return eatEOL(p);
						}
			
			default:	throw Exception(INVALID_INSTRUCTION, mnemonicBegin, mnemonicBegin + 4);
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

	const Operator* op = reinterpret_cast<const Operator*>(bsearch(key, OPERATORS, OPERATOR_COUNT, sizeof (*OPERATORS)
			, operatorCompare));
	if (op == 0) throw Exception(INVALID_INSTRUCTION, mnemonicBegin, op2End);
	
	Value v;
	UInt size;
	int types;
	
	if (isCompileTime) {
		if ((op->accepts[0] & COMPILE_TIME) != 0) {																		// Compile-time calculation
			if (op0End != op0Begin + 3 || *op0Begin != '<' || op0Begin[2] != '>' || !isValidIdentifierChar(op0Begin[1]))
				throw Exception(INVALID_COMPILE_TIME_VARIABLE, op0Begin, op0End);
			CompileTimeVar& cv = compileTimeVars[op0Begin[1]];
			cv.value = calcConstant(op, op1Begin, op1End, op2Begin, op2End);
			cv.types = op->declareTypes;
			if ((cv.types & CONST_INT_P) != 0 && cv.value.i < 0) cv.types &= ~CONST_INT_P;
		} else if ((op->accepts[2] & BRANCH) != 0) {																	// Compile-time conditional
			assert(*op2Begin == '@');
			// FIX : sub
			if (!isValidIdentifier(op2Begin + 1, op2End))
				throw Exception(INVALID_IDENTIFIER, op2Begin + 1, op2End);
			if (doConstantBranch(op, op0Begin, op0End, op1Begin, op1End))
				skipUntilLabel = std::string(op2Begin + 1, op2End);
		} else {
			switch (op->opcode) {
				case DEFI____:	parseOperand(op0Begin, op0End, op->accepts[0], &v);										// ! DEFi, ! DEFf
								declare(globals, labelBegin, labelEnd, op->declareTypes, v);
								break;
				
				case IFDF_CB_:
				case IFND_CB_:	if (!isValidIdentifier(op0Begin + 1, op0End))											// ! IFDF, ! IFND
									throw Exception(INVALID_IDENTIFIER, op0Begin + 1, op0End);
								if (!isValidIdentifier(op1Begin + 1, op1End))
									throw Exception(INVALID_IDENTIFIER, op1Begin + 1, op1End);
								assert(op0End > op0Begin + 1);
								if (globals.lookup(op0Begin + 1, op0End, op->accepts[0], types, v, size)
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
							dataLabel = std::string(labelBegin, labelEnd);
							dataLabelType = op->declareTypes;
							if (dataPointer != 0)
								memset(dataPointer, 0, (dataEnd - dataPointer) * sizeof (*dataPointer));
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
							v.p = (Int)(dataPointer - memoryBase + MEMORY_OFFSET);
							declare(globals, labelBegin, labelEnd, op->declareTypes, v, size);
							break;
					
			case FUNC_CC_:	if (functionCount >= maxFunctionCount) throw Exception(NOT_ENOUGH_FUNCTION_SPACE);			// FUNC
							v.p = (Int)(IP_OFFSET + functionCount);		// A function pointer is its stable declaration-order ordinal (not a code offset), resolved through `functionTable` at call time.
							functionTable[functionCount] = (UInt)(ip - codeBase);
							++functionCount;
							declare(globals, labelBegin, labelEnd, op->declareTypes, v);
							if (functionStart != 0) finalizeFunction();
							if (ip >= codeEnd) throw Exception(NOT_ENOUGH_CODE_SPACE);
							ip->opcode = op->opcode;
							ip->p0.i = ip->p1.i = ip->p2.i = 0;
							functionStart = ip++;
							break;
				
			case LOCA____:	if (ip != (functionStart + 1)) throw Exception(MUST_DEFINE_LOCALS_FIRST);					// LOCA, PARA, LOCi, LOCf, LOCp, INPi, INPf, INPp, OUTi, OUTf, OUTp
							v.i = 1;
							parseOperand(op0Begin, op0End, op->accepts[0], &v);
							size = v.i;
							v.i = localsSize;
							declare(locals, labelBegin, labelEnd, op->declareTypes, v, size);
							localsSize += size;
							break;
							
			default:		if (functionStart == 0) throw Exception(MISSING_FUNCTION_DECLARATION);						// The rest
							v.p = (Int)(ip - codeBase);
							declare(locals, labelBegin, labelEnd, BRANCH, v);
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
								parseOperand(op0Begin, op0End, op->accepts[0], p0);
								parseOperand(op1Begin, op1End, op->accepts[1], p1);
								if (op->opcode == SWCH_VCC) {															// SWCH
									assert(*op2Begin == '@');
									assert(op->accepts[2] == FWD_BRANCH);
									size = (p1->i + 1);
									if (size > constantsPointer - globalsPointer) {
										throw Exception(NOT_ENOUGH_MEMORY_SPACE);
									}
									Value v;
									constantsPointer -= size;
									v.p = (Int)(constantsPointer - memoryBase + MEMORY_OFFSET);
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
								if ((op->otherFlags & LOCAL_BOUNDS) != 0)
									paramsSize = maximum((Int)(paramsSize), p1->i + p2->i);
								++ip;
							}
							break;
				

			case DATA____:	assert(0);
		}
	}
	
	return eatEOL(e);
}

Processor::Processor() : codeSize(0), codeBase(0), memorySize(0), memoryBase(0), rwMemorySize(0), dataStackBase(0)
		, dataStackEnd(0), ipStackBase(0), ipStackEnd(0), natives(0), functionTable(0), functionCount(0), ip(0), dsp(0)
		, ipsp(0), userData(0), clockCyclesLeft(0) {
}

Processor::Processor(UInt codeSize, const Instruction* code, UInt memorySize, Value* memory, UInt rwMemorySize
		, UInt dataStackOffset, UInt dataStackSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* natives
		, const UInt* functionTable, UInt functionCount, void* userData)
		: codeSize(codeSize), codeBase(code), memorySize(memorySize - 1), memoryBase(memory), rwMemorySize(rwMemorySize)
		, dataStackBase(memory + dataStackOffset), dataStackEnd(memory + dataStackOffset + dataStackSize)
		, ipStackBase(ipStack), ipStackEnd(ipStack + ipStackSize), natives(natives), functionTable(functionTable)
		, functionCount(functionCount), ip(codeBase), dsp(dataStackBase), ipsp(ipStackBase), userData(userData)
		, clockCyclesLeft(0x7FFFFFFFU) {
	assert(rwMemorySize <= memorySize);
	assert(dataStackOffset + dataStackSize <= rwMemorySize);
	assert(code != 0);
	assert(memory != 0);
	assert(ipStack != 0);
}

Processor::Processor(UInt codeSize, const Instruction* code, UInt memorySize, Value* memory, UInt globalsSize
		, UInt constsSize, UInt ipStackSize, CallStackEntry* ipStack, NativeFunc const* natives
		, const UInt* functionTable, UInt functionCount, void* userData)
		: codeSize(codeSize), codeBase(code), memorySize(memorySize - 1), memoryBase(memory)
		, rwMemorySize(memorySize - constsSize), dataStackBase(memory + globalsSize)
		, dataStackEnd(memory + memorySize - constsSize), ipStackBase(ipStack), ipStackEnd(ipStack + ipStackSize)
		, natives(natives), functionTable(functionTable), functionCount(functionCount), ip(codeBase), dsp(dataStackBase)
		, ipsp(ipStackBase), userData(userData), clockCyclesLeft(0x7FFFFFFFU) {
	assert(globalsSize + constsSize <= memorySize);
	assert(code != 0);
	assert(memory != 0);
	assert(ipStack != 0);
}

#define V0 (dsp[ip->p0.i])
#define V1 (dsp[ip->p1.i])
#define V2 (dsp[ip->p2.i])
#define C0 (ip->p0)
#define C1 (ip->p1)
#define C2 (ip->p2)
	#define CHECK_INT_DIV_BY_ZERO(v) if (v == 0) { err = DIVISION_BY_ZERO; goto ret; }
	#define CHECK_FLOAT_DIV_BY_ZERO(v) if (v == 0) { err = DIVISION_BY_ZERO; goto ret; }

Int Processor::run() {
	assert(codeBase != 0);
	
	Int clockCyclesLeft = this->clockCyclesLeft;
	const Instruction* ip = this->ip;
	Value* dsp = this->dsp;
	CallStackEntry* ipsp = this->ipsp;
	Value* const mb = memoryBase - MEMORY_OFFSET;
	UInt ui, ui2;
	Int nativeError;
	Int err = TIME_OUT;

	assert(ip != 0);
	assert(dsp != 0);
	assert(ipsp > ipStackBase && ipsp <= ipStackEnd); // If this blows it probably means you are trying to continue GAZL after it has returned to your native caller. You need to setup a new call with enterCall().
	
	while (--clockCyclesLeft >= 0) {
		switch (ip->opcode) {
			case FUNC_CC_:	if ((dsp += (UInt)(C0.i)) + C1.i > dataStackEnd) { err = DATA_STACK_OVERFLOW; goto ret; } break;
			case CALL_VVC:	ui = V0.p - IP_OFFSET;						// ui = function ordinal
							if (ui >= functionCount) { err = BAD_CALL; goto ret; }
							ui = functionTable[ui];						// ui = code offset
							assert((codeBase + ui)->opcode == FUNC_CC_);
							goto call;
			case CALL_CVC:	ui = C0.p - IP_OFFSET;						// ui = function ordinal (constant, validated at assembly)
							assert(ui < functionCount);
							ui = functionTable[ui];						// ui = code offset
							assert((codeBase + ui)->opcode == FUNC_CC_);
							goto call;
			call:			if (ipsp >= ipStackEnd) { err = IP_STACK_OVERFLOW; goto ret; }
							ipsp->ip = ip;
							ipsp++->dsp = dsp;
							dsp += (UInt)(C1.i);
							ip = codeBase + ui; // FIX : same thing here as with memory_offset, precalc the constant
							continue;
			case CALL_NVC:	this->clockCyclesLeft = clockCyclesLeft;
							this->ip = ip;
							this->dsp = dsp + (UInt)(C1.i);
							this->ipsp = ipsp;
							if ((nativeError = (*natives[C0.i])(this)) != 0) { err = nativeError; goto ret; }
							clockCyclesLeft = this->clockCyclesLeft;
							break;
			case RETU_C__:	ip = (--ipsp)->ip;
							dsp = ipsp->dsp;
							if (dsp == 0) { dsp = (--ipsp)->dsp; assert(ipsp->ip == ip); err = OK; goto ret; }
							break;
			case MOVE_VV_:	V0 = V1; break;
			case MOVE_VC_:	V0 = C1; break;
			case PEEK_VC_:	V0 = mb[C1.p]; break; // FIX : remove memory_offset from constant indexes and move back mb -> memoryBase +/- 0
			case POKE_CV_:	mb[C0.p] = V1; break;
			case POKE_CC_:	mb[C0.p] = C1; break;
			case PEEK_VVV:	if ((ui = V1.i + V2.i - MEMORY_OFFSET) < memorySize) { V0 = mb[ui + MEMORY_OFFSET]; break; } else { err = BAD_PEEK; goto ret; }
			case PEEK_VCV:	if ((ui = C1.i + V2.i - MEMORY_OFFSET) < memorySize) { V0 = mb[ui + MEMORY_OFFSET]; break; } else { err = BAD_PEEK; goto ret; }
			case POKE_VVV:	if ((ui = V0.i + V1.i - MEMORY_OFFSET) < rwMemorySize) { mb[ui + MEMORY_OFFSET] = V2; break; } else { err = BAD_POKE; goto ret; }
			case POKE_CVV:	if ((ui = C0.i + V1.i - MEMORY_OFFSET) < rwMemorySize) { mb[ui + MEMORY_OFFSET] = V2; break; } else { err = BAD_POKE; goto ret; }
			case POKE_VVC:	if ((ui = V0.i + V1.i - MEMORY_OFFSET) < rwMemorySize) { mb[ui + MEMORY_OFFSET] = C2; break; } else { err = BAD_POKE; goto ret; }
			case POKE_CVC:	if ((ui = C0.i + V1.i - MEMORY_OFFSET) < rwMemorySize) { mb[ui + MEMORY_OFFSET] = C2; break; } else { err = BAD_POKE; goto ret; }
			case GETL_VVV:	if ((ui = V2.i) < (UInt)(dataStackEnd - dsp - C1.i)) { V0 = (dsp + C1.i)[ui]; break; } else { err = BAD_PEEK; goto ret; };
			case SETL_VVV:	if ((ui = V1.i) < (UInt)(dataStackEnd - dsp - C0.i)) { (dsp + C0.i)[ui] = V2; break; } else { err = BAD_POKE; goto ret; };
			case SETL_VVC:	if ((ui = V1.i) < (UInt)(dataStackEnd - dsp - C0.i)) { (dsp + C0.i)[ui] = C2; break; } else { err = BAD_POKE; goto ret; };
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
			case ABSF_VV_:	V0.f = absolute(V1.f); break;
			case FLOF_VV_:	V0.f = floorf(V1.f); break;
			case ADDF_VVV:	V0.f = V1.f + V2.f; break;
			case ADDF_VVC:	V0.f = V1.f + C2.f; break;
			case SUBF_VVV:	V0.f = V1.f - V2.f; break;
			case SUBF_VVC:	V0.f = V1.f - C2.f; break;
			case SUBF_VCV:	V0.f = C1.f - V2.f; break;
			case MULF_VVV:	V0.f = V1.f * V2.f; break;
			case MULF_VVC:	V0.f = V1.f * C2.f; break;
			case DIVF_VVV:	CHECK_FLOAT_DIV_BY_ZERO(V2.f); V0.f = V1.f / V2.f; break;
			case DIVF_VVC:	V0.f = V1.f / C2.f; break;
			case DIVF_VCV:	CHECK_FLOAT_DIV_BY_ZERO(V2.f); V0.f = C1.f / V2.f; break;
			case FTOI_VVC:	V0.i = ftoi(V1.f * C2.f); break;
			case ITOF_VVC:	V0.f = (Float)(V1.i) * C2.f; break;
			// FIX : all constant addresses here should be checked compile-time, but then we would need to parse operand 2 first and have an option for forward linking where the size is added to the check.
			case COPY_VVC:	ui = V0.i - MEMORY_OFFSET; ui2 = V1.i - MEMORY_OFFSET; goto copy;
			case COPY_VCC:	ui = V0.i - MEMORY_OFFSET; ui2 = C1.i - MEMORY_OFFSET; goto copy;
			case COPY_CVC:	ui = C0.i - MEMORY_OFFSET; ui2 = V1.i - MEMORY_OFFSET; goto copy;
			case COPY_CCC:	ui = C0.i - MEMORY_OFFSET; ui2 = C1.i - MEMORY_OFFSET; goto copy;
			copy:			if (ui + C2.i < rwMemorySize && ui2 + C2.i < memorySize) {
								// std::copy(&mb[ui2 + MEMORY_OFFSET], &mb[ui2 + MEMORY_OFFSET] + C2.i, &mb[ui + MEMORY_OFFSET]);
								// memcpy(&mb[ui + MEMORY_OFFSET], &mb[ui2 + MEMORY_OFFSET], sizeof (Value) * C2.i);
								const Value* sp = &mb[ui2 + MEMORY_OFFSET];
								Value* dp = &mb[ui + MEMORY_OFFSET];
								Value* ep = dp + C2.i;
								while (dp < ep) *dp++= *sp++;
								break;
							} else {
								err = ACCESS_VIOLATION;
								goto ret;
							}
			case FORi_VVB:	if (++V0.i < V1.i) { ip += C2.i; continue; }; break;
			case FORi_VCB:	if (++V0.i < C1.i) { ip += C2.i; continue; }; break;
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
#undef CHECK_FLOAT_DIV_BY_ZERO

Status Processor::enterCall(Pointer functionPointer) {
	assert(codeBase != 0);
	UInt ui = functionPointer - IP_OFFSET;						// ui = function ordinal
	if (ui >= functionCount) return BAD_CALL;
	ui = functionTable[ui];										// ui = code offset
	assert((codeBase + ui)->opcode == FUNC_CC_);
	if (ipsp + 2 > ipStackEnd) return IP_STACK_OVERFLOW;
	ipsp->ip = this->ip;
	ipsp++->dsp = this->dsp;
	ipsp->ip = this->ip;
	ipsp++->dsp = 0;			// Mark return to native caller.
	this->ip = codeBase + ui;
	return OK;
}
 
/*
	--- Memory serialization ("freeze" / "thaw") ---

	Persists the mutable global memory of a Processor so it can be reconstructed later. Only the writable, non-TEMP
	globals are stored; source text and compile-time constants are the host's responsibility (thaw re-assembles them,
	which deterministically rebuilds identical code, layout and function ordinals). The blob is engine-agnostic and
	designed to survive compiler changes: function pointers stored in globals are stable ordinals (see FUNC), so they
	resolve correctly through the re-assembled function table regardless of code layout.

	The caller must freeze/thaw at a quiescent boundary (between `run()` calls, with no active GAZL call), so that the
	data stack, call stack and instruction pointer hold nothing that needs saving.
*/
const UInt MEMORY_FORMAT_VERSION = 1;
const UInt MEMORY_INT_CANARY = 0x01020304;						// Detects endianness / format bugs on thaw.
const UInt MEMORY_HEADER_SIZE = 4 + 6 * 4;						// Magic + { format version, GAZL version, word size, int canary, float canary, global count }.
static const char MEMORY_MAGIC[4] = { 'G', 'Z', 'M', 'M' };

static unsigned char* putMemoryWord(unsigned char* p, UInt v) {	// Writes a 32-bit little-endian word.
	p[0] = static_cast<unsigned char>(v);
	p[1] = static_cast<unsigned char>(v >> 8);
	p[2] = static_cast<unsigned char>(v >> 16);
	p[3] = static_cast<unsigned char>(v >> 24);
	return p + 4;
}

static const unsigned char* getMemoryWord(const unsigned char* p, const unsigned char* end, UInt& value, bool& ok) {
	if (p + 4 > end) { ok = false; value = 0; return p; }		// Truncated: stop reading, leave `ok` false.
	value = (UInt)p[0] | ((UInt)p[1] << 8) | ((UInt)p[2] << 16) | ((UInt)p[3] << 24);
	return p + 4;
}

UInt freezeMemorySize(const Processor& processor, const Symbols& symbols) {
	(void)processor;
	UInt size = MEMORY_HEADER_SIZE;
	Symbols::Iterator it;
	for (bool ok = symbols.findFirstGlobal(it, false); ok; ok = symbols.findNextGlobal(it, false)) {
		bool isTemp;
		Pointer address;
		UInt globalSize;
		const char* name = symbols.getGlobalInfo(it, isTemp, address, globalSize);
		size += 4 + (UInt)strlen(name) + 4 + globalSize * 4;	// name length + name + size + words
	}
	return size;
}

UInt freezeMemory(const Processor& processor, const Symbols& symbols, void* buffer, UInt bufferSize) {
	const UInt needed = freezeMemorySize(processor, symbols);
	if (buffer == 0 || bufferSize < needed) return needed;	// Too small: write nothing, tell the caller the required size.

	unsigned char* const start = static_cast<unsigned char*>(buffer);
	unsigned char* p = start;
	Symbols::Iterator it;
	UInt globalCount = 0;
	for (bool ok = symbols.findFirstGlobal(it, false); ok; ok = symbols.findNextGlobal(it, false)) ++globalCount;

	memcpy(p, MEMORY_MAGIC, 4);
	p += 4;
	Value floatCanary;
	floatCanary.f = 1.0f;										// Detects a divergent float bit-layout on thaw.
	p = putMemoryWord(p, MEMORY_FORMAT_VERSION);
	p = putMemoryWord(p, (UInt)VERSION);
	p = putMemoryWord(p, (UInt)WORD_SIZE);
	p = putMemoryWord(p, MEMORY_INT_CANARY);
	p = putMemoryWord(p, floatCanary.p);
	p = putMemoryWord(p, globalCount);

	for (bool ok = symbols.findFirstGlobal(it, false); ok; ok = symbols.findNextGlobal(it, false)) {
		bool isTemp;
		Pointer address;
		UInt size;
		const char* name = symbols.getGlobalInfo(it, isTemp, address, size);
		assert(!isTemp);
		const UInt nameLength = (UInt)strlen(name);
		p = putMemoryWord(p, nameLength);
		memcpy(p, name, nameLength);
		p += nameLength;
		p = putMemoryWord(p, size);
		const Value* source = processor.accessConstMemory(address, size);
		assert(source != 0);
		for (UInt i = 0; i < size; ++i) p = putMemoryWord(p, source[i].p);
	}
	assert((UInt)(p - start) == needed);					// Byte count must match freezeMemorySize exactly.
	return needed;
}

MemoryLoad thawMemory(Processor& processor, const Symbols& symbols, const void* buffer, UInt bufferSize) {
	const unsigned char* p = static_cast<const unsigned char*>(buffer);
	const unsigned char* const end = p + bufferSize;
	bool ok = true;

	if (bufferSize < 4 || memcmp(p, MEMORY_MAGIC, 4) != 0) return MEMORY_BAD_MAGIC;
	p += 4;
	UInt formatVersion, gazlVersion, wordSize, intCanary, floatCanary, globalCount;
	p = getMemoryWord(p, end, formatVersion, ok);
	p = getMemoryWord(p, end, gazlVersion, ok);
	p = getMemoryWord(p, end, wordSize, ok);
	p = getMemoryWord(p, end, intCanary, ok);
	p = getMemoryWord(p, end, floatCanary, ok);
	p = getMemoryWord(p, end, globalCount, ok);
	if (!ok) return MEMORY_TRUNCATED;
	if (formatVersion != MEMORY_FORMAT_VERSION || gazlVersion != (UInt)VERSION) return MEMORY_BAD_VERSION;
	if (wordSize != (UInt)WORD_SIZE) return MEMORY_BAD_WORDSIZE;
	Value expectedFloat;
	expectedFloat.f = 1.0f;
	if (intCanary != MEMORY_INT_CANARY || floatCanary != expectedFloat.p) return MEMORY_BAD_CANARY;

	for (UInt g = 0; g < globalCount && ok; ++g) {
		UInt nameLength;
		p = getMemoryWord(p, end, nameLength, ok);
		if (!ok || p + nameLength > end) { ok = false; break; }
		std::string name(reinterpret_cast<const char*>(p), nameLength);
		p += nameLength;
		UInt size;
		p = getMemoryWord(p, end, size, ok);

		UInt declaredSize = 0;
		const Pointer address = symbols.findGlobal(name.c_str(), declaredSize);
		Value* destination = (address != NULL_POINTER && declaredSize == size) ? processor.accessMemory(address, size) : 0;
		for (UInt i = 0; i < size && ok; ++i) {
			UInt word;
			p = getMemoryWord(p, end, word, ok);
			if (ok && destination != 0) destination[i].i = (Int)word;
		}
	}
	return ok ? MEMORY_OK : MEMORY_TRUNCATED;
}

#if !defined(NDEBUG)

#include "UnitTest.inc"

struct TestCallbackData {
	Pointer globalPointer;
	Pointer callBack;
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
	params[1].p = data->callBack;
	int err = p->enterCall(data->callBack);
	assert(err == 0);
	if (err != 0) return err;
	err = p->run();
	assert(memory->p == data->callBack);
	return 0;
}

bool unitTest() {
	assert(sizeof (Int) == sizeof (Pointer));
	assert(sizeof (UInt) == sizeof (Int));
	
	for (int i = 0; i < OPERATOR_COUNT; ++i) {
		const Operator& op = OPERATORS[i];
		assert(i == 0 || strcmp(op.key, OPERATORS[i - 1].key) > 0);
		// std::cout << op.key << std::endl;
		for (int j = 0; j < 3; ++j) {
			assert(strchr("_bcnsv", op.key[6 + j]) != 0);
			assert(op.key[6 + j] != '_' || op.accepts[j] == 0);
			assert(op.key[6 + j] != 'b' || (op.accepts[j] & (ANY_VAR | CONST_INT_P | CONST_FLOAT | BRANCH | ADDRESS | FUNC | NATIVE | COMPILE_TIME)) == BRANCH);
			assert(op.key[6 + j] != 'v' || (op.accepts[j] & ANY_VAR) != 0);
			assert(op.key[6 + j] != 'v' || (op.accepts[j] & (CONST_INT_P | CONST_FLOAT | BRANCH | ADDRESS | FUNC | NATIVE | COMPILE_TIME)) == 0);
			assert(op.key[6 + j] != 'c' || (op.accepts[j] & (CONST_INT_P | CONST_FLOAT | ADDRESS | FUNC | NATIVE | COMPILE_TIME)) != 0);
			assert(op.key[6 + j] != 'c' || (op.accepts[j] & (ANY_VAR | BRANCH)) == 0);
			assert(op.opcode == FTOI_CCC || op.opcode == FTOI_VVC || op.opcode == IFDF_CB_ || op.key[4] != 'I' || (op.accepts[j] & (VAR_FLOAT_W | CONST_FLOAT)) == 0);
			assert(op.opcode == ITOF_CCC || op.opcode == ITOF_VVC || op.opcode == IFDF_CB_ || op.key[4] != 'F' || (op.accepts[j] & (VAR_INT_W | CONST_INT_P)) == 0);
			assert(op.accepts[j] == 0 || (op.accepts[j] & (ANY_VAR | CONST_INT_P | CONST_FLOAT | BRANCH | ADDRESS | FUNC | NATIVE | COMPILE_TIME)) != 0);
			assert((op.accepts[j] & ANY_VAR) == 0 || (op.accepts[j] & (CONST_INT_P | CONST_FLOAT | BRANCH | ADDRESS | FUNC | NATIVE | COMPILE_TIME)) == 0);
			assert((op.accepts[j] & ANY_VAR) == 0 || (op.accepts[j] & FORWARD) == 0);
			assert(op.accepts[0] != COMPILE_TIME || (((op.accepts[1] & FORWARD) == 0 || op.accepts[1] == 0) && (((op.accepts[2] & FORWARD) == 0) || op.accepts[2] == 0)));
			assert((op.accepts[j] & CONST_INT_P) != 0 || (op.accepts[j] & CONST_INT_N) == 0);
// FIX : fails			assert((op.accepts[j] & (CONST_INT_P | CONST_FLOAT)) == 0 || ((op.accepts[j] & FORWARD) == 0));
			assert((op.accepts[j] & BRANCH) == 0 || (op.accepts[j] & FORWARD) != 0);
		}
		assert((op.otherFlags & YIELDS_CONST) == 0 || (op.opcode >= FIRST_COMPILE_TIME_OPCODE));
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
		globals.registerNative("assertFail", 0);
		globals.registerNative("testMul", 1);
		globals.registerNative("testCallback", 2);

		UInt codySize = 0;
		UInt globalsSize = 0;
		UInt constsSize;
		UInt functionCount = 0;
		{
			Assembler assem(MAX_CODE_SIZE, cody, MAX_FUNCTION_COUNT, functionTable, MEMORY_SIZE, memory, globals);
			assem.newUnit("UnitTest");
			const Char* cp = UNITTEST;
			while (*cp != 0) {
				try {
					cp = assem.feed(cp);
					if (*cp == 0) assem.finalize(codySize, globalsSize, constsSize, functionCount);
				}
				catch (const Exception& e) {
					(void)e;
					assert(0);
				}
			}
		}
			
		TestCallbackData callbackData;
		
		UInt size;
		callbackData.globalPointer = globals.findGlobal("global.pointer", size);
		assert(callbackData.globalPointer != 0);
		callbackData.callBack = globals.findFunction("CallBack");
		assert(callbackData.callBack != 0);

		std::vector<unsigned char> memoryBlob;
		{
			Processor pmachine(codySize, cody, MEMORY_SIZE, memory, globalsSize, constsSize, CALL_STACK_SIZE, callStack
					, nativeTable, functionTable, functionCount, &callbackData);
			Pointer funcy = globals.findFunction("test");
			assert(funcy != 0);
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
			globalPointer->p = globals.findFunction("CallBack");
			memoryBlob.resize(freezeMemorySize(pmachine, globals));
			UInt written = freezeMemory(pmachine, globals, memoryBlob.empty() ? 0 : &memoryBlob[0], (UInt)memoryBlob.size());
			assert(written == memoryBlob.size());
		}

		// Thaw into a freshly re-assembled machine and verify every non-TEMP global round-trips -- including the
		// function pointer, which must resolve to the same function through the fresh function table.
		{
			std::vector<Value> memory2(MEMORY_SIZE);
			std::vector<Instruction> cody2(MAX_CODE_SIZE);
			std::vector<UInt> functionTable2(MAX_FUNCTION_COUNT);
			std::vector<CallStackEntry> callStack2(CALL_STACK_SIZE);
			Symbols globals2;
			globals2.registerNative("assertFail", 0);
			globals2.registerNative("testMul", 1);
			globals2.registerNative("testCallback", 2);

			UInt codySize2 = 0;
			UInt globalsSize2 = 0;
			UInt constsSize2 = 0;
			UInt functionCount2 = 0;
			{
				Assembler assem(MAX_CODE_SIZE, &cody2[0], MAX_FUNCTION_COUNT, &functionTable2[0], MEMORY_SIZE, &memory2[0]
						, globals2);
				assem.newUnit("UnitTest");
				const Char* cp = UNITTEST;
				while (*cp != 0) {
					cp = assem.feed(cp);
					if (*cp == 0) assem.finalize(codySize2, globalsSize2, constsSize2, functionCount2);
				}
			}

			Processor pmachine2(codySize2, &cody2[0], MEMORY_SIZE, &memory2[0], globalsSize2, constsSize2, CALL_STACK_SIZE
					, &callStack2[0], nativeTable, &functionTable2[0], functionCount2, &callbackData);
			MemoryLoad loaded = thawMemory(pmachine2, globals2, memoryBlob.empty() ? 0 : &memoryBlob[0], (UInt)memoryBlob.size());
			assert(loaded == MEMORY_OK);

			// Every non-TEMP global must match the frozen original (identical assemblies share global addresses).
			Symbols::Iterator it;
			for (bool ok = globals.findFirstGlobal(it, false); ok; ok = globals.findNextGlobal(it, false)) {
				bool isTemp;
				Pointer address;
				UInt size;
				globals.getGlobalInfo(it, isTemp, address, size);
				const UInt index = address - MEMORY_OFFSET;
				for (UInt i = 0; i < size; ++i) assert(memory2[index + i].i == memory[index + i].i);
			}
			// The restored function pointer resolves to CallBack in the fresh assembly (and equals the original ordinal).
			UInt gpSize;
			const Pointer gp = globals2.findGlobal("global.pointer", gpSize);
			const Value* restored = pmachine2.accessConstMemory(gp, 1);
			assert(restored != 0);
			assert(restored->p == globals2.findFunction("CallBack"));
			assert(restored->p == globals.findFunction("CallBack"));
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

// FIX : drop
static void instructionReport() {
	const Operator* currentMnemonic = 0;
	for (int i = 0; i < OPERATOR_COUNT; ++i) {
		const Operator& op = OPERATORS[i];

		const int FIRST_OPERAND_COLUMN = 10;
		const int OPERAND_WIDTH = 16;
		const int ADDRESS_MASK = (ADDRESS_R | ADDRESS_W | TEMPORARY | NULL_PTR | FUNC);
		const int CONST_MASK = (CONST_INT_P | CONST_INT_N | CONST_FLOAT);
		const int VAR_MASK = (TRANSIENT | VAR_INT_R | VAR_INT_W | VAR_FLOAT_R | VAR_FLOAT_W | VAR_PTR_R | VAR_PTR_W);
		const int CATEGORY_MASK = COMPILE_TIME | ADDRESS_MASK | CONST_MASK | VAR_MASK;

		assert(strlen(op.key) == 9);
		std::string s;
		if (currentMnemonic == 0 || memcmp(op.key, currentMnemonic, 5) != 0) {
			if (currentMnemonic != 0) std::cout << std::endl;
			currentMnemonic = &op;
			s = op.key;
			if (s[0] == '!') s = s.substr(0, 1) + ' ' + s.substr(1, 4);
			else s = s.substr(1, 4);
		}
		s += std::string(FIRST_OPERAND_COLUMN - s.size(), ' ');
		for (int j = 0; j < 3; ++j) {
			int a = op.accepts[j];
			switch (op.key[6 + j]) {
				case '_': assert(a == 0); break;
				case 'v': {
					if ((a & CATEGORY_MASK) == TRANSIENT) {
						s += "%temp";
					} else if ((a & CATEGORY_MASK) == (a & VAR_MASK)) {
						if ((a & VAR_MASK) == (a & (VAR_INT_R | VAR_INT_W))) s += "int";
						else if ((a & VAR_MASK) == (a & (VAR_FLOAT_R | VAR_FLOAT_W))) s += "float";
						else if ((a & VAR_MASK) == (a & (VAR_PTR_R | VAR_PTR_W))) s += "ptr";
						else s += "var";
						if ((a & VAR_MASK) == (a & (VAR_INT_W | VAR_FLOAT_W | VAR_PTR_W))) s += "(d)";
						// else if ((a & VAR_MASK) != (a & (VAR_INT_R | VAR_FLOAT_R | VAR_PTR_R))) s += "(s+d)";
					}
					break;
				}
				case 'c': {
					if ((a & CATEGORY_MASK) == COMPILE_TIME) {
						s += "<?>";
					} else if ((a & CATEGORY_MASK) == FUNC) {
						s += "&function";
					} else if ((a & CATEGORY_MASK) == (a & ADDRESS_MASK)) {
						s += "&address";
						if ((a & (ADDRESS_R | ADDRESS_W)) == ADDRESS_R) s += "(r)";
						else if ((a & (ADDRESS_R | ADDRESS_W)) == ADDRESS_W) s += "(w)";
					} else if ((a & CATEGORY_MASK) == (a & CONST_MASK) || op.opcode == IFDF_CB_ || op.opcode == IFND_CB_) {
						s += "#";
						if ((a & (CONST_MASK)) == CONST_FLOAT) s += "float";
						else if ((a & (CONST_MASK)) != (CONST_INT_P | CONST_INT_N | CONST_FLOAT)) s += "int";
						else s += "const";
					}
// FIX : fails					// else assert(0);
					break;
				}
				case 'b': {
					assert(a == FWD_BRANCH);
					s += "@label";
					break;
				}
				case 'n': {
					assert((a & ~FORWARD) == NATIVE);
					s += "^native";
					break;
				}
				case 's': {
					assert(a == CONST_INT_P);
					s += "*size";
					break;
				}
				default: assert(0);
			}
			size_t count = FIRST_OPERAND_COLUMN + (j + 1) * OPERAND_WIDTH - s.size();
			assert(count > 1);
			if (j != 2) s += std::string(count, ' ');
		}
		std::cout << s.c_str() << std::endl;
	}
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

