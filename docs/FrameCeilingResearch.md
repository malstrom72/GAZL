# Frame ceiling: toward true interrupts and CPU freeze

Status: design notes / research, 2026-07. Nothing here is implemented; this records the problem, the design
space we explored, and the recommendation, so the eventual implementation does not have to rediscover it.

## The problem

At any suspension point, the live extent of the current data-stack frame is
`[dsp - localsSize, dsp + paramsSize)` - both sizes are static per function (`FUNC p0` / `p1`), but the
runtime does not know **which function it is suspended in**, so it cannot compute the frame's ceiling
(`dsp + paramsSize`, the first provably dead cell). Two capabilities want exactly that number:

1. **True interrupts** - injecting a call into a processor suspended at an *arbitrary* instruction requires a
   window above all live data. Today this is sidestepped: `pushCall()` injects only at native call sites
   (where the `^call`'s reserved window IS the safe spot), and host-level interrupts either use `enterCall`'s
   sentinel pattern (safe by the yield-at-statement-level convention, not by construction) or a reserved
   stack slice / companion processor. All good solutions - but none can inject at an arbitrary suspension.

2. **CPU freeze** - serializing a *suspended mid-execution* machine ("suspend and resume full machine state"
   is a stated GAZL goal). Globals serialize today; a mid-execution freeze must also persist the live part of
   the data stack. Without the ceiling the only sound choice is the whole span `[dataStackBase, dsend)` -
   correct but maximal (and for Permut8-sized stacks, mostly dead bytes). With the ceiling, the live extent
   is exact: `[dataStackBase, dsp + paramsSize)` (everything deeper is covered - callers' frames lie below
   `dsp`). The ipStack must be serialized either way; frames hold `dsp` pointers and, under the JIT, compiled
   return addresses that need relocation to (ordinal, offset) form - a separate problem, noted, not solved
   here.

Note the interpreter can suspend between ANY two instructions (per-instruction fuel), so "arbitrary
suspension" includes mid-call-setup with live transients; the ceiling is precisely what makes even those
points safe, because `paramsSize` bounds every transient the function can touch.

## Options considered

**A. Track a member (`currentFunc` / ceiling) updated at call entry.** Not viable alone: `RETU` has nothing
to restore it from (the popped frame's ip points into the caller's *code*, not at its `FUNC`). Degenerates
into B.

**B. Carry it in the call frame** (widen `CallStackEntry` to `{ip, dsp, func}`).
Pros: O(1) answer anywhere, no invariants; a current-function pointer is also good diagnostics material.
Cons: hot-path cost on every call/return, and the frame stride is baked into BOTH JIT backends (the
`ipsp +/- 16` in the emitters) plus frame serialization - the largest total surface of any option. Taxes
every program for a rare feature.

**C. Binary-search `functionTable` at need.** Function offsets are ascending (assembly order), so
`upper_bound(ip - codeBase) - 1` finds the enclosing `FUNC`; read `p1`. Zero hot-path cost, one cold lookup
per use. Cons: an invariant to protect (assert at finalize), and "where am I" answered by searching - the
KISS objection. Fine as an internal implementation detail of a freeze/inject API; questionable as a load-
bearing runtime mechanism.

**D. The JIT publishes it at suspend stubs.** The JIT statically knows the current function at every
suspend stub it emits, so it could store `frameCeiling` as an immediate alongside the state it already
publishes. Rejected in favor of C once the C-under-JIT question was answered (below): D costs a store per
suspend stub in both backends and extends the published-state format, for a value C can derive on demand.

## Recommendation: option C, symmetrically on both engines

The objection to C was the JIT: compiled code never maintains `this->ip`, so there is nothing to search
with. But the JIT has the exact analog - at any suspension or native call, `resume` points INTO the current
function's compiled code, and `funcEntries` ascends in ordinal order just as `functionTable` does. The same
cold binary search works on both engines with a different key:

- interpreter: bsearch `functionTable` by `this->ip` (code offsets, ascending in assembly order);
- JIT: bsearch `funcEntries` by `this->resume` (native addresses, ascending in ordinal order).

One virtual `UInt currentFunction() const` (returns the ordinal), and everything falls out engine-
independently: `Value* frameCeiling() const` = `dsp + (codeBase + functionTable[currentFunction()])->p1.i`.

- Zero hot-path cost, zero frame/ABI/backend changes, no new published state, nothing to serialize.
- Two invariants, both already true by construction, each worth an assert: functionTable offsets ascending
  (assembler finalize) and funcEntries ascending (JIT compile/bind).
- Validity contract: meaningful while SUSPENDED or inside a NATIVE call (the only times a host asks); after
  a final return to the host the value is stale by definition.

On top of it: `Value* interruptCall(fn)` = inject at `frameCeiling()` with the callee's own FUNC check done
early (the `pushCall` pattern generalized to any suspension point), and freeze = serialize
`[dataStackBase, frameCeiling())` + the ipStack.

## Interactions to keep in mind

- **Serialized-state compatibility**: `frameCeiling` extends the freeze format; version it.
- **The sentinel discipline stays**: this proposal does not touch `enterCall`/`pushCall` semantics; it adds
  the one datum they cannot compute. The reserved-slice/companion-processor interrupt patterns remain valid
  (and remain the strongest isolation); `interruptCall` covers the "no reservation, inject anywhere" case.
- **JIT residency (v2.2+)**: a suspended loop may hold state in registers; suspend stubs already spill to
  the frame before publishing, so the ceiling remains the correct bound. Any future optimization that keeps
  live values ABOVE `dsp + paramsSize` would break the invariant and must not be written.
- **Threads**: the multi-stack constructor composes - each processor's ceiling is its own.
