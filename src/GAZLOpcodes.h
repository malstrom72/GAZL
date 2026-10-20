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
	The single GAZL opcode enum, shared by the assembler, interpreter and JIT; internal, clients include GAZL.h or
	GAZLJit.h. Every consumer derives its ordinals from here and never spells them out itself.

	Never gate an opcode out with #if: that renumbers every opcode after it, so the instruction encoding would depend
	on a build flag. Gate mnemonic acceptance (the table in GAZL.cpp) and the version instead.

	A new opcode may go anywhere: the numbering is internal to one build, never an on-disk format. The JIT asserts
	`FINALIZED_OPCODE_COUNT`, so a new finalized opcode needs a case in every backend.
*/

#ifndef GAZLOpcodes_h
#define GAZLOpcodes_h

namespace GAZL {

const Int FIRST_OPCODE_VALUE = 0x2345;

enum Opcode {
	FUNC_CC_ = FIRST_OPCODE_VALUE, CALL_VVC, CALL_CVC, CALL_NVC, RETU_C__, TAIL_CC_, TAIL_VC_
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

	, NOOP____, GLOB____, CNST____, DATA____, LOCA____, OUTP____, MARK____, FREE____, SPAN____, GAZL____
	
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

// The finalized opcodes (`FUNC_CC_` to `SWCH_VCC`) reach the code stream; compile-time opcodes never survive assembly.
const int FINALIZED_OPCODE_COUNT = SWCH_VCC - FUNC_CC_ + 1;

enum OperandRole { OPERAND_OTHER = 0, OPERAND_SLOT_READ = 1, OPERAND_SLOT_WRITE = 2, OPERAND_SLOT_READ_WRITE = 3 };

void operandRoles(int opcode, OperandRole roles[3]);	// The frame-slot role of each operand of a finalized `opcode`, in `p0`, `p1`, `p2` order. Roles are bit flags.

}																														// namespace GAZL

#endif
