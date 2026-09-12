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
	The GAZL opcode enum - the ONE definition, shared by the assembler/interpreter (GAZL.cpp) and the JIT
	(GAZLJit.*). Internal to the implementation: a client includes GAZL.h or GAZLJit.h and never this file.

	It exists because the JIT used to keep a hand-copied MIRROR of this enum with literal ordinals
	(`OP_MOVE_VV = 0x2345 + 5`). That mirror is only correct for one numbering, and an opcode inserted
	mid-enum renumbers every opcode above it - so the JIT would lower each one as its neighbour, with no
	compile error and no exception. Deriving both sides from this file makes that class of bug impossible.

	TWO RULES, both load-bearing:

	1. NEVER gate an opcode out of this enum with #if. A configuration that omits one renumbers everything
	   after it, and the instruction encoding then depends on a build flag. Gate mnemonic ACCEPTANCE (the
	   table in GAZL.cpp) and VERSION instead - that is where engine generations actually differ: an
	   engine that does not know a word rejects it as unknown, rather than knowing it and refusing.

	2. A new opcode may go ANYWHERE in the finalized block, mid-enum included - GAZL ships as assembly
	   TEXT and is assembled on the end user's machine, so this numbering is internal to one build and
	   never an on-disk format. `TAIL_CC_`/`TAIL_VC_` were inserted at ordinals 5 and 6 by GAZL 2, which
	   renumbered 86 of the 91 opcodes above them and is harmless precisely because every consumer now
	   derives from here. What is NOT safe is a consumer that spells the ordinals out for itself.
	   FINALIZED_OPCODE_COUNT below is asserted by the JIT, so gaining an opcode breaks the build rather
	   than the output - fix it by giving both backends a case for the new opcode.
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

	, NOOP____, GLOB____, CNST____, DATA____, LOCA____, OUTP____, SCOP____, ENDS____, SEEK____, GAZL____
	
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

/*
	The finalized (runtime) opcodes are FUNC_CC_ .. SWCH_VCC - the ones that reach the code stream and so
	must all be lowered by every JIT backend. The compile-time opcodes above continue the same numbering
	but never survive assembly.
*/
const int FINALIZED_OPCODE_COUNT = SWCH_VCC - FUNC_CC_ + 1;

}																														// namespace GAZL

#endif
