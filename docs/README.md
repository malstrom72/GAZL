# Documentation index

27 documents accumulated here (28 with this index) with only a handful reachable from the top-level `README.md`, which is how
the same finding ended up recorded in three places and one blocker list went stale for a week. This index
is the fix: every doc, what it is for, and how much to trust it.

**Categories.** NORMATIVE = the rule, follow it. REFERENCE = an inventory, verify before relying on it.
SPEC = the design of something, which may or may not be built. NOTE = a record of investigation, true when
written. PLAN / BACKLOG = not done.

## Start here

| Doc | Kind | What it is |
|---|---|---|
| [WhatsNewInImpala2](WhatsNewInImpala2.md) | REFERENCE | **What changed from 1.0, in one page.** Every sample on it is compiled by `impala/docSamples.js` on each build, so it cannot drift |
| [Overview](Overview.md) | REFERENCE | Architecture and goals; how to embed the VM |
| [Impala](Impala.md) | REFERENCE | The Impala language and toolchain |
| [InstructionSet](InstructionSet.md) | REFERENCE | Extracted opcode descriptions |
| [TwoStageConstants](TwoStageConstants.md) | **NORMATIVE** | Why a constant is not always a number Impala knows. **Read before touching any constant handling, folding, bounds check or diagnostic.** |
| [MemorySafetyModel](MemorySafetyModel.md) | REFERENCE | Frames, what is bounds-checked and when, what `*size` is for |
| [UsageExample](UsageExample.md) | REFERENCE | Compile and run a simple program |

## The symbol and layout scheme

| Doc | Kind | What it is |
|---|---|---|
| [SymbolNamespace](SymbolNamespace.md) | REFERENCE | Every symbol the compiler mints for itself (`.o. .z. .s_ ...`) and how to add one without colliding |
| [StructLayoutConstants](StructLayoutConstants.md) | SPEC | The `.o.` / `.z.` scheme and why layout is a named constant |
| [MultidimensionalArrays](MultidimensionalArrays.md) | SPEC (implemented) | Array SHAPES - `int array cells[H, W]`, `a[y, x]`, the per-axis `.d.` constants, and per-axis bounds checking in all three tiers. Slices 1-2 implemented; slice 3 (shape identity) is 3.0 |
| [ExternPrototypes](ExternPrototypes.md) | REFERENCE | Extern declarations and what is checkable about them |
| [ImpalaTypeCheckingSpec](ImpalaTypeCheckingSpec.md) | SPEC | The type rules |

## The 2.0 design itself

| Doc | Kind | What it is |
|---|---|---|
| [Impala2](Impala2.md) | SPEC (implemented) | The Impala 2.0 design - typed pointers/arrays, structs, typed function pointers, import. Steps 1/2/3/5 implemented; Step 4 parked. 1600 lines, the deepest reference on **why** 2.0 is shaped as it is. For **what** it gained, read [WhatsNewInImpala2](WhatsNewInImpala2.md) first - this one opens with three paragraphs on what is parked. |

## Known problems and backlog

| Doc | Kind | What it is |
|---|---|---|
| [Impala2Review](Impala2Review.md) | NOTE (2026-07-29) | **The home for "silent-wrong shapes" and toolchain gripes.** Section C6 is the canonical list; check it before reporting a compiler bug as new. |
| [CompileTimeHardening](CompileTimeHardening.md) | DESIGN NOTE | "The compiler should have caught that" items, plus the 2026-08-01 scan for guess/refuse/skip sites. Overlaps C6 above - attributions mark which copy is authoritative. |
| [SyntaxConsistency](SyntaxConsistency.md) | AUDIT (2026-07-29) | Every place Impala's C-looking surface does not behave like C, each reproduced against this tree and graded FORCED / ARBITRARY / BUG. The bugs section is FIXED and kept as the record. |
| [PortabilityAudit](PortabilityAudit.md) | NOTE | C++ portability findings |

## Parked and future

| Doc | Kind | What it is |
|---|---|---|
| [ParkedFeatures](ParkedFeatures.md) | **INDEX** | What was built then deliberately removed, which branch holds it, and why. **Start here when you wonder "didn't we already build that?"** |
| [Inlining](Inlining.md) | SPEC | `inline function` - PARKED on this branch, implemented on `GAZL2` |
| [InliningInvestigation](InliningInvestigation.md) | NOTE | Background measurements behind the inlining spec |
| [GAZLSymbolicWindows](GAZLSymbolicWindows.md) | FINDING | GAZL already supports symbolic by-value call windows; Impala's transient allocator is the blocker. Proof: [`symbolicWindows.gazl`](symbolicWindows.gazl) + its re-packed twin |
| [FutureOptimizations](FutureOptimizations.md) | CANDIDATES | Compiler optimisation ideas, not committed to |
| [GAZLAssemblerOptimizations](GAZLAssemblerOptimizations.md) | DESIGN NOTE | Assembler-side optimisation, starting with identity folding |
| [GAZL2FunctionPointers](GAZL2FunctionPointers.md) | PROPOSAL (2026-08-02) | A distinct function-pointer storage type `t`, because `p` doing double duty is a silent-wrong shape Impala cannot close from its side. Nothing implemented. |
| [TailCalls](TailCalls.md) | DESIGN NOTE | Tail-call elimination. Nothing implemented; needs a GAZL instruction *and* Impala syntax, so neither side can do it alone. |

The last two belong to the **GAZL 2** line, which ships after Impala 2.0 and is independent of it -
nothing in Impala 2.0 waits on either.

## Toolchain internals

| Doc | Kind | What it is |
|---|---|---|
| [JSPEGFuture](JSPEGFuture.md) | PLAN | Where the JSPEG compiler-compiler goes next |
| [CodingStyle](CodingStyle.md) | NORMATIVE | House style for this repo |

## Runnable proofs

Hand-written GAZL kept beside the doc it proves, so a claim in prose can always be executed. They are
hand-written because they demonstrate things the compiler cannot emit yet, which is also why they are the
claims a reader is least likely to re-derive. **`tools/checkDocProofs.js` runs them on every build**
(`tools/test-js.sh`), asserting the outputs their own headers document - added 2026-08-05, because until
then nothing ran them and a proof nobody runs is a claim that rots silently:

- [`symbolicWindows.gazl`](symbolicWindows.gazl) / [`symbolicWindowsRepacked.gazl`](symbolicWindowsRepacked.gazl) - a fully symbolic by-value call window; re-packing the layout header changes no instruction
- [`deferredShapeCheck.gazl`](deferredShapeCheck.gazl) - array shape identity decided at assembly with `! EQUi` / `! FAIL`
- [`nativeCallbackSignatures.gazl`](nativeCallbackSignatures.gazl) - native callback signature shapes. A
  MANIFEST, not a program: it has no `main`, is cited as a reference by three docs, and is the one file
  here `checkDocProofs` does not run, because there is nothing to run

## The rule that keeps this from rotting

One fact, one home. If something belongs in two docs, put it in the one that owns the subject and
**link** from the other - a copied paragraph is a paragraph that will disagree with itself later. Both
stale-blocker incidents this repo has had came from copies, not from missing information.
