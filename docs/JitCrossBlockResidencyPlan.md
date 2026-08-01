# v2.2-full: cross-block register residency (working plan, 2026-07-19)

> **Historical working plan**, moved here 2026-08-01 from an untracked `temp/` directory where it was
> one `rm -rf` from being lost. The work it plans has SHIPPED - see `JitSessionBenchMatrix.md`, milestone
> `maps2` (`114fb44`). Kept for the reasoning, not as a to-do list.

Goal: keep the floating register cache live ACROSS control-flow boundaries (not just single-block loop
back-edges), so loop-carried values stop reloading every iteration on loops that have internal branches
(sieve early-exit, mandelbrot iteration test). Aliasing-FREE (control-flow flush removal only; hazard flushes
stay). The roadmap's designated "next real target". Expected win: mandelbrot-class +20-40%.

## What exists (v2.2 first cut, commit c77be2b) - verified in src/GAZLJit.{h,cpp}

- `jitResidencyLeaders(code, funcStart, endIndex, headers, entryReadSlots, entryWriteSlots)`: qualifies ONLY
  loop headers whose body is a SINGLE register-only block - a back-edge target reached by no forward branch/SWCH,
  every body op `jitResidencySafe`. Others are disqualified.
- `RegisterCache::capture(map, readInLoop, writtenInLoop)`: snapshots the resident set at the header as a fixed
  `ResidencyMap` (pruned to slots the loop reads; RESIDENCY_HEADROOM=3 regs/class left free; read-only slots
  canonicalized register==home via one pre-header store; written slots flagged expectDirty).
- `RegisterCache::reconcileTo(map)`: at a back-edge, spill-and-drop strays, fill missing - emits nothing when
  already matching. Flags untouched (may sit between compare and branch).
- `reconcileOrBarrier(cache, entryMaps, target)` (GAZLJit.h inline): if target has an entry map -> reconcileTo,
  else `barrier()` (full flush). Both backends call this at every block-ending branch.
- Suspend/resume: residency headers spill the map in the suspend stub, refill in the resume trampoline.

Limitation: any loop with an internal forward branch/join drops to barrier() at that leader -> the whole loop
reloads every iteration. Single-block only.

## v2.2-full design

### Phase A - global backward dataflow (arch-neutral, GAZLJit.cpp)
Compute, per basic-block leader, the set of slots LIVE-IN (read before written along some path from the leader),
using the existing block partition (jitFuelSafepoints leaders) + a standard backward fixed-point over the
successor graph (successors from jitBranchTarget + fall-through). Reuse operandRoles for gen/kill. Output:
`std::map<UInt, std::set<Int>> liveInAtLeader`. Also reuse buildUseSchedule for next-use ordering.

### Phase B - per-leader entry maps (replaces single-block capture)
For each leader, decide a residency map = the live-in slots that fit the pool (rank by next-use / loop depth;
keep RESIDENCY_HEADROOM free). This is the Liftoff "merge point state". Constraints:
- A leader reached by MULTIPLE edges must have ONE agreed map (all predecessors reconcile to it). Compute the
  map at the leader from liveIn, independent of predecessors, so every edge just reconciles to it.
- Back-edge headers keep expectDirty modeling (a written loop-carried slot is dirty on re-entry).
- Fall-through edges also reconcile (today a fall-through into a leader assumes barrier state).

### Phase C - edge reconciliation codegen (both backends)
Replace `reconcileOrBarrier` so EVERY leader has a map (possibly empty) and every in-edge (branch, fall-through,
back-edge) emits `reconcileTo(leaderMap)`. Edge shuffles: when predecessor residency != leader map, reconcileTo
already handles spill-stray/fill-missing; a value in the wrong register spills+refills (a later optim: parallel
register moves to avoid the memory round-trip - DEFER).

### Phase D - suspend/resume from the same table
A leader's suspend stub spills its map; the resume trampoline refills it. Falls out of reconcileTo primitives -
already true for the single-block case; generalize to all leaders.

## Correctness gates (this is cross-block cache state - highest-risk area this session touched)
- The v2.3a bug + div work proved: trap/suspend exits must leave memory interpreter-current. Every leader map
  transition must preserve that. Lean HARD on:
  - Lower test (all kernels, incl the new realm teeth kernels) arm64 + Rosetta + native Win64.
  - 27 firmwares both engines.
  - Differential fuzzer G3 (control flow: nested FORi + if-skips) is the key policeman - it already generates
    the internal-branch loops v2.2-full newly makes resident. Soak 300k+ deep PER backend BEFORE trusting.
  - A/B min-of-3 both boxes: expect mandelbrot/sieve gains; watch the x64 layout-noise band.

## Staging (land behind the engine switch, each step lockstep-green)
1. Phase A liveness + a focused unit test (assert live-in sets on a hand loop) - pure analysis, no codegen change.
2. Phase B+C together behind a compile flag or gated to the SAME loops the first cut already handles - prove
   byte-identical / equivalent to the first cut (no regression) before widening qualification.
3. Widen qualification to internal-forward-branch loops; soak; A/B.
4. Phase D generalization; soak.
5. DEFER: parallel-move edge shuffles (avoid spill/refill round-trip); cost-based (v2.4).

## Risks / open questions
- reconcileTo currently assumes it runs "between instructions" (asserts no pinned/scratch). At a fall-through
  leader mid-emission that must still hold - verify.
- A leader with an empty map = barrier-equivalent? Not quite: barrier() flushes ALL dirty; an empty reconcile
  drops all. Need barrier semantics (flush dirty to home) when the map is empty AND lines are dirty - confirm
  reconcileTo spills dirty strays (it does: spillLine). So empty map == flush-all == barrier. Good.
- Join of differing predecessor dirtiness: if one edge has slot dirty and another clean, the leader map's
  expectDirty must be the JOIN (dirty wins) or a predecessor's clean value gets a skipped store. Model dirty.
