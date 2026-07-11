// Hand-written AArch64 machine code modelling what the proposed GAZL baseline JIT would
// emit, for Phase -1 de-risking spike A3 (see docs/JitCompilerResearch.md, section 11.0
// and the worked example in section 5.8). Each function is the register/ABI-pinned
// lowering of a small GAZL kernel, including the per-basic-block fuel check (subs/b.mi).
//
// This is a .S (capital) so the C preprocessor runs; symbols use the platform C-symbol
// prefix so it links on both macOS (Mach-O, leading underscore) and Linux (ELF) arm64.
#if defined(__APPLE__)
# define GLOBL(n) .globl _##n
# define ENTRY(n) _##n:
#else
# define GLOBL(n) .globl n
# define ENTRY(n) n:
#endif

        .text
        .align  2

// ---------------------------------------------------------------------------
// Arithmetic kernel:  acc = acc*1103515245 + 12345 ; sum += acc & 0x3FFFFFFF
// v2 = register-allocated locals.  int bench_v2(int n /*w0*/, int fuel /*w1*/)
// ---------------------------------------------------------------------------
        GLOBL(bench_v2)
ENTRY(bench_v2)
        mov     w9,  #12345             // acc
        mov     w10, #0                 // sum
        mov     w11, #0                 // i
        movz    w13, #0x4E6D
        movk    w13, #0x41C6, lsl #16   // K = 1103515245
        movz    w14, #12345             // C
        movz    w15, #0xFFFF
        movk    w15, #0x3FFF, lsl #16   // mask = 0x3FFFFFFF
Lv2_loop:
        subs    w1, w1, #5              // fuel -= blockWeight(5)
        b.mi    Lv2_done
        mul     w9, w9, w13             // acc *= K
        add     w9, w9, w14             // acc += C
        and     w12, w9, w15            // tmp = acc & mask
        add     w10, w10, w12           // sum += tmp
        add     w11, w11, #1            // ++i
        cmp     w11, w0                 // i < n ?
        b.lt    Lv2_loop
Lv2_done:
        mov     w0, w10
        ret

// v1 = memory-resident locals (per-op load/store), models a JIT with no register alloc.
// int bench_v1(int n /*w0*/, int fuel /*w1*/, int* frame /*x2*/)   frame: acc,sum,i,tmp,n
        GLOBL(bench_v1)
ENTRY(bench_v1)
        mov     w3, #12345
        str     w3, [x2, #0]            // acc = 12345
        str     wzr, [x2, #4]           // sum = 0
        str     wzr, [x2, #8]           // i = 0
        str     w0, [x2, #16]           // n
        movz    w13, #0x4E6D
        movk    w13, #0x41C6, lsl #16
        movz    w14, #12345
        movz    w15, #0xFFFF
        movk    w15, #0x3FFF, lsl #16
Lv1_loop:
        subs    w1, w1, #5
        b.mi    Lv1_done
        ldr     w3, [x2, #0]            // MULi acc acc #K
        mul     w3, w3, w13
        str     w3, [x2, #0]
        ldr     w3, [x2, #0]            // ADDi acc acc #C
        add     w3, w3, w14
        str     w3, [x2, #0]
        ldr     w3, [x2, #0]            // ANDi tmp acc #mask
        and     w3, w3, w15
        str     w3, [x2, #12]
        ldr     w3, [x2, #4]            // ADDi sum sum tmp
        ldr     w4, [x2, #12]
        add     w3, w3, w4
        str     w3, [x2, #4]
        ldr     w3, [x2, #8]            // FORi i n @loop
        add     w3, w3, #1
        str     w3, [x2, #8]
        ldr     w4, [x2, #16]
        cmp     w3, w4
        b.lt    Lv1_loop
Lv1_done:
        ldr     w0, [x2, #4]
        ret

// ---------------------------------------------------------------------------
// Sequential memory kernel:  idx=i&mask ; x=buf[idx]+prev ; buf[idx]=x ; prev=x
// v2, WITH the software bounds check emitted per access (no guard pages).
// int bench_mem_v2(int n /*w0*/, int fuel /*w1*/, int* buf /*x2*/, int bufsize /*w3*/)
// ---------------------------------------------------------------------------
        GLOBL(bench_mem_v2)
ENTRY(bench_mem_v2)
        mov     w9, #12345              // prev
        mov     w10, #0                 // i
        sub     w13, w3, #1             // mask = bufsize-1
Lm2_loop:
        subs    w1, w1, #6              // fuel -= blockWeight(6)
        b.mi    Lm2_done
        and     w11, w10, w13           // idx = i & mask
        cmp     w11, w3                 // read bounds check
        b.hs    Lm2_trap
        ldr     w12, [x2, w11, uxtw #2] // x = buf[idx]
        add     w12, w12, w9            // x += prev
        cmp     w11, w3                 // write bounds check
        b.hs    Lm2_trap
        str     w12, [x2, w11, uxtw #2] // buf[idx] = x
        mov     w9, w12                 // prev = x
        add     w10, w10, #1
        cmp     w10, w0
        b.lt    Lm2_loop
Lm2_done:
        mov     w0, w9
        ret
Lm2_trap:
        mov     w0, #-999               // unreachable (idx always in range)
        ret

// ---------------------------------------------------------------------------
// Random-access memory kernel: idx from an LCG's high bits -> cache-missing scatter.
// st=st*K+C ; idx=(uint)st>>10 ; x=buf[idx]+st ; buf[idx]=x ; sum+=x
// int bench_rnd_v2(int n /*w0*/, int fuel /*w1*/, int* buf /*x2*/, int bufsize /*w3*/)
// ---------------------------------------------------------------------------
        GLOBL(bench_rnd_v2)
ENTRY(bench_rnd_v2)
        mov     w9,  #12345             // st
        mov     w10, #0                 // sum
        mov     w11, #0                 // i
        movz    w13, #0x4E6D
        movk    w13, #0x41C6, lsl #16   // K
        movz    w15, #12345             // C
Lr2_loop:
        subs    w1, w1, #8              // fuel -= blockWeight(8)
        b.mi    Lr2_done
        mul     w9, w9, w13             // st *= K
        add     w9, w9, w15             // st += C
        lsr     w12, w9, #10            // idx = (uint)st >> 10   (22-bit)
        cmp     w12, w3
        b.hs    Lr2_trap
        ldr     w14, [x2, w12, uxtw #2] // x = buf[idx]
        add     w14, w14, w9            // x += st
        cmp     w12, w3
        b.hs    Lr2_trap
        str     w14, [x2, w12, uxtw #2] // buf[idx] = x
        add     w10, w10, w14           // sum += x
        add     w11, w11, #1
        cmp     w11, w0
        b.lt    Lr2_loop
Lr2_done:
        mov     w0, w10
        ret
Lr2_trap:
        mov     w0, #-999
        ret

// ---------------------------------------------------------------------------
// "Registers as cache" design variants (see JitCompilerResearch.md 5.7 discussion).
// The cache is block-local: cleared at every label, dirty entries flushed at the
// back-edge. No pinned registers, no liveness (dead slots stored anyway).
// ---------------------------------------------------------------------------

// Arithmetic kernel, pure block cache (label-clear each iteration).
// Models Magnus's v2.0 on a pointer-free loop: isolates the label-clear cost.
// Per iteration: 3 cache-fill loads (acc,sum,i) + 4 flush stores (acc,tmp,sum,i).
// int bench_cache(int n /*w0*/, int fuel /*w1*/, int* frame /*x2*/)
        GLOBL(bench_cache)
ENTRY(bench_cache)
        mov     w3, #12345
        str     w3, [x2, #0]            // acc = 12345
        str     wzr, [x2, #4]           // sum = 0
        str     wzr, [x2, #8]           // i = 0
        movz    w13, #0x4E6D
        movk    w13, #0x41C6, lsl #16   // K
        movz    w14, #12345             // C
        movz    w15, #0xFFFF
        movk    w15, #0x3FFF, lsl #16   // mask
Lbc_loop:                               // block leader: cache empty, memory current
        subs    w1, w1, #5
        b.mi    Lbc_done
        ldr     w3, [x2, #0]            // fill: acc
        mul     w3, w3, w13             // acc *= K   (reg, dirty)
        add     w3, w3, w14             // acc += C   (forwarding hit)
        and     w4, w3, w15             // tmp def    (reg, dirty, no load: write-first)
        ldr     w5, [x2, #4]            // fill: sum
        add     w5, w5, w4              // sum += tmp (hit)
        ldr     w6, [x2, #8]            // fill: i
        add     w6, w6, #1              // ++i
        str     w3, [x2, #0]            // back-edge flushAll: acc
        str     w4, [x2, #12]           //   tmp (dead, but no liveness info)
        str     w5, [x2, #4]            //   sum
        str     w6, [x2, #8]            //   i
        cmp     w6, w0
        b.lt    Lbc_loop
Lbc_done:
        ldr     w0, [x2, #4]
        ret

// Sequential memory kernel, block cache, CONSERVATIVE pointer policy:
// flush dirty before every PEEK/POKE, invalidate everything after a POKE.
// (Spec-free: sound without any aliasing rule.) bufptr reload is generously
// omitted (modelled as hoisted), so this slightly flatters the design.
// int bench_mem_cache_cons(int n, int fuel, int* buf /*x2*/, int bufsize /*w3*/, int* frame /*x4*/)
// frame: [0]=prev [4]=i [8]=idx [12]=x
        GLOBL(bench_mem_cache_cons)
ENTRY(bench_mem_cache_cons)
        mov     w9, #12345
        str     w9, [x4, #0]            // prev = 12345
        str     wzr, [x4, #4]           // i = 0
        sub     w13, w3, #1             // mask (constant)
Lmc_loop:                               // cache empty
        subs    w1, w1, #6
        b.mi    Lmc_done
        ldr     w9, [x4, #4]            // fill: i
        and     w10, w9, w13            // idx def (dirty)
        str     w10, [x4, #8]           // PEEK barrier: flush dirty (idx)
        cmp     w10, w3                 // bounds check (read)
        b.hs    Lmc_trap
        ldr     w11, [x2, w10, uxtw #2] // x = buf[idx]  (x def, dirty)
        ldr     w12, [x4, #0]           // fill: prev
        add     w11, w11, w12           // x += prev
        str     w11, [x4, #12]          // POKE barrier: flush dirty (x)
        cmp     w10, w3                 // bounds check (write)
        b.hs    Lmc_trap
        str     w11, [x2, w10, uxtw #2] // buf[idx] = x
                                        // POKE invalidates the whole cache
        ldr     w11, [x4, #12]          // MOVi prev x: reload x
        mov     w12, w11                // prev def (dirty)
        ldr     w9, [x4, #4]            // FORi: reload i
        add     w9, w9, #1
        str     w12, [x4, #0]           // back-edge flushAll: prev
        str     w9, [x4, #4]            //   i
        cmp     w9, w0
        b.lt    Lmc_loop
Lmc_done:
        ldr     w0, [x4, #0]
        ret
Lmc_trap:
        mov     w0, #-999
        ret

// Sequential memory kernel, block cache, PROVENANCE pointer policy:
// bufptr is global-born, so PEEK/POKE through it do NOT flush; only the
// label-clear at the back-edge remains.
// int bench_mem_cache_prov(int n, int fuel, int* buf /*x2*/, int bufsize /*w3*/, int* frame /*x4*/)
        GLOBL(bench_mem_cache_prov)
ENTRY(bench_mem_cache_prov)
        mov     w9, #12345
        str     w9, [x4, #0]            // prev = 12345
        str     wzr, [x4, #4]           // i = 0
        sub     w13, w3, #1             // mask
Lmp_loop:                               // cache empty
        subs    w1, w1, #6
        b.mi    Lmp_done
        ldr     w9, [x4, #4]            // fill: i
        and     w10, w9, w13            // idx def (dirty)
        cmp     w10, w3                 // PEEK: no barrier (global-born pointer)
        b.hs    Lmp_trap
        ldr     w11, [x2, w10, uxtw #2] // x = buf[idx]
        ldr     w12, [x4, #0]           // fill: prev
        add     w11, w11, w12           // x += prev
        cmp     w10, w3                 // POKE: no barrier
        b.hs    Lmp_trap
        str     w11, [x2, w10, uxtw #2] // buf[idx] = x
        mov     w12, w11                // prev = x (reg-to-reg)
        add     w9, w9, #1              // ++i
        str     w10, [x4, #8]           // back-edge flushAll: idx (dead, stored)
        str     w11, [x4, #12]          //   x (dead, stored)
        str     w12, [x4, #0]           //   prev
        str     w9, [x4, #4]            //   i
        cmp     w9, w0
        b.lt    Lmp_loop
Lmp_done:
        ldr     w0, [x4, #0]
        ret
Lmp_trap:
        mov     w0, #-999
        ret
