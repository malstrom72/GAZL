# GAZL JIT - Differential Fuzzer Plan

Plan for a JIT-vs-interpreter differential fuzzer. Today's `GAZLFuzz` (tools/buildGazlFuzz.sh + the `LIBFUZZ`
block in tools/GAZLCmd.cpp) feeds raw bytes as GAZL source text, assembles them, and runs ONLY the interpreter at
high fuel to catch crashes. It does not link the JIT at all, and random bytes almost never assemble, so the JIT is
untested by fuzzing. The 25-kernel three-way lockstep in tools/GAZLJitLowerTest.cpp is currently the JIT's only
correctness net: good breadth, but a fixed, hand-written set. This plan closes that gap.

Related: the v2 register allocator (docs/JitCompilerResearch.md §5.7, docs/JitAliasingRegAlloc.md) is the code most
in need of adversarial coverage - its coherence machinery (flush at block boundaries, spill/invalidate around pointer
ops, cache empty at every safepoint, cross-file slot resolution, main-path trap flush) is exactly the bug class a
differential fuzzer catches systematically.

## 1. Goal

For every generated program: assemble once, run the interpreter and the JIT from an identical initial state, and
assert that the **final memory image and status are byte-identical** - at full fuel AND tiny fuel. Any divergence is a
bug: `abort()` so libFuzzer captures and minimizes the input. This makes the coherence-bug class (the div/mod
main-path-trap bug, cross-file slots, frame aliasing, suspend-at-safepoint) findable on BOTH backends, not just where
a hand-written kernel happens to look.

## 2. Architecture: three pieces

### 2a. The oracle (differential runner) - the high-certainty part

Reuse the proven pattern already in `GAZLJitLowerTest` (`runInterpreter` + JIT run + `imagesEqual`). Per program:

```
assemble(text) -> AssembledProgram program            // once
if (!jitAvailable()) return;                           // no JIT on this host -> nothing to diff
NativeJitCompiler().compile(program, module);          // throws => the generator emitted an uncovered opcode = bug
results = {}
for engine E in { interpreter, jit }:
    for fuel F in { HUGE, tiny }:                       // tiny forces suspend/resume at every safepoint
        image = clone(initialImage)                    // each run gets its OWN memory buffer
        E.enterCall(main); do { E.resetTimeOut(F); } while (E.run() == TIME_OUT);
        results += (status, image)
assert all four (status, image) are identical          // interp-full is the oracle
```

The four-way cross (interp/jit x full/tiny) also proves suspend/resume transparency on both engines for free. On a
mismatch: print the generated source + inputs + the first diverging word, then `abort()`.

Engine construction (current API): interpreter `Processor p(program, CALL_STACK_SIZE, callStack, NATIVE_TABLE, 0)`;
JIT `NativeJitCompiler().compile(program, module)` then `JitProcessor jp(module, program, ...)`. Both drive the same
host loop and the same native table.

### 2b. Program generation - where yield lives

Two phases.

- **Phase 1 (cheap, ship first): corpus-seeded text mutation.** Keep the existing text -> assemble path; just add the
  oracle from 2a. Seed libFuzzer's corpus with the golden corpus `.gazl` + the bench kernels. libFuzzer mutates the
  text; the assembler rejects most, but the valid mutations exercise the JIT immediately. Low yield, but it is a
  working JIT differential fuzzer with almost no new code, reusing everything.

- **Phase 2 (the real engine): a structured generator.** Decode the fuzzer bytes as a stream of choices into an
  **always-assembler-valid** GAZL program - a pure function `bytes -> GAZL source`. High yield (every input runs the
  JIT), and the place to bias toward the coherence-hard patterns. Staged, mirroring how the JIT was built:
  - **G1 straight-line**: one `main`, a fixed frame (~12 int + 6 float locals, one 16-word `LOCA`, a few
    globals/consts), then K random ops over those slots from the covered set - value / float / shift / constant-address
    memory. No control flow. Hammers the within-block cache and cross-class slot reuse with zero control-flow generation.
  - **G2 + memory and traps**: `ADRL` a local then pointer `PEEK`/`POKE`/`GETL`/`SETL` at random (in- and out-of-bounds)
    indices, and `DIVI`/`MODI` with possibly-zero divisors - exercises `spillDirtyResident`/`invalidateAll` and the
    main-path trap flush.
  - **G3 + control flow**: a bounded counter loop wrapper (`FORi`) and forward `if`-branches to generated labels - block
    boundaries and suspend across loops.
  - **G4 + calls**: a couple of generated helper functions + `CALL` (direct / indirect / native), including recursion
    (both engines trap identically on ipStack overflow).

### 2c. Generation biases

Deliberately over-represent what breaks coherence:
- reuse one slot as int THEN float (drives `evictOtherClass`);
- take `ADRL` of a local then write through it (frame aliasing);
- pack more simultaneously-live values than the 4-6 register pool (forces eviction and spills);
- a possibly-zero divisor AFTER a chain of dirty defines (the main-path trap-flush case);
- always include a tiny-fuel pass (suspend mid-block).

## 3. Determinism (non-negotiable for a differential fuzzer)

- **Natives**: a curated table of PURE functions only (`sqrt` / `log` / `atan2` / `testMul`). Drop `input` (reads
  external state) and `abort`. Same table for both engines.
- **Memory init**: both engines start from the SAME fixed initial image (memset to a constant), so uninitialized reads
  are deterministic.
- **Float**: interpreter (C++ `float`) vs JIT (`fadd` / `addss`) on the SAME host are bit-identical for
  +/-/*//sqrt/floor; `FTOI` saturation and NaN handling were hand-matched to the interpreter - the fuzzer verifies
  exactly this, and any diff is a real bug.
- **Generator is a pure function of the input bytes**, so a crashing input reproduces exactly. Add a `--dump` mode that
  re-decodes a saved crash input back to `.gazl` for human inspection.

## 4. Build and platform

- `buildGazlFuzz.sh` must add `-DGAZL_JIT` + `src/GAZLJit.cpp` + the host backend (`GAZLJitArm64.cpp` /
  `GAZLJitX64.cpp`) + a `GAZLJitMem*` backend, and a new `-DJITDIFF` selecting the diff `LLVMFuzzerTestOneInput`. No new
  file: it rides in `GAZLCmd.cpp` under the existing `#if LIBFUZZ`.
- **ASan + W^X**: ASan instruments the harness / assembler / interpreter (catches C++ bugs) but NOT the JIT's machine
  code - fine, because the interpreter side plus the diff catch JIT semantic bugs. Each iteration
  `makeExecutable` / `freeExecutable` (mmap `MAP_JIT` on macOS) churns a page; cheap, but reuse one page if it
  bottlenecks.
- Runs natively per host: arm64 fuzzing on the Mac, x64 fuzzing on the Windows box (the register-pressure backend,
  where it matters most).

## 5. Rollout

1. Phase 1 (oracle + corpus mutation) - a working JIT differential fuzzer, immediately.
2. G1 generator (straight-line) - the big yield jump; likely shakes out the most.
3. G2 -> G3 -> G4 - expand to memory/traps, control flow, calls.
4. Wire into CI as a short smoke run per push (e.g. 60 s), plus longer soak runs.
