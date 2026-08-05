# GAZL JIT Benchmark Suite - Plan

Status: in progress. P0 primitives landing; workload ports (P1+) pending.

## Why

`tools/bench.sh` today has a micro lane (per-op 128M-op loops, interpreter `-Os`) and a macro lane
(perfTest/perfTest1/perfTest2, interpreter `-O2` vs the native JIT in the same binary, toggled by `--jit`). That is
enough to show the JIT works (≈1.5-5.4× over an optimally-built interpreter, byte-identical output) but is thin: three
whole programs, one of them call-bound `fib`, and it measures only wall-time. We want a suite that (a) spans the JIT's
real behaviour, (b) measures more than wall-time, and (c) doubles as an extended differential-correctness net.

Constraint that shapes everything: **GAZL has no GC and no objects.** So the standard OO/allocation-heavy suites do not
port cleanly - but their **numeric and algorithmic** kernels are a near-perfect fit for a baseline JIT.

## What to measure (beyond wall-time)

Per program, interpreter (`-O2`, the reference) vs `--jit` in the same binary:

- **Speedup** vs the `-O2` interpreter (headline).
- **JIT compile time** - the compile-once load cost. `--jit-stats` prints `compile_ms` (measured so far at ~0.03-0.05 ms
  for whole programs - negligible vs run time).
- **Emitted code size** - `--jit-stats` prints `code_bytes` (track bloat as lowering changes).
- **Full-JIT assertion** - fail the row if the program falls back (catch a coverage regression).
- **Checksum, diffed interp-vs-JIT and vs a stored golden** - correctness is the point.
- **Warmup delta** - confirm the compile-once model (near-zero steady-state warmup).

`--jit-stats` (compile time + code size to stderr, line prefixed `jitstats`) is the one new GAZLCmd primitive - done.

## Workload taxonomy (deliberately span the axes)

- **Integer arithmetic / loops** - Sieve, Fannkuch, matmul. *JIT sweet spot (~5×).*
- **Float / transcendental** - NBody, Mandelbrot, Spectral-norm, SciMark. *(2-4×)*
- **Call-bound / recursion** - Fib, Ackermann, Tak, Towers, Queens. *(~1.5×; #19 direct-threading target.)*
- **Control-flow / branch-heavy** - a `SWCH`-dispatch kernel, a tiny bytecode VM in Impala. *(jump table + branches.)*
- **Memory / stride** - array scan, SOR stencil, sparse mat-vec. *(bounds-check + load/store bound.)*

## Borrowing from standard suites

Clean-room **reimplement the algorithms** in Impala (algorithms aren't copyrightable; a fresh port sidesteps licensing).
Port the kernel, not the OO scaffolding.

- **SciMark2** (numeric, no-GC) → FFT, SOR stencil, Monte-Carlo π, sparse mat-vec, LU. Canonical numeric JIT yardstick,
  gives a composite float score. **Anchor suite.**
- **Computer Language Benchmarks Game** → spectral-norm, fannkuch-redux, n-body, mandelbrot. *Skip* binary-trees /
  k-nucleotide / pidigits (GC / hashing / bignum).
- **Are We Fast Yet?** → the algorithmic micros: Sieve, Queens, Towers, Permute, Mandelbrot, NBody. *Skip* the
  object/GC-centric ones (Richards, DeltaBlue, Havlak, Json, Storage, Bounce, List).
- **Classics** - Fib / Ackermann / Tak (call stressors), matmul, a tiny bytecode VM (control-flow + switch stressor).

Net target set: **SciMark five + ~6 CLBG/AWFY kernels + ~4 call/control-flow stressors ≈ 15 programs.**

## Correctness built in

Every benchmark's `main` computes a **deterministic checksum** (fixed seeds; integer or bit-exact float reduction) and
prints it. The harness runs interp and `--jit`, diffs the checksum, checks it against a stored golden, and asserts no
fallback. The perf suite thus doubles as a much broader differential-correctness suite than today's 25 kernels.

## Infrastructure

```
benchmarks/suite/
  sources/*.impala      # the ports (regen goldens via the Impala compiler)
  golden/*.gazl         # compiled bytecode
  expected/*.checksum   # golden result per program
```

`tools/bench.sh` gains a **suite lane**: per program → interp + jit timings, `compile_ms`, `code_bytes`, checksum diff,
full-JIT assert, and drift vs a committed baseline (flag > ±X%). CI-friendly.

## Phasing

- **P0 - harness primitives.** `--jit-stats` flag (done). Next: suite-lane skeleton (checksum diff + full-JIT assert +
  stats columns) validated on the existing 3 programs; baseline file.
- **P1 - SciMark five** → composite float score.
- **P2 - CLBG/AWFY kernels** (~6).
- **P3 - call/control-flow tier** (fib/ack/tak/switch-VM) - the #19/#20 measurement targets.
- **P4 - reporting/CI** (baseline drift, JSON out).

## Open decisions

1. **Anchor** on SciMark2 + a curated CLBG/AWFY/classics spread (recommended).
2. **Port path:** Impala where expressible (higher-level, needs the compiler to regen goldens); hand-`.gazl` only for
   control-flow stressors Impala can't express.
3. **Scope:** full P0-P4 vs start minimal (P0 + P1) and grow.
