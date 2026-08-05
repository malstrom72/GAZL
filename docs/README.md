# Documentation

End-user documentation for GAZL, Impala and the C++ embedding API. If you are working ON the compiler or
the VM rather than WITH them, you want [`design/`](../design/README.md) instead - design notes, audits,
proposals and backlogs live there, and nothing in this folder assumes you have read them.

**Categories.** REFERENCE = an inventory, verify before relying on it. SPEC = the design of something,
which may or may not be built.

## Start here

| Doc | Kind | What it is |
|---|---|---|
| [WhatsNewInImpala2](impala/WhatsNewInImpala2.md) | REFERENCE | **What changed from 1.0, in one page.** Every sample on it is compiled by `impala/docSamples.js` on each build, so it cannot drift |
| [Overview](Overview.md) | REFERENCE | Architecture and goals; how the VM works and **how to embed it from C++** |
| [UsageExample](impala/UsageExample.md) | REFERENCE | Compile and run a simple program |

## GAZL

| Doc | Kind | What it is |
|---|---|---|
| [InstructionSet](gazl/InstructionSet.md) | REFERENCE | Extracted opcode descriptions |

The C++ embedding API is currently a section of [Overview](Overview.md) rather than its own page;
extracting it to `gazl/Embedding.md` is planned.

## Impala

| Doc | Kind | What it is |
|---|---|---|
| [Impala](impala/Impala.md) | REFERENCE | The Impala language and toolchain |
| [Impala2](impala/Impala2.md) | SPEC (implemented) | The Impala 2.0 design - typed pointers/arrays, structs, typed function pointers, import. Steps 1/2/3/5 implemented; Step 4 parked. 1600 lines, the deepest reference on **why** 2.0 is shaped as it is. For **what** it gained, read [WhatsNewInImpala2](impala/WhatsNewInImpala2.md) first - this one opens with three paragraphs on what is parked |
| [MultidimensionalArrays](impala/MultidimensionalArrays.md) | SPEC (implemented) | Array SHAPES - `int array cells[H, W]`, `a[y, x]`, the per-axis `.d.` constants, and per-axis bounds checking in all three tiers. Slices 1-2 implemented; slice 3 (shape identity) is 3.0 |
| [MemorySafetyModel](impala/MemorySafetyModel.md) | REFERENCE | Frames, what is bounds-checked and when, what `*size` is for |

`Impala2.md` and `MultidimensionalArrays.md` are still separate pages; folding them into `Impala.md` so
there is one language reference is outstanding work.

## The rule that keeps this from rotting

One fact, one home. If something belongs in two docs, put it in the one that owns the subject and
**link** from the other - a copied paragraph is a paragraph that will disagree with itself later. Both
stale-blocker incidents this repo has had came from copies, not from missing information.
