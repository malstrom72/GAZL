// Independent clang-assembled oracle for the GAZLJit X64Emitter test (tools/GAZLJitX64Test.cpp).
//
// Each `ref_*` symbol marks the start of one x86-64 instruction; the test computes that instruction's length as the
// distance to the NEXT ref symbol (x64 is variable length) and compares those bytes to what X64Emitter produces for the
// same mnemonic. The whole file is assembled by clang, so it is a second, independent encoding of every form.
//
// `ref_seq` .. `ref_seq_end` bracket a small branch loop (add/cmp/jcc-forward/jmp-back/ret); the test emits the same via
// X64Emitter (labels + rel32 fixups) and asserts the byte arrays match, exercising the Label/finalize pass.
//
// Built -arch x86_64 and run under Rosetta on Apple Silicon (or native on x64). Symbols use the platform C prefix.
#if defined(__APPLE__)
# define GLOBL(n) .globl _##n
# define ENTRY(n) _##n:
#else
# define GLOBL(n) .globl n
# define ENTRY(n) n:
#endif

		.intel_syntax noprefix
		.text

// One labeled instruction per form; GLOBL and ENTRY sit on their own lines (the C preprocessor cannot emit the
// newline that would let them share one). The instruction follows the label on the ENTRY line.
		GLOBL(ref_mov_imm)
ENTRY(ref_mov_imm)			mov eax, 0x12345678
		GLOBL(ref_mov_imm64)
ENTRY(ref_mov_imm64)		movabs rax, 0x123456789abcdef0
		GLOBL(ref_mov_rr)
ENTRY(ref_mov_rr)			mov eax, ecx
		GLOBL(ref_mov_rr_ext)
ENTRY(ref_mov_rr_ext)		mov r8d, ecx
		GLOBL(ref_movq_rr)
ENTRY(ref_movq_rr)			mov rbx, rsp
		GLOBL(ref_add_rr)
ENTRY(ref_add_rr)			add eax, ecx
		GLOBL(ref_sub_rr)
ENTRY(ref_sub_rr)			sub eax, ecx
		GLOBL(ref_imul_rr)
ENTRY(ref_imul_rr)			imul eax, ecx
		GLOBL(ref_and_rr)
ENTRY(ref_and_rr)			and eax, ecx
		GLOBL(ref_or_rr)
ENTRY(ref_or_rr)			or eax, ecx
		GLOBL(ref_xor_rr)
ENTRY(ref_xor_rr)			xor eax, ecx
		GLOBL(ref_cmp_rr)
ENTRY(ref_cmp_rr)			cmp eax, ecx
// Non-accumulator register (ecx): clang uses the general `81 /ext id` form here, matching X64Emitter. (For eax it would
// pick the shorter accumulator-specific `05/2D/3D id` opcodes, an encoding X64Emitter does not bother with.)
		GLOBL(ref_add_imm)
ENTRY(ref_add_imm)			add ecx, 0x12345678
		GLOBL(ref_sub_imm)
ENTRY(ref_sub_imm)			sub ecx, 0x12345678
		GLOBL(ref_cmp_imm)
ENTRY(ref_cmp_imm)			cmp ecx, 0x12345678
		GLOBL(ref_addq_rr)
ENTRY(ref_addq_rr)			add rbx, rcx
		GLOBL(ref_load_d8)
ENTRY(ref_load_d8)			mov eax, [rbx + 0x40]
		GLOBL(ref_load_dneg)
ENTRY(ref_load_dneg)		mov eax, [rbx - 0x40]
		GLOBL(ref_load_d32)
ENTRY(ref_load_d32)			mov eax, [rbx + 0x12345678]
		GLOBL(ref_load_rsp)
ENTRY(ref_load_rsp)			mov eax, [rsp + 0x40]
		GLOBL(ref_load_r13)
ENTRY(ref_load_r13)			mov eax, [r13]
		GLOBL(ref_store_d8)
ENTRY(ref_store_d8)			mov [rbx + 0x40], eax
		GLOBL(ref_loadq_d8)
ENTRY(ref_loadq_d8)			mov rax, [rbx + 0x40]
		GLOBL(ref_push_rbx)
ENTRY(ref_push_rbx)			push rbx
		GLOBL(ref_push_r12)
ENTRY(ref_push_r12)			push r12
		GLOBL(ref_pop_rbx)
ENTRY(ref_pop_rbx)			pop rbx
		GLOBL(ref_ret)
ENTRY(ref_ret)				ret

// Branch loop for the Label/fixup pass. ref_seq also terminates the single-instruction list above (its address is the
// end of `ref_ret`).
// 130 nops pad the loop body past 127 bytes so clang emits rel32 (not the relaxed rel8) for both jumps, matching
// X64Emitter's always-near branches.
		GLOBL(ref_seq)
ENTRY(ref_seq)
ref_seq_back:				add eax, ecx
							cmp eax, edx
							jge ref_seq_done
							.rept 130
							nop
							.endr
							jmp ref_seq_back
ref_seq_done:				ret
		GLOBL(ref_seq_end)
ENTRY(ref_seq_end)			ret
