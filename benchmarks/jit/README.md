# JIT spike A3 — interpreter vs. hand-written baseline JIT

This is **Phase −1 de-risking spike A3** from the native-compiler research
([docs/JitCompilerResearch.md](../../docs/JitCompilerResearch.md), section 11.0). It answers the go/no-go question:
*does a load-time baseline JIT actually beat the interpreter by enough to be worth building?*

It does **not** contain a JIT. It hand-writes the machine code a baseline JIT *would* emit for a few small kernels, runs
it against the real GAZL interpreter running the equivalent `.gazl`, and compares speed and results.

## What it runs

Each kernel is executed several ways and timed (min of N runs):

- **interpreter (GAZL)** — the real `Assembler` + `Processor`. The reference, and the baseline to beat.
- **JIT v1** — hand-written AArch64 with locals kept in memory (per-op load/store); models a JIT with *no* register
  allocation.
- **JIT v2** — hand-written AArch64 with locals in registers; models a baseline JIT *with* simple register allocation.
  Includes the per-basic-block fuel check (`subs/b.mi`) and, for memory kernels, the software bounds check per access
  (no guard pages — matching the sandbox design).
- **C −O2** — the same kernel in C, compiled `-O2`; the optimizing-compiler ceiling (note: no bounds check, so it is a
  deliberately generous ceiling, not an apples-to-apples target).

All variants must print `MATCH` (bit-identical results), which validates both the hand-written code and that everyone
does the same work.

Kernels: a dispatch-bound arithmetic loop (LCG mix), a sequential bounds-checked read+write over buffers of several
sizes, and a random-access (cache-missing) buffer walk.

## Running

```
bash benchmarks/jit/buildAndRunJitBenchA3.sh
```

**AArch64 only** (Apple Silicon / Linux arm64). The x64 and Windows lowerings are exactly what the real JIT will emit,
so they are left as future work; the `.cmd` prints a notice and exits.

## Representative results (Apple Silicon, one machine — numbers are illustrative)

Besides v1/v2, the benchmark includes two **"registers as a block-local cache"** variants (the §5.7 design
discussion): *cache label-clear* reloads/flushes all cached slots at every basic-block boundary (no pinned registers);
*cache cons* additionally flushes/invalidates around **every** `PEEK`/`POKE` (fully conservative: sound with no
aliasing spec at all); *cache prov* flushes only around frame-derived pointers (provenance rule).

```
ARITHMETIC                            speedup vs interpreter
  JIT v1 (mem-resident)                 2.8x
  JIT cache (label-clear)               3.9x
  JIT v2 (pinned registers)             6.1x
  C -O2 (ceiling)                       8.4x

SEQ MEMORY (bounds-checked)             32 KB / 4 MB / 64 MB
  JIT cache cons (flush@every ptr op)   ~5.2-5.5x
  JIT cache prov (label-clear only)     ~5.7-6.4x
  JIT v2 (pinned registers)             ~12-13x
  C -O2                                 ~18-19x

RND MEMORY (16 MB, cache-missing)
  JIT v2                                ~6-7x
  C -O2                                ~11-12x
```

Reading of the cache variants (measured, not estimated): the **label-clear is the expensive part** (pinning roughly
doubles the memory-kernel numbers), while the **fully conservative pointer flush costs only ~10-20 %** over
provenance-scoped flushing — store-to-load forwarding makes the flush/reload traffic cheap, at least on this core.
Design consequence: the aliasing spec's real payoff is enabling *pinned* registers (the 2x lever), not avoiding
pointer-op flushes (the ~15 % lever).

## Findings (see the research doc §9 for the full discussion)

- A register-allocating baseline JIT gives **~6× or better over the interpreter across every regime tested**; even the
  no-register-allocation v1 gives ~2.8×. A3 passes.
- **The software bounds check is cheap** — two predicted-not-taken instructions per access. Memory kernels still hit
  6–13×; the gap to the (unchecked) C ceiling is only ~1.5–2×. That is the price of guard-page-free safety, and it is
  affordable.
- **The memory wall does not flatten the win.** Even random, cache-missing access keeps ~6.9×, because the interpreter's
  per-instruction dispatch overhead dominates over memory-latency stalls. Only true DRAM-bandwidth saturation would
  erode it, which the (slow) interpreter never reaches and prefetcher-friendly DSP streaming avoids.
- **What compresses the win:** long-latency arithmetic both engines must pay (the ~6× arithmetic case vs. the higher
  memory numbers, because of the multiply latency). Float-divide / transcendental-heavy code would sit at the low end,
  ~3–4×, but never below worthwhile.
- Sanity check: the interpreter runs at ~10–12% of optimizing-native speed here, matching GAZL's own documented
  "10–25% of fully optimized machine code" claim — so this is real headroom, not a strawman baseline.

## Limitations

Integer kernels only; one machine; the hand-written v2 is close to optimal, so a real simple-register-allocator JIT
would land perhaps 10–30% behind these numbers. None of that changes the go decision.
