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
	Test runner for the GAZLJit arm64 Emitter (assemble-diff harness). For every instruction form in the covered subset
	it emits one word and compares it to the matching clang-assembled reference in tools/GAZLJitTestRef.arm64.S; then it
	rebuilds the whole `bench_v2` kernel through the Emitter (Label + branch fixups) and compares it word-for-word to the
	reference copy of that function; finally it runs a deliberately-corrupted encoding to prove the harness catches a bad
	byte. Exits non-zero on any failure. See docs/JitEmitterHandoff.md.
*/

#include "GAZLJit.h"

#include <cstdio>
#include <cstdint>
#include <cstddef>

using namespace GAZL;

// The clang-assembled oracle: each symbol is one 4-byte instruction (read as a uint32_t word). `ref_bench_v2` ..
// `ref_bench_v2_end` bracket the reference copy of the kernel.
extern "C" {
	extern const uint32_t ref_movz, ref_movk, ref_movn, ref_mov_reg;
	extern const uint32_t ref_add, ref_add_imm, ref_sub, ref_sub_imm, ref_subs, ref_subs_imm;
	extern const uint32_t ref_cmp, ref_cmp_imm, ref_mul, ref_and, ref_orr, ref_eor;
	extern const uint32_t ref_sdiv, ref_msub, ref_lslv, ref_lsrv, ref_asrv;
	extern const uint32_t ref_lsl, ref_lsr, ref_asr;
	extern const uint32_t ref_ldr, ref_str, ref_str_zr, ref_ldr_reg, ref_str_reg, ref_ldr_regs, ref_str_regs;
	extern const uint32_t ref_fadd, ref_fmul, ref_fsub, ref_fcvtzs, ref_scvtf, ref_ldr_s, ref_str_s;
	extern const uint32_t ref_fdiv, ref_fcmp, ref_fmov_sw, ref_ldur_s, ref_stur_s, ref_ldr_sxs, ref_str_sxs;
	extern const uint32_t ref_ldur, ref_stur, ref_ldr_x, ref_str_x, ref_ldr_xr, ref_adr;
	extern const uint32_t ref_add_immx, ref_sub_immx, ref_cbnz_x, ref_cmp_x, ref_add_x;
	extern const uint32_t ref_b, ref_bcond_lt, ref_bcond_mi, ref_cbz, ref_cbnz, ref_ret, ref_br, ref_blr;
	extern const uint32_t ref_bench_v2;
	extern const uint32_t ref_bench_v2_end;
}

static int failures = 0;

// Compare one emitted word against its reference, printing the result. Counts mismatches as failures.
static void check(const char* name, uint32_t emitted, uint32_t reference) {
	const bool ok = (emitted == reference);
	std::printf("  %-16s emit=%08X  ref=%08X  %s\n", name, emitted, reference, ok ? "MATCH" : "MISMATCH");
	if (!ok) {
		++failures;
	}
}

// Emit a single instruction through a fresh Emitter and return its one word.
template<class F> static uint32_t one(F emitOne) {
	Emitter e;
	emitOne(e);
	e.finalize();
	return e.code()[0];
}

// Emit the whole `bench_v2` kernel through the Emitter, exercising the Label/fixup pass (forward `b.mi`, backward
// `b.lt`). Must reproduce benchmarks/jit/JitBenchA3.arm64.S byte-for-byte.
static void emitBenchV2(Emitter& e) {
	e.movz(W9, 12345);							// mov w9, #12345   (acc)
	e.movz(W10, 0);								// mov w10, #0      (sum)
	e.movz(W11, 0);								// mov w11, #0      (i)
	e.movz(W13, 0x4E6D);
	e.movk(W13, 0x41C6, 16);					// K = 1103515245
	e.movz(W14, 12345);							// C
	e.movz(W15, 0xFFFF);
	e.movk(W15, 0x3FFF, 16);					// mask = 0x3FFFFFFF

	Label loop = e.newLabel();
	Label done = e.newLabel();
	e.bind(loop);
	e.subsImm(W1, W1, 5);						// fuel -= blockWeight(5)
	e.bcond(MI, done);
	e.mul(W9, W9, W13);							// acc *= K
	e.add(W9, W9, W14);							// acc += C
	e.and_(W12, W9, W15);						// tmp = acc & mask
	e.add(W10, W10, W12);						// sum += tmp
	e.addImm(W11, W11, 1);						// ++i
	e.cmp(W11, W0);								// i < n ?
	e.bcond(LT, loop);
	e.bind(done);
	e.mov(W0, W10);
	e.ret();
	e.finalize();
}

int main() {
	std::printf("GAZLJit Emitter assemble-diff test (arm64)\n\n");

	std::printf("Per-form encodings vs clang oracle:\n");
	check("movz",      one([](Emitter& e) { e.movz(W13, 0x4E6D); }),        ref_movz);
	check("movk",      one([](Emitter& e) { e.movk(W13, 0x41C6, 16); }),    ref_movk);
	check("movn",      one([](Emitter& e) { e.movn(W0, 998); }),            ref_movn);
	check("mov(reg)",  one([](Emitter& e) { e.mov(W0, W10); }),             ref_mov_reg);

	check("add",       one([](Emitter& e) { e.add(W9, W9, W14); }),         ref_add);
	check("add(imm)",  one([](Emitter& e) { e.addImm(W11, W11, 1); }),      ref_add_imm);
	check("sub",       one([](Emitter& e) { e.sub(W0, W1, W2); }),          ref_sub);
	check("sub(imm)",  one([](Emitter& e) { e.subImm(W13, W3, 1); }),       ref_sub_imm);
	check("subs",      one([](Emitter& e) { e.subs(W0, W1, W2); }),         ref_subs);
	check("subs(imm)", one([](Emitter& e) { e.subsImm(W1, W1, 5); }),       ref_subs_imm);
	check("cmp",       one([](Emitter& e) { e.cmp(W11, W0); }),             ref_cmp);
	check("cmp(imm)",  one([](Emitter& e) { e.cmpImm(W11, 1); }),           ref_cmp_imm);
	check("mul",       one([](Emitter& e) { e.mul(W9, W9, W13); }),         ref_mul);
	check("sdiv",      one([](Emitter& e) { e.sdiv(W0, W1, W2); }),         ref_sdiv);
	check("msub",      one([](Emitter& e) { e.msub(W0, W1, W2, W3); }),     ref_msub);
	check("lslv",      one([](Emitter& e) { e.lslv(W0, W1, W2); }),         ref_lslv);
	check("lsrv",      one([](Emitter& e) { e.lsrv(W0, W1, W2); }),         ref_lsrv);
	check("asrv",      one([](Emitter& e) { e.asrv(W0, W1, W2); }),         ref_asrv);
	check("and",       one([](Emitter& e) { e.and_(W12, W9, W15); }),       ref_and);
	check("orr",       one([](Emitter& e) { e.orr(W0, W1, W2); }),          ref_orr);
	check("eor",       one([](Emitter& e) { e.eor(W0, W1, W2); }),          ref_eor);
	check("lsl(imm)",  one([](Emitter& e) { e.lslImm(W0, W1, 3); }),        ref_lsl);
	check("lsr(imm)",  one([](Emitter& e) { e.lsrImm(W12, W9, 10); }),      ref_lsr);
	check("asr(imm)",  one([](Emitter& e) { e.asrImm(W0, W1, 5); }),        ref_asr);

	check("ldr",       one([](Emitter& e) { e.ldrW(W3, X2, 8); }),          ref_ldr);
	check("str",       one([](Emitter& e) { e.strW(W3, X2, 4); }),          ref_str);
	check("str(wzr)",  one([](Emitter& e) { e.strW(WZR, X2, 4); }),         ref_str_zr);
	check("ldr(reg)",  one([](Emitter& e) { e.ldrWx(W12, X2, W11); }),      ref_ldr_reg);
	check("str(reg)",  one([](Emitter& e) { e.strWx(W12, X2, W11); }),      ref_str_reg);
	check("ldr(regs)", one([](Emitter& e) { e.ldrWxs(W0, X1, W2); }),       ref_ldr_regs);
	check("str(regs)", one([](Emitter& e) { e.strWxs(W0, X1, W2); }),       ref_str_regs);

	check("fadd",      one([](Emitter& e) { e.faddS(S0, S1, S2); }),        ref_fadd);
	check("fmul",      one([](Emitter& e) { e.fmulS(S0, S1, S2); }),        ref_fmul);
	check("fsub",      one([](Emitter& e) { e.fsubS(S0, S1, S2); }),        ref_fsub);
	check("fdiv",      one([](Emitter& e) { e.fdivS(S0, S1, S2); }),        ref_fdiv);
	check("fcmp",      one([](Emitter& e) { e.fcmpS(S1, S2); }),            ref_fcmp);
	check("fmov(sw)",  one([](Emitter& e) { e.fmovSW(S0, W1); }),           ref_fmov_sw);
	check("ldur(s)",   one([](Emitter& e) { e.ldurS(S0, X2, -4); }),        ref_ldur_s);
	check("stur(s)",   one([](Emitter& e) { e.sturS(S0, X2, -4); }),        ref_stur_s);
	check("ldr(sxs)",  one([](Emitter& e) { e.ldrSxs(S0, X1, W2); }),       ref_ldr_sxs);
	check("str(sxs)",  one([](Emitter& e) { e.strSxs(S0, X1, W2); }),       ref_str_sxs);
	check("fcvtzs",    one([](Emitter& e) { e.fcvtzs(W0, S1); }),           ref_fcvtzs);
	check("scvtf",     one([](Emitter& e) { e.scvtf(S0, W1); }),            ref_scvtf);
	check("ldr(s)",    one([](Emitter& e) { e.ldrS(S0, X2, 8); }),          ref_ldr_s);
	check("str(s)",    one([](Emitter& e) { e.strS(S0, X2, 8); }),          ref_str_s);

	check("ldur",      one([](Emitter& e) { e.ldurW(W3, X2, -4); }),        ref_ldur);
	check("stur",      one([](Emitter& e) { e.sturW(W3, X2, -4); }),        ref_stur);
	check("ldr(x)",    one([](Emitter& e) { e.ldrX(X3, X2, 8); }),          ref_ldr_x);
	check("str(x)",    one([](Emitter& e) { e.strX(X3, X2, 8); }),          ref_str_x);
	check("ldr(xr)",   one([](Emitter& e) { e.ldrXr(X9, X10, W9); }),       ref_ldr_xr);
	check("adr",       one([](Emitter& e) { Label l = e.newLabel(); e.bind(l); e.adr(X0, l); }),  ref_adr);
	check("add(immx)", one([](Emitter& e) { e.addImmX(X1, X1, 16); }),      ref_add_immx);
	check("sub(immx)", one([](Emitter& e) { e.subImmX(X4, X4, 16); }),      ref_sub_immx);
	check("cbnz(x)",   one([](Emitter& e) { Label l = e.newLabel(); e.bind(l); e.cbnzX(X0, l); }), ref_cbnz_x);
	check("cmp(x)",    one([](Emitter& e) { e.cmpX(X1, X9); }),             ref_cmp_x);
	check("add(x)",    one([](Emitter& e) { e.addX(X1, X1, X9); }),         ref_add_x);

	// Self-referential branches (displacement 0): isolates the opcode/cond/register fields from the displacement.
	check("b",         one([](Emitter& e) { Label l = e.newLabel(); e.bind(l); e.b(l); }),          ref_b);
	check("b.lt",      one([](Emitter& e) { Label l = e.newLabel(); e.bind(l); e.bcond(LT, l); }),  ref_bcond_lt);
	check("b.mi",      one([](Emitter& e) { Label l = e.newLabel(); e.bind(l); e.bcond(MI, l); }),  ref_bcond_mi);
	check("cbz",       one([](Emitter& e) { Label l = e.newLabel(); e.bind(l); e.cbz(W0, l); }),    ref_cbz);
	check("cbnz",      one([](Emitter& e) { Label l = e.newLabel(); e.bind(l); e.cbnz(W0, l); }),   ref_cbnz);
	check("ret",       one([](Emitter& e) { e.ret(); }),                    ref_ret);
	check("br",        one([](Emitter& e) { e.br(X9); }),                   ref_br);
	check("blr",       one([](Emitter& e) { e.blr(X9); }),                  ref_blr);

	// Cross-check the whole kernel: proves the Label/fixup pass, not just individual instructions.
	std::printf("\nbench_v2 kernel cross-check (Label/fixup pass):\n");
	Emitter kernel;
	emitBenchV2(kernel);
	const size_t refCount = static_cast<size_t>(&ref_bench_v2_end - &ref_bench_v2);
	const uint32_t* refWords = &ref_bench_v2;
	std::printf("  emitted %zu words, reference %zu words\n", kernel.wordCount(), refCount);
	if (kernel.wordCount() != refCount) {
		std::printf("  WORD COUNT MISMATCH\n");
		++failures;
	} else {
		int diffs = 0;
		for (size_t i = 0; i < refCount; ++i) {
			if (kernel.code()[i] != refWords[i]) {
				std::printf("  word %2zu  emit=%08X  ref=%08X  MISMATCH\n", i, kernel.code()[i], refWords[i]);
				++diffs;
			}
		}
		if (diffs == 0) {
			std::printf("  all %zu words MATCH (byte-for-byte)\n", refCount);
		} else {
			failures += diffs;
		}
	}

	// Corrupted control: prove the harness has teeth. A deliberately-broken encoding MUST report a mismatch; if it
	// somehow matched the reference, the harness itself is broken.
	std::printf("\nCorrupted control (must be caught):\n");
	const uint32_t good = one([](Emitter& e) { e.add(W9, W9, W14); });
	const uint32_t corrupted = good ^ 0x1u;					// flip a bit: Rd 9 -> 8, i.e. `add w8, w9, w14`
	const bool caught = (corrupted != ref_add);
	std::printf("  corrupted add   emit=%08X  ref=%08X  %s\n", corrupted, ref_add,
			caught ? "MISMATCH (caught, as expected)" : "MATCH (HARNESS FAILURE)");
	if (!caught) {
		++failures;
	}

	std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures,
			failures == 1 ? "" : "s");
	return failures == 0 ? 0 : 1;
}
