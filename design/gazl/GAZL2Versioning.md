# GAZL 2 versioning: the GAZL directive and Impala's --gazl option

Status: IMPLEMENTED 2026-08-25 - `GAZL #n` as a REGION directive (see below; user-facing semantics in
`docs/gazl/InstructionSet.md` under `GAZL`). Distribution decided 2026-08-24: Impala is distributed WITH
the products that need it, and the product picks the dialect - Permut8 stays on GAZL 1 for compatibility
with the players already in the field; Synplant 2 and Microtonic 4, where GAZL appears for the first
time, default to GAZL 2.


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


## The directive: `GAZL #n` regions (implemented 2026-08-25)

`GAZL #n` SETS the dialect for what follows; it is a mode, not a one-shot header. `GAZL #2` opens a
GAZL 2 region, `GAZL #1` closes it, the default outside any region is 1, and `finalize` requires the
file to END in dialect 1 - so a truncated region is a detected error, never a silently leaking mode.
What dialect 2 changes is exactly two things, both about functions: `FUNC` mints a `TARGET`-typed
address, and ADDRESS-carrying operand positions stop accepting function constants. Everything else -
including which mnemonics exist - is dialect-free.

- **Absence means GAZL 1.** Every existing source lacks the line; nothing in the field changes meaning.
- On an engine too old to know the directive: `Invalid mnemonic` on line 1 of the unit. On a v2 engine,
  a declared version above `VERSION` is "File requires a newer GAZL engine".
- **The bracket is the concatenation idiom** (the reason a region beat every file-level form): GAZL
  links units by plain concatenation, and a `GAZL #2` ... `GAZL #1` bracketed unit carries its dialect
  with it and hands the stream back to the default when it ends. Bracketed and unbracketed units mix in
  ANY order; per-symbol `TARGET` minting carries the funcptr protection across the seams. Emit the
  bracket in anything meant for concatenation - `--gazl2` does.
- **Additive mnemonics are NOT gated by the directive** (decided 2026-08-24, unchanged). `SCOP`, `ENDS`,
  `SEEK` and the `t` family are self-gating: an engine that lacks them rejects them as unknown
  mnemonics, and a `t` row outside a region can never receive a function constant anyway (the type bits
  gate it). The directive exists solely for the meaning change - the `t` split.

Considered and rejected on the way (each killed by the concatenation constraint or by strictness):
`VERS #2` (says less as a first word); a `!` compile-time spelling (no gain); a single sticky
first-line declaration (headerless GAZL 1 units INHERIT the mode of whatever preceded them, and
assembly outcome depends on cat order); a per-function `FUNt` mnemonic and a `use strict`-style
head pragma (compose fine, but cannot express the region-wide rule that also refuses GAZL 1 function
addresses in the region's own `p` rows); an explicit `ENDG` closer (paired-bracket nesting errors catch
one seam mode-set misses, but cost a second mnemonic - the end-of-file dialect check recovers the case
that matters).


## Impala: `--gazl2`

One compiler, one flag. IMPLEMENTED 2026-08-24 as the boolean **`--gazl2`** (matching the shape of every
other compiler flag - `--legacy`, `--dead-strip`, `--range-checks` - across all three surfaces:
`impala.node.js`, `impala.nuxjs.js`, `impalaJsCompilerRunner.js`; absence is the GAZL 1 default, so no
`--gazl 1` spelling exists):

- default: today's output, byte for byte - `E454`/`E459` in force, `SEEK` never emitted. The
  byte-compare corpus is the gate that this mode IS the GAZL 1 compiler. (On this branch `inline` /
  `SCOP` are live regardless of the flag - the flag governs INITIALIZER placement today; folding
  inline's GAZL 2 dependence under it is what unifying with the Impala2-branch compiler would add.)
- `--gazl2`: `SEEK` regions place struct and extern-struct initializers, retiring `E459` and `E454`;
  the whole output is bracketed `GAZL #2` ... `GAZL #1`, and funcptrs stop collapsing to `p` - `LOCt`,
  `MOVt`, `DATt`, `EQUt` render through the one `TYPE_SUFFIXES` entry, and the GAZL 2 assembler
  enforces the funcptr contract Impala always knew.

The default is set per distribution and the flag always overrides: **Permut8 ships without `--gazl2`**,
**Synplant 2 and Microtonic 4 ship with it**.
