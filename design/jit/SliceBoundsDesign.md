# Slice bounds: within-realm pointer precision (design record)

**Status: DESIGNED AND SHELVED** (2026-07-19). Not on the JIT roadmap. Revive only when a shipped firmware
demonstrably loses performance to same-frame bank aliasing in a hot loop, AND the Impala range-analysis lift is
budgeted. The JIT's sound aliasing work terminates at v2.3a (realm-scoped flushing, `JitCompilerResearch.md` section 1.1).

This document records why the feature was cut, and the minimal design that survived review, so the reasoning does
not have to be reconstructed later.

## 1. The problem it would solve

v2.3a distinguishes pointers *between* realms (globals / constants / this frame's params / locals / transients):
a non-frame pointer access provably cannot alias a register-cached local, so the JIT skips the cache flush. What
v2.3a cannot do is tell two slots apart *inside* one realm: a `POKE` through a MYFRAME pointer still conservatively
flushes every cached local, because "within a realm nothing is assumed" (section 1.1). Within-realm precision would
let a write into a known sub-range (a bank) keep unrelated locals resident.

This was originally staged as "v2.3b". It was removed from the JIT roadmap for two independent reasons.

## 2. Why it was cut

**2.1 The motivating workload evaporated.** The one firmware cited for the aliasable-bank case was the vortex
reverb (eggtones). Reading the actual shipped build (`benchmarks/firmware/golden/vortex_code.gazl`) shows its hot
per-sample loop is pure v2.3a territory: the delay line is walked through `$line = &verbLine` (a GLOBAL symbol
pointer, 21 PEEK/POKE per sample) and the `verbState` bank COPY is once per `process()` call, outside the loop.
v2.3a alone measured vortex -6.0% / specular -7.1% (native arm64, --jit). No shipped firmware is known to need
within-realm precision.

**2.2 An optimizer-only range contract breeds JIT-only miscompiles.** A slice annotation is an *assertion*, not a
derivation. If it is ever wrong, the JIT optimizes against a false premise while the interpreter's flat memory
quietly does the "right" thing: a divergence only the JIT exhibits - the worst bug class this codebase knows (see
the v2.3a-lite scalar-deref incident, commit b09affe). Realms never had this exposure because they are derived
from pointer origin with no annotation at all; both engines agree by construction.

## 3. The design that survived review (if ever revived)

**Core principle: a slice bound is a RUNTIME-ENFORCED check executed identically by both engines - never an
optimizer-only hint.** A wrong slice is then a deterministic `BAD_PEEK`/`BAD_POKE` in interpreter and JIT alike,
visible to the differential fuzzer, never a silent divergence. The JIT's aliasing win (two accesses with disjoint
static ranges cannot alias) falls out of the same constants the check enforces.

**3.1 Encoding: the index-less deref.** The indexed memory forms have no free operand (`PEEK_VVV dst,base,index`
and `POKE_VVV base,index,value` use all three slots), so sliced variants of those cannot encode bounds. Instead:

```
PEEK_slice  dst, ptr, bounds        ; dst = *ptr, trap unless ptr inside the slice
POKE_slice  ptr, val, bounds        ; *ptr = val, trap unless ptr inside the slice
```

The index folds into the pointer with an ordinary `ADDi` before the access. Fits the existing three-operand
instruction format: no fat pointers, no Instruction widening, flat int pointers throughout.

**3.2 The check.** One unsigned compare, form depending on the slice's realm:

- Globals/constants slice (absolute range, assembly-time constant):
  `(unsigned)(ptrWord - start) < len` - constants only, NO memory load; today's unsliced check loads
  `rwMemorySize` from the context, so this is cheaper than the check it replaces.
- Frame slice (dsp-relative range): `(unsigned)(ptrWord - dspWords - start) < len` - one extra subtract; the
  interpreter has `dsp` in hand and the JIT has DSP pinned.

**3.3 Bounds operand.** Packed `(start,len)` immediate as the fast path (frame slices trivially fit; a bit split
for globals is UNRESOLVED - candidate 20/12), with a two-word `{start,len}` descriptor in constant memory as the
general fallback (`SWCH`'s jump table established that pattern; the load lands in hot constant memory, roughly
replacing the `rwMemorySize` load the sliced check no longer does).

**3.4 Function calls: slices never cross them, and never need to.** The register cache holds only the CURRENT
frame's slots, so within-realm precision only matters for MYFRAME pointers - which are by definition ADRL-born in
the same function, where the compiler statically knows the slice. Received pointers are NONFRAME under v2.3a and
already skip flushes; memory behind pointers is never cached, so pointer-vs-pointer overlap is irrelevant to the
cache. Slices are therefore static, per-access-site knowledge that decays at the call boundary (like a C pointer).
If cross-call CHECKING is ever wanted, it is pure Impala surface, no engine change: a parameter type with a static
length (`ptr[12]` - callee sites re-anchor on the received base with the existing indexed forms + a static bound),
or a runtime length passed as an ordinary extra int argument (a Rust-style fat pointer spelled as two values).

**3.5 Impala surface.** Slice-typed pointers, e.g. `&a[3:9]` ("may only touch words [3,9) of `a`"; syntax TBD).
Impala emits sliced ops only where it can PROVE the range (interval analysis on the index - the real lift), else
the plain ops (whole object, today's behavior, zero regression). A hand-written slice is an unchecked assertion
downgraded by co-enforcement from miscompile to trap.

## 4. Known costs and open questions

- The interpreter pays one extra `ADDi` dispatch per sliced access on walked pointers (the indexed form's add was
  free). One binary serves both engines, so sliced ops must be emitted selectively where the JIT residency win
  outweighs the interpreter add.
- The packed-immediate bit split for large globals ranges is unresolved (packed vs descriptor threshold).
- The Impala interval analysis is unscoped and is the bulk of the work.
- The JIT-side consumption (slot-range vs slice overlap tests in the flush logic) is straightforward bookkeeping
  but untouched.

## 5. Revival criteria

1. A shipped firmware with a demonstrated hot-loop penalty from same-frame aliasing that v2.3a cannot remove
   (measure first: the vortex lesson is that the presumed case may already be covered).
2. Acceptance of the Impala lift (syntax + proofs) in the same initiative - this is a language feature with an
   optimizer rider, not a JIT release.
