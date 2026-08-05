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

## Impala

| Doc | Kind | What it is |
|---|---|---|
| [ImpalaTypeCheckingSpec](impala/ImpalaTypeCheckingSpec.md) | SPEC | The type rules |

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
