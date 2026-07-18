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
	Test runner for the GAZLJit X64Emitter (assemble-diff harness). For each instruction form it emits the bytes and
	compares them to the matching clang-assembled reference in tools/GAZLJitX64TestRef.x64.s (x64 is variable length, so
	each form's length is the distance to the next ref symbol). Then it rebuilds a small branch loop through the emitter
	(Label + rel32 fixups) and compares it byte-for-byte; finally it corrupts one byte to prove the harness bites. Built
	-arch x86_64, run under Rosetta on Apple Silicon. Exits non-zero on any mismatch.
*/

#include "GAZLJitX64.h"

#include <cstdio>
#include <cstdint>
#include <cstring>

using namespace GAZL;

// The clang-assembled oracle: each symbol marks the first byte of one instruction; length = distance to the next.
extern "C" {
	extern const uint8_t ref_mov_imm, ref_mov_imm64, ref_mov_rr, ref_mov_rr_ext, ref_movq_rr;
	extern const uint8_t ref_add_rr, ref_sub_rr, ref_imul_rr, ref_and_rr, ref_or_rr, ref_xor_rr, ref_cmp_rr;
	extern const uint8_t ref_add_imm, ref_sub_imm, ref_cmp_imm, ref_addq_rr;
	extern const uint8_t ref_load_d8, ref_load_dneg, ref_load_d32, ref_load_rsp, ref_load_r13;
	extern const uint8_t ref_store_d8, ref_loadq_d8;
	extern const uint8_t ref_push_rbx, ref_push_r12, ref_pop_rbx, ref_ret;
	extern const uint8_t ref_seq, ref_seq_end;
	extern const uint8_t ref_movss_load, ref_movss_store, ref_movaps_rr, ref_movaps_ext;
	extern const uint8_t ref_addss, ref_subss, ref_mulss, ref_divss, ref_ucomiss, ref_xorps;
	extern const uint8_t ref_cvtsi2ss, ref_cvttss2si, ref_movd_to_xmm, ref_movd_from_xmm, ref_roundss, ref_float_end;
	extern const uint8_t ref_pool_seq, ref_pool_seq_end;
}

static int failures = 0;

static void hex(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; ++i) { std::printf("%02X", p[i]); } }

// Compare the emitter's bytes to the reference byte range [start, end).
static void check(const char* name, const X64Emitter& e, const uint8_t* start, const uint8_t* end) {
	const size_t refLen = static_cast<size_t>(end - start);
	const bool ok = (e.size() == refLen) && (e.code() != 0) && (std::memcmp(e.code(), start, refLen) == 0);
	std::printf("  %-14s emit=", name);
	hex(e.code(), e.size());
	std::printf("  ref=");
	hex(start, refLen);
	std::printf("  %s\n", ok ? "MATCH" : "MISMATCH");
	if (!ok) { ++failures; }
}

int main() {
	std::printf("GAZLJitX64Test (x86-64 assemble-diff)\n\n");

	{ X64Emitter e; e.movImm(RAX, 0x12345678u); check("mov_imm", e, &ref_mov_imm, &ref_mov_imm64); }
	{ X64Emitter e; e.movImm64(RAX, 0x123456789ABCDEF0ull); check("mov_imm64", e, &ref_mov_imm64, &ref_mov_rr); }
	{ X64Emitter e; e.mov(RAX, RCX); check("mov_rr", e, &ref_mov_rr, &ref_mov_rr_ext); }
	{ X64Emitter e; e.mov(R8, RCX); check("mov_rr_ext", e, &ref_mov_rr_ext, &ref_movq_rr); }
	{ X64Emitter e; e.movQ(RBX, RSP); check("movq_rr", e, &ref_movq_rr, &ref_add_rr); }
	{ X64Emitter e; e.add(RAX, RCX); check("add_rr", e, &ref_add_rr, &ref_sub_rr); }
	{ X64Emitter e; e.sub(RAX, RCX); check("sub_rr", e, &ref_sub_rr, &ref_imul_rr); }
	{ X64Emitter e; e.imul(RAX, RCX); check("imul_rr", e, &ref_imul_rr, &ref_and_rr); }
	{ X64Emitter e; e.and_(RAX, RCX); check("and_rr", e, &ref_and_rr, &ref_or_rr); }
	{ X64Emitter e; e.or_(RAX, RCX); check("or_rr", e, &ref_or_rr, &ref_xor_rr); }
	{ X64Emitter e; e.xor_(RAX, RCX); check("xor_rr", e, &ref_xor_rr, &ref_cmp_rr); }
	{ X64Emitter e; e.cmp(RAX, RCX); check("cmp_rr", e, &ref_cmp_rr, &ref_add_imm); }
	{ X64Emitter e; e.addImm(RCX, 0x12345678u); check("add_imm", e, &ref_add_imm, &ref_sub_imm); }
	{ X64Emitter e; e.subImm(RCX, 0x12345678u); check("sub_imm", e, &ref_sub_imm, &ref_cmp_imm); }
	{ X64Emitter e; e.cmpImm(RCX, 0x12345678u); check("cmp_imm", e, &ref_cmp_imm, &ref_addq_rr); }
	{ X64Emitter e; e.addQ(RBX, RCX); check("addq_rr", e, &ref_addq_rr, &ref_load_d8); }
	{ X64Emitter e; e.load(RAX, RBX, 0x40); check("load_d8", e, &ref_load_d8, &ref_load_dneg); }
	{ X64Emitter e; e.load(RAX, RBX, -0x40); check("load_dneg", e, &ref_load_dneg, &ref_load_d32); }
	{ X64Emitter e; e.load(RAX, RBX, 0x12345678); check("load_d32", e, &ref_load_d32, &ref_load_rsp); }
	{ X64Emitter e; e.load(RAX, RSP, 0x40); check("load_rsp", e, &ref_load_rsp, &ref_load_r13); }
	{ X64Emitter e; e.load(RAX, R13, 0); check("load_r13", e, &ref_load_r13, &ref_store_d8); }
	{ X64Emitter e; e.store(RBX, 0x40, RAX); check("store_d8", e, &ref_store_d8, &ref_loadq_d8); }
	{ X64Emitter e; e.loadQ(RAX, RBX, 0x40); check("loadq_d8", e, &ref_loadq_d8, &ref_push_rbx); }
	{ X64Emitter e; e.push(RBX); check("push_rbx", e, &ref_push_rbx, &ref_push_r12); }
	{ X64Emitter e; e.push(R12); check("push_r12", e, &ref_push_r12, &ref_pop_rbx); }
	{ X64Emitter e; e.pop(RBX); check("pop_rbx", e, &ref_pop_rbx, &ref_ret); }
	{ X64Emitter e; e.ret(); check("ret", e, &ref_ret, &ref_seq); }

	// Branch loop through the label/fixup pass:  back: add eax,ecx ; cmp eax,edx ; jge done ; jmp back ; done: ret
	{
		X64Emitter e;
		Label back = e.newLabel(), done = e.newLabel();
		e.bind(back);
		e.add(RAX, RCX);
		e.cmp(RAX, RDX);
		e.jcc(CC_GE, done);
		for (int i = 0; i < 130; ++i) { e.nop(); }		// pad past 127 bytes -> rel32 (matches the padded reference)
		e.jmp(back);
		e.bind(done);
		e.ret();
		e.finalize();
		check("seq(branches)", e, &ref_seq, &ref_seq_end);
	}

	// SSE scalar-float forms (XMM operands reuse the Reg 0..15 encodings). xmm1/xmm2 are the low-8 pool range; xmm9
	// exercises the REX.R path on the SSE opcode map. movssReg lowers to movaps (0F 28) since the merge false-dep fix.
	const Reg X1 = static_cast<Reg>(1), X2 = static_cast<Reg>(2), X9 = static_cast<Reg>(9);
	{ X64Emitter e; e.movssLoad(X1, RBX, 0x40); check("movss_load", e, &ref_movss_load, &ref_movss_store); }
	{ X64Emitter e; e.movssStore(RBX, 0x40, X1); check("movss_store", e, &ref_movss_store, &ref_movaps_rr); }
	{ X64Emitter e; e.movssReg(X1, X2); check("movaps_rr", e, &ref_movaps_rr, &ref_movaps_ext); }
	{ X64Emitter e; e.movssReg(X9, X2); check("movaps_ext", e, &ref_movaps_ext, &ref_addss); }
	{ X64Emitter e; e.addss(X1, X2); check("addss", e, &ref_addss, &ref_subss); }
	{ X64Emitter e; e.subss(X1, X2); check("subss", e, &ref_subss, &ref_mulss); }
	{ X64Emitter e; e.mulss(X1, X2); check("mulss", e, &ref_mulss, &ref_divss); }
	{ X64Emitter e; e.divss(X1, X2); check("divss", e, &ref_divss, &ref_ucomiss); }
	{ X64Emitter e; e.ucomiss(X1, X2); check("ucomiss", e, &ref_ucomiss, &ref_xorps); }
	{ X64Emitter e; e.xorps(X1, X1); check("xorps", e, &ref_xorps, &ref_cvtsi2ss); }
	{ X64Emitter e; e.cvtsi2ss(X1, RCX); check("cvtsi2ss", e, &ref_cvtsi2ss, &ref_cvttss2si); }
	{ X64Emitter e; e.cvttss2si(RAX, X1); check("cvttss2si", e, &ref_cvttss2si, &ref_movd_to_xmm); }
	{ X64Emitter e; e.movdToXmm(X1, RCX); check("movd_to_xmm", e, &ref_movd_to_xmm, &ref_movd_from_xmm); }
	{ X64Emitter e; e.movdFromXmm(RAX, X1); check("movd_from_xmm", e, &ref_movd_from_xmm, &ref_roundss); }
	{ X64Emitter e; e.roundss(X1, X2, 1); check("roundss", e, &ref_roundss, &ref_float_end); }

	// RIP-relative float literal pool: three loads (xmm1 no REX, xmm9 REX.R, xmm2 REUSING the first literal - proves
	// deduplication), ret, then emitLiteralPool (2 nops pad the 26 code bytes to a 4-aligned pool) and the rel32 fixups.
	{
		X64Emitter e;
		e.movssRip(X1, e.floatLiteral(0x3F800000u));
		e.movssRip(X9, e.floatLiteral(0x40490FDBu));
		e.movssRip(X2, e.floatLiteral(0x3F800000u));
		e.ret();
		e.emitLiteralPool();
		e.finalize();
		check("pool(movssRip)", e, &ref_pool_seq, &ref_pool_seq_end);
	}

	// Negative control: a deliberately wrong encoding must be flagged (proves the harness bites).
	{
		X64Emitter e; e.movImm(RAX, 0x12345679u);	// off-by-one immediate vs ref_mov_imm
		const size_t refLen = static_cast<size_t>(&ref_mov_imm64 - &ref_mov_imm);
		const bool caught = !(e.size() == refLen && std::memcmp(e.code(), &ref_mov_imm, refLen) == 0);
		std::printf("  %-14s %s\n", "neg_control", caught ? "MATCH" : "MISMATCH");
		if (!caught) { ++failures; }
	}

	std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
	return failures == 0 ? 0 : 1;
}
