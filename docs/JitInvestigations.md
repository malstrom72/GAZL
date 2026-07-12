# JIT — open investigations

Running list of design investigations for the GAZL native compiler that are deferred, not yet resolved, or need a
measurement/prototype pass. Higher‑level design lives in [JitCompilerResearch.md](JitCompilerResearch.md); this file is
the scratch backlog for "come back and dig into this." (Branch `jit-compiler`.)

---

## 1. Impala structs, `LOCA`, and the aliasing/caching region model

**Why this matters.** The bulk global↔local `COPY` idiom (a contiguous run of locals bulk‑copied from/to a global
state struct, then worked on register‑style) is the single thing that forces the coarse *escape floor* and blocks the
clean "distinct named locals don't alias" rule (research doc §1.1, §5.7.2). We want a form that gives, all at once:
(a) direct‑access opcodes on each field (fast, like a named scalar today — not `GETL`/`SETL`), (b) the fast bulk copy,
(c) an **explicit contiguous region** the JIT can treat precisely (so only `[base, base+N)` is aliasable, everything
else private — no invented directional span).

**The plan of record was** a per‑`ADRL` extent (`ADRL base *N`, reviving the today‑dead `*size`) plus, in Impala, a
`struct` that lowers to *contiguous named scalar slots*. Magnus's note: **Impala structs would more likely lower to a
`LOCA`** (an array), not to individual named scalars.

**Why LOCA might be *better*, not worse — the thing to confirm.** A constant offset into a local array is already a
**direct operand** in GAZL (`localArray:3` is a valid `int`/`float`/`ptr` operand — Overview.md operand table), i.e. it
compiles to a normal direct‑access opcode, *not* `GETL`/`SETL` (those are only for **dynamic** indices). So a struct
lowered to `LOCA *N` with fields accessed as `s:0`, `s:1`, … would:
- keep direct‑access opcodes per field (need to **confirm** Impala emits `s:k` constant offsets, not dynamic `GETL`),
- make the region **explicitly the `LOCA` extent** — no `ADRL *N` needed, no directional ambiguity, the size is declared,
- and the whole‑struct copy is just `ADRL &s *0`/`COPY *N` bounded by the known `LOCA` size.

That is arguably cleaner than the named‑scalar‑bank + `ADRL *N` route: the region is a first‑class declaration.

**The catch to resolve.** The current rule says *arrays are never register‑cached* — they're the backing store
(research doc §5.7.1 rule 6). A struct‑as‑`LOCA` would fall under that and stay memory‑resident, which is exactly the
pessimization we're trying to remove (perfTest's bank is the hot working set; we want its fields in registers between
the `COPY` barriers). So the rule needs refining:

> **Refinement to investigate:** split "`LOCA`" into two classes.
> - *Dynamic array* — ever `GETL`/`SETL`'d with a runtime index, or `ADRL`'d and the pointer escapes past a local
>   barrier → memory‑resident, as today.
> - *Constant‑offset bank* — accessed only at **constant** offsets (`s:k`) plus whole‑region bulk `COPY`, no dynamic
>   index, no escaping pointer → its elements are register‑cacheable **exactly like named scalars**, keyed by constant
>   offset, with the `ADRL`+`COPY` sites as the flush/reload barriers (v2.0 cache model already does this for scalars).
> Then a struct‑`LOCA` is a *bank*: direct‑opcode fields cached in registers, one explicit region, no floor, no
> declaration‑order sensitivity.

**Sub‑questions for the pass:**
1. Confirm Impala's struct lowering: does `s.field` become a constant‑offset operand (`s:k`, direct opcode) or a
   dynamic `GETL`/`SETL`? If the latter, that's the real problem to fix, and it's an Impala codegen question, not a
   JIT one.
2. Does GAZL's assembler/type system already treat `LOCA`‑at‑constant‑offset identically to a named scalar for operand
   validation (bounds against the `LOCA` size)? Spot‑check the operand grammar and an example.
3. Define the "bank vs dynamic array" classifier precisely and cheaply (one pass): a `LOCA` is a *bank* iff it is never
   the base of a `GETL`/`SETL` and its `ADRL` (if any) is pristine (consumed only by constant‑count `COPY`, pointer not
   otherwise escaping). Fold into `benchmarks/jit/JitEscapeAnalysis.cpp` and re‑measure the corpus: how many hot
   functions become fully bank‑cacheable under this rule vs the escape floor?
4. Whole‑struct assignment (`s = gState;` / `gState = s;`) codegen: `ADRL`+`COPY` today; does the JIT recover the region
   from the `LOCA` size (preferred) or does it still want `ADRL *N`? If the `LOCA` size suffices, `ADRL *size` can stay
   dead and the earlier "revive `*N`" idea is unnecessary.
5. Freeze interaction: a struct in globals is contiguous words; freeze is by‑name (research doc §5.6 note / `GAZL.h`).
   Does a struct global need a single aggregate name or per‑field names? Confirm round‑trip.
6. Does this subsume the named‑scalar bank entirely (i.e. can Impala emit *all* banks as `LOCA` structs), retiring the
   "contiguous named scalars spanned by an over‑long `COPY`" idiom for recompiled firmwares? Old firmwares keep the old
   idiom + v2.0; new ones get precise regions for free.

**If it pans out:** the aliasing story simplifies a lot — the escape *floor* (coarse, directional, declaration‑order
sensitive) is replaced by *explicit declared regions* (`LOCA` bank extents), the invented up/down span disappears, and
v2.1 bound‑register eligibility improves in exactly the `process()` functions the measurement flagged. Update §1.1 /
§5.7.1–5.7.2 accordingly.

---

## Other open investigations (tracked elsewhere, listed here for the index)

- **Spike A2 — ARM64 cross‑thread code publication** (compile on loader thread, first execute on audio thread):
  **design‑resolved** via ARM's authoritative protocol — writer does broadcast `dc cvau`/`ic ivau`/`dsb`
  (`sys_icache_invalidate` / `FlushInstructionCache`), and the **reader does one `isb` at the dispatcher entry** after
  the acquire‑load of the "ready" flag. GAZL's immutable‑after‑publish code keeps us in the easy publish‑then‑execute
  regime (no concurrent modification). Remaining: *confirm* on real M‑series + Windows‑ARM with a deliberately‑broken
  variant to prove the test bites. Research doc §6.2#2, §12 risk #1. (A1 → GO; A3 → GO.)
- **Liftoff‑style cross‑block reconciliation** is now **committed** as v2.2 (the general varying‑map form; bound lines
  are its fixed‑map instance), not parked. Research doc §5.7.6. (No longer an open investigation.)
- **Front‑end reordering** — Impala declares arrays / address‑taken slots *last* so the escape floor lands above the
  scalars. Cheap, complementary; only helps recompiled firmwares. §5.7.6.
- **Provenance‑scoped flushing (v2.2)** — taint frame‑born pointers so global/param pointer ops skip the flush; ~10–20%,
  x64‑gated. §5.7.3.
- **PortabilityAudit interpreter fixes** — `INT_MIN/-1`, `FTOI` saturation, shift/overflow UB; independent of the JIT
  but Phase‑0 prerequisites for a defined oracle. [PortabilityAudit.md](PortabilityAudit.md).
