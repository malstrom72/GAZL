# v2.3a-full: pointer realm stamping + realm-scoped flushing (working plan, 2026-07-19)

> **Historical working plan**, moved here 2026-08-01 from an untracked `temp/` directory where it was
> one `rm -rf` from being lost. The work it plans has SHIPPED as `0cfe34a` (v2.3a-full pointer-realm
> stamping). Kept for the reasoning, not as a to-do list.

Spec: docs/JitCompilerResearch.md section 1.1 "Memory-realm rule" (normative, adopted). Prior art in tree:
v2.3a-lite (commit 9ce8d69) already skips flushes for CONST-address indexed PEEK/POKE (const base = globals/constants
realm). This work extends the same soundness argument to RUNTIME pointers whose provenance traces to a constant.

## Structural facts (verified against the code)

- GAZL pointers are plain Ints in ordinary value slots (word index + MEMORY_OFFSET). PEEK_VVV's base (p1) and
  POKE_VVV/VVC's base (p0) are frame slots holding pointer values. There are no dedicated MOVp/ADDp opcodes at the
  finalized level - pointer arithmetic is ADDI/SUBI/MOVE on the slot.
- The register cache holds FRAME slots only, so the only question that matters per memory op is: can this pointer
  reach MY frame? If provably not, the spillDirtyResident()/invalidateAll() around it can be skipped.
- ADRL computes dsp-relative -> a MY-FRAME pointer. A CONSTANT operand can never be a my-frame address (frames are
  dynamic; the assembler cannot produce a static dsp-relative address). Params/memory-loads/call-results can carry
  anything (incl. caller-transient addresses that physically overlap MY params region) -> Unknown.
- Real firmware idioms (verber8): `ADRL %0 $params *0; COPY %0 &params *N` (bank copy, frame dest <- globals src);
  `COPY &taps %0 *N` (globals dest <- frame src); `PEEK $x &clock` (const scalar, already lite-covered);
  delay-line pointers = global symbol constants walked with ADDI in per-sample loops (the Vortex &verbLine case).

## Lattice + analysis

Per-function forward dataflow over frame slots, arch-neutral (GAZLJit.cpp, alongside buildUseSchedule):

    realm(slot) in { BOTTOM, NONFRAME, MYFRAME, UNKNOWN }   // NONFRAME = globals|constants|foreign frame

Transfer:
- MOVE_VC / any constant write -> NONFRAME
- ADRL -> MYFRAME
- MOVE_VV -> copy operand realm
- ADDI/SUBI (any V form) -> join of slot-operand realms (const operand joins as NONFRAME; NONFRAME+int stays
  NONFRAME, which is what keeps a walked delay pointer clean)
- PEEK*/GETL (any load), CALL/native results, FUNC entry (params): -> UNKNOWN
- everything else writing a slot -> UNKNOWN (conservative default; refine op-by-op later)

Joins at block leaders; iterate to fixed point (lattice height 2, cheap). Loop-carried pointers (init NONFRAME
before the loop, ADDI on the back edge) resolve to NONFRAME at the head - required for the per-sample loops.
Function entry: all slots UNKNOWN.

## Consumption in the backends

At each runtime-pointer op, query realm of the base slot:
- PEEK_VVV base NONFRAME: skip spillDirtyResident() (frame lines cannot be observed through it)
- POKE_VVV/VVC base NONFRAME: skip spill AND invalidateAll()
- COPY: scope by side; dest NONFRAME -> no invalidate; src NONFRAME contributes no spill requirement beyond the
  dest's. Frame-side (via ADRL, MYFRAME) keeps today's behavior.
- MYFRAME/UNKNOWN: unchanged (flush all). Sub-realm precision (params/locals/transients split) is v2.3b territory;
  do NOT attempt the params-boundary derivation in this cut (caller-transient overlap makes param-received pointers
  reach MY params - so param pointers stay UNKNOWN, full flush).
- GETL/SETL: MYFRAME by construction, unchanged.

Trap arms: these ops are checked; keep/extend the ColdTrap captureDirtyLines pattern (capture at branch point) -
skipping the flush makes the snapshot the ONLY store path on the trap exit, same soundness argument as ea068ba.

## Phase 0 (spec-mandated, BEFORE the flushing change ships)

1. Golden .gazl tests (new, e.g. tests/ or benchmarks/firmware lane): bank COPY, by-ref out-param, passed array -
   each asserting interp==JIT today AND documenting the realm each access stays inside (comment-level proof; the
   idioms above from verber8 are the templates).
2. Differential fuzzer: add a generator stage emitting deliberately CROSS-REALM pointer walks (ADRL then walk out of
   the frame; global pointer walked into the data stack). Expect STATUS match; affected memory compared per the
   K_MEMORY crossRealmInput precedent (unspecified values excluded). Run against CURRENT jit first to catalog
   behavior, then against realm-flushing to confirm divergences only where the spec says unspecified.

## Validation gates (after implementation)

- Lockstep lower test all 3 platforms; slice tests; 27 firmwares both engines.
- 100k+ deep fuzz per backend INCLUDING the new cross-realm stage.
- A/B min-of-3 fresh processes: native arm64 + SILICON x64, suite + perfTests + all firmwares. Expect wins on
  delay-line firmwares (verber8, specular, multitap, mozaik) and sieve-class kernels; watch the usual layout noise.

## Open questions for Magnus

- Where should the Phase 0 golden tests live (new tests/realm/ lane vs benchmarks/firmware extra checksums)?
- Is COPY's dest-side invalidate scoping worth doing in cut 1, or PEEK/POKE only first?
