# Design and internals

Notes for anyone - human or agent - working ON GAZL, Impala or the JSPEG toolchain. For documentation on
USING them, see [`docs/`](../docs/README.md).

The split is by audience, not by topic: if a reader will never modify the compiler or the VM, the page
belongs in `docs/`. Everything else - design notes, audits, proposals, backlogs, and the runnable proofs
behind claims made in prose - belongs here. Subfolders mirror `docs/`: `gazl/`, `impala/`, `jspeg/`.

These accumulated with only a handful reachable from the top-level `README.md`, which is how the same
finding ended up recorded in three places and one blocker list went stale for a week. This index is the
fix: every doc, what it is for, and how much to trust it.

**Categories.** NORMATIVE = the rule, follow it. REFERENCE = an inventory, verify before relying on it.
SPEC = the design of something, which may or may not be built. NOTE = a record of investigation, true when
written. PLAN / BACKLOG = not done.

## Read before you change anything

| Doc | Kind | What it is |
|---|---|---|
| [TwoStageConstants](impala/TwoStageConstants.md) | **NORMATIVE** | Why a constant is not always a number Impala knows. **Read before touching any constant handling, folding, bounds check or diagnostic.** |
| [CodingStyle](CodingStyle.md) | **NORMATIVE** | House style for this repo |
| [ParkedFeatures](ParkedFeatures.md) | **INDEX** | What was built then deliberately removed, which tag holds it, and why. **Start here when you wonder "didn't we already build that?"** |

## Known problems and backlog

| Doc | Kind | What it is |
|---|---|---|
| [Impala2Review](impala/Impala2Review.md) | NOTE | **The home for "silent-wrong shapes" and toolchain gripes.** Section C6 is the canonical list; check it before reporting a compiler bug as new |
| [CompileTimeHardening](impala/CompileTimeHardening.md) | DESIGN NOTE | "The compiler should have caught that" items, plus the 2026-08-01 scan for guess/refuse/skip sites. Overlaps C6 above - attributions mark which copy is authoritative |
| [SyntaxConsistency](impala/SyntaxConsistency.md) | AUDIT | Every place Impala's C-looking surface does not behave like C, each reproduced against this tree and graded FORCED / ARBITRARY / BUG. The bugs section is FIXED and kept as the record |
| [PortabilityAudit](gazl/PortabilityAudit.md) | NOTE | C++ portability findings |
| [SymbolNamespace](gazl/SymbolNamespace.md) | REFERENCE | Every symbol the compiler mints for itself, and how to add one without colliding |
| [GAZLSymbolicWindows](gazl/GAZLSymbolicWindows.md) | FINDING | GAZL already supports symbolic by-value call windows |
| [GAZLAssemblerOptimizations](gazl/GAZLAssemblerOptimizations.md) | DESIGN NOTE | Assembler-side optimisation |
| [GAZL2FunctionPointers](gazl/GAZL2FunctionPointers.md) | PROPOSAL | A distinct function-pointer storage type `t` |
| [TailCalls](gazl/TailCalls.md) | DESIGN NOTE | Tail-call elimination. Nothing implemented |

## The JIT

This branch's own work: the native backend. Handoffs, research and plans, roughly newest-relevant first.
Nothing here is end-user documentation - the JIT is an implementation detail of running GAZL.

| Doc | Kind | What it is |
|---|---|---|
| [JitTechnologyMap](jit/JitTechnologyMap.md) | REFERENCE | The lay of the land - start here |
| [JitCompilerResearch](jit/JitCompilerResearch.md) | NOTE | Background research behind the design |
| [CppBackendSpec](jit/CppBackendSpec.md) | SPEC | The C++ backend |
| [LeafNativeDesign](jit/LeafNativeDesign.md) | SPEC | Leaf native calls |
| [SliceBoundsDesign](jit/SliceBoundsDesign.md) | SPEC | Slice bounds |
| [JitAliasingRegAlloc](jit/JitAliasingRegAlloc.md) | DESIGN NOTE | Aliasing and register allocation |
| [JitCrossBlockResidencyPlan](jit/JitCrossBlockResidencyPlan.md) | PLAN | Cross-block register residency |
| [JitRealmStampingPlan](jit/JitRealmStampingPlan.md) | PLAN | Realm stamping |
| [JitSimplifyPlan](jit/JitSimplifyPlan.md) | PLAN | Simplification tiers |
| [JitFuzzPlan](jit/JitFuzzPlan.md) / [JitBenchmarkPlan](jit/JitBenchmarkPlan.md) | PLAN | Fuzzing and benchmarking |
| [FrameCeilingResearch](jit/FrameCeilingResearch.md) / [JitInvestigations](jit/JitInvestigations.md) / [JitSpikeA1-Results](jit/JitSpikeA1-Results.md) / [JitSessionBenchMatrix](jit/JitSessionBenchMatrix.md) | NOTE | Investigations and measurements |
| [JitEmitterHandoff](jit/JitEmitterHandoff.md) / [JitMemoryConsolidationHandoff](jit/JitMemoryConsolidationHandoff.md) / [JitWindowsHandoff](jit/JitWindowsHandoff.md) / [JitSessionHandoff-2026-07-19](jit/JitSessionHandoff-2026-07-19.md) | HANDOFF | Session handoffs, true when written |

## Symbols, layout and types

| Doc | Kind | What it is |
|---|---|---|
| [SymbolNamespace](gazl/SymbolNamespace.md) | REFERENCE | Every symbol the compiler mints for itself (`.o. .z. .s_ ...`) and how to add one without colliding |
| [StructLayoutConstants](impala/StructLayoutConstants.md) | SPEC | The `.o.` / `.z.` scheme and why layout is a named constant |
| [ExternPrototypes](impala/ExternPrototypes.md) | REFERENCE | Extern declarations and what is checkable about them |
| [ImpalaTypeCheckingSpec](impala/ImpalaTypeCheckingSpec.md) | SPEC | The type rules |
| [Impala2Slices](impala/Impala2Slices.md) | PLAN | How the 2.0 work was cut into slices, with each one's landing state |

## Parked and future

| Doc | Kind | What it is |
|---|---|---|
| [Inlining](impala/Inlining.md) | SPEC | `inline function` - PARKED on this branch, implemented on `GAZL2` |
| [InliningInvestigation](impala/InliningInvestigation.md) | NOTE | Background measurements behind the inlining spec |
| [GAZLSymbolicWindows](gazl/GAZLSymbolicWindows.md) | FINDING | GAZL already supports symbolic by-value call windows; Impala's transient allocator is the blocker. Proof: [`symbolicWindows.gazl`](proofs/symbolicWindows.gazl) + its re-packed twin |
| [FutureOptimizations](FutureOptimizations.md) | CANDIDATES | Compiler optimisation ideas, not committed to |
| [GAZLAssemblerOptimizations](gazl/GAZLAssemblerOptimizations.md) | DESIGN NOTE | Assembler-side optimisation, starting with identity folding |
| [GAZL2FunctionPointers](gazl/GAZL2FunctionPointers.md) | PROPOSAL | A distinct function-pointer storage type `t`, because `p` doing double duty is a silent-wrong shape Impala cannot close from its side. Nothing implemented |
| [TailCalls](gazl/TailCalls.md) | DESIGN NOTE | Tail-call elimination. Nothing implemented; needs a GAZL instruction *and* Impala syntax, so neither side can do it alone |

The last two belong to the **GAZL 2** line, which ships after Impala 2.0 and is independent of it -
nothing in Impala 2.0 waits on either.

## The JSPEG toolchain

| Doc | Kind | What it is |
|---|---|---|
| [JSPEG](jspeg/JSPEG.md) | REFERENCE | The JSPEG grammar language, and the status of the JavaScript PEG compiler |
| [ImpalaJS](jspeg/ImpalaJS.md) | REFERENCE | The generated JavaScript compiler and how it is driven |
| [JSPEGFuture](jspeg/JSPEGFuture.md) | PLAN | Where the JSPEG compiler-compiler goes next |
| [RefactorPlan](jspeg/RefactorPlan.md) | PLAN | Return-style JSPEG helpers; LIVE, not started |

## Runnable proofs

Hand-written GAZL under [`proofs/`](proofs), so a claim in prose can always be executed. They are
hand-written because they demonstrate things the compiler cannot emit yet, which is also why they are the
claims a reader is least likely to re-derive. **`tools/checkDocProofs.js` runs them on every build**
(`tools/test-js.sh`), asserting the outputs their own headers document - added 2026-08-05, because until
then nothing ran them and a proof nobody runs is a claim that rots silently:

- [`symbolicWindows.gazl`](proofs/symbolicWindows.gazl) / [`symbolicWindowsRepacked.gazl`](proofs/symbolicWindowsRepacked.gazl) - a fully symbolic by-value call window; re-packing the layout header changes no instruction
- [`deferredShapeCheck.gazl`](proofs/deferredShapeCheck.gazl) - array shape identity decided at assembly with `! EQUi` / `! FAIL`

The host's native table used to live here as `nativeCallbackSignatures.gazl`, a `; signature`-only
manifest that existed for `gazl-validate` to compare against. Both retired 2026-08-05: it is
`impala/natives.impala` now, so every call is checked by the compiler at the call site.

## The rule that keeps this from rotting

One fact, one home. If something belongs in two docs, put it in the one that owns the subject and
**link** from the other - a copied paragraph is a paragraph that will disagree with itself later. Both
stale-blocker incidents this repo has had came from copies, not from missing information.
