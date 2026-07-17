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

#include "GAZLCpp.h"

#include <sstream>
#include <set>
#include <vector>

namespace GAZL {

/*
	Finalized GAZL opcodes (base = 0x2345, declaration order). Re-declared here so the C++ backend stays
	independent of the arm64 JIT header; the numbering is the frozen ABI shared with the interpreter.
*/
enum {
	OP_FUNC = 0x2345 + 0, OP_CALL_VVC = 0x2345 + 1, OP_CALL_CVC = 0x2345 + 2, OP_CALL_NVC = 0x2345 + 3,
	OP_RETU = 0x2345 + 4, OP_MOVE_VV = 0x2345 + 5, OP_MOVE_VC = 0x2345 + 6, OP_PEEK_VC = 0x2345 + 7,
	OP_POKE_CV = 0x2345 + 8, OP_POKE_CC = 0x2345 + 9, OP_PEEK_VVV = 0x2345 + 10, OP_PEEK_VCV = 0x2345 + 11,
	OP_POKE_VVV = 0x2345 + 12, OP_POKE_CVV = 0x2345 + 13, OP_POKE_VVC = 0x2345 + 14, OP_POKE_CVC = 0x2345 + 15,
	OP_GETL_VVV = 0x2345 + 16, OP_SETL_VVV = 0x2345 + 17, OP_SETL_VVC = 0x2345 + 18, OP_ADRL = 0x2345 + 19,			// outside the subset; recognized only by the promotion guard
	OP_ABSI = 0x2345 + 20,
	OP_ADDI_VVV = 0x2345 + 21, OP_ADDI_VVC = 0x2345 + 22,
	OP_SUBI_VVV = 0x2345 + 23, OP_SUBI_VVC = 0x2345 + 24, OP_SUBI_VCV = 0x2345 + 25,
	OP_MULI_VVV = 0x2345 + 26, OP_MULI_VVC = 0x2345 + 27,
	OP_DIVI_VVV = 0x2345 + 28, OP_DIVI_VVC = 0x2345 + 29, OP_DIVI_VCV = 0x2345 + 30,
	OP_MODI_VVV = 0x2345 + 31, OP_MODI_VVC = 0x2345 + 32, OP_MODI_VCV = 0x2345 + 33,
	OP_ANDI_VVV = 0x2345 + 34, OP_ANDI_VVC = 0x2345 + 35,
	OP_IORI_VVV = 0x2345 + 36, OP_IORI_VVC = 0x2345 + 37,
	OP_XORI_VVV = 0x2345 + 38, OP_XORI_VVC = 0x2345 + 39,
	OP_SHLI_VVV = 0x2345 + 40, OP_SHLI_VVC = 0x2345 + 41, OP_SHLI_VCV = 0x2345 + 42,
	OP_SHRI_VVV = 0x2345 + 43, OP_SHRI_VVC = 0x2345 + 44, OP_SHRI_VCV = 0x2345 + 45,
	OP_SHRU_VVV = 0x2345 + 46, OP_SHRU_VVC = 0x2345 + 47, OP_SHRU_VCV = 0x2345 + 48,
	OP_ABSF = 0x2345 + 49, OP_FLOF = 0x2345 + 50,
	OP_ADDF_VVV = 0x2345 + 51, OP_ADDF_VVC = 0x2345 + 52,
	OP_SUBF_VVV = 0x2345 + 53, OP_SUBF_VVC = 0x2345 + 54, OP_SUBF_VCV = 0x2345 + 55,
	OP_MULF_VVV = 0x2345 + 56, OP_MULF_VVC = 0x2345 + 57,
	OP_DIVF_VVV = 0x2345 + 58, OP_DIVF_VVC = 0x2345 + 59, OP_DIVF_VCV = 0x2345 + 60,
	OP_FTOI_VVC = 0x2345 + 61, OP_ITOF_VVC = 0x2345 + 62,
	OP_FORi_VVB = 0x2345 + 67, OP_FORi_VCB = 0x2345 + 68,
	OP_LSSI_VVB = 0x2345 + 69, OP_LSSI_VCB = 0x2345 + 70, OP_LSSI_CVB = 0x2345 + 71,
	OP_EQUI_VVB = 0x2345 + 72, OP_EQUI_VCB = 0x2345 + 73,
	OP_NLSI_VVB = 0x2345 + 74, OP_NLSI_VCB = 0x2345 + 75, OP_NLSI_CVB = 0x2345 + 76,
	OP_NEQI_VVB = 0x2345 + 77, OP_NEQI_VCB = 0x2345 + 78,
	OP_LSSF_VVB = 0x2345 + 79, OP_LSSF_VCB = 0x2345 + 80, OP_LSSF_CVB = 0x2345 + 81,
	OP_EQUF_VVB = 0x2345 + 82, OP_EQUF_VCB = 0x2345 + 83,
	OP_NLSF_VVB = 0x2345 + 84, OP_NLSF_VCB = 0x2345 + 85, OP_NLSF_CVB = 0x2345 + 86,
	OP_NEQF_VVB = 0x2345 + 87, OP_NEQF_VCB = 0x2345 + 88,
	OP_GOTO = 0x2345 + 89
};

// --- operand rendering (all computed at translate time; the emitted text is plain C++) ---

static std::string I(Int v) { std::ostringstream s; s << v; return s.str(); }
static std::string U(UInt v) { std::ostringstream s; s << v << "u"; return s.str(); }
static std::string islot(const Value& p) { return "dsp[" + I(p.i) + "].i"; }
static std::string fslot(const Value& p) { return "dsp[" + I(p.i) + "].f"; }
// A float constant is stored as its bit pattern in the operand's int field; rebuild it via f32().
static std::string fconst(const Value& p) { return "f32(" + U(static_cast<UInt>(p.i)) + ")"; }
static std::string memIndex(const Value& p) { return U(static_cast<UInt>(p.p - MEMORY_OFFSET)); }
static std::string label(UInt j, Int off) { std::ostringstream s; s << "L_" << (static_cast<Int>(j) + off); return s.str(); }

// A binary int op with the three operand-const modes (VVV/VVC/VCV); op is a C operator string.
static std::string ibin(const Instruction& in, const char* op, bool s1Const, bool s2Const) {
	const std::string a = s1Const ? I(in.p1.i) : islot(in.p1);
	const std::string b = s2Const ? I(in.p2.i) : islot(in.p2);
	return islot(in.p0) + " = " + a + " " + op + " " + b + ";";
}
/*
	Wrap-semantics int ops go through helpers copied verbatim from the interpreter (iadd/isub/imul/idiv/imod/ineg,
	emitted in the prelude): raw signed `+`/`-`/`*`/INT_MIN-negation is UB in C++, and once the optimizer can see through
	the values (constant seeds, Tier-1 promoted locals) it folds through that UB and the output silently diverges from
	the interpreter - observed with an xorshift PRNG. GAZL's semantics are two's-complement wrap; say so.
*/
static std::string icall(const Instruction& in, const char* helper, bool s1Const, bool s2Const) {
	const std::string a = s1Const ? I(in.p1.i) : islot(in.p1);
	const std::string b = s2Const ? I(in.p2.i) : islot(in.p2);
	return islot(in.p0) + " = " + helper + "(" + a + ", " + b + ");";
}
static std::string fbin(const Instruction& in, const char* op, bool s1Const, bool s2Const) {
	const std::string a = s1Const ? fconst(in.p1) : fslot(in.p1);
	const std::string b = s2Const ? fconst(in.p2) : fslot(in.p2);
	return fslot(in.p0) + " = " + a + " " + op + " " + b + ";";
}
// Shift amount masked to 5 bits (matches the arm64 backend and avoids C++ UB on >=32 shifts).
static std::string ishift(const Instruction& in, const char* op, bool cast, bool s1Const, bool s2Const) {
	const std::string a = (cast ? "(uint32_t)" : "") + (s1Const ? I(in.p1.i) : islot(in.p1));
	const std::string b = s2Const ? I(in.p2.i & 31) : ("(" + islot(in.p2) + " & 31)");
	return islot(in.p0) + " = (Int)(" + a + " " + op + " " + b + ");";
}
// Compare-branch: `if (a <cmp> b) goto L;` - a/b in the const mode named by the opcode.
static std::string ibranch(const Instruction& in, UInt j, const char* cmp, bool c0Const, bool c1Const) {
	const std::string a = c0Const ? I(in.p0.i) : islot(in.p0);
	const std::string b = c1Const ? I(in.p1.i) : islot(in.p1);
	return "if (" + a + " " + cmp + " " + b + ") goto " + label(j, in.p2.i) + ";";
}
static std::string fbranch(const Instruction& in, UInt j, const char* cmp, bool c0Const, bool c1Const) {
	const std::string a = c0Const ? fconst(in.p0) : fslot(in.p0);
	const std::string b = c1Const ? fconst(in.p1) : fslot(in.p1);
	return "if (" + a + " " + cmp + " " + b + ") goto " + label(j, in.p2.i) + ";";
}

// Is instruction j a branch, and if so where does it target? (for label placement)
static bool branchTarget(const Instruction* code, UInt j, UInt& target) {
	const Int op = code[j].opcode;
	if (op == OP_GOTO) { target = static_cast<UInt>(static_cast<Int>(j) + code[j].p0.i); return true; }
	if ((op >= OP_FORi_VVB && op <= OP_NEQF_VCB)) {
		target = static_cast<UInt>(static_cast<Int>(j) + code[j].p2.i); return true;
	}
	return false;
}

/*
	Tier 1 local promotion: rewrite a function's private `dsp[-N]` frame accesses to C++ `Value` locals `v_mN` (declared
	after the frame advance), so the optimizer stops assuming MEM stores may alias them. Private = the frame minus the
	CALLER-SHARED window: a callee's OUT/INP slots are the deepest `sharedWindow` slots of its frame (the caller writes
	arguments there and reads results back through memory), so only offsets -1 down to -(frameSize - sharedWindow) are
	promotable. Additionally requires no ADRL/GETL/SETL in the function - nothing may address its frame - which today is
	every translatable function (those opcodes are outside the Tier-0 subset); the guard in emitFunction keeps this
	correct if the subset grows. Cross-realm accesses (§1.1) would observe the unpromoted frame word: UB under the realm
	rule. Token replacement is unambiguous because the closing bracket terminates the match (`dsp[-1]` never matches
	inside `dsp[-10]`).
*/
static void promoteFunctionLocals(std::string& text, Int localsSize) {
	std::string declarations;
	for (Int n = 1; n <= localsSize; ++n) {
		const std::string token = "dsp[-" + I(n) + "]";
		const std::string name = "v_m" + I(n);
		bool used = false;
		for (size_t at = text.find(token); at != std::string::npos; at = text.find(token, at)) {
			text.replace(at, token.size(), name);
			at += name.size();
			used = true;
		}
		if (used) { declarations += "\tValue " + name + " = {0};\n"; }
	}
	const size_t advance = text.find("\tdsp += ");
	text.insert(text.find('\n', advance) + 1, declarations);
}

// Emit one function body; returns false on an unsupported opcode. `sharedWindow` = the caller-shared slot count (see above).
static bool emitFunction(std::ostringstream& out, const Instruction* code, UInt funcIndex, UInt ordinal, bool promoteLocals, Int sharedWindow) {
	UInt retIndex = funcIndex;
	while (code[retIndex].opcode != OP_RETU) { ++retIndex; }

	std::set<UInt> targets;
	bool frameAddressed = false;
	for (UInt j = funcIndex; j <= retIndex; ++j) {
		UInt t;
		if (branchTarget(code, j, t)) { targets.insert(t); }
		const Int op = code[j].opcode;
		if (op == OP_GETL_VVV || op == OP_SETL_VVV || op == OP_SETL_VVC || op == OP_ADRL) { frameAddressed = true; }	// outside the subset today; guards promotion if it grows
	}

	std::ostringstream fn;
	fn << "static int gazl_fn_" << ordinal << "(Value* dsp) {\n";
	fn << "\tdsp += " << I(code[funcIndex].p0.i) << ";\n";		// FUNC: advance to this frame

	for (UInt j = funcIndex; j <= retIndex; ++j) {
		if (targets.count(j)) { fn << "L_" << j << ":;\n"; }
		const Instruction& in = code[j];
		const Int op = in.opcode;
		std::string s;
		switch (op) {
			case OP_FUNC: continue;
			case OP_RETU: s = "return 0;"; break;

			case OP_MOVE_VV: s = "dsp[" + I(in.p0.i) + "] = dsp[" + I(in.p1.i) + "];"; break;
			case OP_MOVE_VC: s = islot(in.p0) + " = " + I(in.p1.i) + ";"; break;

			case OP_PEEK_VC: s = "dsp[" + I(in.p0.i) + "] = MEM[" + memIndex(in.p1) + "];"; break;
			case OP_POKE_CV: s = "MEM[" + memIndex(in.p0) + "] = dsp[" + I(in.p1.i) + "];"; break;
			case OP_POKE_CC: s = "MEM[" + memIndex(in.p0) + "].i = " + I(in.p1.i) + ";"; break;
			case OP_PEEK_VCV: s = "dsp[" + I(in.p0.i) + "] = MEM[" + memIndex(in.p1) + " + (UInt)" + islot(in.p2) + "];"; break;
			case OP_POKE_CVV: s = "MEM[" + memIndex(in.p0) + " + (UInt)" + islot(in.p1) + "] = dsp[" + I(in.p2.i) + "];"; break;
			case OP_POKE_CVC: s = "MEM[" + memIndex(in.p0) + " + (UInt)" + islot(in.p1) + "].i = " + I(in.p2.i) + ";"; break;

			case OP_ABSI: s = islot(in.p0) + " = " + islot(in.p1) + " < 0 ? ineg(" + islot(in.p1) + ") : " + islot(in.p1) + ";"; break;
			case OP_ADDI_VVV: s = icall(in, "iadd", false, false); break;
			case OP_ADDI_VVC: s = icall(in, "iadd", false, true); break;
			case OP_SUBI_VVV: s = icall(in, "isub", false, false); break;
			case OP_SUBI_VVC: s = icall(in, "isub", false, true); break;
			case OP_SUBI_VCV: s = icall(in, "isub", true, false); break;
			case OP_MULI_VVV: s = icall(in, "imul", false, false); break;
			case OP_MULI_VVC: s = icall(in, "imul", false, true); break;
			case OP_DIVI_VVV: s = "if (" + islot(in.p2) + " == 0) return -6; " + icall(in, "idiv", false, false); break;
			case OP_DIVI_VVC: s = icall(in, "idiv", false, true); break;
			case OP_DIVI_VCV: s = "if (" + islot(in.p2) + " == 0) return -6; " + icall(in, "idiv", true, false); break;
			case OP_MODI_VVV: s = "if (" + islot(in.p2) + " == 0) return -6; " + icall(in, "imod", false, false); break;
			case OP_MODI_VVC: s = icall(in, "imod", false, true); break;
			case OP_MODI_VCV: s = "if (" + islot(in.p2) + " == 0) return -6; " + icall(in, "imod", true, false); break;
			case OP_ANDI_VVV: s = ibin(in, "&", false, false); break;
			case OP_ANDI_VVC: s = ibin(in, "&", false, true); break;
			case OP_IORI_VVV: s = ibin(in, "|", false, false); break;
			case OP_IORI_VVC: s = ibin(in, "|", false, true); break;
			case OP_XORI_VVV: s = ibin(in, "^", false, false); break;
			case OP_XORI_VVC: s = ibin(in, "^", false, true); break;
			case OP_SHLI_VVV: s = ishift(in, "<<", true, false, false); break;										// unsigned shift: signed << overflow is UB
			case OP_SHLI_VVC: s = ishift(in, "<<", true, false, true); break;
			case OP_SHLI_VCV: s = ishift(in, "<<", true, true, false); break;
			case OP_SHRI_VVV: s = ishift(in, ">>", false, false, false); break;
			case OP_SHRI_VVC: s = ishift(in, ">>", false, false, true); break;
			case OP_SHRI_VCV: s = ishift(in, ">>", false, true, false); break;
			case OP_SHRU_VVV: s = ishift(in, ">>", true, false, false); break;
			case OP_SHRU_VVC: s = ishift(in, ">>", true, false, true); break;
			case OP_SHRU_VCV: s = ishift(in, ">>", true, true, false); break;

			case OP_ABSF: s = fslot(in.p0) + " = f32(0x7fffffffu & (uint32_t)dsp[" + I(in.p1.i) + "].i);"; break;
			case OP_FLOF: s = fslot(in.p0) + " = (float)std::floor(" + fslot(in.p1) + ");"; break;
			case OP_ADDF_VVV: s = fbin(in, "+", false, false); break;
			case OP_ADDF_VVC: s = fbin(in, "+", false, true); break;
			case OP_SUBF_VVV: s = fbin(in, "-", false, false); break;
			case OP_SUBF_VVC: s = fbin(in, "-", false, true); break;
			case OP_SUBF_VCV: s = fbin(in, "-", true, false); break;
			case OP_MULF_VVV: s = fbin(in, "*", false, false); break;
			case OP_MULF_VVC: s = fbin(in, "*", false, true); break;
			case OP_DIVF_VVV: s = fbin(in, "/", false, false); break;
			case OP_DIVF_VVC: s = fbin(in, "/", false, true); break;
			case OP_DIVF_VCV: s = fbin(in, "/", true, false); break;
			case OP_FTOI_VVC: s = islot(in.p0) + " = ftoi(" + fslot(in.p1) + " * " + fconst(in.p2) + ");"; break;	// scaled + saturating, as the interpreter
			case OP_ITOF_VVC: s = fslot(in.p0) + " = (float)" + islot(in.p1) + " * " + fconst(in.p2) + ";"; break;

			case OP_FORi_VVB: s = islot(in.p0) + " += 1; if (" + islot(in.p0) + " < " + islot(in.p1) + ") goto " + label(j, in.p2.i) + ";"; break;
			case OP_FORi_VCB: s = islot(in.p0) + " += 1; if (" + islot(in.p0) + " < " + I(in.p1.i) + ") goto " + label(j, in.p2.i) + ";"; break;
			case OP_GOTO: s = "goto " + label(j, in.p0.i) + ";"; break;

			case OP_LSSI_VVB: s = ibranch(in, j, "<", false, false); break;
			case OP_LSSI_VCB: s = ibranch(in, j, "<", false, true); break;
			case OP_LSSI_CVB: s = ibranch(in, j, "<", true, false); break;
			case OP_EQUI_VVB: s = ibranch(in, j, "==", false, false); break;
			case OP_EQUI_VCB: s = ibranch(in, j, "==", false, true); break;
			case OP_NLSI_VVB: s = ibranch(in, j, ">=", false, false); break;
			case OP_NLSI_VCB: s = ibranch(in, j, ">=", false, true); break;
			case OP_NLSI_CVB: s = ibranch(in, j, ">=", true, false); break;
			case OP_NEQI_VVB: s = ibranch(in, j, "!=", false, false); break;
			case OP_NEQI_VCB: s = ibranch(in, j, "!=", false, true); break;
			case OP_LSSF_VVB: s = fbranch(in, j, "<", false, false); break;
			case OP_LSSF_VCB: s = fbranch(in, j, "<", false, true); break;
			case OP_LSSF_CVB: s = fbranch(in, j, "<", true, false); break;
			case OP_EQUF_VVB: s = fbranch(in, j, "==", false, false); break;
			case OP_EQUF_VCB: s = fbranch(in, j, "==", false, true); break;
			case OP_NLSF_VVB: s = fbranch(in, j, ">=", false, false); break;
			case OP_NLSF_VCB: s = fbranch(in, j, ">=", false, true); break;
			case OP_NLSF_CVB: s = fbranch(in, j, ">=", true, false); break;
			case OP_NEQF_VVB: s = fbranch(in, j, "!=", false, false); break;
			case OP_NEQF_VCB: s = fbranch(in, j, "!=", false, true); break;

			case OP_CALL_CVC: {
				const UInt callee = in.p0.p - IP_OFFSET;
				s = "{ int st = gazl_fn_" + I(static_cast<Int>(callee)) + "(dsp + " + I(in.p1.i) + "); if (st) return st; }";
				break;
			}
			case OP_CALL_NVC:
				s = "{ int st = NAT[" + I(in.p0.i) + "](dsp + " + I(in.p1.i) + "); if (st) return st; }";
				break;

			default: return false;		// unsupported opcode
		}
		fn << "\t" << s << "\n";
	}
	fn << "}\n\n";
	std::string text = fn.str();
	if (promoteLocals && !frameAddressed) { promoteFunctionLocals(text, code[funcIndex].p0.i - sharedWindow); }
	out << text;
	return true;
}

std::string translateToCpp(const AssembledProgram& program, UInt mainOrdinal, bool promoteLocals) {
	const Instruction* const code = program.code;
	const UInt functionCount = program.functionCount;
	const UInt* const functionTable = program.functionTable;
	const Value* const memory = program.memory;
	const UInt memorySize = program.memorySize;
	const UInt globalsSize = program.globalsSize;
	const UInt constsSize = program.constsSize;

	// Caller-shared window per callee: the widest `*N` any CALL_CVC passes it (its OUT/INP region; excluded from promotion).
	std::vector<Int> sharedWindow(functionCount, 0);
	for (UInt ord = 0; ord < functionCount; ++ord) {
		for (UInt j = functionTable[ord]; code[j].opcode != OP_RETU; ++j) {
			if (code[j].opcode == OP_CALL_CVC) {
				const UInt callee = code[j].p0.p - IP_OFFSET;
				if (callee < functionCount && code[j].p2.i > sharedWindow[callee]) { sharedWindow[callee] = code[j].p2.i; }
			}
		}
	}

	std::ostringstream body;
	for (UInt ord = 0; ord < functionCount; ++ord) {
		if (!emitFunction(body, code, functionTable[ord], ord, promoteLocals, sharedWindow[ord])) { return std::string(); }
	}

	std::ostringstream out;
	out << "/* generated by GAZLCpp (Tier " << (promoteLocals ? 1 : 0) << ") -- do not edit */\n";
	out << "#include <cstdio>\n#include <cstdint>\n#include <cstring>\n#include <cmath>\n#include <chrono>\n\n";
	out << "typedef int32_t Int; typedef uint32_t UInt; typedef uint32_t Ptr;\n";
	out << "union Value { Int i; float f; Ptr p; };\n";
	out << "static const Ptr MEMORY_OFFSET = 0x12345678u;\n";
	out << "static inline float f32(uint32_t b) { float r; std::memcpy(&r, &b, 4); return r; }\n";
	out << "static inline Int iadd(Int a, Int b) { return (Int)((UInt)a + (UInt)b); }\n";							// wrap semantics, verbatim from the interpreter (see icall)
	out << "static inline Int isub(Int a, Int b) { return (Int)((UInt)a - (UInt)b); }\n";
	out << "static inline Int imul(Int a, Int b) { return (Int)((UInt)a * (UInt)b); }\n";
	out << "static inline Int ineg(Int a) { return (Int)(0u - (UInt)a); }\n";
	out << "static inline Int idiv(Int a, Int b) { return (b == -1) ? ineg(a) : a / b; }\n";
	out << "static inline Int imod(Int a, Int b) { return (b == -1) ? 0 : a % b; }\n";
	out << "static inline Int ftoi(float v) {\n";																	// saturating float->int, verbatim from the interpreter
	out << "\tUInt b; std::memcpy(&b, &v, sizeof b);\n";
	out << "\tconst UInt e = (b >> 23) & 0xFF;\n";
	out << "\tif (e >= 158) {\n";
	out << "\t\tif (e == 255 && (b & 0x7FFFFF) != 0) return 0;\n";
	out << "\t\treturn (b >> 31) != 0 ? (Int)(-2147483647 - 1) : 2147483647;\n";
	out << "\t}\n";
	out << "\treturn (Int)v;\n";
	out << "}\n\n";
	out << "static Value MEM[" << memorySize << "];\n\n";

	// Native table (ordinals match tools/GAZLCmd.cpp registration order). Params base = dsp + window.
	out << "static int nat_stub(Value*) { return -1; }\n";
	out << "static int nat_printInt(Value* p) { std::printf(\"%d\", p[1].i); return 0; }\n";
	out << "static int nat_printFloat(Value* p) { std::printf(\"%g\", (double)p[1].f); return 0; }\n";
	out << "static int nat_print(Value* p) { const Value* v = &MEM[p[1].p - MEMORY_OFFSET]; for (; v->i; ++v) std::putchar((int)v->i); return 0; }\n";
	out << "static int nat_printLF(Value*) { std::putchar('\\n'); return 0; }\n";
	out << "static int nat_sqrt(Value* p) { p[0].f = (float)std::sqrt(p[1].f); return 0; }\n";
	out << "static int nat_log(Value* p) { p[0].f = (float)std::log(p[1].f); return 0; }\n";
	out << "static int nat_atan2(Value* p) { p[0].f = (float)std::atan2(p[1].f, p[2].f); return 0; }\n";
	out << "typedef int (*Native)(Value*);\n";
	out << "static Native NAT[] = { nat_stub, nat_stub, nat_printInt, nat_printFloat, nat_print, nat_printLF, nat_stub, nat_atan2, nat_sqrt, nat_log };\n\n";

	// Forward declarations.
	for (UInt ord = 0; ord < functionCount; ++ord) { out << "static int gazl_fn_" << ord << "(Value* dsp);\n"; }
	out << "\n" << body.str();

	// Sparse memory-image init (globals at the front, consts at the back; the data stack stays zero).
	out << "static void init_mem() {\n";
	for (UInt k = 0; k < globalsSize && k < memorySize; ++k) {
		if (memory[k].i != 0) { out << "\tMEM[" << k << "].i = " << I(memory[k].i) << ";\n"; }
	}
	for (UInt k = (constsSize <= memorySize ? memorySize - constsSize : 0); k < memorySize; ++k) {
		if (memory[k].i != 0) { out << "\tMEM[" << k << "].i = " << I(memory[k].i) << ";\n"; }
	}
	out << "}\n\n";

	out << "int main() {\n";
	out << "\tinit_mem();\n";
	out << "\tValue* dsp = MEM + " << globalsSize << ";\n";
	out << "\tstd::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();\n";
	out << "\tint st = gazl_fn_" << mainOrdinal << "(dsp);\n";
	out << "\tstd::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();\n";
	out << "\tstd::fflush(stdout);\n";
	out << "\tdouble ms = std::chrono::duration<double, std::milli>(t1 - t0).count();\n";
	out << "\tstd::fprintf(stderr, \"\\n[cpp] status=%d time_ms=%.3f\\n\", st, ms);\n";
	out << "\treturn 0;\n}\n";

	return out.str();
}

} // namespace GAZL
