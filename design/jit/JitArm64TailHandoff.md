# Handoff: verify the arm64 TAIL lowering on Apple Silicon

Status: DONE and validated on Apple Silicon (2026-09-13). See Result at the end. Review item 1 was fixed and the
300k-deep arm64 soak is clean. The text below is the original handoff, kept as written.

Since then this file has served as the running arm64 VERIFICATION LOG rather than a TAIL document: the dated
sections at the end cover `ce10da6`, `K_DEADTAIL`, and most recently the cross-register-file bridge and the
per-backend bridge policy (2026-09-17).

The arm64 `TAIL` lowering was written on a Windows x64 box in the GAZL2 merge
(`6c6161af`) and has **never been compiled, let alone run** - there is no arm64 toolchain there. The x64
counterpart of the same change is fully verified. This file is the review checklist and the gate.

Nothing else about GAZL 2 is waiting on this: a v2 engine without a working arm64 `TAIL` still declines the
module to the interpreter rather than miscompiling it, because an uncovered finalized opcode throws
`JitException`. In a DEBUG build it asserts first (`throwUnlowerableOpcode`), which is how you will notice.

## What the task is

1. Compile `src/GAZLJitArm64.cpp` on Apple Silicon and fix whatever does not build.
2. Run the gates below and make them pass.
3. Work through the review items - the x64 side is the oracle for intent, not for encoding.

## What was added

Two things in `src/GAZLJitArm64.cpp`, mirroring `emitTailWindow` / `case OP_TAIL_CC` / `case OP_TAIL_VC` in
`src/GAZLJitX64.cpp`:

- **`emitTailWindow(e, window, frame)`** - sits between `subImmXBig` and the `slotNear` block. Computes the
  destination base into `X12` (`dsp - frame*4`), copies `window` words up from `[X1 + k*4]` to
  `[X12 + k*4]` through `W11`, then rewinds `X1` by `frame*4`.
- **`case OP_TAIL_CC` / `case OP_TAIL_VC`** - immediately above `case OP_CALL_CVC` in `lowerFunction`. The
  direct form slides and branches to `entryLabels[ordinal]`. The indirect form reads the target slot FIRST
  (`loadSlot` into `W9`), bounds-checks the ordinal against `functionCount` -> `BAD_CALL`, resolves
  `funcEntries[ordinal]` into `X9`, then slides and `br`s.

Semantics, from the interpreter's `tail:` block in `src/GAZL.cpp`: the `%0..window-1` window moves DOWN onto
this frame's base, `dsp` rewinds to it, and the callee's `FUNC` re-stacks from OUR base. No return is pushed,
so the callee's `RETU` returns to our caller - that is what makes self-recursion run in constant stack. The
destination is always below the source, so an ascending copy is overlap-safe at any size.

The register cache needs no special handling: `TAIL` is not in `cacheLowered`, so `lowerFunction` already
emits a full `cache.barrier()` before it.

## Review items, most dangerous first

1. **`ldrW`/`strW` cap the offset at 4095 words, and the arm64 path has no fallback.** Both assert
   `(byteOffset >> 2) < 0x1000`. `emitTailWindow` unrolls unconditionally, so a window of 4096 words or more
   asserts in debug and emits a WRONG instruction in release. The x64 side does not have this hazard: it
   unrolls only to 8 words and uses `rep movsd` above that. Nothing in the assembler caps `paramsSize` - it
   is derived from the highest `%N` slot referenced (`src/GAZL.cpp`, the `paramsSize = maximum(...)` sites) -
   so the size is attacker-reachable in principle even if no real program comes near it. Fix by mirroring the
   x64 structure: a register-offset copy loop above some threshold, or an explicit `JitException` decline.
   **This is the one item that is a real defect rather than a question.**
2. **Register choices.** `X12` for the destination base and `W11` for the word in flight, chosen so `X9` stays
   free to carry the resolved target across the slide in the indirect form. The file's own comment says W13 is
   the slot-index scratch kept distinct from the W9..W12 operand scratches, and `subImmXBig` uses W15
   internally. `ARM64_GENERAL_POOL` is `{ W5, W6, W7, W8, W16, W17 }`, so none of these collide with a cached
   line - and the barrier has spilled the pool anyway. Confirm on real hardware rather than from the comment.
3. **`subImmXBig` is called twice** in `emitTailWindow` (once for `X12`, once for `X1`) with the same
   immediate. Correct but redundant; a `movX X1, X12` would be shorter if the emitter has one.
4. **Frame of zero.** `frame == 0` makes destination and source identical and the copy a self-copy. Harmless,
   and the `if (frame != 0)` guard skips the rewind. The x64 side behaves the same way.
5. **Fuel.** `TAIL` branches to the callee's `FUNC` entry, which is a block leader that charges its weight and
   owns a suspend stub, so a tail loop still yields. This is verified on x64 (suspend counts below) and should
   reproduce exactly on arm64 - if it does not, the leader analysis is treating `TAIL` differently there.

## The gates

```bash
bash tools/buildAndRunGAZLJitLowerTest.sh      # compiles GAZLJitArm64.cpp and runs the new TAIL kernel
bash tools/buildAndRunGAZLJitTest.sh           # arm64 byte-golden emitter test (clang-assembled oracle)
bash tools/buildAndRunGAZLJitExecTest.sh
bash tools/checkPermut8Firmwares.sh --jit      # 28 shipped firmwares, interp-produced goldens
bash build.sh                                  # includes tools/test-jit.sh
```

Then the standing gate for any codegen change - 300k deep per backend, which arm64 has NOT had for this work:

```bash
bash tools/buildGazlFuzz.sh standalone
./output/GAZLFuzz --gen 300000 1 deep
```

Note the generative fuzzer does not emit `TAIL` (its grammar predates GAZL 2), so the soak covers the
RENUMBERING - 86 of 91 opcodes shifted when GAZL 2 inserted `TAIL_CC_`/`TAIL_VC_` at ordinals 5 and 6 - not
`TAIL` itself. `TAIL` coverage is the lower-test kernel plus the firmwares. Teaching the generator to emit
`TAIL` is worth doing and is not done.

## Expected output

`tools/GAZLJitLowerTest.cpp` gained a `K_TAIL` kernel (gated `#if GAZL_2`) covering the direct form, the
indirect form and a `CALL` into a tail chain, at full AND tiny fuel. On native x64 it prints:

```
Kernel "tail          [GAZL 2 TAIL]":
  n=0        [fullfuel]  status=0  suspends=0     gOut=0
  n=1        [fullfuel]  status=0  suspends=0     gOut=2
  n=2        [fullfuel]  status=0  suspends=0     gOut=6
  n=5        [fullfuel]  status=0  suspends=0     gOut=30
  n=10       [tinyfuel]  status=0  suspends=1     gOut=110
  n=100      [tinyfuel]  status=0  suspends=10    gOut=10100
  n=1000     [tinyfuel]  status=0  suspends=100   gOut=1001000
```

`gOut` and `status` are architecture-independent and must match exactly; the test compares against the
interpreter on the whole memory image, so `OK` on every row is the real pass condition. The suspend counts
come from the shared fuel accounting and should also match - a divergence there means the safepoint placement
around `TAIL` differs between backends.

`compiled 275 native words for 3 function(s)` is x64-specific; expect a different arm64 count.

## Done looks like

Every gate above green on Apple Silicon, review item 1 resolved, and this file updated to
`Status: DONE and validated on Apple Silicon` with the soak result recorded.

## Result (Apple Silicon, 2026-09-13)

**It compiled unchanged, and the `K_TAIL` kernel passed on the first build.** Every row `OK` in debug (`-O0`,
asserts live) and release, with `gOut` and the suspend counts (0 / 1 / 10 / 100) identical to the x64 rows above.
arm64 emits 210 native words for that kernel.

Review items:

1. **Fixed.** Reproduced first: a new kernel with a 5000-word window aborted in `ldrW` on the imm12 assert. Note
   that `buildAndRunGAZLJitLowerTest.sh` "release" is `-O2` *without* `-DNDEBUG`, so asserts are live there too; the
   silent wrong-instruction case is the `BuildCpp.sh` release build (`-Os -DNDEBUG`), which is what ships `GAZLCmd`.
   `emitTailWindow` now mirrors the x64 structure: a window of 8 words or fewer still unrolls through
   `ldrW`/`strW`, and anything larger copies through an ascending register-offset loop (`ldr w11, [x1, w13, uxtw
   #2]` / `str w11, [x12, w13, uxtw #2]`, index `W13`, bound `W15`). No window size can emit an out-of-range offset.
   Regression kernel `K_TAILBIG` in `tools/GAZLJitLowerTest.cpp`: window 5000 and frame 1504 words, so the
   `subImmXBig` register form runs too. It covers the direct and indirect forms, with the window overlapping the frame
   it slides onto, at full and tiny fuel. It passes on arm64 (debug and release) and on x64 (Rosetta cross-build,
   the `rep movsd` path) with identical `gOut` and suspends (703 at tiny fuel).
2. **Confirmed.** `X12`/`W11` for the slide and `W13`/`W15` for the loop never collide with `X9`: the indirect
   big-window form carries the resolved target across a 5000-word loop and lands correctly. None of them is in
   `ARM64_GENERAL_POOL`.
3. **Done.** The second `subImmXBig` is now `add x1, x12, #0` (`mov x1, x12`): one word instead of one or three.
4. **Unchanged.** `frame == 0` still self-copies and skips the rewind, as on x64.
5. **Confirmed.** The suspend counts match x64 on both TAIL kernels, so safepoint placement around `TAIL` is the
   same on both backends.

Gates, all run on Apple Silicon:

- `buildAndRunGAZLJitLowerTest.sh`: ALL PASS (debug and release).
- `buildAndRunGAZLJitTest.sh`: ALL PASS. No new encodings were needed; the loop uses emitter forms already covered
  by the golden.
- `buildAndRunGAZLJitExecTest.sh`: ALL PASS.
- `checkPermut8Firmwares.sh --jit`: all 28 checksums match. It needs `output/GAZLCmd` built first; without the
  binary it reports all 28 as `GOLDEN!` with empty output rather than saying the binary is missing.
- `build.sh`: exit 0. It includes `test-jit.sh`: the lower test ALL PASS, all 28 firmware checksums match, and
  `gen 2000 programs, no divergence`. The node and Impala smoke steps passed too.
- `GAZLFuzz --gen 300000 1 deep` (arm64, standalone beta build): `gen 300000 programs, no divergence`, in 4m58s.
  As noted above, the generator does not emit `TAIL`, so this soak covers the renumbering, not `TAIL` itself.

### Rerun on `0779ba9` (after `ce10da6`)

The fix above was built and gated on `f773dcb`, then rebased onto `0779ba9`. That brought in `ce10da6`: no
fall-through leader after GOTO/SWCH, and block weight stops at the terminator. The change is in the shared
`src/GAZLJit.cpp`, so every gate ran again:

- Lower test, debug and release: ALL PASS. **All 468 kernel rows are byte-identical to the `f773dcb` run:**
  status, host calls, tiny-fuel suspends and `gOut`. The per-kernel native-word counts did not change either.
  No lower-test kernel has unreachable filler after a GOTO, SWCH, RETU or TAIL, so the weight change is a no-op
  here, as expected. It would still fail loudly rather than hide if it diverged, because each row is diffed
  against the interpreter.
- Emitter golden, exec test, engine test, slice test: ALL PASS. The engine and slice tests are new to this run.
- `checkPermut8Firmwares.sh --jit`: all 28 checksums match.
- `build.sh`: exit 0. Inside it, `test-jit.sh` ran the lower test (ALL PASS, all 14 `K_TAILBIG` rows `OK`), all
  28 firmware checksums matched, and the fuzz smoke reported `gen 2000 programs, no divergence`. The NuXJS
  Impala smoke test passed.
- `GAZLFuzz --gen 300000 1 deep` (arm64, standalone beta build): `gen 300000 programs, no divergence`, in 4m56s.

x64 reference on `0779ba9`, from the Windows session: lower test ALL PASS, 28/28 firmwares, 2000-program fuzz
smoke clean, 113/113 Impala programs, `build.cmd` exit 0. `K_TAIL` is 275 native words there and 210 on arm64.
`K_TAILBIG` has only run on x64 under Rosetta; a run on native x64 is still to come.

### Rebased onto `631c9db` (`K_DEADTAIL`)

`631c9db` added `K_DEADTAIL`: a mid-function RETU on the hot path followed by 64 unreachable instructions, the
first kernel that actually exercises `ce10da6`'s weight change. On arm64 at the rebased head:

- Lower test, debug and release: ALL PASS. `K_DEADTAIL` suspends 9 times at n=100 and 90 at n=1000, matching the
  interpreter. `K_TAIL` and `K_TAILBIG` are 14/14 each, and every previously existing kernel row is identical to the
  `0779ba9` run.
- **Backout check:** with `ce10da6` reversed in a scratch copy (working tree untouched), arm64 fails exactly as x64
  did: `FIDELITY n=100 interp_suspends=9 jit_suspends=99 ratio=11.00` and `n=1000 ... ratio=11.10`, 2 failures.
  The two backends agree on a block's extent; the weight computation is shared, and this confirms it.

### The cross-register-file bridge, `8971402` through `73fc9bd` (2026-09-16/17)

Not TAIL. This file has been the running arm64 verification log since the `ce10da6` and `K_DEADTAIL` sections
above, and this continues that rather than starting a fourth handoff document.

`89714023` made `RegisterCache::read` bridge a slot register-to-register when it is resident in the other
register file, instead of spilling and reloading through the frame slot. On arm64 that is `fmovSW` / `fmovWS`.
Two things were unverified when it was pushed, and a third turned up during the work.

**1. `fmovWS` had never been assembled.** It was read out of the ARM ARM, and `emitCrossMove` was the only
emitter in that commit with no reference entry. Verified three ways on Apple Silicon:

    llvm-mc -triple=arm64    fmov w0, s1    -> [0x20,0x00,0x26,0x1e]  = 0x1E260020
    clang + otool            fmov w0, s1    -> 1e260020
    non-trivial registers    fmov w13, s22  -> 1e2602cd = 0x1E260000 | (22 << 5) | 13

The third is the one that counts. A `w0`/`s1` check passes even with the register fields misplaced or
mis-masked, since both indices are small and one is zero. That was not a hypothetical: `fb4178ae` added an
oracle entry and used exactly that weak pair, for both directions. `81abdc4f` moved both to registers drawn
from the real pools (`ARM64_GENERAL_POOL` has `W17`, `ARM64_FLOAT_POOL` has `V22`), confirmed on arm64 as
`fmov s22, w17` = 0x1E270236 and `fmov w17, s22` = 0x1E2602D1.

**2. The bridge had never EXECUTED on arm64.** `4a4473fa`'s 300k soak predates it. On `89714023`: the 300k-deep
soak is clean in 5m07s, `build.sh` and `test-jit.sh` exit 0, 28/28 firmwares, all 482 lower-test rows identical
to the `4a4473f` run. What makes that evidence rather than a green tick is the instrumented count - seed 1's
first 3000 programs emit **7480 bridges, 6959 general->float against 521 float->general** - and the benchmark
suite is 13/13 byte-identical with `--jit` against without.

**3. No lower-test kernel emitted a bridge at all.** The cross-file row was isolation test H against
`RecordingBackend`, which LOGS a move without encoding one, so the fast gate could not have caught a wrong
`fmov` however the oracle was written. `81abdc4f` adds `K_CROSSFILE`. `MOVE` is the lever: it is
`ANY_VAR_W`/`ANY_VAR_R`, the one instruction that can name a float local while lowering through the GENERAL
file, so it leaves a slot in the wrong file for the next instruction to find. A typed op cannot - `ADDf` will
not name a `LOCi`.

An asymmetry fell out of that. The backends put `ABSf` in different register files - x64 clears the sign bit
bitwise in a GP register, arm64 uses `fabsS` and stays in the float file - so `float abs/flr` bridges twice by
accident on x64 and zero times on arm64. **On arm64 `K_CROSSFILE` is therefore the only lower-test coverage of
`fmovWS`**, the direction the fuzzer hits about 13x less often; `divf zero` reaches only `fmovSW`. That is
recorded at the kernel so nobody retires it after looking at an x64 bridge count.

A measurement caveat worth carrying forward, because it cost a wrong conclusion in both directions: an
instrumented probe writing to `stderr` while the kernel headers go to `stdout` will mis-attribute or lose
tokens, since block-buffered `stdout` flushes mid-line into a pipe. That produced a false "0 bridges in every
kernel" here and a false per-kernel attribution on the x64 side, within an hour of each other. Put the probe on
`stdout`, or `stderr` alone to a file.

### The spectralnorm regression, and why the bridge policy is per-backend

`89714023` was reproducibly ~1-2% SLOWER on spectralnorm on x64, instruction-count neutral, and unexplained.
The cause: **the bridge did not remove a store, it relocated it and changed its domain.** The old spill wrote
the home, which left the line clean; the bridge carried the dirty flag, so the line had to spill later - on the
block's back edge, once per iteration, as an FP `movss` where it had been an integer `mov`.

`cc69dd03` fixed that on x64 by writing the home from the source register and marking the line clean. **On
arm64 the same change is a LOSS** - spectralnorm +6.9%, sor +5.1% - so it was a live regression there, and one
rule cannot serve both. `b1cea191` makes it `RegisterCacheBackend::bridgeWritesHome()`: true on x64, false on
arm64, which restores this backend to exactly the `89714023` path. Confirmed: the emitted code is
**byte-identical** to `89714023` for spectralnorm, sor, mandelbrot and leibniz, layout sidecars included.

                       spectralnorm            sor
  x64 (Zen 4 7950X)    eager 89.35            eager 68.10
                       deferred 90.39 (+1.2%) deferred 68.02 (-0.1%)
  arm64 (Apple Si)     eager 53.22 (+6.9%)    eager 95.24 (+5.1%)
                       deferred 49.79         deferred 90.64

Two explanations died here, each killed by the other machine's data: that the fix would be x64-local (arm64
showed the identical relocation), and that arm64's win came from removing a store-to-load pair at one address
(writing the home eagerly removes that pair and still loses there). The comment in `GAZLJit.h` therefore says
the arm64 side is measured and unexplained rather than offering a story.

Static counts predict none of it. The predicted whole-function counts held EXACTLY on arm64 - spectralnorm's
int stores back to 90, float stores back to 22, float loads staying at 8 - and the timing still went the other
way. In sor the eager form has fewer stores in total than the deferred one, 62 against 64, and is 5% slower.

Finally, a noise calibration nobody went looking for: byte-identical arm64 code measured **3.4% apart** on
best-of-8 min across two builds of one kernel, drifting upward through the run under load. Treat anything under
about 3% there as nothing unless the per-round ranges separate - which is how both decisions above were
actually settled, not by a gap in a single min.

**Gates at `73fc9bd`, arm64:** emitter golden ALL PASS with both `fmov` words matching; lower test ALL PASS in
debug and release, including both `cross-file slot (eager)` and `(deferred)` rows and `K_CROSSFILE`; `build.sh`
and `test-jit.sh` exit 0; 28/28 firmwares; 300k-deep soak clean.
