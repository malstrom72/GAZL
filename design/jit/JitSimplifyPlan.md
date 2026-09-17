# JIT Simplify Plan (tiers B and C)

Findings from a `/simplify` review pass over the JIT compiler (the `jit-compiler`-vs-`main` diff: `src/GAZLJit*`,
the JIT parts of `src/GAZL.*`, `tools/GAZLCmd.cpp`). Tier A (dead run-state fields, redundant `operandRoles` decode,
unconditional `buildLiveIn`, duplicated `keepMax` formula, arm64 double map lookups) is DONE, and so is tier B (its
section records the outcome). Tier C was deliberately deferred, with the evidence, so it can be picked up without
re-deriving it; its section records what has landed since.

Line numbers are from the state right after Tier A; treat them as pointers, not gospel.

## Root cause behind most of this

`JitCompilerArm64::lowerFunction` and `JitCompilerX64::lowerFunction` are structurally parallel skeletons - identical
pass-1 analysis, pass-2 leader/residency bookkeeping, and cold-section phases - wrapping arch-specific `case` emits.
That skeleton was COPIED rather than hoisted, so the JIT's subtlest logic exists as two hand-kept-identical copies.
Tier C is the real fix; tier B are the pieces that can move without restructuring.

## Tier B - DONE (2026-09-17)

Every item landed as a pure refactor, verified against `f47c379`: the JIT's emitted code (`GAZLCmd --emit-jit`,
code plus layout sidecar) is byte-identical on BOTH backends over 135 programs - the benchmark suite, the Impala
goldens, and 40 Permut8 firmwares through the host wrapper - arm64 native and x64 under Rosetta, after each item.
Gates on the result: `build.sh`, the lower test debug and release on both backends, both emitter byte-golden tests,
exec/engine/slice tests, `checkPermut8Firmwares.sh` plain and `--jit`, and a 300k-deep arm64 soak (seed 600001, no
divergence). 61 lines smaller across six files.

- **Shared `cacheLowered`.** `isCacheLowered(op)` in `GAZLJit.cpp`. The two lists held the same 81 opcodes.
- **SWCH jump-table decode.** `switchTargets(code, j, memory)` returns the absolute targets in table order; the six
  hand-written decodes are gone.
- **x64 two-operand alias helper.** `emitTwoOperand(emitter, cache, copy, op, registerClass, d, a, b)` states the
  aliasing argument once for `emitBinary`, `emitDivFChecked` and `emitBinaryFloat`.
- **Loop set builders merged.** `buildLoopSets` fills the read/written and general/float sets in one walk.
- **`RegisterCache` accessors.** Closed without a change. Every site already goes through `linesOf`/`registersOf`
  except `captureDirtyLines`, which is `const`, and a `const` overload of `linesOf` would add more lines than it
  removes.
- **FORi's read-modify-write counter.** `operandRoles` marks it `OPERAND_SLOT_READ_WRITE`; roles are bit flags
  tested with `&`, and the four JIT overrides are gone. No non-JIT caller exists. The one consumer this was not
  obviously neutral for is `buildPointerRealms`, whose pass 1 used to see the counter as write-only: a live-in
  counter now gets an initial NONFRAME stamp. Pass 2 still forces the counter to UNKNOWN and only ever joins upward,
  so it reaches the same fixed point - which the byte comparison confirms.

Found while merging the loop sets, then FIXED: the class sets filed `ABSF` operands under GENERAL, which matched x64
(it cleared the sign bit in a GP register) but not arm64 (`fabs` in the FLOAT file). On arm64 a float slot touched
by `ABSF` therefore looked dual-class and was never kept resident across a loop. Rather than a per-backend hook, x64
now lowers `ABSF` in the float file too - `andps` against a pooled 0x7FFFFFFF, bit-identical to the interpreter's
`fabsf`, NaN payload included - and the shared class sets call `ABSF` float for both backends.

- arm64, a 20M-iteration `x = abs(x - 0.75); acc += x` loop: 53.82 -> 22.44 ms, min of 6 alternating rounds, same
  output as the interpreter. The store and reload of `x` every iteration is gone.
- Corpus (`--emit-jit`, 135 programs): no arm64 code changed; x64 code changed in the 24 programs that use `ABSF`,
  and disassembly of two of them (`bender`, `phaser`) shows the change confined to the `ABSF` sites.
- Gates: `build.sh`, both emitter byte-golden tests (new `andps` entry), lower test on both backends, exec/engine/slice,
  firmwares plain and `--jit` on arm64 and `--jit` on x64 under Rosetta, 300k-deep soaks on both backends (seed
  900001; x64 under Rosetta).
- Native x64 (AMD 7950X, Windows session), same kernel, JIT vs JIT, 6 alternating rounds: `8bbbf3e` 54.86 ms best
  (54.86-55.06) -> `b89e919` 14.73 ms best (14.73-14.77), 3.7x; both JITs and the interpreter print 8601898. At
  `b89e919` natively: `build.cmd` exit 0 with "28/28 firmware checksums match" and "gen 2000 programs, no divergence"
  observed - the lower test and emitter goldens it runs are covered by that exit code, not quoted - and
  `GAZLFuzz --gen 300000 900001 deep` "no divergence". Seed 900001 repeats the Rosetta band on purpose: no new
  programs, but it shows native x64 agrees with Rosetta on the same ones.
- The kernel is now `benchmarks/suite/absloop` (Impala source, golden, checksum): the suite had no ABSf coverage.

## Tier C - architectural, needs a deliberate decision

This is a real refactor of a bit-exact JIT. High value (it removes the two-copies-of-everything problem), but it must
be done in verifiable steps: after each step run the lower/exec/engine/slice tests, both emitter byte-golden tests, and
`checkPermut8Firmwares.sh` both plain and `--jit`, plus a fuzz soak.

**Status 2026-09-17: C1, C3 and C4 DONE; C2 set aside.** C1 and C3 landed as pure refactors: `--emit-jit` output is
byte-identical to `b89e919` on both backends over the 135-program corpus, lower test green on both backends after each
step, then `build.sh`, both emitter goldens, exec/engine/slice, firmwares plain and `--jit` on arm64 and `--jit` on x64
under Rosetta, and 300k-deep soaks (seed 1200001: arm64 310 s, x64 under Rosetta 640 s, no divergence). 28 lines
smaller.

- **C1:** `establishLeader` in `GAZLJit.cpp`; each backend keeps only its label bind.
- **C3:** `planConditionalEdge` makes the decision (reconcile, ColdEdge stub, or barrier). The note below that the
  edge policy needs no templating is only half right: `Label`, `ColdEdge` and the emitter are distinct per-backend
  types, so the decision is shared and each backend keeps label allocation and branch emission (a few lines each).
- **C4:** a pure refactor too, verified the same way against `ce3c9cb` (136 programs, now including absloop; soaks
  at seed 1500001: arm64 306 s, x64 under Rosetta 625 s, no divergence); 34 lines smaller. `spillResidencyMap` and
  `fillResidencyMap` route the cold-section stores and reloads through the backend's own `emitSpill` / `emitFill`,
  replacing `emitDirtyStores` and the suspend-stub loops - byte-identical, because those encodings were already the
  same, so the worry below about routing through `emitSpill` did not materialize. arm64's ColdTrap carries a
  `Status` (the `movn` immediate is its complement at emit time), and ColdTrap / ColdEdge are one template each in
  `GAZLJit.h`, typedef'd per backend `Label`. The cold loops stay per backend: what is left in them is
  emitter-specific, and templating them would add an abstraction, not remove code.
- **C2, set aside:** the duplicated setup is about 17 lines per backend, but pass 2 uses what it builds some 40 times,
  so a shared context object comes out size-neutral or larger (an estimate, not a prototype). C4 did not need a
  shared skeleton either, so nothing currently pulls it in.

- **Hoist the loop-header residency orchestration.** `GAZLJitArm64.cpp:762-809` and `GAZLJitX64.cpp:664-712` are the
  same ~48 lines, comment for comment: `freshHeader` detection, `multiBlock` gate, the loop slot/class sets, the
  wanted-set filter (live-in and read and single-class), the `residencyCapacity` pressure gate, `capture`, the
  `filterResidencyMap` sweep over interior leaders, and the reconcile/barrier fallback. It is entirely arch-neutral -
  it drives `RegisterCache` and the shared analyses and touches the emitter only on the final `bind`. Move to a shared
  `JitCompiler` helper (e.g. `establishLeader(cache, code, j, loopExtent, loopWeight, liveIn, entryMaps, resident&,
  residentEnd&)`), leaving each backend the `bind`. This is the single highest-value change: today every v2.2 residency
  fix must be made twice and proven twice.
- **Unify the cold sections.** `ColdTrap`/`ColdEdge` (`GAZLJitArm64.cpp:495-511` vs `GAZLJitX64.cpp:347-363`) are
  identical except that arm64 stores `unsigned statusComplement` (the movn immediate) where x64 stores a `Status` -
  a gratuitous divergence that then forces the two cold-section loops to differ. Unify on `Status` (compute the arm64
  complement at emit time) and share the structs. Then the trap-arm loop, the cold-edge loop, the suspend-stub
  spill/reload (`arm64:1228-1276`, `x64:1131-1183`) and `emitDirtyStores` (`arm64:486`, `x64:337`) collapse - note
  `emitDirtyStores` hand-inlines a per-entry store that `RegisterCacheBackend::emitSpill` already emits, so route the
  residency spill/fill through that existing abstraction instead of around it.
- **Shared per-function analysis context.** `GAZLJitArm64.cpp:738-756` and `GAZLJitX64.cpp:640-658` construct the same
  objects in the same order (pool, cache, `buildUseSchedule`, `buildPointerRealms`, `jitResidencyLeaders`,
  `buildLiveIn`, `entryMaps`, `coldTraps`/`coldEdges`, `residentEnd`/`resident`); only the pool arrays and the
  `SlotBackend` type differ. One context object constructed from `code`/`memory`/`funcStart`/`funcEnd` plus the
  backend's pool + `RegisterCacheBackend`.
- **One conditional-edge policy.** `emitConditionalEdge` (`GAZLJitArm64.cpp:562-575`) and `resolveConditionalEdge`
  (`GAZLJitX64.cpp:371-383`) encode the same three-way decision (entry map -> `reconcileTo`; resident -> `ColdEdge`;
  else `barrier`); only the branch emission differs. Its reconcile/barrier half is ALREADY shared as
  `reconcileOrBarrier` (`GAZLJit.h:445`) - the resident half was re-forked. Collapse to one shared "plan the edge"
  returning a destination/descriptor; x64's return-a-`Label` shape works for arm64 too.

Because `Reg`/`Cond`/`Label` are per-backend types with matching member names, the emitter-free pieces (residency
orchestration, the structs, the edge policy) move to `GAZLJit.cpp` with no templating; the emitter-touching pieces
(cold sections, prologue) need the skeleton templated on the emitter type.

## Second /simplify pass, 2026-09-17 (after tier C)

Four review agents (reuse / simplification / efficiency / altitude) over the whole JIT. Everything below is verified
the same way as the tiers: `--emit-jit` byte-identical to `5542b3d` on BOTH backends over 136 programs, lower test
debug and release on both, then `build.sh`, both emitter goldens, exec/engine/slice, firmwares plain and `--jit` on
arm64 and `--jit` on x64 under Rosetta, and 300k-deep soaks (seed 1800001, both backends, no divergence). The
cleanups remove 29 lines; the two measured optimizations add 30, so the pass is line-neutral and emits the same code.

Applied:

- **`jitResidencySafe` is `isCacheLowered` minus ten frame-touching opcodes** (-22 lines). The 71-opcode list was a
  hand-kept subset of the 81-opcode one; a new cache-lowered opcode used to fall out of residency silently. The
  `static_assert` message now names every list a new opcode must join.
- **`loadIntOperand`** beside each backend's `loadFloatOperand` (-14): seven copies of the const-or-slot preamble.
- **arm64 entry offsets come from the bound label** (`Arm64Emitter::labelOffset`, as x64 already did): the
  `entryOffset` vector and two `lowerFunction` parameters are gone (-6).
- **arm64 exit statuses are symbolic** - `movn(W0, ~BAD_PEEK)` rather than `movn(W0, 1)` with a decoding comment, at
  ten sites. Same instruction; the Status enum is no longer re-derived by hand.
- **`std::swap` in `JitModule::swap`** (-8), and the dead `TRANSFER` / `NATIVE_CALL` sentinels are gone.
- **Stale comments fixed**, including two user-visible ones in `tools/GAZLCmd.cpp` (`--forward` is not interpreter-
  only; the JIT is not arm64-only) and x64's execution-model prose, which still described segments returning
  TRANSFER / NATIVE_CALL sentinels when they return only terminal statuses and call `^native` inline.

Measured optimizations (compile time only - emitted code is byte-identical):

- **`buildLiveIn` accumulates in place.** The fixed point built two `std::set` trees per instruction per sweep; it
  now adds into `liveIn[j]` directly (monotone, so the same least fixed point) with the successor vector hoisted.
- **`operandRoles` is a table built once at load** instead of a linear scan of 294 operator rows per call - the JIT
  asks per instruction, per analysis, per sweep.
- Together, whole-program compile (min of 15, arm64): flakes 4.50 -> 1.41 ms (3.2x), pongdev 1.86x, phaser 1.74x,
  vortex 1.52x, mozaik 1.43x. The 300k-deep soak, which is mostly compilation, went 310 s -> 138 s on arm64 and
  640 s -> 300 s on x64 under Rosetta.

Identified and NOT applied (all still open):

- **Per-opcode const flags from `operandRoles`.** Both backends hand-encode `(s1Const, s2Const)` / `form` ~100
  times; the roles table already answers it (`OPERAND_OTHER` == a constant), verified for every form. -15 to -25
  lines, but a wide mechanical edit to bit-exact lowering; gate it on an opcode-by-opcode dump of the derivation.
- **`planColdTrap`.** The four-step trap protocol (label, status, `captureDirtyLines` AT the branch point, push) is
  hand-written at 12 sites. A helper called at the capture point keeps emission identical; the capture must not move.
- **The indexed PEEK/POKE and GETL/SETL cases** are four variants of one sequence per backend (~-45 and ~-18): the
  cache acquisition order is the whole risk, so it needs the byte comparison on both backends.
- **One `Label` type.** The two backends define `GAZL::Label` differently (arm64 POD, x64 defaults to -1); merging
  them retires the `BasicColdTrap` / `BasicColdEdge` templates (-10) and makes a missing label fail loudly.
- **A shared frame-aliasing predicate** for the eight `constAddrBase` / realm sites (~-10).

Changes emitted code, so measurement-gated (not applied):

- **Immediate compares.** Both backends materialize a constant compare operand into a pool register instead of using
  `cmpImm`, which also burns a pool acquisition inside loops. Prototyped on arm64: spectralnorm 49.6-52.4 ms ->
  44.8-46.8 (~9%), everything else flat, emitted code smaller everywhere. Needs a native x64 measurement.
- **Tighter successors.** `jitSuccessors` gives SWCH a fall-through edge and TAIL both a fall-through and no target;
  it is deliberately conservative (its comment says so) and only widens liveness. Tightening it shrinks residency
  maps, so it is a benchmark question, not a cleanup.
- **Route every terminal trap through `ColdTrap`.** GETL/SETL/COPY and the inline FUNC/CALL traps predate it.


## Looked at and deliberately NOT recommended

- **The LRU fallback path** (`Line::lastUse`, the `useSchedule == 0` branches at `GAZLJit.cpp:509, 532-533`) is never
  taken in production - both backends always `setUseSchedule` - and survives only for `tools/GAZLJitLowerTest.cpp`.
  Dual-mode machinery kept for one non-production caller, but not a clean delete while that test depends on it.
- **Folding const-address aliasing into the realm lattice.** Both backends answer "does this access alias a cached
  frame slot?" two ways: `pointerRealm` for the VVV forms, but an ad-hoc `constAddrBase = (p >= MEMORY_OFFSET)` for the
  VCV/CVV forms (`arm64:907, 949`, `x64:818, 834`). Arguably one question, but low confidence that merging them is
  behaviour-preserving - needs its own analysis.
- **`jitResidencyLeaders` side-entry check is O(headers x functionLength)** (`GAZLJit.cpp:201-224`), re-reading SWCH
  tables per candidate. Real only for large functions with several loop candidates; do not restructure without a
  measured need (CodingStyle section 2: optimize only for a proven win). MEASURED 2026-09-17 and CLOSED: 0.003-0.006
  ms, under 1% of a whole-program compile and the smallest of the five analysis phases. Do not restructure it.
