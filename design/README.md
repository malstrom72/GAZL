# Design and internals

Notes for anyone - human or agent - working ON GAZL, Impala or the JSPEG toolchain. For documentation on
USING them, see [`docs/`](../docs).

The split is by audience, not by topic: if a reader will never modify the compiler or the VM, the page
belongs in `docs/`. Everything else - design notes, audits, proposals, backlogs, and the runnable proofs
behind claims made in prose - belongs here. Subfolders mirror `docs/`: `gazl/`, `impala/`, `jspeg/`.

**Categories.** NORMATIVE = the rule, follow it. REFERENCE = an inventory, verify before relying on it.
SPEC = the design of something, which may or may not be built. NOTE = a record of investigation, true when
written. PLAN / BACKLOG = not done.

## Read before you change anything

| Doc | Kind | What it is |
|---|---|---|
| [CodingStyle](CodingStyle.md) | **NORMATIVE** | House style for this repo |

## GAZL

| Doc | Kind | What it is |
|---|---|---|
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

## Impala

| Doc | Kind | What it is |
|---|---|---|
| [ImpalaTypeCheckingSpec](impala/ImpalaTypeCheckingSpec.md) | SPEC | The type rules |
| [TwoStageConstants](impala/TwoStageConstants.md) | **NORMATIVE** | Why a constant is not always a number Impala knows |
| [Impala2Review](impala/Impala2Review.md) | NOTE | Silent-wrong shapes and toolchain gripes |
| [CompileTimeHardening](impala/CompileTimeHardening.md) | DESIGN NOTE | "The compiler should have caught that" items |
| [StructLayoutConstants](impala/StructLayoutConstants.md) | SPEC | The `.o.` / `.z.` scheme |
| [SyntaxConsistency](impala/SyntaxConsistency.md) | AUDIT | Where Impala's C-looking surface does not behave like C |
| [ExternPrototypes](impala/ExternPrototypes.md) | REFERENCE | Extern declarations and what is checkable |
| [Inlining](impala/Inlining.md) / [InliningInvestigation](impala/InliningInvestigation.md) | SPEC / NOTE | `inline function`, live on this branch via GAZL2 |
| [Impala2Slices](impala/Impala2Slices.md) | PLAN | How the 2.0 work was cut into slices |

## The JSPEG toolchain

| Doc | Kind | What it is |
|---|---|---|
| [JSPEG](jspeg/JSPEG.md) | REFERENCE | The JSPEG grammar language, and the status of the JavaScript PEG compiler |
| [ImpalaJS](jspeg/ImpalaJS.md) | REFERENCE | The generated JavaScript compiler and how it is driven |
| [RefactorPlan](jspeg/RefactorPlan.md) | PLAN | Return-style JSPEG helpers |
| [NodeRemovalPlan](jspeg/NodeRemovalPlan.md) | PLAN | Dropping the Node dependency from the toolchain |
| [jspeg-dollar-report](jspeg/jspeg-dollar-report.md) | NOTE | How `$` forms lower in generated parsers |

## Runnable proofs

Hand-written GAZL under [`proofs/`](proofs), so a claim in prose can always be executed. They are
hand-written because they demonstrate things the compiler cannot emit yet, which is also why they are the
claims a reader is least likely to re-derive.

- [`nativeCallbackSignatures.gazl`](proofs/nativeCallbackSignatures.gazl) - native callback signature
  shapes. A MANIFEST, not a program: it has no `main`, and it is `gazl-validate`'s default native manifest

## The rule that keeps this from rotting

One fact, one home. If something belongs in two docs, put it in the one that owns the subject and
**link** from the other - a copied paragraph is a paragraph that will disagree with itself later.
