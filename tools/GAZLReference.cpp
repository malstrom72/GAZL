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

#include <cmath>
#include <cstring>
#include <vector>
#include "GAZLReference.h"
#include "../src/GAZLOpcodes.h"

namespace GAZLReference {

// Adding a run-time opcode must break this build until the reference has a case for it.
typedef char FinalizedOpcodeCountCheck[(FINALIZED_OPCODE_COUNT == 93) ? 1 : -1];

static Int wrap(long long v) { return (Int)(UInt)(unsigned long long)v; }

static Int divide(Int a, Int b) {						// b != 0; truncates toward zero; INT_MIN / -1 wraps to INT_MIN
	return wrap((long long)a / (long long)b);
}

static Int modulus(Int a, Int b) {					// b != 0; the sign of the dividend; INT_MIN % -1 is 0
	return wrap((long long)a % (long long)b);
}

static Int shiftLeft(Int a, Int n) { return wrap((long long)((unsigned long long)(UInt)a << (n & 31))); }

static Int shiftRightArithmetic(Int a, Int n) {			// a negative `a` shifts in ones: ~(~a >> n), all unsigned
	return (a >= 0) ? (Int)((UInt)a >> (n & 31)) : (Int)~(~(UInt)a >> (n & 31));
}

static Int shiftRightLogical(Int a, Int n) { return (Int)((UInt)a >> (n & 31)); }

static Int absoluteInt(Int a) { return wrap(a < 0 ? -(long long)a : (long long)a); }	// INT_MIN stays INT_MIN

static Int floatToInt(Float v) {						// already scaled; truncates and saturates, NaN is 0
	if (v != v) return 0;
	if ((double)v >= 2147483648.0) return 0x7FFFFFFF;
	if ((double)v < -2147483648.0) return (Int)0x80000000u;
	return (Int)(long long)v;
}

Value* Machine::accessMemory(Pointer pointer, UInt count) {
	const long long index = (long long)(UInt)(pointer - MEMORY_OFFSET);
	return (index + (long long)count <= (long long)rwSize) ? &memory[index] : 0;
}

Status Machine::call(Pointer function, long long startFuel) {
	fuel = startFuel;
	unspecified = false;
	violation.clear();

	const UInt ordinal = (UInt)(function - FUNCTION_OFFSET);
	if (ordinal >= functionCount) return BAD_CALL;
	if (ipStackSize < 2) return IP_STACK_OVERFLOW;		// `enterCall` takes two entries: the caller and the return mark

	std::vector<Frame> calls;							// the GAZL calls made since the entry, one entry each
	long long ip = functionTable[ordinal];
	long long dsp = stackBase;

	for (;;) {
		if (--fuel < 0) return TIME_OUT;
		if (ip < 0 || ip >= (long long)codeSize) return fail("instruction pointer outside the code");
		const Instruction& in = code[ip];
		const Value& c0 = in.p0;
		const Value& c1 = in.p1;
		const Value& c2 = in.p2;

		// A variable operand is a frame slot: `dsp` plus the operand. The assembler keeps it inside the frame.
		#define SLOT(c, name)	if (!slotOk(dsp + (c).i)) return fail("slot outside the data stack"); \
								Value& name = memory[dsp + (c).i];
		// A constant address (`&address` with no index) is checked at assembly; the reference checks it again.
		#define CONSTANT_ADDRESS(c, limit, name)	const long long name = (long long)(UInt)((c).p - MEMORY_OFFSET); \
								if (name >= (long long)(limit)) return fail("constant address outside memory");
		#define BRANCH(condition, offset)	if (condition) { ip += (offset).i; continue; }

		switch (in.opcode) {
			case FUNC_CC_: {							// C0 = locals, C1 = the deepest call window after them
				if ((long long)(UInt)c0.i + (long long)(UInt)c1.i > (long long)stackEnd - dsp) {
					return DATA_STACK_OVERFLOW;
				}
				dsp += (UInt)c0.i;
				break;
			}
			case CALL_VVC:
			case CALL_CVC: {							// P0 = the function, C1 = the callee's window
				Pointer target;
				if (in.opcode == CALL_VVC) {
					SLOT(c0, v0);
					target = v0.p;
				} else {
					target = c0.p;
				}
				const UInt callee = (UInt)(target - FUNCTION_OFFSET);
				if (callee >= functionCount) return BAD_CALL;
				if (calls.size() + 2 >= ipStackSize) return IP_STACK_OVERFLOW;
				Frame frame = { ip, dsp };
				calls.push_back(frame);
				dsp += (UInt)c1.i;
				ip = functionTable[callee];
				if (ip >= (long long)codeSize || code[ip].opcode != FUNC_CC_) return fail("call target is not a FUNC");
				continue;
			}
			case CALL_NVC: {							// C0 = the native's index, C1 = its window
				const long long window = dsp + (UInt)c1.i;
				if (!slotOk(window)) return fail("native window outside the data stack");
				const long long fuelBefore = fuel;
				const Status s = natives[c0.i](*this, &memory[window], (UInt)((long long)stackEnd - window));
				if (s != OK) {
					fuel = fuelBefore;					// a native that fails leaves the fuel as it found it
					return s;
				}
				break;
			}
			case RETU_C__: {
				if (calls.empty()) return OK;			// back to the native caller
				ip = calls.back().ip;
				dsp = calls.back().dsp;
				calls.pop_back();
				break;
			}
			case TAIL_CC_:
			case TAIL_VC_: {							// P0 = the function, C1 = window words, C2 = this frame's size
				Pointer target;
				if (in.opcode == TAIL_VC_) {
					SLOT(c0, v0);
					target = v0.p;
				} else {
					target = c0.p;
				}
				const UInt callee = (UInt)(target - FUNCTION_OFFSET);
				if (callee >= functionCount) return BAD_CALL;
				const long long base = dsp - (UInt)c2.i;
				for (long long i = 0; i < (long long)(UInt)c1.i; ++i) {		// ascending: the window slides down
					if (!slotOk(base + i) || !slotOk(dsp + i)) return fail("TAIL window outside the data stack");
					memory[base + i] = memory[dsp + i];
				}
				dsp = base;
				ip = functionTable[callee];
				if (ip >= (long long)codeSize || code[ip].opcode != FUNC_CC_) return fail("tail target is not a FUNC");
				continue;
			}
			case MOVE_VV_: { SLOT(c0, v0); SLOT(c1, v1); v0 = v1; break; }
			case MOVE_VC_: { SLOT(c0, v0); v0 = c1; break; }
			case PEEK_VC_: { SLOT(c0, v0); CONSTANT_ADDRESS(c1, memorySize, a); v0 = memory[a]; break; }
			case POKE_CV_: { CONSTANT_ADDRESS(c0, rwSize, a); SLOT(c1, v1); memory[a] = v1; break; }
			case POKE_CC_: { CONSTANT_ADDRESS(c0, rwSize, a); memory[a] = c1; break; }
			case PEEK_VVV:
			case PEEK_VCV: {							// P1 = the pointer, V2 = the index
				SLOT(c0, v0);
				Pointer base;
				if (in.opcode == PEEK_VVV) {
					SLOT(c1, v1);
					base = v1.p;
				} else {
					base = c1.p;
				}
				SLOT(c2, v2);
				const long long a = (long long)(UInt)(base + (UInt)v2.i - MEMORY_OFFSET);
				if (a >= (long long)memorySize) return BAD_PEEK;
				v0 = memory[a];
				break;
			}
			case POKE_VVV:
			case POKE_CVV:
			case POKE_VVC:
			case POKE_CVC: {							// P0 = the pointer, V1 = the index, P2 = the value
				Pointer base;
				if (in.opcode == POKE_VVV || in.opcode == POKE_VVC) {
					SLOT(c0, v0);
					base = v0.p;
				} else {
					base = c0.p;
				}
				SLOT(c1, v1);
				Value value = c2;
				if (in.opcode == POKE_VVV || in.opcode == POKE_CVV) {
					SLOT(c2, v2);
					value = v2;
				}
				const long long a = (long long)(UInt)(base + (UInt)v1.i - MEMORY_OFFSET);
				if (a >= (long long)rwSize) return BAD_POKE;
				memory[a] = value;
				break;
			}
			case GETL_VVV: {							// V0 = the result, C1 = the local array's slot, V2 = the index
				SLOT(c0, v0);
				SLOT(c2, v2);
				const long long a = dsp + c1.i + (long long)v2.i;
				if (v2.i < 0 || a >= (long long)stackEnd) return BAD_PEEK;
				if (!slotOk(a)) return fail("GETL below the data stack");
				v0 = memory[a];
				break;
			}
			case SETL_VVV:
			case SETL_VVC: {							// C0 = the local array's slot, V1 = the index, P2 = the value
				SLOT(c1, v1);
				Value value = c2;
				if (in.opcode == SETL_VVV) {
					SLOT(c2, v2);
					value = v2;
				}
				const long long a = dsp + c0.i + (long long)v1.i;
				if (v1.i < 0 || a >= (long long)stackEnd) return BAD_POKE;
				if (!slotOk(a)) return fail("SETL below the data stack");
				memory[a] = value;
				break;
			}
			case ADRL_VV_: { SLOT(c0, v0); v0.p = (Pointer)(UInt)(dsp + c1.i) + MEMORY_OFFSET; break; }
			case ABSI_VV_: { SLOT(c0, v0); SLOT(c1, v1); v0.i = absoluteInt(v1.i); break; }
			case ABSF_VV_: { SLOT(c0, v0); SLOT(c1, v1); v0.f = std::fabs(v1.f); break; }
			case FLOF_VV_: { SLOT(c0, v0); SLOT(c1, v1); v0.f = std::floor(v1.f); break; }

			// Two-operand arithmetic: V0 = P1 op P2, the operand kinds read from the opcode name.
			case ADDI_VVV: case ADDI_VVC: case SUBI_VVV: case SUBI_VVC: case SUBI_VCV:
			case MULI_VVV: case MULI_VVC: case DIVI_VVV: case DIVI_VVC: case DIVI_VCV:
			case MODI_VVV: case MODI_VVC: case MODI_VCV: case ANDI_VVV: case ANDI_VVC:
			case IORI_VVV: case IORI_VVC: case XORI_VVV: case XORI_VVC:
			case SHLI_VVV: case SHLI_VVC: case SHLI_VCV: case SHRI_VVV: case SHRI_VVC: case SHRI_VCV:
			case SHRU_VVV: case SHRU_VVC: case SHRU_VCV: {
				SLOT(c0, v0);
				const int op = in.opcode;
				const bool constantA = (op == SUBI_VCV || op == DIVI_VCV || op == MODI_VCV || op == SHLI_VCV
						|| op == SHRI_VCV || op == SHRU_VCV);
				const bool constantB = (op == ADDI_VVC || op == SUBI_VVC || op == MULI_VVC || op == DIVI_VVC
						|| op == MODI_VVC || op == ANDI_VVC || op == IORI_VVC || op == XORI_VVC || op == SHLI_VVC
						|| op == SHRI_VVC || op == SHRU_VVC);
				Int a = c1.i;
				Int b = c2.i;
				if (!constantA) { SLOT(c1, v1); a = v1.i; }
				if (!constantB) { SLOT(c2, v2); b = v2.i; }
				Int r = 0;
				if (op >= ADDI_VVV && op <= ADDI_VVC) r = wrap((long long)a + b);
				else if (op >= SUBI_VVV && op <= SUBI_VCV) r = wrap((long long)a - b);
				else if (op >= MULI_VVV && op <= MULI_VVC) r = wrap((long long)a * b);
				else if (op >= DIVI_VVV && op <= DIVI_VCV) {
					if (b == 0) {
						if (constantB) return fail("constant division by zero");
						return DIVISION_BY_ZERO;
					}
					r = divide(a, b);
				} else if (op >= MODI_VVV && op <= MODI_VCV) {
					if (b == 0) {
						if (constantB) return fail("constant division by zero");
						return DIVISION_BY_ZERO;
					}
					r = modulus(a, b);
				}
				else if (op >= ANDI_VVV && op <= ANDI_VVC) r = a & b;
				else if (op >= IORI_VVV && op <= IORI_VVC) r = a | b;
				else if (op >= XORI_VVV && op <= XORI_VVC) r = a ^ b;
				else if (op >= SHLI_VVV && op <= SHLI_VCV) r = shiftLeft(a, b);
				else if (op >= SHRI_VVV && op <= SHRI_VCV) r = shiftRightArithmetic(a, b);
				else r = shiftRightLogical(a, b);
				v0.i = r;
				break;
			}
			case ADDF_VVV: case ADDF_VVC: case SUBF_VVV: case SUBF_VVC: case SUBF_VCV:
			case MULF_VVV: case MULF_VVC: case DIVF_VVV: case DIVF_VVC: case DIVF_VCV: {
				SLOT(c0, v0);
				const int op = in.opcode;
				const bool constantA = (op == SUBF_VCV || op == DIVF_VCV);
				const bool constantB = (op == ADDF_VVC || op == SUBF_VVC || op == MULF_VVC || op == DIVF_VVC);
				Float a = c1.f;
				Float b = c2.f;
				if (!constantA) { SLOT(c1, v1); a = v1.f; }
				if (!constantB) { SLOT(c2, v2); b = v2.f; }
				Float r;
				if (op >= ADDF_VVV && op <= ADDF_VVC) r = a + b;
				else if (op >= SUBF_VVV && op <= SUBF_VCV) r = a - b;
				else if (op >= MULF_VVV && op <= MULF_VVC) r = a * b;
				else {
					if (b == 0.0f) {
						if (constantB) return fail("constant division by zero");
						return DIVISION_BY_ZERO;
					}
					r = a / b;
				}
			#ifdef GAZL_CANONICAL_NAN
				if (r != r) {			// the strict-FP build gives every arithmetic NaN one bit pattern
					v0.i = 0x7FC00000;
					break;
				}
			#endif
				v0.f = r;
				break;
			}
			case FTOI_VVC: { SLOT(c0, v0); SLOT(c1, v1); v0.i = floatToInt(v1.f * c2.f); break; }		// C2 = scale
			case ITOF_VVC: { SLOT(c0, v0); SLOT(c1, v1); v0.f = (Float)v1.i * c2.f; break; }			// C2 = scale
			case COPY_VVC:
			case COPY_VCC:
			case COPY_CVC:
			case COPY_CCC: {							// P0 = destination, P1 = source, C2 = words
				Pointer to = c0.p;
				Pointer from = c1.p;
				if (in.opcode == COPY_VVC || in.opcode == COPY_VCC) { SLOT(c0, v0); to = v0.p; }
				if (in.opcode == COPY_VVC || in.opcode == COPY_CVC) { SLOT(c1, v1); from = v1.p; }
				const long long count = (long long)(UInt)c2.i;
				const long long d = (long long)(UInt)(to - MEMORY_OFFSET);
				const long long s = (long long)(UInt)(from - MEMORY_OFFSET);
				if (d + count > (long long)rwSize || s + count > (long long)memorySize) return ACCESS_VIOLATION;
				if (d < s + count && s < d + count && count > 0 && d != s) unspecified = true;
				for (long long i = 0; i < count; ++i) memory[d + i] = memory[s + i];
				break;
			}
			case FORi_VVB:
			case FORi_VCB: {							// V0 = the counter, P1 = the limit, C2 = the branch
				SLOT(c0, v0);
				Int limit = c1.i;
				if (in.opcode == FORi_VVB) { SLOT(c1, v1); limit = v1.i; }
				v0.i = wrap((long long)v0.i + 1);
				BRANCH(v0.i < limit, c2);
				break;
			}
			case LSSI_VVB: case LSSI_VCB: case LSSI_CVB: case EQUI_VVB: case EQUI_VCB:
			case NLSI_VVB: case NLSI_VCB: case NLSI_CVB: case NEQI_VVB: case NEQI_VCB:
			case LSSF_VVB: case LSSF_VCB: case LSSF_CVB: case EQUF_VVB: case EQUF_VCB:
			case NLSF_VVB: case NLSF_VCB: case NLSF_CVB: case NEQF_VVB: case NEQF_VCB: {
				const int op = in.opcode;				// P0 compared with P1, C2 = the branch
				const bool constantA = (op == LSSI_CVB || op == NLSI_CVB || op == LSSF_CVB || op == NLSF_CVB);
				const bool constantB = (op == LSSI_VCB || op == EQUI_VCB || op == NLSI_VCB || op == NEQI_VCB
						|| op == LSSF_VCB || op == EQUF_VCB || op == NLSF_VCB || op == NEQF_VCB);
				Value a = c0;
				Value b = c1;
				if (!constantA) { SLOT(c0, v0); a = v0; }
				if (!constantB) { SLOT(c1, v1); b = v1; }
				bool taken;
				if (op >= LSSI_VVB && op <= LSSI_CVB) taken = (a.i < b.i);
				else if (op >= EQUI_VVB && op <= EQUI_VCB) taken = (a.i == b.i);
				else if (op >= NLSI_VVB && op <= NLSI_CVB) taken = !(a.i < b.i);
				else if (op >= NEQI_VVB && op <= NEQI_VCB) taken = (a.i != b.i);
				else if (op >= LSSF_VVB && op <= LSSF_CVB) taken = (a.f < b.f);
				else if (op >= EQUF_VVB && op <= EQUF_VCB) taken = (a.f == b.f);
				else if (op >= NLSF_VVB && op <= NLSF_CVB) taken = !(a.f < b.f);	// true when either is NaN
				else taken = (a.f != b.f);
				BRANCH(taken, c2);
				break;
			}
			case GOTO_B__: { ip += c0.i; continue; }
			case SWCH_VCC: {							// V0 = the case, C1 = the default's index, C2 = the table
				SLOT(c0, v0);
				const UInt index = ((UInt)v0.i < (UInt)c1.i) ? (UInt)v0.i : (UInt)c1.i;	// out of range -> default
				const long long t = (long long)(UInt)(c2.p - MEMORY_OFFSET) + index;
				if (t >= (long long)memorySize) return fail("switch table outside memory");
				ip += memory[t].i;
				continue;
			}
			default: return fail("unknown opcode");
		}
		#undef SLOT
		#undef CONSTANT_ADDRESS
		#undef BRANCH
		++ip;
	}
}

}																														// namespace GAZLReference
