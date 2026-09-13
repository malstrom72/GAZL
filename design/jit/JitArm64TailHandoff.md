# Handoff: verify the arm64 TAIL lowering on Apple Silicon

Status: TASK, open. The arm64 `TAIL` lowering was written on a Windows x64 box in the GAZL2 merge
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
