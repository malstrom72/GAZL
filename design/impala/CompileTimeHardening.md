# Hardening compile-time errors (design note)

Status: DESIGN NOTE. Collects the "the compiler should have caught that" items, plus the one constraint
that shapes all of them.

Impala 2.0 turned a lot of silent runtime garbage into diagnostics (typed pointers, struct types, strict
expressions, extern/struct link checking). What follows is the remaining list, and how to implement it
without breaking the thing that makes GAZL interesting.

Read the list as a DIAGNOSTIC-QUALITY backlog, not a soundness one. The assembler already rejected most of
these; what was wrong is WHERE the error points - at a symbol and a line in generated GAZL, not at the
`.impala` line that caused it. Check claims against the assembler, not against `tools/gazl-validate.sh`,
which is a JS-side metadata linter that passes every item here.

**Status 2026-08-03: every item on this list is CLOSED.** Items 2-5 went in one diagnostics pass on
2026-07-29; item 1 shipped on 2026-08-03 as `E461`, scoped to dereference (plus any negative index) and
backed by a `--range-checks` runtime tier for the dynamic indices no constant check can see. The rule as
shipped is in `docs/impala/Impala2.md` under "Array bounds"; this section records how it was reasoned about.

| Item | Status | Code |
|---|---|---|
| 1. Constant array index out of bounds | closed | `E461` |
| 2. Write to a readonly array element | closed | `E404` |
| 3. Case label outside the switch range | closed | `E444` |
| 4. `goto` to an undefined label | closed | `E445` |
| 5. Duplicate `case` labels | closed | `E443` |
| 6. Duplicate label in one function | closed | `E446` |

Note the numbering is not in code order - E443 closes item 5 and E444 closes item 3, because the duplicate
check and the range check landed in that order. E446 (item 6) reuses the label map `processBranches`
already builds for item 4, so it landed in the same place.

One thing the pass did NOT reach, verified 2026-07-29 and **since FIXED**:

- ~~**E445 points one statement late.**~~ `goto nowhere;` on line 3 reported line 4, because the position
  was recorded after `';'_` had swallowed the trailing newline. Fixed by recording the label's offset
  before that term; E445 now names the label itself, and `jspegCompilerTests.js` pins the exact position.
  E443/E444 land on the `:` after the case value, which is benign.

**A symbolic switch range is not on this list, by design.** `const int N = 3; switch (i == 0 to N) {
case 8: }` compiles and assembles clean, with the arm unreachable. That is correct: a build configuration
may legitimately narrow the range, and the surplus arms are then dead code in the same way an arm behind a
branch Impala folds to false is dead (see [`FutureOptimizations.md`](../FutureOptimizations.md)). Rejecting it
would make a valid configuration unbuildable. Unreachable is not wrong.

**`&a[k]` PAST THE END is not on this list, by design.** `p = &a[7]` compiles, assembles and runs, and
should: forming a pointer is not a memory access, and one-past-the-end is a standard idiom -
`e = &a[4]` on a 4-element array is how you write a loop bound, and it works today. A bounds check on
address-of would reject the idiom while catching nothing real. Only the dereference matters.
(A NEGATIVE `k` is the exception and IS rejected, address or not - see `docs/impala/Impala2.md` "Array bounds".
`&g:-1` and `$a:-1` are rejected by the assembler, and on a struct field the offset folds to a positive
one naming the previous field, so it would assemble and alias silently.)


## The constraint: a constant is not always a number Impala knows

> Summarized here because it governs every item below. The canonical, normative statement is
> [`design/impala/TwoStageConstants.md`](TwoStageConstants.md) - read that first if any of this is new to you.

This is the trap. In Impala 2.0 a constant can be resolved at GAZL ASSEMBLY time, not at Impala compile time:

- `.z.Struct` / `.o.Struct.field` for an `extern struct` are supplied by the HOST at load.
- A macro-assembler or host header can define any `! DEFi` constant the Impala source references.
- Impala's own folds (`! ADDi`, `! MULi` into `<A>`) are assemble-time values too.

So a "constant array index" or a "constant switch range" may be a symbol Impala cannot evaluate. Three
rules follow, and every check below must obey them:

1. **Only reject on a value Impala genuinely knows.** If the operand folded to a numeric literal, check
   it. If it is a symbol, Impala has no grounds to reject.
2. **Never silently pass a symbolic value off as checked.** Not knowing is not the same as being fine.
3. **First ask whether the symbolic case is even wrong.** Usually it is not. A host-supplied size or a
   config-dependent `const` is a legitimate reason for an index or a case arm to be out of range in one
   build and in range in another; the surplus arm is dead code, not an error, and rejecting it would make
   a valid configuration unbuildable. Deferring the check to assembly time (below) is only worth doing
   when the shape is genuinely wrong in every configuration - which, as it turns out, none of the items
   here are.

Existing precedent: GAZL already rejects a CONSTANT out-of-range offset into a global
(`POKE &g0:6` with `g0` 2 words -> `Offset out of bounds: g0`), but it does NOT bounds-check a RUNTIME
index (`POKE &g0 %0`). So partial coverage already exists at the assembler level.


## The deferred assertion

**One item needs this, and only one: a constant index into a struct array FIELD whose extent is symbolic**
(item 1, shipped 2026-08-03). That case is genuinely wrong in every configuration AND invisible to the
assembler, because the overrun stays inside the struct's allocation. Everything else here turned out to be
either already covered by the assembler (a plain array, symbolic extent or not) or legitimately
configuration-dependent and therefore not an error at all (item 3). Before reaching for it, re-read rule 3
above - the bar is "wrong in every configuration", and almost nothing clears it.

> **Superseded mechanism, 2026-07-31.** Everything below works, but it is the wrong tool. GAZL has a
> purpose-built `! FAIL <free text>` directive (`Assembler::feed`, its `FAIL_DIRECTIVE` throw) that
> aborts assembly with your own message, and `src/UnitTest.gazl:30-40` already uses `! IFDF`/`! EQUi` +
> `! FAIL` as the canonical idiom for exactly this. Use that instead of the undefined-label trick: it
> takes a real sentence rather than encoding the message in a label name. See
> [`design/impala/TwoStageConstants.md`](TwoStageConstants.md), "The deferred assertion". The section below is
> kept because the branch-condition half (which comparison to emit, and when) still applies verbatim.

GAZL has compile-time comparisons that branch (`! LSSi`, `! GEQi`, `! EQUi`, `! NEQi`, ... `_ccb`) and
`! GOTO`. Branching to a label that does not exist is an assembly error. Together that is an
assemble-time assertion:

    SIZE:       ! DEFi #4
    main:       FUNC
                PARA *1
                ! LSSi #2 #SIZE @.inRange       ; index < size -> skip the failure
                ! GOTO @.INDEX_OUT_OF_BOUNDS    ; undefined label = assembly-time error
    .inRange:   MOVi %1 #111
                ...

Verified behaviour:

- index 2, size 4 -> assembles and runs normally.
- index 9, size 4 -> `Exception: Compile time label not found: .INDEX_OUT_OF_BOUNDS`.
- **index 9 against a symbolic `.z.Frame` supplied elsewhere -> same failure.** This is the important
  one: the check works even when Impala never knew the size.

Notes for using it:

- The diagnostic text IS the label name, so name it to read as an error:
  `.ERROR_index_9_out_of_bounds_for_Frame`. That is the whole message the user sees.
- The label must sit on the same line as an instruction (`.inRange:  MOVi ...`); a label alone on a line
  is `Missing instruction`.
- Cost is zero at run time (all `!` directives), but it adds emitted lines, so it should be opt-in or
  reserved for cases that cannot be decided at Impala compile time.

Open question before adopting it widely: whether these assertions should be emitted always, only under a
flag, or only when the operand is symbolic (cases known at Impala compile time being rejected outright).


## The items

### 1. Constant array index out of bounds (task #22)

    int array a[4]
    a[7] = 1;              // accepted today

Today this compiles. Correction to an earlier claim here: the assembler catches BOTH storage classes, not
just globals - `Symbols::resolve` rejects `offset >= symbol.size`, and a local's size comes from its
`LOCA *size`, so `MOVi $buf:9 #1` under `LOCA *4` fails with `Offset out of bounds: $buf`. Verified
2026-08-01 for both - and `design/impala/Impala2Review.md` C6 had already flagged this section as "backwards on
which case is uncovered", so this correction was overdue rather than new. So this is not a missing check;
it is a check that fires at the WRONG TIME - at assembly, on the end user's machine, when Impala had the
number all along. `ADDp` is deliberately `UNCHECKED_ADDRESS` (`src/GAZL.cpp:285`), so dynamic indices
correctly fall through to the runtime `PEEK`/`POKE` region check instead.

The gate is whether Impala knows BOTH ends, index and extent; either one turning symbolic moves the access
down a tier, and which tier it lands in depends on the shape, not on which end it was:

- Numeric index and numeric extent -> tier 1, an Impala-compile-time `E461` naming the extent.
- **Either end symbolic on a PLAIN array -> do nothing.** Not a deferred assertion: the assembler resolves
  the symbol before it checks the offset, so it catches this case natively. Verified with `const int N = 4;
  int array a[N]; a[7] = 1;` -> `Offset out of bounds: $a`, and a symbolic INDEX is the same shape -
  `global g[K] = 1` emits a bare `POKE &g:K #1`, checked once `K` has a value. Emitting `! LSSi`/`! GOTO`
  scaffolding to re-check what the assembler checks for free would be strictly worse than silence -
  measured when E461 shipped, doing so grew 15 of 87 goldens, versus 1 when scoped.
- **Either end symbolic on a struct array FIELD -> tier 2, the deferred assertion after all.** The
  paragraph above is right about plain arrays and wrong about this one: a field overrun stays INSIDE the
  struct's allocation, so `Symbols::resolve` sees a legal offset and nothing catches it at any stage. So a
  symbolic index earns the `! LSSi`/`! FAIL` scaffolding here even against a numeric extent. `global
  s.v[K] = 1` on a `v[4]`, with a valueless `const int K;`, emits BOTH bounds - a non-literal index may
  be negative, so the low compare is not optional - and every `! FAIL` carries the `(resolved only at
  assembly)` tail:

        ! ADDi <A> #.o.S.v #K			; global s.v[K] = 1
        ! LSSi #K #0 @.g1
        ! LSSi #K #.z.S.v @.g0
        .g1:	! FAIL index K outside S.v (resolved only at assembly)
        .g0:	POKE &s:<A> #1

  (`docs/impala/Impala2.md`, "Array bounds", tier 2.)
- **Runtime index -> tier 3, `--range-checks`, opt-in and off by default.** Two `DEBUG`-gated compares per
  subscript - **per AXIS** for a shaped array, against that axis's own `.d.` symbol rather than the flat
  `.z.` - calling the host's `assertFail`. Off by default
  because those are not the same switch: `DEBUG 0` stops the assembler EMITTING the guards, but the guard
  LINES stay in the shipped `.gazl` text whatever `DEBUG` says (~95 bytes of compacted source each,
  measured) and that text is the artifact. A bare pointer `p[i]` has no extent, and is unchecked in every
  tier.

**Priority: low.** This is caret placement on an error that already names the source expression, and it is
the whole of what is left. Do not let its position at the top of this list imply otherwise.

**Scope: dereference only, never address formation.** The example above is a store, and the check applies
to that shape - `a[7]`, `a[7] = 1`, `p[9].a` as a value. It must NOT reject `&a[7]`, `&a[N]` or `&p[i]`.
An out-of-range address is a value like any other, and GAZL itself accepts it: `MOVp $e &a:9` on a 4-word
`a` assembles and runs, because the `Offset out of bounds` check fires on constant-offset *access*
operands only. Containment is guaranteed by every dereference being bounds-checked at run time (see
`docs/impala/MemorySafetyModel.md`), so rejecting address arithmetic would make Impala stricter than the machine
it transliterates to, and would outlaw the one-past-the-end pointer that every walk loop needs. See F3 in
`design/impala/Impala2Review.md`.

### 2. Writes to a readonly array element (task #21)

    readonly int array table[4] = { 1, 2, 3, 4 }
    table[0] = 9;          // not caught by Impala; GAZL says `Incompatible types: table`

`readonly` is tracked on the symbol (`declare(..., readonly, ...)`) and is honoured for scalars, but an
indexed write did not consult it. Purely an Impala-compile-time check on the symbol; no assembly-time
subtlety - the readonly array lands in the const region, so the `POKE` is rejected there as a backstop.

**Structs had the same hole in two places, found and closed 2026-08-01.** `readonly S s; global s.a = 1;`
emitted its `POKE`, and `global s = global t;` emitted its whole-struct `COPY`, both with no diagnostic:
`lookup` never propagated `readonly` onto a struct-value place, and the whole-struct path returns before
the scalar check. Both now report.

The mechanism is the `readonly` FLAG, not the operator spelling. `makeMeta` deliberately clears the flag
(pooled slots must not inherit a previous symbol's writability), so every branch that mints a readonly
meta re-publishes it afterwards - the idiom the codebase already used for arrays. An earlier attempt
inferred readonly-ness from the operator (`:=`) plus an `&`/`$` operand instead; that looked equivalent
and was not, because a function name, a whole global array, `nullfunc`, `null`, `&f` and a parameter all
match that shape while none of them is readonly, and each was told to "declare it `global` instead of
`readonly`". Scalars, array elements, struct fields and whole structs now share one message, and a
genuine non-lvalue such as `1 = q` still reports `Invalid lvalue`.

### 3. Case label outside the switch range (task #33)

    switch (i == 0 to 3) { case 8: ... }        // silently unreachable dead code
    switch (i == 5 to 9) { case -1: ... }       // emits `.s0.-6` - the module will not load at all

`SWCH` clamps `min(value, count)`, and the assembler only resolves table entries `label.0 ..
label.count-1`, so a `case 8:` in a `0 to 3` switch can never fire. The body is emitted as unreachable
dead code with no warning, and the stray `.sN#8` labels make the GAZL confusing to read.

A case BELOW `from` is worse than dead code. Its offset goes negative, the emitted `.sN#<X>` folds to
`.sN.-6`, and `Symbols::link` rejects that as `Invalid identifier` - so the whole module fails to
assemble, from a program the compiler accepted without a word. `switchtest.gazl` used to be in exactly
that state, which is why `runJspegTests.js` had an exemption list - **both are now fixed**: E444 makes an
out-of-range case a compile error and `KNOWN_UNLOADABLE` is empty. Verified 2026-08-01.

**DONE for a numeric range; the symbolic half is still open** - see S5 in the scan below, which reproduces
`Invalid identifier: .s0.-5` from `switch (x == LO to HI)` with host-supplied bounds.

CLOSED by `E444`, on the terms below:

- Numeric `from`/`to` -> Impala-compile-time error naming the range. Implemented.
- **Symbolic range** (`switch (x == 0 to sizeof(V))`, or a plain `const int N`) -> **left unchecked, and
  that is the right answer, not a shortfall.** A configuration may narrow the range legitimately, leaving
  surplus arms dead; erroring would make that configuration unbuildable. Below `from` the negative offset
  still fails assembly, which self-reports.

### 4. `goto` to a label that is never defined

    function f() { goto nowhere; }              // accepted; the assembler is what complains

Emitted verbatim as `GOTO @nowhere`, with the first sign of trouble being
`Symbol not found (in expected scope): nowhere` at load. Unlike everything above, this item has no
symbolic escape hatch and so no need for a deferred assertion: a label is always local, always defined
in the same function body, and the compiler holds the complete set by the time that body is parsed.
`processBranches` already builds exactly that map for its own post-condition, which checks only
compiler-minted `@.` labels and deliberately leaves user labels alone precisely because an undefined one
is a user error that deserves a real diagnostic, not an internal assertion. A plain Impala-compile-time
error naming the label is all this needs.

### 5. Duplicate `case` labels (fuzzer-found)

    switch (i == 0 to 3) { case 0: { i = 1; } case 0: { i = 2; } }

Both arms are emitted, each under its own `.s0#0:`, and the assembler rejects the second with
`Symbol already defined: .s0.0`. So the build does fail - but on a compiler-minted label the user never
wrote, which is the worst possible way to report "you listed case 0 twice". Fully decidable at
Impala compile time whenever the case values are numeric, exactly like item 3, and it wants the same
pass: collect the arm values for a switch, then reject a repeat naming the value and both source positions.

**DONE as E443, for EVERY range.** It first shipped inheriting item 3's gate, so a symbolic range
disabled it until 2026-08-02 - and unlike item 3, duplicate detection never needed the range base at all.
See S6 below.


## Scan: where else does Impala guess, refuse, or skip? (2026-08-01)

A systematic sweep for the pattern this note is about - Impala either baking a number it cannot justify,
refusing a construct for lack of one, or silently skipping a check when the value turns symbolic. Each
entry below was REPRODUCED, not inferred.

**Read the attributions.** Roughly half of this was already on record and is repeated here only because
the sweep reproduced it at a specific site; the genuinely new findings are S1's reachability and the
validator group S7-S11. Where an item says ALREADY RECORDED, that other place is authoritative - do not
let the two copies drift.

### Confirmed miscompiles (a wrong number reaches the artifact)

**S1. A by-value struct argument to an UNPROTOTYPED callee bakes a host-owned size.** *(The `*NaN`
mechanism is ALREADY RECORDED as an anti-pattern in `design/impala/TwoStageConstants.md` - `field.size * per`.
NEW here: the reachability, i.e. that `rejectByValueStruct` guards declarators only.)*
`rejectByValueStruct` guards declarators only, so `extern native sink` / bodiless `extern function sink`
have nothing to reject and the parked by-value path runs at the call site. One artifact then makes two
contradictory statements about one quantity:

    b:      GLOB *.z.AB          ; size is host-owned here...
            ADRL %3 %1 *2        ; ...and baked to 2 here
            COPY %3 &b *2
            CALL ^sink %0 *3

A host defining `.z.AB` as 3 gets a silently truncated `COPY`. Worse sub-case, **still live but no longer
`NaN`**: `fieldWords` used to multiply the extent OPERAND by a number, so `array v[N]` with a valueless
`const int N;` produced `*NaN`. It now asks `constInt` and returns `undefined` instead (fixed 2026-07-31,
`design/impala/TwoStageConstants.md`), so the operands read `*undefined` on the `ADRL`/`COPY` and the CALL window
is still `*NaN`:

        ADRL %1 %1 *undefined			; sink(global s)
        COPY %1 &s *undefined
        CALL ^sink %0 *NaN

Both spellings are legal GAZL identifiers, so it is an undefined-symbol error at best, and `claimSlot`'s
loop still never runs - which is why the `ADRL` still self-aliases (`ADRL %1 %1`). The defect is
unchanged; only the operand text moved.
Fix: call `rejectByValueStruct` from the ARGUMENT site, not just declarators. Do NOT emit `*.z.Name` here
- see `design/gazl/GAZLSymbolicWindows.md`, the numeric window ABI is correct.

**S2. `buildStructInit` iterated a symbolic field extent, silently dropping initializer words - CLOSED
2026-07-31.** *(ALREADY RECORDED, `design/impala/TwoStageConstants.md`, which carries the authoritative account of
both the defect and the fix. Reproduced here at the site.)*
`for (var e = 0; e < f.size; ++e)` where `f.size` is the extent string. With
`struct S { int array v[N]; int tag }` and a `{{1,2},7}` initializer the emitted data was `DATA #7` - the
`1, 2` vanished and the `7` landed at `.o.S.v`, not `.o.S.tag`, with no diagnostic. Now
`global S s = { v: {1,2}, tag: 7 }` reports `E454` (a field BEHIND a symbolic extent has no position
Impala can know) and the positional spelling reports `E455`.

**S3. `parseFloat` on an emitted operand string mis-folds a pointer offset - CLOSED 2026-08-04.**
*(ALREADY RECORDED, `design/impala/TwoStageConstants.md`, which names `parseFloat("0x10")` being `0` exactly.
Reproduced here.)* The `p - const` fold tested `operands[2][0] === '#'`, which establishes CONSTANT, not
DECIMAL, then ran `parseFloat` on it. `*(p - 0x2)` emitted `PEEK $x $p #0` instead of `#-2` - completely
silent - and `*(p - K)` for any named const emitted `#NaN`.

Both sites now ask `constInt`, the one decoder, and branch on `undefined`: `*(&g[32] - 0x10)` folds to
`#-16` exactly as the decimal spelling does, and `*(p - K)` stops trying to fold at all, emitting
`SUBp %0 $p #K` for GAZL to resolve. `subConstInt` had the same shape one function away - it spanned
decimal digits only, so a hex subtraction fell through to a runtime temp instead of folding - and was
fixed with it. Worth recording how long this survived: it was written down twice, here and in
`TwoStageConstants.md`, and still shipped, because the entry that retired the OTHER `parseInt`/`parseFloat`
sites (S4) closed the two it happened to be looking at. A decoder is not retired until its siblings are
gone; a cleanup review found these by asking who else answers this question, not by re-reading the notes.

**S4. `parseInt('0x2', 10) === 0` slipped past E414 and dropped a whole initializer - CLOSED 2026-08-04.**
*(Same anti-pattern as S3, ALREADY RECORDED. NEW: that it defeated E414's `isNaN` guard specifically, `0`
not being NaN.)* `constInt` now decodes hex and `+`-signed literals, so
`readonly S array t[0x2] = { { a: 1 }, { a: 2 } }` emits its `DATA #1 #2`, `global int array g[0x4]` arms
`E461`, and a `[-0x1]` extent reaches `E462` instead of shipping a backwards layout.

### Confirmed silent acceptances that become gibberish at assembly

**S5. A symbolic switch range disables E444.** *(Item 3 above predicted this; `Impala2Review.md` C6
recorded the pre-E444 version. NEW: reproduced as the residual AFTER E444 shipped.)* `checkCaseValue` returns early when `constInt` cannot read
the range, so `switch (x == LO to HI)` with host-supplied bounds is unchecked. With `LO=5`, a `case 0`
folds to a negative offset and the module fails to load with `Invalid identifier: .s0.-5` - a
compiler-minted label the user never wrote. This is the deferred-assertion case: emit
`! GEQi #<off> #0 @ok / ! FAIL <source>: case 0 is below the switch range`.

**S6. The same gate disabled E443 (duplicate case) - FIXED 2026-08-02.** *(Item 5 above;
`Impala2Review.md` C6.)* `caseSeen` was indexed by `value - fromNum`, inheriting a dependency on the
range base that duplicate detection does not have, and sitting behind the same early return as the window
check. Two `case 0:` arms under a symbolic range compiled clean and then failed assembly with
`Symbol already defined: .s0.0`, naming a compiler-minted label the user never wrote; under a literal
range the same source was a clean E443. `checkCaseValue` now keys the duplicate check on the raw value
and returns only afterwards when `fromNum` is unknown, so the two checks no longer share a gate.

Note this did NOT need a deferred assertion, unlike S5 beside it: the case values are all in hand at
Impala compile time whatever the range does. The window check stays off under a symbolic range on
purpose - a configuration may legitimately narrow it, and erroring on a now-surplus arm would make that
configuration unbuildable. Both halves are pinned in `jspegCompilerTests.js`, non-zero base included.

### Link-checking holes (see also `tools/gazl-validate.nuxjs.js`)

**S7.** `fieldListsMatch`'s array-vs-scalar guard is an `&&` whose first clause is false in exactly the
case it was written for, because `fieldParts` returns `size: undefined` for both `int[]` and `int`. Since
E430 forces `int[]` on every extern struct array field, a unit declaring an array field as a SCALAR
validates clean.

**S8.** The cross-unit extent check is dead code: the grammar gives `extern array` nowhere to state an
extent and E430 forbids one on an extern struct field, so `a.size && b.size` can never both hold on
compiler output. Its only regression test uses a row the compiler cannot emit.

**S9.** Two conflicting struct DEFINITIONS in different units are never compared - `validateExternStructs`
iterates only the extern map. Every other symbol kind has a definition-vs-definition path.

**S10.** `arraySignaturesCompatible` compares extents as raw strings, so `[4]` vs `[N]` with host `N=4` is
a false conflict and `[4]` vs `[]` is a false pass. This is the case `design/proofs/deferredShapeCheck.gazl` exists
to demonstrate.

**S11.** Struct field types are lower-cased wholesale, so `int[N]` matches `int[n]` and `Filter` matches
`filter` - while ARRAY rows do not lower-case the extent, so the same disagreement errors through one
path and passes through another.

**S12.** *(ALREADY RECORDED, `Impala2Review.md` C6: "extern struct host layout is essentially
unchecked".)* The check is name-presence-only and inspects only the FIRST declaration: a
layout that transposes two fields validates clean. Offsets are host-supplied constants, so ordering,
overlap and the `.z.` bound are all decidable with `! EQUi` / `! LSSi` chains.

### The shape of the answer

These split three ways, and the split is the useful part:

1. **Never needed the number** (S6, S7, S9, S11) - the check was accidentally made to depend on a value it
   does not need. Ordinary bug fixes, cheapest and highest value.
2. **Had the number and threw it away** (S3, S4, and item 1 above) - `parseInt`/`parseFloat` on an emitted
   OPERAND STRING, which rule 5 guarantees will not look like a number. Fix by reading the value before it
   becomes text.
3. **Genuinely cannot have the number** (S1, S2, S5, S8, S10, S12) - either refuse honestly (S1, S2) or
   name both sides and let the assembler decide (S5, S8, S10, S12).

Only category 3 wants `! FAIL`. Reaching for it first would be a mistake - most of this list is category 1.

## Not in this list

- UNCONDITIONAL runtime bounds checking. Impala is a transliterator; a check on every access, always,
  would break the 1:1 model and the cost predictability. What ships instead is the opt-in `--range-checks`
  tier under item 1, off by default. The assembler's own region checks remain the backstop.
- Anything requiring Impala to evaluate host-supplied constants. It cannot, by design - that is the point
  of the layout-as-constants scheme (see `design/impala/StructLayoutConstants.md`).
