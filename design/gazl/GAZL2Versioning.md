# GAZL 2 versioning: the GAZL directive and Impala's --gazl option

Status: PROPOSAL, not implemented. Direction decided 2026-08-24: Impala is distributed WITH the products
that need it, and the product picks the dialect - Permut8 stays on GAZL 1 for compatibility with the
players already in the field; Synplant 2 and Microtonic 4, where GAZL appears for the first time,
default to GAZL 2.


## What exists today

Three engine generations matter:

- **Shipped GAZL 1** (the Permut8 in the field): predates `GAZL_VERSION`. Assumed - NOT verifiable from
  this repository - to reject an unknown mnemonic and an unknown `!` directive; verify against the real
  Permut8 source before relying on its exact failure mode.
- **Current GAZL 1** (`main` / `Impala2`, `VERSION = 1`): defines the assemble-time constant
  `GAZL_VERSION` (`src/GAZL.cpp:753` on `Impala2`).
- **GAZL 2** (this branch, `VERSION = 2`): `SCOP`/`ENDS` landed, `SEEK` proposed
  ([`GAZL2DataRegions.md`](GAZL2DataRegions.md)). The jit-compiler branch carries the engine-side
  compile switch for the same split: `GAZL_LOCAL_SCOPES=0` builds a 1.0-compatible engine
  (`gazl-jit src/GAZL.h:78`).

All of that is engine-side. A SOURCE can adapt to the engine (`! IFDF #GAZL_VERSION` then
`! GEQi #GAZL_VERSION #2 @skip` - the `docs/gazl/InstructionSet.md` idiom, used by `UnitTest.gazl`) but
it cannot DECLARE what it is. Today a GAZL 2 file on a GAZL 1 engine dies at the first v2 mnemonic with
`Invalid mnemonic: SCOP` - true, late, and unhelpful.


## The declaration: `GAZL #2`

First mnemonic line of the file, at most once, before any section or function:

    GAZL #2

- **Absence means GAZL 1.** Every existing source lacks the line; nothing in the field changes meaning.
- On an engine too old to know the directive: `Invalid mnemonic` on line 1 - the failure moves to the
  front of the file and points at the version line, which is as close to a version diagnostic as an
  unpatchable engine can give.
- On a v2 assembler: declares the source's dialect. A declared version above the engine's `VERSION` is
  an error with a real message.
- **Additive mnemonics are NOT gated by the declaration** (decided 2026-08-24). `SCOP`, `ENDS` and
  `SEEK` are self-gating: an engine that lacks them rejects them as unknown mnemonics anyway, so
  requiring a declaration for them adds friction and no safety - and the `--gazl 1` byte-compare corpus
  already proves that mode's output clean of them. What the declaration exists for is changes that
  alter the meaning of EXISTING forms - the `t` split is the case in point, where an undeclared file
  must keep GAZL 1's `p`-union reading if the gentler (declaration-gated) migration is chosen over the
  decided hard break.

Considered and rejected: `VERS #2` (says less than `GAZL #2` as a file's first word), and a `!`
compile-time spelling (no gain - an old engine rejects an unknown `!` directive just as loudly, and the
declaration is a property of the file, not a computation).


## Impala: `--gazl2`

One compiler, one flag. IMPLEMENTED 2026-08-24 as the boolean **`--gazl2`** (matching the shape of every
other compiler flag - `--legacy`, `--dead-strip`, `--range-checks` - across all three surfaces:
`impala.node.js`, `impala.nuxjs.js`, `impalaJsCompilerRunner.js`; absence is the GAZL 1 default, so no
`--gazl 1` spelling exists):

- default: today's output, byte for byte - `E454`/`E459` in force, `SEEK` never emitted. The
  byte-compare corpus is the gate that this mode IS the GAZL 1 compiler. (On this branch `inline` /
  `SCOP` are live regardless of the flag - the flag governs INITIALIZER placement today; folding
  inline's GAZL 2 dependence under it is what unifying with the Impala2-branch compiler would add.)
- `--gazl2`: `SEEK` regions place struct and extern-struct initializers, retiring `E459` and `E454`.
  No `GAZL #2` declaration line is emitted - additive mnemonics are self-gating (above).

The default is set per distribution and the flag always overrides: **Permut8 ships without `--gazl2`**,
**Synplant 2 and Microtonic 4 ship with it**.
