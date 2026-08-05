# Handoff: consolidate `jit-roadmap-status` out of agent memory and into this branch

Status: TASK, open. Written 2026-08-01 from the `Impala2` branch, where the problem was noticed but
cannot be fixed - the material is JIT work and belongs here.

## The problem

The project's agent memory store at `~/.claude/projects/-Users-magnus-git-GAZL/memory/` holds one file,
`jit-roadmap-status.md`, that is **45.7 KB - 64% of the entire store** (71 KB across 14 files). Every other
memory is 0.8-6 KB. That store is designed for one fact per file, loaded into context every session; this
one is a full design document living in it.

Consequences already observed:

- Its `MEMORY.md` index line had drifted to describe an older state ("C2 + C3-minimal built; blocked on the
  read-only-VM choice") than the file itself ("v1 shipped on arm64 **and** x64; v2 staging locked"). Fixed
  2026-08-01, but the drift is a symptom - a 45 KB file is not reviewable at a glance, so it rots quietly.
- It is structured as **5 paragraphs**, i.e. walls of text, which is why nobody re-reads it.

## What was measured before handing this over

Do not redo this; do verify anything you intend to rely on.

| Measurement | Result |
|---|---|
| Size | 45,718 bytes, 5 paragraphs |
| Commit hashes referenced | 36 (one, `9227465`, is a fib(35) result, not a hash) |
| Of those, **real commits** resolvable in this repo | **23 of the remaining 35** |
| Of those, hashes also appearing in this branch's `docs/` | **12** |
| Distinct backticked identifiers | 60 |

**The core finding: most of the file is a narrative over history that `git log` already holds.** 23 of its
commit references resolve as real commits, so the hashes and their subjects are recoverable at any time.
What is *not* recoverable is the connective reasoning - why a milestone was staged where it was, what a
measurement meant, which approach was rejected. That is the part worth keeping, and it is the part a
consolidation must not lose.

Spot-checked and confirmed accurate, so the file is trustworthy, just misfiled:

- tag `last-before-register-allocation` resolves to `a6e2761`, exactly as the memory claims
- `0cfe34a` is v2.3a-full realm stamping; `114fb44` is v2.2 varying maps - both as described

## Two things that exist ONLY in that memory

Checked against all `docs/Jit*.md` on this branch. Everything else I probed (`v2.1`, `§5.7`, `RESUME`,
the API description) has a home here already.

1. **The tag `last-before-register-allocation` is named nowhere in `docs/`.** It is the v1 boundary marker.
2. **The 2026-07 renumbering rationale is nowhere in `docs/`**, verbatim from the memory:

   > the true axis is *which* flush you remove. v2.1/v2.2 remove the CONTROL-FLOW (block-boundary) flush =
   > **aliasing-FREE**; v2.3 removes the HAZARD-op flush = **needs the aliasing spec**. The old v2.1 "bound
   > lines needs `*N`" bundled both - unbundled here; its aliasing part folded into v2.3. Key realization:
   > the block-boundary clear is the expensive part (per §5.7's own measurement - conservative flushing
   > costs only ~10-20% over provenance), so the big win is aliasing-free.

   That is a design decision with a measurement behind it. Losing it means re-deriving the staging.

## The task

1. Diff `jit-roadmap-status.md` against the fourteen `docs/Jit*.md` on this branch. Most of it should land
   in `JitTechnologyMap.md` (staging/roadmap) or `JitCompilerResearch.md` (the 149 KB reference).
2. Fold the two orphans above into whichever doc owns the subject, and drop hash-by-hash history that
   `git log --oneline` gives for free.
3. Reduce the memory file to what a fresh session genuinely needs and cannot derive: current branch and
   state in one or two sentences, the v1/v2 boundary, and pointers to the docs. Target the 1-2 KB the other
   project memories occupy.
4. Update its `MEMORY.md` index line to match whatever it then says, and drop the
   "NEEDS CONSOLIDATING" note that flags this task.

## Constraints

- **Do not delete before the content has a home.** Four JIT design docs were rescued on 2026-08-01 from an
  untracked `temp/` where no branch held them (`JitCrossBlockResidencyPlan`, `JitRealmStampingPlan`,
  `JitSessionBenchMatrix`, `JitSessionHandoff-2026-07-19`). `/temp` and `/spike` are gitignored as of that
  date, so anything written there now is genuinely scratch.
- The memory was authored in a different session (`originSessionId f07e71ca-...`). Treat it as someone
  else's notes: preserve reasoning you do not fully follow rather than compressing it away.
- Repo conventions apply: short one-line commit subjects, no `Co-Authored-By` trailer, no em/en dashes.
  Commit and push only when Magnus says so.

## Acceptance

- `wc -c` on the memory file is in the 1-2 KB range and it reads as one fact plus pointers.
- Every claim removed from it is either in a `docs/Jit*.md` on this branch, or recoverable from `git log`.
- The two orphans above are in `docs/` and findable by name.
- `MEMORY.md`'s line for it is accurate against the file it points at.

## Why this is worth doing rather than tolerating

The 45 KB is loaded into context every session on this project, including sessions doing no JIT work at
all - which is where it was noticed. And the same failure mode has already cost real time on the Impala
side of the repo: a blocker list in `design/ParkedFeatures.md` stayed stale for a week and sent a research
pass down a dead end, because a fact had two homes and only one was updated. See the rule at the bottom of
`design/README.md` on `Impala2`: one fact, one home, link don't copy.
