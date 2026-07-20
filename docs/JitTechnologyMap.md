# GAZL JIT technology map (2026-07-20)

How every mechanism in the JIT works and how they compose. Files: `src/GAZLJit.{h,cpp}` (arch-neutral core),
`src/GAZLJitArm64.{h,cpp}` / `src/GAZLJitX64.{h,cpp}` (backends), `src/GAZLJitMem*` (W^X page handling).

## 0. Ground rules the whole design serves
- The interpreter is the semantic oracle. The JIT must be BIT-IDENTICAL to it: same results, same Status, same
  memory image at every observable point (suspend, trap, return). Every optimization below is shaped by this.
- The assembler is the trusted gatekeeper: the JIT consumes only finalized `Instruction[]` it produced.
- Cooperative scheduling (fuel) and re-entrancy (pushCall/enterCall) are first-class, not afterthoughts.

## 1. Compilation shape
`JitCompiler::compile` lowers each function separately (extent = this `functionTable` entry to the next; the
last ends at `codeSize` - the language rule, since 5008d3b). One shared exit epilogue + one dispatcher per module.

**Dispatcher/segment model (spec 5.4b):** the host calls one native entry; it loads the pinned registers
(ctx, dsp, memory base, fuel, ipsp) once and jumps to the RESUME continuation. GAZL->GAZL calls tail-branch
directly between compiled segments (the ipStack holds return continuations); native calls run inline. Every
terminal (suspend / return / trap) sets Status and branches to the shared exit. No C++ frames mid-run.

**Fuel safepoints (5.5):** basic-block leaders partition each function (`jitFuelSafepoints`); each leader is
charged its block's static weight so JIT fuel spend equals the interpreter's per-instruction spend. Every leader
owns a suspend stub (cold): write state back, park RESUME, return TIME_OUT. `MAX_BLOCK_WEIGHT` splits long runs.
A leader after every CALL_NVC makes `resetTimeOut(0)+OK` natives (the cooperative yield idiom) suspend with
zero slack.

## 2. The register story - four stacked layers, no layer subsumes another

**v2.0 - floating register cache** (`RegisterCache`, arch-neutral; backends provide fill/spill via
`RegisterCacheBackend`). Frame slots are cached in a small pool of caller-saved registers: load-on-use,
dirty-on-def, write-back on eviction. Correctness discipline: the cache FLUSHES (spill dirty + drop) at every
hazard - originally every block boundary, every runtime-pointer memory op, every call. Pools: x64 R8-R11 +
xmm2-xmm5 (caller-saved on SysV AND Win64, disjoint from RAX/RCX/RDX fixed scratch); arm64 W5-W8,W16,W17 +
V16-V23.

**v2.0.5 - Belady eviction.** WITHIN the above regime, when the pool is full, evict the line whose next READ is
furthest (from `buildUseSchedule` next-read lists; `operandRoles` classifies operands). This is only VICTIM
SELECTION: it cannot remove a boundary flush (a correctness rule, not an eviction) and cannot stop fill-on-use
from reloading a loop-carried value each iteration. FAQ "is Belady enough?": no - Belady optimizes what happens
BETWEEN the flushes; the next two layers REMOVE flushes.

**v2.2 - loop residency (entry maps).** At a qualified loop header, `capture()` fixes a slot->register binding
(ResidencyMap) for that loop: wanted = read-in-loop AND live-in at the header (`buildLiveIn` backward liveness)
AND single-class; absent wanted slots are PRELOADED (residency by need). Every in-edge (branch, fall-through,
back-edge, resume) `reconcileTo()`s the target leader's map; interior leaders carry the header bindings FILTERED
to their own live-in (dead bindings free registers). Loop exits spill on the TAKEN path only (ColdEdge stubs).
The binding is per-loop (never whole-function) and SOFT - evictable under pressure, re-established at the next
leader. Qualification (`jitResidencyLeaders`): back-edge target with no forward/SWCH entry, no side entries into
the body, no nested back-edges, all ops residency-safe; a per-class PRESSURE GATE (wanted vs capture keepMax)
rejects working sets that would thrash the map (mandelbrot lesson: a strangling map loses to fill-on-use).
Suspend stubs spill the leader's map; resume trampolines refill it.

**v2.3a - pointer realms (aliasing).** Spec 1.1: memory is five disjoint realms (globals, constants, and each
frame's params/locals/transients); cross-realm pointer access is UB. `buildPointerRealms` stamps each slot's
pointer provenance (fixed point over MOVE/ADDI/SUBI; ADRL -> MYFRAME; symbol const -> NONFRAME; live-in param ->
NONFRAME; loads/calls -> UNKNOWN). A PEEK/POKE through a NONFRAME base provably cannot alias a cached local ->
its flush is SKIPPED. Const-address forms skip only when the const is a genuine address (>= MEMORY_OFFSET) -
the scalar-deref idiom shares the opcode with a bare offset (the v2.3a-lite soundness bug, b09affe). Sound on
all GAZL 1.0 with no annotation: realms are derived, never asserted. Within-realm precision was REMOVED from the
roadmap (see `docs/SliceBoundsDesign.md`).

How the four compose in a hot loop: residency keeps the loop's values in fixed registers across iterations;
realms keep NONFRAME pointer traffic from flushing them; Belady arbitrates the leftover headroom registers;
v2.0's write-back discipline underlies all of it.

## 3. Cold sections - the trap/exit contract at zero hot-path cost
Every checked op (bounds, div-by-zero) and every loop-exit edge branches to a COLD stub instead of flushing
inline: the stub stores a `captureDirtyLines` snapshot (the dirty registers AT THE BRANCH POINT) and then traps /
enters the exit leader. Memory is interpreter-identical on every exit path while the hot path stays
branch-and-continue. Capture timing is load-bearing: snapshot at the trap-branch emission point, never after the
op's own define (the model must equal the trap-path runtime state).

## 4. Emitters
- `Arm64Emitter`: fixed 32-bit words, label/fixup patching, `matConst` movz/movk materialization.
- `X64Emitter`: byte stream; rel32 labels patched by `finalize()` (which asserts every referenced label is
  bound); RIP-relative float LITERAL POOL (deduped, 4-aligned after the dispatcher; movssRip) so float constants
  are one load with no GP pressure; 16-BYTE FUNCTION-ENTRY ALIGNMENT so code-size changes cannot cascade layout
  shifts across functions (Zen 4 is fetch-alignment sensitive; miditest lesson).
- Backend asymmetries that matter: x64's tiny pool (4+4) makes preload/residency decisions far more valuable
  there (leibniz -64%); arm64's bigger pool gains mostly from flush removal (div cold traps, realms).

## 5. The safety net (why any of this can be trusted)
- **Lockstep lower test** (`tools/GAZLJitLowerTest.cpp`): ~35 kernels through the real `compile()`, JIT vs
  interpreter on WHOLE memory image + Status, at full AND tiny fuel (forcing suspend/resume through every leader).
  Includes realm teeth kernels, multi-RETU, cross-class capture isolation tests (mock backend records every
  fill/spill).
- **Byte-golden emitter tests** (`GAZLJitX64Test` + clang-assembled oracle .s): every encoding form, including
  the literal pool and REX paths.
- **28-firmware differential** (`checkPermut8Firmwares.sh --jit`): real shipped products, interp-produced
  checksums, both engines, both boxes.
- **Generative differential fuzzer** (GAZLCmd -DJITDIFF): generates always-valid GAZL TEXT (the assembler is the
  gatekeeper), diffs engines at full+tiny fuel; stages G1-G4 (value ops, memory, control flow, calls incl a
  multi-RETU and a pointer-param callee). Found three real miscompiles to date (DIVf/0, v2.3a-lite scalar deref,
  varying-maps cross-class preload) - the first two AFTER all other tests passed. 300k-deep soak per backend is
  the standing gate for any codegen change.
- Discipline notes: never pipe a soak through tail/findstr (masks the abort); min-of-fresh-processes on idle
  boxes for benchmarks; arm64 deltas need code_bytes equality checks; Rosetta absolutes are noise.

## 6. Current state / what is deliberately NOT here
- Roadmap complete through v2.2-full + v2.3a. v2.1 skipped; v2.3b removed (language initiative); v2.4
  (rematerialization, live-range splitting) closed unless a workload demands - nothing measured is spill-bound.
- No inlining (open placement decision: `docs/InliningInvestigation.md`), no true per-leader register
  REASSIGNMENT (fixed binding per loop suffices so far), no native-call fast path (the per-sample ^yield
  boundary is the other big remaining lever).
