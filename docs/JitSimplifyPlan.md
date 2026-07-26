# JIT Simplify Plan (tiers B and C)

Findings from a `/simplify` review pass over the JIT compiler (the `jit-compiler`-vs-`main` diff: `src/GAZLJit*`,
the JIT parts of `src/GAZL.*`, `tools/GAZLCmd.cpp`). Tier A (dead run-state fields, redundant `operandRoles` decode,
unconditional `buildLiveIn`, duplicated `keepMax` formula, arm64 double map lookups) is DONE. What follows is what was
deliberately deferred, with the evidence, so it can be picked up without re-deriving it.

Line numbers are from the state right after Tier A; treat them as pointers, not gospel.

## Root cause behind most of this

`JitCompilerArm64::lowerFunction` and `JitCompilerX64::lowerFunction` are structurally parallel skeletons - identical
pass-1 analysis, pass-2 leader/residency bookkeeping, and cold-section phases - wrapping arch-specific `case` emits.
That skeleton was COPIED rather than hoisted, so the JIT's subtlest logic exists as two hand-kept-identical copies.
Tier C is the real fix; tier B are the pieces that can move without restructuring.

## Tier B - contained helpers, low-to-medium risk

- **Shared `cacheLowered`.** `GAZLJitArm64.cpp:515-553` and `GAZLJitX64.cpp:386-424` list the same opcode set (which
  opcodes route through the cache is a property of the IR, not the target); the copies differ only in row order and
  `OP_ABSI` placement. Hoist to one `JitCompiler::isCacheLowered(op)` in `GAZLJit.cpp`, beside `jitResidencySafe`
  (`GAZLJit.cpp:145`). Verify the two lists are coverage-identical BEFORE merging - a silent drift is possible today.
- **SWCH jump-table decode helper.** The idiom `size = p1.i + 1; table = p2.p - MEMORY_OFFSET; for k: target = j +
  memory[table + k].i` recurs at `GAZLJit.cpp:94-97`, `:196-197`, `:215-218`, `:383-385`, plus both backends' `OP_SWCH`
  - six copies of one nontrivial address computation. Collapse to a `forEachSwitchTarget(code, j, memory, fn)` (or an
  index-yielding helper).
- **x64 two-operand alias helper.** `emitBinary` (`GAZLJitX64.cpp:435-443`), `emitDivFChecked` (`:553-561`) and
  `emitBinaryFloat` (`:571-579`) each spell out the same `if (d != b) { if (d != a) copy(d, a); op(d, b); } else
  { temp; copy(t, a); op(t, b); copy(d, t); }` dance, differing only in the move/binary op. One
  `emitTwoOperand(copyOp, binOp, d, a, b, scratchClass)` states the aliasing argument once.
- **Merge `buildLoopSlotSets` + `buildLoopClassSets`.** Always called back-to-back over the identical
  `[header, extent]` range (`GAZLJitArm64.cpp:773-775`, `GAZLJitX64.cpp:675-678`), each re-decoding `operandRoles` per
  instruction. One pass filling all four sets halves the walk.
- **`RegisterCache` should use its own accessors.** `captureDirtyLines` (`GAZLJit.cpp:651-664`) rebuilds
  `count`/`lines`/`registers` from `(c == 0) ? GENERAL : FLOAT` by hand instead of calling `linesOf`/`registersOf`; the
  same manual selection recurs in `capture`, `reconcileTo`, `spillDirtyResident`, `assertNoDirty`. Go through the
  accessors so the "line `i` holds `registersOf(class)[i]`" invariant is stated once.
- **FORi's read-modify-write counter belongs in the operand table.** `GAZLJit.cpp:288, 401, 434, 469` each call
  `operandRoles()` and then hand-override p0 for `OP_FORi_VVB/VCB`. Four consumers re-encode one fact, so the role
  abstraction lies for FORi and every future consumer must remember the fixup. Express it once - a
  `OPERAND_SLOT_READ_WRITE` role, or have `operandRoles()` mark it. CAUTION: `operandRoles()` lives in `GAZL.cpp` and
  has non-JIT callers; audit them all before changing its contract.

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
