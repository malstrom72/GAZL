# Hand-off: JIT step 1 — the arm64 `Emitter` + assemble-diff test harness

**For:** the agent taking over this task (likely on another machine). This file is self-contained; read it fully before
touching anything. **Repo:** GAZL, branch **`jit-compiler`**. **Owner:** Magnus Lidström.

**Do not `git commit` or `git push`** unless Magnus explicitly says so (standing rule). Leave work in the tree and
report back.

## What this is

First implementation step of a native (arm64) JIT for the GAZL VM. The full design is settled and documented in
[JitCompilerResearch.md](JitCompilerResearch.md) — you do **not** need to re-derive or re-open any design decisions;
this step is one small, isolated brick. Skim, for context only: §5.7 (register allocator / the `Emitter` shape), §5.8
(worked example — the exact arm64 lowering style), and **`benchmarks/jit/JitBenchA3.arm64.S`** (hand-written,
already-validated arm64 asm whose every instruction is a known-good reference for what the Emitter must produce).

**The step:** build a minimal arm64 **`Emitter`** (a C++ class that writes machine-code bytes into a buffer, one method
per instruction) and a **test harness** that proves every encoded form is byte-correct. It touches **none** of the
shipped VM (`src/GAZL.h`/`src/GAZL.cpp`), so it cannot regress Permut8/MidiGAZL. This is roadmap spike **C2**.

**Explicitly NOT in this step** (do not build these): the dispatcher, RESUME/suspend/resume, fuel checks, traps, the
register allocator, the engine factoring (base `Processor` + subclasses), the x64 emitter, *executing* emitted code, or
wiring anything into the VM. One brick.

## Decisions already made — do not re-open

- **Hand-rolled Emitter** (no library). Nothing reusable is vendored in `externals/` (only NuXJS). Confirmed.
- **Source location: `src/GAZLJit.h` / `src/GAZLJit.cpp`** — a flat module beside `GAZL.*`, matching GAZL's
  single-header/source ethos. The whole JIT (Emitter now, dispatcher/allocator later) grows in this one pair, the way
  `GAZL.cpp` is one big file. The Emitter has **no dependency on `src/GAZL.*`** yet (it only produces bytes).
- **arm64 only.** x64 is a later step.
- **Canonical-form-per-op encoding** — do not chase short encodings; pick one form per operation (e.g. always the
  imm12 / reg form, materialize 32-bit constants via `movz`+`movk`). Keeps the encoder tiny and uniform.
- **Harness oracle = the clang *assembler*, not a disassembler.** This machine has **no** raw-byte disassembler
  (`llvm-mc`/capstone/`cstool` absent; `otool`/`objdump`/`llvm-objdump` only decode object files, not raw streams). So
  verify by **assemble-and-compare**: the independent oracle is clang assembling the same mnemonic; compare its bytes to
  the Emitter's. (If your machine *does* have `llvm-mc`/capstone, disassemble-and-compare is equally fine — the point is
  an independent second encoding, not the specific tool.)

## Instruction subset to cover

Exactly what the first kernel (the A3 sum-loop, `bench_v2` in `benchmarks/jit/JitBenchA3.arm64.S`) needs — every form
there is a known-good reference:

- `mov`/`movz`/`movk` (32-bit constant materialization in 16-bit chunks)
- `add`/`sub` register and immediate; `subs` (flag-setting); `cmp` (subs-to-zero-reg alias)
- `mul`, `and`, `orr`, `eor`, `lsl`/`lsr`/`asr`
- `ldr`/`str` word: base+imm12 offset, and base+reg-indexed (`[Xn, Wm, uxtw #2]`)
- float scalar: `fadd`/`fmul`/`fsub` `Sd,Sn,Sm`; `fcvtzs` (FTOI), `scvtf` (ITOF); `ldr`/`str` `s`
- branches: `b`, `b.cond` (LT/GE/HS/MI/EQ/NE), `cbz`/`cbnz`, `ret`
- a **`Label`** type + a forward-reference **fixup pass** (record patch sites while emitting; bind labels and patch the
  branch displacements at the end)

Design shape (see §5.8 and the `.S`): a class appending `uint32_t` words to a buffer; one method per instruction that
ORs operand fields into a base opcode; register + condition enums; the `Label`/fixup facility.

## Files to create

- **`src/GAZLJit.h`** — the `Emitter` class declaration (buffer, register/condition enums, one method per instruction,
  `Label`).
- **`src/GAZLJit.cpp`** — the `Emitter` implementation. (This is the emerging JIT module; keep it library-only, no test
  code, no `src/GAZL.*` dependency.)
- **`tools/GAZLJitTest.cpp`** — the test runner (matches GAZL's split: library in `src/`, runner in `tools/` like
  `GAZLCmd.cpp`). It includes `GAZLJit.h`, drives the Emitter, and does the assemble-diff.
- **`tools/GAZLJitTestRef.arm64.S`** — labeled reference encodings (the independent clang-assembled oracle): each
  instruction form as a labeled 4-byte entry, e.g. `.globl _ref_add; _ref_add: add w0, w1, w2`. The test reads
  `*(const uint32_t*)&ref_add` and compares to `emitter.add(W0,W1,W2)`. (Follow the platform symbol-prefix macro from
  `benchmarks/jit/JitBenchA3.arm64.S` for macOS/Linux portability.) *Alternative if you prefer not to maintain a
  parallel `.S`:* have the test shell out to `clang -c` on a temp `.s` at run time and extract `__text` bytes via
  `otool -t` / `objdump -s -j __text`. Either is acceptable; the build-time `.S` has no run-time tool dependency.
- **`tools/buildAndRunGAZLJitTest.sh`** + **`tools/buildAndRunGAZLJitTest.cmd`** — build+run script, `clang++`
  **directly** (not `BuildCpp.sh`), arch-gated to arm64, modelled on `benchmarks/jit/buildAndRunJitBenchA3.sh`. Roughly:
  `clang++ -O2 -std=c++17 -I src src/GAZLJit.cpp tools/GAZLJitTest.cpp tools/GAZLJitTestRef.arm64.S -o output/GAZLJitTest`
  then run it. Output to the gitignored `output/`. Follow the repo script conventions in `AGENTS.md` (shebang,
  `set -e -o pipefail -u`, `cd` to repo root, `CPP_COMPILER` override, matching `.cmd`).

**Do NOT** wire this into `build.sh` — that suite is fully hardcoded (no test discovery). Folding the JIT tests into it
(or into the `GAZLCmd` unit-test flow) is a separate, later decision; leave it as a standalone script for now, like the
existing `benchmarks/jit/` scripts.

## Verification (the definition of done)

1. `bash tools/buildAndRunGAZLJitTest.sh` builds and runs on arm64.
2. **Every** instruction form in the subset reports MATCH against the clang-assembled reference.
3. At least one **deliberately-corrupted** encoding reports MISMATCH — proving the harness actually catches a bad encode
   (teeth). Without this, a green run is meaningless.
4. **Cross-check the whole kernel:** emit the exact body of `bench_v2` from `benchmarks/jit/JitBenchA3.arm64.S` via the
   Emitter (including the `Label`/branch fixups for the loop) and assert it reproduces that function's bytes
   word-for-word. This proves the `Label`/fixup pass, not just individual instructions.

## Constraints

- Don't modify `src/GAZL.h`/`src/GAZL.cpp` or anything in the shipped VM.
- Don't touch `externals/` (only NuXJS lives there; nothing to reuse).
- Don't `git commit`/`push` without Magnus saying so.
- arm64 only; x64 is out of scope for this step.
- Report back with: the files created, the test output (all-MATCH + the corrupted-control MISMATCH + the `bench_v2`
  byte-for-byte cross-check), and any encoding form where clang's output surprised you.

## Immediate next step (context, not this task)

Reuse the A1 probe's W^X allocation + icache-flush in `spike/jit-probe/` to *execute* an Emitter-produced sum-loop and
check its return value — the encoding-vs-execution end-to-end check that precedes the full vertical slice
(dispatcher/RESUME/fuel/suspend-resume). That is a separate hand-off.
