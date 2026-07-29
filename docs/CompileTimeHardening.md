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

**Status 2026-07-29: items 2-5 are CLOSED**, by one diagnostics pass. Item 1 is the only one left.

| Item | Status | Code |
|---|---|---|
| 1. Constant array index out of bounds | **OPEN** | - |
| 2. Write to a readonly array element | closed | `E404` |
| 3. Case label outside the switch range | closed | `E444` |
| 4. `goto` to an undefined label | closed | `E445` |
| 5. Duplicate `case` labels | closed | `E443` |

Note the numbering is not in code order - E443 closes item 5 and E444 closes item 3, because the duplicate
check and the range check landed in that order.

Two things the pass did NOT reach, both verified 2026-07-29:

- **E445 points one statement late.** `goto nowhere;` on line 3 reports line 4, because the position is
  consumed in the post-condition loop after the body is parsed. E443/E444 land on the `:` after the case
  value, which is benign; this one names the wrong statement. This is the only genuine defect left here.

**A symbolic switch range is not on this list, by design.** `const int N = 3; switch (i == 0 to N) {
case 8: }` compiles and assembles clean, with the arm unreachable. That is correct: a build configuration
may legitimately narrow the range, and the surplus arms are then dead code in the same way an arm behind a
compile-time-false branch is dead (see [`FutureOptimizations.md`](FutureOptimizations.md)). Rejecting it
would make a valid configuration unbuildable. Unreachable is not wrong.

**`&a[k]` out of bounds is not on this list, by design.** `p = &a[7]` compiles, assembles and runs, and
should: forming a pointer is not a memory access, and one-past-the-end is a standard idiom -
`e = &a[4]` on a 4-element array is how you write a loop bound, and it works today. A bounds check on
address-of would reject the idiom while catching nothing real. Only the dereference matters.


## The constraint: a constant is not always a number Impala knows

This is the trap. In Impala 2.0 a constant can be resolved at ASSEMBLY time, not compile time:

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


## The deferred assertion (verified, but currently unused)

**No item in this document needs this.** It is recorded because the capability is real and was verified,
not because it is planned. Every symbolic case here turned out to be either already covered by the
assembler (item 1) or legitimately configuration-dependent and therefore not an error at all (item 3).
Before reaching for it, re-read rule 3 above.

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
  reserved for cases that cannot be decided at compile time.

Open question before adopting it widely: whether these assertions should be emitted always, only under a
flag, or only when the operand is symbolic (compile-time-known cases being rejected outright instead).


## The items

### 1. Constant array index out of bounds (task #22)

    int array a[4]
    a[7] = 1;              // accepted today

Today Impala compiles this; the **assembler rejects it**, for locals and globals alike:

    Offset out of bounds: $a
    Line 11:  MOVi $a:7 #1   ; a[7] = 1

(An earlier version of this note claimed locals escaped the check while globals were caught. That was
wrong - both fail identically, and the doc's own preamble already said so.)

So the entire remaining value of this item is WHERE the error points. And even that is modest, because the
GAZL line carries the original source text as a trailing comment, so the offending expression is already
in front of you; what is missing is `foo.impala:11:2` and a caret.

- Numeric index and numeric extent -> plain compile-time error, with the extent in the message.
- **Symbolic extent -> do nothing.** Not a deferred assertion: the assembler resolves the symbol before it
  checks the offset, so it catches this case natively. Verified with `const int N = 4; int array a[N];
  a[7] = 1;` -> `Offset out of bounds: $a`. Emitting `! LSSi`/`! GOTO` scaffolding to re-check what the
  assembler checks for free would be strictly worse than silence.

**Priority: low.** This is caret placement on an error that already names the source expression, and it is
the whole of what is left. Do not let its position at the top of this list imply otherwise.
- Runtime index -> out of scope; that is a bounds-check-at-runtime question, deliberately not Impala's model.

### 2. Writes to a readonly array element (task #21)

    readonly int array table[4] = { 1, 2, 3, 4 }
    table[0] = 9;          // not caught by Impala; GAZL says `Incompatible types: table`

`readonly` is tracked on the symbol (`declare(..., readonly, ...)`) and is honoured for scalars, but an
indexed write does not consult it. Purely a compile-time check on the symbol; no assembly-time subtlety -
the readonly array lands in the const region, so the `POKE` is rejected there as a backstop.

### 3. Case label outside the switch range (task #33)

    switch (i == 0 to 3) { case 8: ... }        // silently unreachable dead code
    switch (i == 5 to 9) { case -1: ... }       // emits `.s0.-6` - the module will not load at all

`SWCH` clamps `min(value, count)`, and the assembler only resolves table entries `label.0 ..
label.count-1`, so a `case 8:` in a `0 to 3` switch can never fire. The body is emitted as unreachable
dead code with no warning, and the stray `.sN#8` labels make the GAZL confusing to read.

A case BELOW `from` is worse than dead code. Its offset goes negative, the emitted `.sN#<X>` folds to
`.sN.-6`, and `Symbols::link` rejects that as `Invalid identifier` - so the whole module fails to
assemble, from a program the compiler accepted without a word. `tests/impala/golden/switchtest.gazl` is
in exactly that state today (`case 23+57, -1, CONSTANT` against a `4+1 to 9` switch), which is why
`runJspegTests.js` has to exempt it from the assemble check.

CLOSED by `E444`, on the terms below:

- Numeric `from`/`to` -> compile-time error naming the range. Implemented.
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
is a user error that deserves a real diagnostic, not an internal assertion. A plain compile-time error
naming the label is all this needs.

### 5. Duplicate `case` labels (fuzzer-found)

    switch (i == 0 to 3) { case 0: { i = 1; } case 0: { i = 2; } }

Both arms are emitted, each under its own `.s0#0:`, and the assembler rejects the second with
`Symbol already defined: .s0.0`. So the build does fail - but on a compiler-minted label the user never
wrote, which is the worst possible way to report "you listed case 0 twice". Fully decidable at compile
time whenever the case values are numeric, exactly like item 3, and it wants the same pass: collect the
arm values for a switch, then reject a repeat naming the value and both source positions.


## Not in this list

- Runtime bounds checking. Impala is a transliterator; per-access runtime checks would break the 1:1
  model and the cost predictability. The assembler's own region checks remain the backstop.
- Anything requiring Impala to evaluate host-supplied constants. It cannot, by design - that is the point
  of the layout-as-constants scheme (see `docs/StructLayoutConstants.md`).
