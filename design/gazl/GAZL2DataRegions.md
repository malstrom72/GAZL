# Data regions in GAZL 2: the SPAN directive

Status: IMPLEMENTED END TO END, 2026-08-24 as `SEEK :offset [*extent]`; renamed `SPAN :offset *extent`, with the
extent required, on 2026-10-18 (see "Final syntax" below). Assembler side (see "As implemented" below; runnable re-pack
proof [`design/proofs/spanRegions.gazl`](../proofs/spanRegions.gazl) + twin, gated by
`tools/checkDocProofs.js`) and Impala emission under **`--gazl2`** (`impala/impala.jspeg`
`buildStructInit`/`emitInitData`: one region per leaf field, offsets folded through the same
`scaleByStride`/`foldOffset` every access site uses, typed rows per field; `E459` and `E454` lift under
the flag and stand without it; default output byte-identical, gated by the golden corpus; tests incl. a
reversed host layout and a repack-rerun in `impala/jspegCompilerTests.js`). No VM change, nothing at run
time.
This is the assembler-side half of `design/ParkedFeatures.md` "Placing static data at a symbolic offset -
REQUIRES GAZL 2"; the original wishlist form of it (`637cdc4`, `docs/ParkedFeatures.md`) was dropped in
the docs-split merge and survives only in history - this note replaces it. `SPAN` assembles only
inside a `GAZL #2` region (decided 2026-10-20, reversing the 2026-08-24 rule that it needed none), so
dialect 1 stays exactly GAZL 1; see [`GAZL2Versioning.md`](GAZL2Versioning.md).


## The last numeric channel

Initialized data is the one place where the symbolic-layout story falls back to positions. From
`tests/impala/golden/structInit.gazl:24`:

    voice:  GLOB *.z.Voice
            DATA #60 #0.5 #0.1 #2.0

Every ACCESS site adapts on a repack - `GLOB`/`LOCA`/`COPY` scale through `.z.`, every load rides `.o.` -
and then `DATA #60` puts 60 in whatever word is positionally first. The VALUE channel is fully symbolic
(`DATi <A>` folds assemble today); the POSITION channel is a bare append cursor. Reorder two fields in a
layout header and the program still assembles and runs with `note` and `gain` swapped - no diagnostic at
any tier, because `DATA` gave up the type pairing too. Two Impala features are refused over exactly this
encoding: `E459` (extern-struct initializers, `impala.jspeg:2976` - the host owns the layout, so Impala
does not know which word a value would land in) and what remains of `E454` (fields behind a struct array
field with a symbolic extent). The compiler-side history, and the interim `! LEQi` / `! FAIL` count-guard
idiom that shipped in the meantime, are in `ParkedFeatures.md`.


## The directive

`SPAN :offset *extent` - open a region of `extent` words at an assemble-time offset within the CURRENT
section. The `DAT*` rows that follow are unchanged GAZL 1 rows, filling the region sequentially:

    voice:  GLOB *.z.Voice
            SPAN :.o.Voice.note *1
            DATi #60
            SPAN :.o.Voice.state *.z.Voice.state
            DATf #1.0 #2.0
            DATf #3.0 #4.0

Sigils stay in their lanes: `:` is an offset (as in `$v:.o.P.x`, with the base - the section being
filled - implicit), `*` is an extent, `#` a value. Both operands take the same constant class as every
`*size`, so `.o.`/`.z.` symbols and `<X>` folds work unchanged; element k of a struct array anchors
through the existing `!` machinery (`! MULi <A> #k #.z.Voice`, `! ADDi <A> #<A> #.o.Voice.gain`,
`SPAN :<A> *1`). No new arithmetic anywhere.

**GAZL 1 is the degenerate case.** Every section implicitly opens with a region at offset 0 that claims the
words it writes: a section containing no `SPAN` is a single region bounded by the section's `*size` - which is
exactly GAZL 1 semantics, including its existing overflow check (`Not enough space in data section` fires
per value against `dataEnd`, and `dataEnd` is already the SECTION bound: `dataEnd = dataPointer + size`,
`src/GAZL.cpp:1199`). One mechanism; the old behaviour is its default instance. No legacy mode.


## Semantics

- A region opens at `SPAN` and closes at the next `SPAN`, at the end of its section, or at the end of
  assembly. Regions are sequential, never nested.
- The region CLAIMS `[offset, offset + extent)` whole, written or not; filling past the extent is an
  error. Anchored at an array field the extent is the layout-INVARIANT part: a repack moves
  `.o.Voice.state`, but a 4-word array is 4 words under any layout, so the run adapts through `:` while
  `*` pins its true width.
- Unwritten words are zero - the existing guarantee, unchanged. (Implementation: zero the whole section
  when it OPENS instead of zeroing the tail when it closes; observably identical for GAZL 1 input.)
- Regions may arrive in any offset order. A host-published layout need not follow declaration order and
  a repack may reorder anything; order-dependence would re-bake what the directive exists to unbake.
- `DATs` is out of scope (a string is a section of its own); `DATp` fills like `DATi`/`DATf`.


## Checks

All assemble-time, all with machinery the assembler has:

1. `SPAN` outside a data section: error, as for `DAT*` today.
2. `offset` out of range, or `offset + extent` past the section size: error. Strictly stronger than
   checking the offset alone - a field whose TAIL hangs past `.z.Voice` is caught even when its first
   word is inside, which is precisely the malformed-host-layout shape for extern structs.
3. A value past the region's extent: the existing per-value bound check with a nearer fence. The run
   that silently spilled into a sibling field becomes an error naming the row.
4. Two regions of one section overlap: error (one new code, in the manner of `UNBALANCED_MARK`).

Check 4 is the one piece that is not pure emitter-trust, and it earns its ~10 lines: append-only `DATA`
cannot even express a double write, so placement introduces that hazard, and last-writer-wins would be
silent. It also catches an overlapping host-published layout with no layout model in the assembler at
all - two host fields on the same offset collide as two overlapping regions. (`ParkedFeatures.md` cites
this as `Impala2Review.md` C6, undetected today.)


## Why intervals, not a written-word set

Define-each-word-once reduces to region disjointness: WITHIN a region, filling is append-only against a
bound - the cursor cannot revisit a word, exactly as in GAZL 1 - so a word can be written twice only if
two REGIONS overlap. A per-section list of `(start, end)` pairs, discarded when the section closes, is
the whole bookkeeping. `ParkedFeatures.md` said it first: "a region declaration IS that bookkeeping."
A per-word set adds state and buys nothing a section of a few dozen fields can measure.


## Why a directive, not per-row offset operands

The alternative considered was `DATx :offset [*extent] #value...`. The directive wins on every axis:

1. GAZL 1 falls out as the degenerate case (above). Per-row, old and new rows are two coexisting
   grammars with no equivalent property.
2. The assembler diff shrinks. `DAT*` is four variadic mnemonics parsed by a dedicated path
   (`src/GAZL.cpp:1074`); grafting two optional leading operands onto that is real surgery. `SPAN` is
   one fixed-shape row, and the `DAT` path changes only which bound it compares against.
3. It matches how initializers are emitted. Real tables fill across many short rows interleaved with `!`
   folds (`Priyome2.gazl:83` - eight `DATi` rows for one 8-word table). Per-row, splitting a field
   across rows means re-anchoring every row with a folded offset; under `SPAN`: claim once, fill in as
   many rows as you like, and the bound catches the row that goes one too far.
4. Place and payload stay separated, which is GAZL's idiom already: `GLOB`/`CNST` say where-and-how-big,
   `DAT*` say what, `!` lines compute. `SPAN` joins the "where" family. And the GAZL 1 cursor hazard
   (a miscounted row silently spilling into the neighbour) becomes a fenced one - the same conversion
   `MARK`/`FREE` performed on the frame cursor.

`DAT` rows were never self-contained - they always depended on the current section and cursor - so the
per-row form's one virtue (row-local meaning) gives up nothing that existed. Also considered and
rejected: `ORG`-style bare cursor motion (RELOCATES the defect - the skipped words become undefinable;
the `ParkedFeatures.md` argument), and the fully explicit `DATi &voice:.o.Voice.note #60` (independent
of row order, but repeats the section name on every row to buy generality nothing asked for).


## What Impala emits, and what it lifts

Under `--gazl2` (see [`GAZL2Versioning.md`](GAZL2Versioning.md)) Impala emits ONE `SPAN` per leaf
field: `*1` for a scalar (one word under ANY layout, so the fence is free - and a hand-added surplus row
then fails at its own line instead of landing in an uninitialized neighbour), `*.z.Struct.field` for an
array field (`structArrayFields.gazl:7`, `extentSymbol`; for an `extern struct` the host publishes it with
the field's `.o.`). Never per struct: a hand-written `SPAN` over a struct-sized region followed by positional
`DATA` re-bakes that struct's interior one level down, and whoever places coarser owns the consequence - the same trust model as everything else here. Per-field typed rows
(`DATi`/`DATf`) also restore the type pairing `DATA` gave up: a repack can no longer silently retype a
word.

- The repack/re-assemble promise holds end to end: the layout header changes, access sites adapt, and
  the data lands right - the property `GAZLSymbolicWindows.md` proved for code and nothing could test
  for initialized data.
- `E459` retires: extern-struct globals become initializable, every word placed at a host-defined
  `.o.*` - the unlock the diagnostic's own text promises ("needs GAZL 2").
- `E454` retires: a field behind a symbolic array field has a known place again.
- The emitted `! LEQi` / `! FAIL` count guard retires inside a scalar array's region - check 3 subsumes
  it with a better message and no skip-label counter. A struct-element array keeps it: its elements get a
  region per leaf field, so no region states the array's own extent.


## As implemented

`MARK` / `FREE` (then `SCOP` / `ENDS`) was the template (`f7a19e1`, `72cb803`) and the weight matched: ~55 lines in
`src/GAZL.cpp` / `GAZL.h`, no VM change. The deltas, all in the assembler:

- A new operand class `'o'` for a leading-`:` token (`parseOperandType`), parsed by the same
  strip-the-sigil path `*`/`#` use, so `CONST_INT_P` checking and symbol/`<X>` resolution came free.
  Two `OPERATORS` rows (`SEEK_o__`, `SEEK_os_`) express the optional extent; the debug operator-table
  self-check learned the `'o'` class.
- `dataEnd` became the REGION fence; the section's own range moved to `sectionBegin`/`sectionEnd`. The
  `DAT*` fill paths are UNTOUCHED - their existing bound check now measures the region simply because
  `SEEK` re-aims the pair. `DATs` composes for the same reason.
- `closeDataRegion()` records a closing region's claim into the per-section interval list and throws
  `OVERLAPPING_DATA_REGIONS` ("Data regions overlap") - the one new error code. Range violations at the
  `SEEK` row reuse `OFFSET_OUT_OF_BOUNDS`, joining the regime every other offset already lives in.
- Sections zero WHOLE at open instead of tail-at-close (the two old memsets are gone): after an
  out-of-order `SEEK` the cursor can sit below already-written words, which tail-zeroing would erase.

Tested: `src/UnitTest.gazl` gained a region section (symbolic offsets, out-of-order placement, a bounded
region's zero tail) plus runtime checks, and the checks were verified to be able to FAIL by breaking one
expected value. The three error shapes (overlap, out-of-range, region overflow) each reject with the
intended message. The full corpus (109 programs) and the 3000-seed fuzzer are unchanged-green, which is
the no-region degenerate-case regression gate. User docs: `docs/gazl/InstructionSet.md` "SPAN".


## Final syntax

Before GAZL 2 shipped, `SEEK :offset [*extent]` became `SPAN :offset *extent` (2026-10-18):
- **The name.** `SEEK` read like a cursor jump, and `:offset` like a relative one, but the row never leaves its
  section and fences what follows: it opens a span of the section and fills it.
- **The extent is required.** Without it a region claimed what it wrote and was fenced only by the section,
  so a miscounted host-owned array silently filled the fields after it. Every region Impala emits states its
  width, from its own declaration or, for an `extern struct`, from the host's `.z.` extent.
- **No `DATz`.** Zero words need no row: words no region claims are 0 already.
- **The rows are `SPAN_os_` alone.** The extent-less `SEEK_o__` row is gone, and so is the `regionStart +
  regionExtent` sum that could overflow an `Int`: the bound is now `extent > sectionSize - offset`.
