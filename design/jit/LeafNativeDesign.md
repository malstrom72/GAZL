# Leaf-native fast path: design record

**Status: DESIGNED AND SHELVED** (2026-07-20). The contract + debug gate were implemented (commit f07e820) and
then REVERTED (bdac72d) - shipping a `registerNativeLeaf` API while no optimization consumes leaf-ness advertises
a capability that doesn't exist. Revive only when (a) a host actually exposes hot native primitives AND (b) a
profile shows the native-call boundary is a measurable fraction of runtime AND (c) the callee-saved-residency
work below is accepted (that, not the fast-path sequence, is where the real win lives).

## The idea

Let the host declare a native LEAF - a pure function of its parameter window - so the JIT can call it more
cheaply than the fully paranoid `CALL_NVC` path. Two functions, no flag param: `registerNative` (full, default)
and `registerNativeLeaf`.

## The leaf contract (what a leaf may do)

A leaf reads its params, computes, writes results into the window, returns OK. Precisely - it may ONLY:
- call `accessParams` (any count within the call's window; the existing data-stack bound is the license - a
  leaf's reach is the transient region from the window base up, transients-as-scratch included, exactly what
  accessParams already grants; NO extra size policing needed).
- touch host-side C++ state (its own RNG, tables - outside the GAZL memory model).

It may NOT: `accessMemory` / `accessConstMemory` (reaches arbitrary GAZL memory), `pushCall` / `enterCall` /
`run` (re-enters GAZL), `resetTimeOut` (touches fuel), or return non-OK (the non-zero path is a re-issuable
suspend the fast path doesn't publish state for).

Examples: `sqrt`/`sin`/`cos`/`exp`/`pow`/`atan2`/`fmod`/... are leaves. `print` is NOT (walks a string via
accessConstMemory). `printInt` is NOT (calls resetTimeOut as a host watchdog). The `^yield`/`^read`/`^write`/
`^trace` firmware forwards are NOT (they pushCall).

## The debug gate (how the contract is enforced, cheaply)

GAZL memory/control is reachable ONLY through Processor methods, so gating that exact set is a COMPLETE check,
not a sample. Mechanism (all `#ifndef NDEBUG`, ~16 bytes/Processor in release, zero release logic):
- a `mutable int leafGuardDepth` + an RAII `LeafScope(this, isLeaf)` armed around the native invocation in
  `CALL_NVC`;
- `assert(!leafActive())` one-liners in accessMemory / accessConstMemory / resetTimeOut / pushCall / enterCall /
  run; plus `assert(leaf -> status == OK)` after the call. accessParams is deliberately NOT gated.
The interpreter arms the gate (every native runs through it in the lockstep + fuzz differential, so a mis-declared
leaf trips there regardless of which engine ships). Refutation-by-execution: flag a native leaf optimistically,
run the suite + fuzzer, and each assert names the one native + the rule it broke. Ship conservative (default
full); leaf is a deliberate opt-in, with the flag-all run as evidence not authority (an under-tested wrong-leaf
is a silent field miscompile - so the fuzzer, not unit coverage, is the "all branches" backstop). VERIFIED with
teeth before the revert: a leaf that calls accessMemory aborts with the exact assert.

## Why the JIT win is smaller than it looks - the C ABI

The register-cache pool is ENTIRELY caller-saved (x64: R8-R11 + xmm2-5; arm64: W5-W8/W16/W17 + V16-V23). A
native is a real C function that clobbers caller-saved registers, so the cache is ALREADY fully flushed around
every native call (that is why today's CALL_NVC does a full barrier + reloadState). Leaf-ness CANNOT keep the
cache resident across the call - the C ABI clobbers it either way. So a leaf's direct saving is only the
PLUMBING: skip the `nativeAfter` store + indirect continuation branch (a leaf can't pushCall, so the OK
continuation is the fall-through) and the BLOCK_RETRY republish arm. ~3-4 hot instructions + one indirect branch
per call. Real but modest, and there is NO corpus workload (firmwares bundle their own libm; the suite has zero
native calls) - only a synthetic microbench would show it.

## Where the real win actually is: callee-saved residency (a SEPARATE, bigger project)

To keep a value (a DSP accumulator / oscillator phase) alive across a per-sample `sin()`, it must live in a
CALLEE-saved register - the only registers a C call preserves. That needs a second register class the allocator
uses ONLY for values live across a (leaf) call, plus dispatcher prologue save/restore of that set. The leaf flag
becomes the PERMISSION (a leaf won't re-enter / blow the stack, so a value may survive it); the heavy lifting is
the allocator + prologue work. It is PLATFORM-ASYMMETRIC (callee-saved SCALAR-FLOAT registers):

| platform                    | callee-saved float regs |
|-----------------------------|-------------------------|
| arm64 (Apple Silicon)       | 8   (d8-d15)            |
| x86-64 Win64                | 10  (xmm6-xmm15)       |
| x86-64 SysV (Intel mac/Lin) | 0  - structurally impossible |

So the float-across-`sin()` win is achievable on arm64 + Win64 and IMPOSSIBLE on SysV x64 (no callee-saved xmm
exists). The x64 backend already picks its pins from the common callee-saved set (RBX/R12-R15) and saves them
once in the dispatcher prologue, with the only ABI splits isolated to ARG_0 (RCX/RDI) and CALL_FRAME (Win64
shadow space) - so extending the saved set with callee-saved cache registers is mechanically clean where the
registers exist.

## Revival plan (one coherent pass, when justified)

1. Re-land the contract + gate (f07e820 is the reference).
2. JIT: CALL_NVC-to-leaf emits the reduced sequence (skip nativeAfter/indirect/retry); keep reloadState (pins are
   clobbered regardless) and the barrier (cache clobbered regardless) - UNLESS step 3 lands.
3. The real win: a callee-saved cache class for values live across leaf calls (arm64 + Win64), prologue
   save/restore. SysV x64 keeps spilling floats around calls.
4. Fuzzer arm: a declared-leaf native + a deliberately-cheating one (must trip the gate); host math table under
   the fuzzer with everything optimistically leaf-flagged.
5. Benchmark: a synth-representative per-sample transcendental kernel, arm64 + both x64 ABIs.
