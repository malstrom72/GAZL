// Independent clang-assembled oracle for the GAZLJit Emitter test (tools/GAZLJitTest.cpp).
//
// Each `ref_*` symbol labels exactly one 4-byte AArch64 instruction; the test reads the word at
// `*(const uint32_t*)&ref_*` and compares it to the byte the Emitter produces for the same mnemonic. The whole file is
// assembled by clang, so it is a second, independent encoding of every form — a mismatch means the Emitter is wrong.
//
// `ref_bench_v2` .. `ref_bench_v2_end` bracket a byte-for-byte copy of `bench_v2` from
// benchmarks/jit/JitBenchA3.arm64.S; the test emits the same body via the Emitter (labels + branch fixups) and asserts
// the two word arrays are identical, exercising the Label/fixup pass.
//
// This is a .S (capital) so the C preprocessor runs; symbols use the platform C-symbol prefix so it links on both
// macOS (Mach-O, leading underscore) and Linux (ELF) arm64 — same convention as benchmarks/jit/JitBenchA3.arm64.S.
#if defined(__APPLE__)
# define GLOBL(n) .globl _##n
# define ENTRY(n) _##n:
#else
# define GLOBL(n) .globl n
# define ENTRY(n) n:
#endif

		.text
		.align	2

// One labeled instruction per form (GLOBL and ENTRY must be on their own lines — the C preprocessor cannot emit the
// newline that would let them share one). The instruction sits on the ENTRY line after the label.
		GLOBL(ref_movz)
ENTRY(ref_movz)		movz w13, #0x4E6D
		GLOBL(ref_movk)
ENTRY(ref_movk)		movk w13, #0x41C6, lsl #16
		GLOBL(ref_movn)
ENTRY(ref_movn)		movn w0, #998
		GLOBL(ref_mov_reg)
ENTRY(ref_mov_reg)	mov w0, w10

		GLOBL(ref_add)
ENTRY(ref_add)		add w9, w9, w14
		GLOBL(ref_add_imm)
ENTRY(ref_add_imm)	add w11, w11, #1
		GLOBL(ref_sub)
ENTRY(ref_sub)		sub w0, w1, w2
		GLOBL(ref_sub_imm)
ENTRY(ref_sub_imm)	sub w13, w3, #1
		GLOBL(ref_subs)
ENTRY(ref_subs)		subs w0, w1, w2
		GLOBL(ref_subs_imm)
ENTRY(ref_subs_imm)	subs w1, w1, #5
		GLOBL(ref_cmp)
ENTRY(ref_cmp)		cmp w11, w0
		GLOBL(ref_cmp_imm)
ENTRY(ref_cmp_imm)	cmp w11, #1
		GLOBL(ref_mul)
ENTRY(ref_mul)		mul w9, w9, w13
		GLOBL(ref_and)
ENTRY(ref_and)		and w12, w9, w15
		GLOBL(ref_orr)
ENTRY(ref_orr)		orr w0, w1, w2
		GLOBL(ref_eor)
ENTRY(ref_eor)		eor w0, w1, w2
		GLOBL(ref_lsl)
ENTRY(ref_lsl)		lsl w0, w1, #3
		GLOBL(ref_lsr)
ENTRY(ref_lsr)		lsr w12, w9, #10
		GLOBL(ref_asr)
ENTRY(ref_asr)		asr w0, w1, #5

		GLOBL(ref_ldr)
ENTRY(ref_ldr)		ldr w3, [x2, #8]
		GLOBL(ref_str)
ENTRY(ref_str)		str w3, [x2, #4]
		GLOBL(ref_str_zr)
ENTRY(ref_str_zr)	str wzr, [x2, #4]
		GLOBL(ref_ldr_reg)
ENTRY(ref_ldr_reg)	ldr w12, [x2, w11, uxtw #2]
		GLOBL(ref_str_reg)
ENTRY(ref_str_reg)	str w12, [x2, w11, uxtw #2]

		GLOBL(ref_fadd)
ENTRY(ref_fadd)		fadd s0, s1, s2
		GLOBL(ref_fmul)
ENTRY(ref_fmul)		fmul s0, s1, s2
		GLOBL(ref_fsub)
ENTRY(ref_fsub)		fsub s0, s1, s2
		GLOBL(ref_fcvtzs)
ENTRY(ref_fcvtzs)	fcvtzs w0, s1
		GLOBL(ref_scvtf)
ENTRY(ref_scvtf)	scvtf s0, w1
		GLOBL(ref_ldr_s)
ENTRY(ref_ldr_s)	ldr s0, [x2, #8]
		GLOBL(ref_str_s)
ENTRY(ref_str_s)	str s0, [x2, #8]

		GLOBL(ref_ldur)
ENTRY(ref_ldur)		ldur w3, [x2, #-4]
		GLOBL(ref_stur)
ENTRY(ref_stur)		stur w3, [x2, #-4]
		GLOBL(ref_ldr_x)
ENTRY(ref_ldr_x)	ldr x3, [x2, #8]
		GLOBL(ref_str_x)
ENTRY(ref_str_x)	str x3, [x2, #8]
		GLOBL(ref_ldr_xr)
ENTRY(ref_ldr_xr)	ldr x9, [x10, w9, uxtw #3]
		GLOBL(ref_adr)
ENTRY(ref_adr)		adr x0, .
		GLOBL(ref_add_immx)
ENTRY(ref_add_immx)	add x1, x1, #16
		GLOBL(ref_sub_immx)
ENTRY(ref_sub_immx)	sub x4, x4, #16
		GLOBL(ref_cbnz_x)
ENTRY(ref_cbnz_x)	cbnz x0, .

// Self-referential branches (displacement 0) isolate the opcode+condition/register fields from the displacement.
		GLOBL(ref_b)
ENTRY(ref_b)		b .
		GLOBL(ref_bcond_lt)
ENTRY(ref_bcond_lt)	b.lt .
		GLOBL(ref_bcond_mi)
ENTRY(ref_bcond_mi)	b.mi .
		GLOBL(ref_cbz)
ENTRY(ref_cbz)		cbz w0, .
		GLOBL(ref_cbnz)
ENTRY(ref_cbnz)		cbnz w0, .
		GLOBL(ref_ret)
ENTRY(ref_ret)		ret
		GLOBL(ref_br)
ENTRY(ref_br)		br x9
		GLOBL(ref_blr)
ENTRY(ref_blr)		blr x9

// ---------------------------------------------------------------------------
// Byte-for-byte copy of bench_v2 (benchmarks/jit/JitBenchA3.arm64.S). Bracketed
// so the test can recover its length: (&ref_bench_v2_end - &ref_bench_v2) words.
// ---------------------------------------------------------------------------
		GLOBL(ref_bench_v2)
ENTRY(ref_bench_v2)
		mov		w9,  #12345				// acc
		mov		w10, #0					// sum
		mov		w11, #0					// i
		movz	w13, #0x4E6D
		movk	w13, #0x41C6, lsl #16	// K = 1103515245
		movz	w14, #12345				// C
		movz	w15, #0xFFFF
		movk	w15, #0x3FFF, lsl #16	// mask = 0x3FFFFFFF
Lref_v2_loop:
		subs	w1, w1, #5				// fuel -= blockWeight(5)
		b.mi	Lref_v2_done
		mul		w9, w9, w13				// acc *= K
		add		w9, w9, w14				// acc += C
		and		w12, w9, w15			// tmp = acc & mask
		add		w10, w10, w12			// sum += tmp
		add		w11, w11, #1			// ++i
		cmp		w11, w0					// i < n ?
		b.lt	Lref_v2_loop
Lref_v2_done:
		mov		w0, w10
		ret
		GLOBL(ref_bench_v2_end)
ENTRY(ref_bench_v2_end)
