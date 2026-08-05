# GAZL JIT - Aliasing & Register Allocation: design conclusions

Conclusions from the aliasing / register-allocator design pass. Higher-level context lives in
[JitCompilerResearch.md](JitCompilerResearch.md) (§1.1 aliasing, §5.7 allocator) and
[JitInvestigations.md](JitInvestigations.md) (§1 LOCA/region model); the C++ backend is in
[CppBackendSpec.md](CppBackendSpec.md). This file records what we *decided*, not the derivation.

## 1. Aliasing rule: `*0` legacy / `*N` precise (revive the `ADRL` size hint)

The current §1.1 provenance‑bounded rule ("a pointer from `ADRL var` is defined `[&var, dsp)` upward") is
sound but **directional and error‑prone** - a legitimate backward loop from a high local to a low one
reads as "unspecified." Replace it with a mechanism that is already in the ABI:

- **`ADRL base *0` (legacy) = unbounded, "anything from here can alias."** Conservative. This is what
  *every* existing program emits (the `*size` hint exists today and is ignored), so honoring it
  conservatively is bit‑identical and needs no analysis.
- **`ADRL base *N` (N>0, new) = the pointer accesses exactly `[base, base+N)`.** A trusted extent: only
  those slots are aliasable; everything else in the frame is private. Non‑directional (a region is a
  set of known size, so backward traversal inside it is fine).

Fully wire‑compatible: old tools already ignore `*size`, so they run new `*N` code; a new JIT sees old
`*0` and takes the conservative path. Nothing existing changes meaning.

**New Impala emits the extent** - from the `copy` length or the `LOCA` size - and lowers banks/structs to
`LOCA` objects. Because a high‑level language has no "adjacent unrelated scalars," a cross‑local span is
**unexpressible**, so "no span" is enforced in the *frontend* (a compile error there), not in the GAZL
assembler (which must stay permissive for hand‑written / third‑party GAZL). The `*N` for a `copy` is just
its length; no new surface syntax is needed.

**The rule we wish GAZL had from day one:** *a pointer into a local can never alias another local* - i.e.
the C object model (a pointer addresses the object it came from, not its neighbors). It makes register
allocation trivial (a local is register‑material unless *its own* address is taken) and it bounds even the
unprovable cases (below). The `*N`/`LOCA` path reinstates exactly this rule going forward.

## 2. Empirical: how often is the strict rule actually broken?

Scanned the 57‑program golden corpus **and** the 13 shipped Permut8 firmware banks (scoping‑aware, every
`ADRL` target resolved, 0 unresolved):

- **Legal array aliasing** (`ADRL` of a `LOCA`): 308 golden + 90 wild sites.
- **Single‑scalar by‑ref** (`&x` out‑param, aliases exactly one slot): 61 golden + 16 wild. Benign.
- **Cross‑scalar span** (`ADRL $scalar` + multi‑word `COPY` reaching into neighbors): **6 sites, 4
  programs total** - `perfTest`, `buffer`, `nobuffer`, and `Vortex` (`verbState` = 12 scalars,
  `voiceState` = 31). Every one is the "bulk‑copy a global state struct into a scalar bank" idiom.

So the strict "only `LOCA` may be aliased" rule already holds everywhere except **4 programs**. Those stay
correct under `*0` (conservative) until recompiled with new Impala, at which point their banks become
`LOCA` regions and the violations disappear. This is the whole justification for the mechanism: the cost
is levied on 4 programs, not the corpus.

## 3. The irreducible "never safe" residue

Provenance is lost only when a **frame pointer escapes through an opaque boundary**: a non‑inlined call
return (`p = func(&x)`) or a store‑then‑reload through memory. There is no third way.

- **Always sound:** unknown provenance ⇒ treat as possibly‑aliasing ⇒ flush conservatively.
- **Bounded** (under the clean rule): such a pointer can only reach the *object whose address escaped*,
  never an unrelated local - so the blast radius is exactly the already‑tracked escaped set.
- **Universal:** every compiler stops at this exact line (LLVM: an escaped `alloca` + an external‑call /
  loaded pointer = `MayAlias`). It's the definition of "escaped," not a GAZL flaw. Inlining collapses the
  call case.
- **Harmless in practice:** it doesn't occur in hot loops (they inline, and nobody launders pointers
  per‑sample), so it lands in cold glue as a couple of priced reloads.

## 4. Metadata placement (get this right or the aliasing win evaporates)

| per **value** (keyed by home slot; set at each def) | per **register** (the cache binding) |
|---|---|
| class: int / float / pointer (→ which register file) | dirty (register copy diverged from home?) |
| provenance: frame / global / unknown (**pointers only**) | which value it currently holds; LRU/recency |

Provenance is a fact about *what a pointer points at*, so it lives with the **value**, not the register -
it must **survive spill/reload** (else a reloaded pointer degrades to "unknown" → spurious flushes). It
rides on the pointer‑carrying slots only: `LOCp`, `PARA`/`INPp`, pointer‑typed `LOCA` elements, transients
currently holding a pointer, and `ADRL`/`ADDp`/pointer‑`PEEK` results. Pure `LOCi`/`LOCf` values never
carry it. (The GAZL assembler enforces types via the mnemonic+operand‑type key, so a pointer can only
ever land in a pointer‑typed slot or a typeless `%` transient.)

## 5. Simplest single‑pass allocator (the v2.0 floor)

- **Canonical‑memory joins.** At a branch‑target label the register map is **empty** (every value in its
  frame home); before any branch, flush all *dirty* registers to their homes. Each edge flushes
  independently → joins need no snapshot matching or shuffle code. Correct for arbitrary GAZL, **no
  liveness required**.
- Load‑on‑use, dirty‑on‑def; under pressure evict (clean = drop free, dirty = store to home).
- **Provenance gates clobbers:** a `PEEK`/`POKE` through a global/param pointer does **not** flush the
  local cache; only a frame‑derived or unknown pointer does. This one bit is what keeps a state bank
  resident through a loop full of global‑buffer memory ops.
- Cost: loop‑carried values reload every iteration (the price of canonical‑memory joins). Ships correct
  and already dodges the blanket‑flush disaster; the remaining traffic is what §6 removes.

## 6. Next‑use eviction - split by scope (block-local is cheap, global is the multi-pass)

The "backward pass" is two different-sized things; splitting them puts each where it is cheap:

- **Block-local next‑use = v2.0.5, not a multi-pass.** The floating cache is cleared at every block
  boundary, so the only next‑use it can act on is *within the current block* - a single reverse scan of the
  block, no fixpoint, no cross‑block liveness. It does **not** break v2.0's "no liveness" property (that is
  about cross‑block dataflow). It also yields block-local **last‑use**, so a dead transient is dropped
  instead of flushed (skip-dead-flush). This is a **drop‑in victim‑selection swap** on v2.0's cache, so it
  is a point release (v2.0.5), independent of bound lines; it already bites under x64 pressure.
- **Global next‑use / liveness = the real multi-pass, folded into v2.2.** The full backward dataflow pass
  (fixpoint, `live_in = (live_out − dests) ∪ sources`; up to 3 operands, an instruction's own operands
  **pinned** during its cycle) is only needed to decide what stays resident *across* edges - exactly what
  v2.2's Liftoff join reconciliation needs, and worthless without it. So it rides in with v2.2, not as a
  stage of its own.
- **Belady eviction** (furthest‑next‑use) replaces LRU - fed by whichever scan is in scope (block-local at
  v2.0.5, global at v2.2). This is the single highest‑value change; LRU's fatal move is evicting a hot
  pointer (`$line`) right before its next use.
- Alternatives to LRU **and** pinning: **(a)** Belady (which to evict), **(b)** cost‑based + **remat**
  (recompute cheap invariants like `&global`/`ADRL`/constants instead of spilling), **(c)** live‑range
  **splitting** (hold a value in a register only across the spans that use it - residency where it pays,
  not a fixed reservation).
- Payoff beyond speed: a real cost‑model allocator makes aliasing **graceful** - an imprecise clobber set
  costs a few *priced* reloads, not a cliff. So the aliasing spec only has to be **cheap and
  conservative‑monotone**, not perfect. That removes the pressure that made the §1.1 rule feel fragile.

## 7. Register budgets & where allocation quality shows up

| | GP (int/ptr) | FP/SIMD |
|---|---|---|
| arm64 | 31 | 32 |
| x86‑64 | ~15 usable (16 arch − RSP; RBP if kept) | 16 XMM (32 ZMM only with AVX‑512) |

x64 ≈ half of arm64. On arm64 a Vortex‑sized bank fits with room and the simple allocator is near‑optimal;
on x64 the bank (~10 GP) nearly fills the integer file → real pressure → allocation quality matters. The
4‑GP/4‑FP stress case is pathological: the loop's long‑lived working set (≥7 values) can't fit regardless,
so allocator quality moves memory traffic from ~2× (LRU) to ~1.3× (Belady + cross‑block residency), **not
to 1×** - the last stretch is "buy more registers," which real arm64/x64 have. Also note x64 is
**two‑operand** (`dst OP= src`), so it needs extra `mov`s arm64's three‑operand form avoids - more move
pressure on top of fewer registers.

## 8. Vortex - the one hard firmware (case study)

`processVerb`'s inner loop is a 64‑sample loop interleaving ~21 delay‑line `PEEK`/`POKE`s with a
register‑resident 12‑scalar state bank.

- **Blanket "flush all banked locals at every pointer op" ≈ several‑fold slowdown** (dominated by
  reloading the read‑only tap offsets after every `POKE`).
- But every hot pointer comes from `&verbLine` (global) or `INPp` params - provably can't alias a local
  frame slot. So the **one‑bit provenance tag** (or the `*N` region) skips all those flushes and the state
  stays resident; cost collapses to ~0.
- The committed **v2.0/v2.1 does not** get this on its own (a bank is *aliasable* → floating cache +
  per‑pointer flush; v2.1 bound registers are for *private* slots only). Vortex is the poster child for
  the two refinements: **constant‑offset‑bank classification** (make the region register‑cacheable) +
  **provenance‑scoped flushing** (don't treat a non‑frame pointer op as a barrier). Both are cheap and
  decidable here - offsets are constant, the bank pointer only feeds the `COPY`, the hot pointers are
  manifestly global.

## 9. C++ backend - what it actually needs (corrected)

Delegate almost everything to clang.

- **Minimal (Tier 0):** emit the whole frame as one `Value frame[N]`, locals as `frame[k]`. Correct, zero
  aliasing logic, modest speed (clang can't promote an aliased array).
- **Register win (Tier 1):** emit locals as **bare C++ locals** and `ADRL $x` as `&x`. Clang's
  **mem2reg/SROA does the escape analysis and promotion for free**, including keeping `x` in memory
  exactly when its address escapes (`func(&x)`), and knowing a `mem[]` store can't alias a bare local.
- **The only backend‑side aliasing job: span detection.** A `COPY` / pointer arithmetic across several
  named locals must emit those locals as **one `Value[]` sub‑array** (C++ pointer arithmetic across
  separate objects is UB - clang can't rescue this). That is the same `*N`/region detection, same 4
  programs.

So: **escape/promotion = clang; span/region grouping = backend (Tier 1 only).** The C++ backend needs
*none* of the §5-§6 allocator machinery - it inherits clang's allocator, which is exactly why it serves as
the optimization **ceiling**: if it beats the arm64 output, the gap is our allocator; if they're close,
the gap is our classification.

## 10. Net

- Adopt `*0`/`*N` (revive the size hint); reinstate the C‑object‑model rule via new‑Impala `LOCA` banks;
  keep the GAZL assembler permissive, enforce "no span" in the frontend.
- Ship the simple single‑pass allocator (canonical‑memory joins + provenance‑gated clobbers) first; add
  block-local Belady as a v2.0.5 point release (cheap reverse scan), the global liveness pass with v2.2,
  and splitting/remat as the tail.
- The aliasing spec only needs to be cheap and conservative; a cost‑model allocator prices imprecision
  instead of falling off a cliff.
- Only 4 programs ever exercise the hard case; all stay correct under `*0` meanwhile.
