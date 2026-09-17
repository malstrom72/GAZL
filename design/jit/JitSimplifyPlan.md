# JIT Simplify Plan (tiers B and C)

Findings from a `/simplify` review pass over the JIT compiler (the `jit-compiler`-vs-`main` diff: `src/GAZLJit*`,
the JIT parts of `src/GAZL.*`, `tools/GAZLCmd.cpp`). Tier A (dead run-state fields, redundant `operandRoles` decode,
unconditional `buildLiveIn`, duplicated `keepMax` formula, arm64 double map lookups) is DONE, and so is tier B (its
section records the outcome). Tier C was deliberately deferred, with the evidence, so it can be picked up without
re-deriving it.

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
- NOT measured yet: x64 speed. It needs native hardware; Rosetta timings are noise.

## Tier C - architectural, needs a deliberate decision

This is a real refactor of a bit-exact JIT. High value (it removes the two-copies-of-everything problem), but it must
be done in verifiable steps: after each step run the lower/exec/engine/slice tests, both emitter byte-golden tests, and
`checkPermut8Firmwares.sh` both plain and `--jit`, plus a fuzz soak.

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
  measured need (CodingStyle section 2: optimize only for a proven win).
