# Parked features and where to find them

Status: INDEX. Start here when you wonder "didn't we already build that?"

Some Impala 2.0 features were designed, implemented, reviewed, and then deliberately taken back out. The
work is not lost - each one is preserved under its own tag. This file says what is parked, where it lives,
and why it was parked.


## The parking convention

1. Tag the LAST commit that still contains the feature:

        git tag -a <feature>-park <commit> -m "Parked: <what it is>"

2. Land a rollback commit on the working branch whose message names the tag.

So a park point is normally an ancestor of the working branch (the removal happened afterwards on the same
line). To see the feature, check the tag out; to see how it was removed, read the rollback commit.

These were branches until 2026-08-05, and this file used to say they were the only copy of the work. That
was wrong: being ancestors, the commits are reachable from the working branch whether or not any ref points
at them. What a ref buys is a NAME, and a tag is the honest one - a park point is not a line of
development, and a branch sitting in the list only invites the question of whether it still needs merging.


## Parked: arrays as values / shape-carrying pointer types

    tag:      Impala2-multidim-arrays-park   (4cd52f2)
    removed:  dda4129  "Roll back multidim arrays (slices 1-2) to pre-multidim compiler"
    target:   Impala 3.0

**Multidimensional arrays themselves are NOT parked any more - they are IMPLEMENTED on `Impala2`**
(2026-08-04), by a different design from that one's: one subscript with a comma list, striding by the declared element,
lowered through the place model, with per-axis `.d.` constants the assembler resolves. Struct fields and
standalone `global`/local arrays both, across all three bounds tiers plus cross-unit metadata. See
[`MultidimensionalArrays.md`](../docs/impala/MultidimensionalArrays.md), which supersedes the objection below.

What stays parked is the rest of that work: arrays as VALUES, and shape-carrying pointer types
(`int array[W] pointer m`, slice 2c/2d, `3781f8c`). The latter is the real prerequisite for shape
identity - see slice 3 there, and the "Does the CONCEPT help?" note below.

Contains slices 1-2 of multidimensional array support: shape types, multidim subscript lowering (each index
walking by pointee size rather than stride-1), untyped multidim element typing, and a long design thread on
array-dimension TYPE IDENTITY.

ONE document lives only at that tag: `docs/Impala2OpenItems.md` (a whole backlog). Read it there -
`git show Impala2-multidim-arrays-park:docs/Impala2OpenItems.md` - rather than assuming those items were
dropped. This used to name `docs/impala/MultidimensionalArrays.md` as park-only too, which sent readers to a
superseded 3.0 design requiring numeric literal dimensions: that filename is a LIVE doc on `Impala2`
describing the design that was actually built, and the parked copy is history.

Parked because the type-identity question has no clean answer. An array's dimensions want to be part of its
type so calls can be checked, but dimensions can be arbitrary expressions resolved by the assembler. That
forces either syntactic (form-dependent, incomplete) identity or numeric-literal-only dimensions. The
branch's commit messages walk through the reasoning; the decided rule was "any expression as a value,
single named const as a type", which is non-breaking against 1.0 but still unsatisfying.

Related open item: a constant evaluator is load-bearing for any 2.0 array identity.

### Re-evaluated 2026-07-26: still OUT of Impala 2.0

A deliberately MILDER revival was investigated: no by-value, no implicit decay, no multi-dim
parameters, but a multi-dim array allowed as a struct field and passed around through a struct pointer.
That shape does dodge the original trap - it occupies NO length-as-a-type position at all, so no shape
comparison, no `[<A>]` descriptors, no constant evaluator, no `:` open-axis marker, and indexing reduces
to `a[y*W + x]` folded into the existing single `dynIndex` place. It was still rejected, for reasons
that are NOT the type-identity trap:

1. ~~Expression extents in struct array fields were BROKEN~~ - **already fixed when this was written.**
   `260b57c` (2026-07-26 14:32) emits a field's folded extent before the layout block that reads it,
   with fixture `structFieldExtents`; this re-evaluation was committed 23 minutes later and recorded
   the discovery rather than the state. No longer a prerequisite.
2. An `extern struct` array field states no extent (E430), and an inner extent IS the stride, so the
   multidim SPELLING cannot state one for a host-owned struct. This was written as "extern array fields
   are sizeless" and "a matrix can be a struct field" being mutually exclusive, which overstates it: a
   host-owned matrix is indexable TODAY through a valueless `const int W;`, which the host supplies at
   assembly like any other extent. Verified:

        const int W;
        extern struct Grid { int array cells[]; int tag }   /* `[]` mandatory since 2026-08-04 */
        function get(Grid pointer g, int x, int y) returns int v { v = g->cells[y * W + x]; }
        -> MULi %0 $y #W / ADDi %0 %0 $x / ADDp %1 $g #.o.Grid.cells / PEEK $v %1 %0

   So the constraint is on the syntax's reach, not on the capability.
3. The conclusion stands, but as cost/benefit rather than impossibility. What multidim adds over
   `y*W + x` is subscript sugar plus shape typing. The sugar is available today for both struct kinds
   (see 2), and the shape typing is exactly the unsolved type-identity problem above. The feature costs
   grammar, place-model, validator and fuzzer work while delivering materially less than proposed.

Same call as by-value: the reach does not justify the surface. Do not treat multi-dim arrays as
"nearly done" because the milder design looked clean on paper.

**SUPERSEDED 2026-08-04 - the feature is IMPLEMENTED.** Kept as the record of a decision that was reversed, and
the reversal is worth reading. Point 3 is where it went wrong: it valued multidim as "subscript sugar plus
shape typing", and shape TYPING (identity between two declared shapes) is indeed still unsolved and still
3.0. But the implemented feature's payoff is neither of those - it is per-axis BOUNDS CHECKING, which
hand-striding cannot have at any price. `cells[y * W + x]` with `x >= W` is a legal word offset that
silently lands in the next row; `cells[y, x]` is `E461`, or a deferred `! FAIL`, or a `--range-checks`
trap. The 2026-07-26 argument compared syntax against syntax and never priced the check. Nothing about
type identity had to be solved to get it, which is exactly what point 3 assumed.


## Parked: by-value struct params/returns, multi-return, destructuring

    tag:      Impala3-byvalue-multireturn-park  (985bdcc)
    removed:  e6ad36d  "Park by-value struct params/returns, multi-return and destructuring"
    target:   Impala 3.0

Contains three entangled features, which is why they share one park point:

- **By-value struct parameters and returns.** `function bump(V x) returns V y`. Implemented as a transient
  CALL window: the struct's words ARE frame slots, so fields are reached frame-relative (`$x:.o.V.a`) with
  no indirection. Includes `copyStructArg`, `freeStructWindow`, struct-sized `PARA *N`, the return-window
  `ADRL`, `winBase`/`winWords` place state, and the nested-call window adoption trick (an inner call's
  output window is placed exactly where the outer argument belongs, so the return needs no copy-out).
  *(`e6ad36d` parked the feature by making E426/E427 reject it at the DECLARATOR, but left the lowering
  scaffolding in the tree, unreachable. That remainder - `freeStructWindow`, `winBase`/`winWords`, the
  return-window `ADRL`, the multi-return window, and the `E423` diagnostic, which no longer exists - was
  deleted 2026-08-07. `copyStructArg` stays: it still runs before the argument list closes on E426. The
  park TAG is the source of truth for a revival, not the working tree.)*
- **Multi-return.** `function split(int n) returns int lo, int hi`, multiple `OUT` slots, and multi-return
  function types (`functype SplitFn(int n) returns int, int`).
- **Destructuring assignment.** `lo, hi = split(n)`, including the `_` placeholder for discarded outputs.

What stayed in Impala 2.0: struct locals and globals, struct pointers, field access, `sizeof`, whole-struct
assignment and copy (`dst = src`, `*p = v`), and single named returns (`returns int r`). None of the
symbolic struct-layout work was touched.

Parked because they are the most special-cased machinery in the compiler - the transient window allocator
concentrated most of the complexity and most of the review findings, while nothing in the current corpus
needs them. Confirmed before removal: zero Impala 1.0 corpus programs used either feature, so parking them
cost no backward compatibility.

Test fixtures retired with them (also only on the branch): `multiReturn`, `structByValue`, `structReturn`,
`structValueSemantics`, and `regTransientWindow`. The last one was a fuzzer-found allocator regression whose
trigger needed BOTH parked features at once; the half of its fix that still applies (borrowForCall never
seating a call base below a live transient) stays covered by `regArrayCallWindow`. `funcType` and the
`import/` pair were rewritten to keep their real coverage (funcptr types, import-as-linking) using pointers
and out-parameters.

Rejected now with: `E426` by-value struct parameter, `E427` by-value struct return, `E428` multiple return
values (function or funcptr type), `E429` destructuring assignment. Each diagnostic carries a fix-it hint.


## Parked: `inline function`

    branch:   GAZL2                          (the GAZL 2 line, which is where the rework lives)
    removed:  see the commit naming this file
    target:   Impala 3.0, requiring GAZL 2

Contains the whole expansion machinery: body capture, per-expansion `_i<N>` label and local renaming,
argument substitution with an opaque-write marshalling scan, constant folding through a straight-line
body, transient block relocation, and the `SCOP` / `ENDS` frame scoping that places an expansion's
locals.

Parked because **an expansion needs GAZL 2 and Impala 2 must stay usable on GAZL 1.0 engines.** A 1.0
assembler rejects `SCOP` with `Unknown mnemonic`. The transient-based lowering that did work on 1.0 was
the wrong design and is not what is parked: it re-implemented allocation numerically, so an inline local
whose extent is not a number Impala knows - an `extern struct` array, `t[H * N]` - either got a wrong
frame or hit `E433`. See [`design/impala/TwoStageConstants.md`](impala/TwoStageConstants.md) for why that class of
assumption keeps failing.

The park branch is not an ancestor: `GAZL2` forked from the last Impala 2 commit that still had the
feature and carried it forward rather than freezing it. It is a working line, not an archive.

Rejected now with `E439`, whose hint points at GAZL 2 and at dropping the keyword. The feature's own
codes are retired with it and must not be reused - except `E432`, RE-ALLOCATED 2026-08-05 to the
host-owned-array rank rule. It was `E432` recursive expansion; still burned are `E433` non-literal local
extent (which the extent-naming rework deleted outright), `E434` exported inline, `E435` address of an inline
function, `E436` redeclared inline.

Retired with it: fixtures `inlineEquivalence`, `inlineEquivalenceCall`, `inlineFunctions`,
`inlineReviewArgs`, `inlineReviewCompose`, `inlineReviewControl`, `inlineReviewLocals`,
`inlineReviewTypes`; the `inlineCases` diagnostic table in `jspegCompilerTests.js`; and - the real loss
- the fuzzer's INLINE DIFFERENTIAL, the only oracle there with a reference build to compare against
(same program, keyword stripped, must print the same thing). Three silent miscompiles were caught by it.
`tests/impala/sources/import/mathlib.impala` kept its cross-unit helper as an ordinary function.

What did NOT go with it: extent constants (`design/gazl/SymbolNamespace.md`) and `SCOP` / `ENDS` are
worth having on their own, and the call-window-in-a-hole guard in `borrowForCall` is an Impala 1.0 bug
fix that stays.


## Does anything here get easier now that extents are named constants?

Asked 2026-08-01, after `.z.<name>` array extents landed (`design/gazl/SymbolNamespace.md`). Two different
questions, with two different answers. Recorded so neither is re-derived.

### Does the EXISTING `.z.` symbol help? No.

`.z.` solves exactly one problem: a value that has neither a number Impala knows NOR a name - an array
extent used to be folded into a recycled `<X>` scratch, so it existed for one line and could not be
quoted again. Neither parked feature has that problem.

**By-value structs.** Their size was never nameless: `.z.Name` has been a permanent assemble-time symbol
since struct layouts shipped, and `structAllocSize` already returns `*.z.Name`. The blocker is that
Impala's TRANSIENT allocator keys slots by integer index - `claimSlot` looks up `'%'+n`, `borrowForCall`
compares `maxFree` against `counters-1`, `copyStructArg` iterates `words` slots - and you cannot iterate
`.z.V` slots. The `SCOP`/`ENDS` remedy that fixed inline locals does NOT transfer: it places NAMED FRAME
LOCALS, and there is no assembler-side allocator for transients to delegate to at all (`%N` is an index
the emitter computes; the assembler only takes a `max`). So the fix is REWRITING Impala's allocator into
the symbolic position algebra of [`design/gazl/GAZLSymbolicWindows.md`](gazl/GAZLSymbolicWindows.md) - strictly more
machinery than today, and it trades `claimSlot`'s loud per-slot overlap assert for a discipline whose
failures are silent. The parking rationale gets stronger, not weaker.

**Multidim arrays.** `.z.` is keyed on the array OBJECT (`.z.a`, `.z.b`), so symbol identity as shape
identity would reject every pair of distinct arrays. Its value is the allocation size in WORDS, not a
per-axis element count, and there is one per array, so a rank-2 shape has nothing to compare pairwise.
It is not emitted for struct array fields or parameters - the positions the feature needs - and carries
no signature row, so `gazl-validate` cannot see it.

### Does the CONCEPT - always mint a constant, here one per DIMENSION - help? Yes, for multidim.

This is the sharper question and it has a real answer. The thread's stated limit was:

> There is no way to have BOTH "write any calculation directly" AND "sound, complete type equivalence"
> **when the values are unknown**.

The operative clause is the last one. Name every dimension and the comparison can be DEFERRED to
assembly, where the values are not unknown - so it stops being a syntactic question (the
polynomial-identity problem) and becomes an integer compare: sound, complete, any expression allowed.
The mechanism already exists and is already used this way in `src/UnitTest.gazl:30-40`:

    ! EQUi #.d.a.0 #.d.b.0 @.dim0ok
    ! FAIL f(b): axis 0 is b[N] but f expects [4]
    .dim0ok:

[`design/proofs/deferredShapeCheck.gazl`](proofs/deferredShapeCheck.gazl) is a runnable demo. `[4][3]` checked against
`[N][W-1]` PASSES when the host supplies `N=4, W=4`, and names the offending axis when it does not.
Neither Impala (it does not know `N`) nor `gazl-validate` (`arraySignaturesCompatible` is a raw
`a.size !== b.size` string compare, so `[4]` vs `[N]` is a false conflict and a folded extent publishes
as `[]` and is skipped) can decide that today. Zero runtime cost - every line is a `!` directive.

**This also makes E430 re-examinable.** That rule forbids an `extern struct` array field from stating an
extent because "a number here would be an unverifiable claim Impala never reads"
(`impala/impala.jspeg`). It is unverifiable only because nothing asks. If the host publishes a per-axis
extent the way it already publishes `.o.` and `.z.`, the claim becomes checkable, and the drift is
reported to the host that caused it. Verified to work.

**Honest costs.**

1. The error moves from compile time to ASSEMBLY time, which in GAZL means the end user's machine. Two
   mitigations: decide at compile time whatever Impala CAN decide (both sides literal) and defer only
   the rest; and where a shape genuinely depends on host constants, deferring is CORRECT, not a
   compromise - the answer differs per host.
2. It needs a new per-axis symbol namespace (one per axis per array), not `.z.`, which is one-per-array
   and in words. See `design/gazl/SymbolNamespace.md` before minting a tag.
3. `CONST_INT_P` is not forward-referencable, so both sides must be defined before the check - the same
   ordering discipline the layout blocks already follow.
4. The diagnostic is free text with no caret, though it can carry an Impala source location.
5. ~~It only pays off WITH arrays-as-values, since Impala has no array parameters today.~~ **WRONG, and it
   contradicts the design it is summarizing (corrected 2026-08-04).** A shape check needs a position where
   a DECLARED shape meets another, and `docs/impala/MultidimensionalArrays.md` §8 - "Function parameters -
   dissolved into an ordinary pointer parameter" - is explicit that this is never an array-by-value:
   `function sum(int array[W] pointer m)` is an ordinary pointer parameter whose ELEMENT carries the inner
   shape, and the park branch implemented exactly that (slice 2c/2d, `3781f8c`). No arrays-as-values, no
   array parameters, no ABI change. What the payoff really needs is shape-carrying pointer TYPES, which is
   a type-system change and nothing more. ~~The one piece worth doing independently is still fixing
   `arraySignaturesCompatible`'s form-dependence.~~ **Also wrong, and the same mistake twice (corrected
   2026-08-04.)** That raw-string extent compare is exactly what let 2.0's `int[3x4]` metadata land with
   NO validator change, verified both ways. Normalizing extents numerically would have to teach the
   validator what a shape is, and would silently equate `[3x4]` with `[12]` - two layouts over the same 12
   words that are not interchangeable. There is nothing to fix here.
6. It does nothing for by-value structs, whose blocker is the allocator, not identity.

**The generalization worth remembering:** *if the compiler cannot decide it, name both sides and let the
assembler decide it.* That is the same move `.o.`/`.z.` make for layout and extents, and
`! FAIL` is what turns it into a diagnostic rather than a silent assumption.


## GAZL 2 wishlist

### A distinct function-pointer type (suffix `t`)

`p` is currently both "data pointer" and "function pointer", which are different things - a data pointer is
a memory address, a function pointer is a declaration-order ordinal into `functionTable`, and the two live
`0x44444444` apart on purpose. Most misuse traps, but `ADDp` on a function pointer does NOT: `&one + 1` is
a valid ordinal, so it silently calls a different function. A separate type makes that unrepresentable
rather than merely detectable, because there would be no `ADDt`/`SUBt`/`LSSt` at all.

Researched and written up in [`design/gazl/GAZL2FunctionPointers.md`](gazl/GAZL2FunctionPointers.md), including the
runtime demonstration, the ~14 table entries it needs, and the suffix letters ruled out (`F` collides with
`DATf` on case alone; `a` and `e` collide with `LOCA`/`DATA` and `MOVE`).

## Impala 3.0 wishlist

The first three belong together, because they are all changes to the same calling convention. Doing them in
one pass is much cheaper than three separate ABI migrations. Collect mode is independent of the ABI work
and can land on its own. (Multidimensional arrays were the other independent item on this list and
LANDED IN 2.0 on 2026-08-04, which is exactly the independence this paragraph predicted.)

### Restore by-value structs, multi-return and destructuring

Restore from `Impala3-byvalue-multireturn-park`. The implementation there worked and was well tested; it is the
surrounding complexity that motivated parking, not a defect.

### Remove the mandatory reserved return transient

In Impala 3.0 a function has 0 to n outputs, and the input parameters follow them in the window. So with
zero outputs there is no reserved slot at all and **the first input parameter sits at `%0`**.

Impala 2.0 keeps one transient permanently reserved for the return, even for a void function: a void call is
still `CALL ^printLF %0 *1` and `main` still declares `PARA *1`. That is a leftover from Impala 1.0 not
requiring function prototypes - the caller could not know a callee's output arity, so it always reserved one
slot. Impala 2.0 does know every signature it calls (it emits and validates signature metadata), so the
reservation is no longer needed; it is kept deliberately for now rather than churn the ABI twice.

Once outputs can be 0 to n, insisting on exactly one slot for a function with no outputs makes no sense.
This touches every call site, so it wants to land with the other ABI work, not before it.

### Symbolic call windows

See `design/gazl/GAZLSymbolicWindows.md`. GAZL already supports everything needed (symbolically-indexed transients
`%<A>`, symbolic `PARA`/`CALL` extents); the blocker is that Impala's transient allocator keys slots by
integer index. Making it symbolic would let by-value structs adapt to a re-packed layout, let extern
(host-owned) structs be passed and returned by value, and make the modifiable-layout promise hold end to
end instead of stopping at the call boundary. (Not "lift E425" - there is no E425; by-value params and
returns are blocked for every struct alike by E426/E427.)

Note the dependency: this only matters once by-value structs are back. And the current numeric ABI is
CORRECT, not a stopgap - see that document before "fixing" any by-value size to `*.z.V`.

### Shape-carrying pointer types

**Multidimensional arrays are implemented and off this list** - the cost/benefit objection that used to
sit here ("multidim SYNTAX cannot state an inner extent for a host-owned struct") was answered by decision
2 in [`MultidimensionalArrays.md`](../docs/impala/MultidimensionalArrays.md): an `extern struct` field states its RANK and
the host supplies every axis, exactly as it already supplies `.o.` and `.z.`.

What 3.0 would add is `int array[W] pointer m` - a pointer whose ELEMENT carries a shape. Independent of
the ABI work above; it needs no calling-convention change and no arrays-as-values. It is the only thing
that creates a position where two DECLARED shapes meet, and so the only thing that makes the deferred
`! EQUi` + `! FAIL` identity check (proven in [`deferredShapeCheck.gazl`](proofs/deferredShapeCheck.gazl)) have a
caller. Without it that check is a mechanism with nothing to check.

### Block implicit array->pointer decay

**No park branch - this one was never built.** It is a decided *restriction*, not a parked feature, and it
is the only item on this page a 2.0 user should act on today.

    decided:  2026-07 (re-verified 2026-08-04, still unimplemented)
    status:   decay is LIVE - `makeRValue` (`impala/impala.jspeg:1532`) turns an array place used
              without a subscript into a pointer; `docs/impala/Impala2.md:193` correctly says so
    target:   Impala 3.0

The rule when it lands: an aggregate never implicitly becomes a pointer. You take the address of an array
explicitly, exactly as you already do for a struct; a bare `a` in a value context becomes an error.
`--legacy` will keep 1.0-style decay so existing `foo(a)` code still builds.

Two reasons, and note that neither is type safety. Since shape-carrying array pointer types were dropped
with the multidim work above, an explicit address and a decayed `a` are both plain `int pointer`, so decay
is no longer unsound - it is merely inconsistent:

1. **One rule for all aggregates.** Structs already require the `&`. Arrays not requiring it is a wart a
   reader has to memorize.
2. **Forward compatibility.** Keeping bare `a` an error leaves the syntax free to mean something else later
   (an array value, a copy, a bounds-carrying slice) without a breaking change.

**What to do today: write `&a[0]`, not `&a`.** `&a[0]` compiles now, is unaffected by decay, and stays
correct under the future rule, so it is the forward-proof spelling. `&a` is **not** - it is `E404 Invalid
lvalue` today (verified 2026-07-29), because taking the address of a whole array was never made an lvalue
form. That is the sting in this item: blocking decay is not a pure removal. `&a` has to become legal in the
same change, or the rule would leave no way to spell what `foo(a)` spells now.

`design/impala/ExternPrototypes.md` notes the interaction with extern prototypes that take arrays or pointers.

### Placing static data at a symbolic offset - REQUIRES GAZL 2

**No park branch - never built.** Unlike the rest of this list this is not a missing feature: it was a
pair of live **wrong-output defects**, now closed by refusing the shapes rather than guessing at them.
Restoring the capability needs GAZL 2.

Two shapes have no sound lowering on GAZL 1, and both were silently wrong before 2026-08-01:

- **`extern struct` initializers** (`E459`). `buildStructInit` walked fields in declaration order and
  emitted positional `DATA`, while every read goes through the host-supplied `.o.*`. That guesses at
  field order, at `.z.`, and at whether the host has fields Impala never saw; an array of them also
  strided elements by Impala's guess at the size. Naming the fields (`{ x: 1, y: 2 }`, `E455`) fixed how
  the source READS but not where the words LAND.
- **Fields placed BEHIND a struct array field with a symbolic extent** (`E454`). The words the array did
  not fill are a symbolic count `DATA` can neither emit nor skip, so nothing after it has a known
  position. (The array itself is no longer part of this - see below.)

Both now reject any NON-ZERO word from the unplaceable point on. Zero stays legal because it is what the
region fills under any layout, so `{ }`, an omitted field and an explicit `0` all still compile and emit
nothing. One mechanism serves both (`blockInitFrom`); they differ only in the message.

**The `E454` half shrank on 2026-08-01**, once field extents got a name. Filling the symbolic array
*itself* is sound on GAZL 1: its words start where Impala knows, and the one thing Impala could not do -
compare the value count against the extent - is now deferred to the assembler, which by then knows it.
`struct S { int a; int array v[N]; int z }` with `N` 2 given three values emits four words that fit
`1+N+1` exactly, so the assembler sees nothing wrong with the row and `z` used to receive the third
value in silence; a `! LEQi` / `! FAIL` guard above the row is what now catches it.
So `E454` no longer covers the array, only what is behind it.

Verified 2026-08-01 that GAZL 1 has no way to express it:

- no fill, repeat or origin directive exists (`docs/gazl/InstructionSet.md`);
- a FORWARD `! GOTO` assembles, and can even skip a `DATA` line;
- a BACKWARD one does not assemble at all (`Compile time label not found`), so there are no
  assemble-time loops.

`DATA` is therefore positional and append-only, and there is no way to emit *or* skip a symbolic number of
words. What GAZL 2 needs is two CAPABILITIES - **the syntax below is illustrative only and is not
decided**:

**Take a REGION, not a cursor.** The two capabilities below started out as "place the next word at a
symbolic offset (say `ORG *offset`)" plus "reject writing a word twice", and that pair does NOT close the
problem: a cursor can place a field, but the words it skipped over are still undefined, and a symbolic gap
cannot be enumerated to fill them. `ORG` alone RELOCATES the defect rather than fixing it. One primitive -
a region of `*size` words at `*offset`, ZERO unless written - subsumes both and adds what neither had:

1. **Fill falls out.** Untouched words in the region are defined without anyone counting them. This is the
   only part that makes a symbolic gap expressible at all, and it is why the cursor form is not enough.
2. **Overlap becomes an interval test.** A monotonic cursor is not sufficient - host offsets need not
   follow declaration order, so out-of-order placement has to stay legal, which otherwise forces a
   written-WORD set to stay safe. A region declaration IS that bookkeeping. It also catches an overlapping
   host layout, which `Impala2Review.md` C6 lists as undetected.
3. **Total accounting.** A struct is a region of `.z.Name` words, so every placement is checked against the
   real size instead of each field independently landing somewhere.

Cost: the assembler must hold interval state across the data section, where `DATA` is append-only today.
**BOTH HALVES ARE IMPLEMENTED (2026-08-24)**: the `SEEK` directive in the assembler and Impala emission
under `--gazl2` - see [`GAZL2DataRegions.md`](gazl/GAZL2DataRegions.md). `E459` and `E454` lift under the
flag; WITHOUT it (the GAZL 1 default) everything in this section still stands, which is why the section
stays.

**One part did NOT need GAZL 2 and is DONE as of 2026-08-01**: the value-count check behind `E454`.
Impala cannot compare a literal count against a symbolic extent, but it EMITS the comparison instead, for
the assembler to make once the extent has a value. Rule 4 of
[`TwoStageConstants.md`](impala/TwoStageConstants.md), and it costs nothing at run time. A symbolic array can be
filled again on GAZL 1 whenever the count provably fits; only fields AFTER it still need the region.

It shipped as the canonical `! FAIL` idiom, emitted ABOVE the rows it guards:

    s:      GLOB *.z.S
            ! LEQi #3 #.z.S.v @.g0
            ! FAIL too many initializer values for S.v: 3 given, room for .z.S.v
    .g0:    !
            DATA #1 #7 #8 #9

It briefly shipped (2026-08-01) as one line branching to a deliberately UNDEFINED label, so the label
name doubled as the message. That is the trick [`TwoStageConstants.md`](impala/TwoStageConstants.md) and
[`CompileTimeHardening.md`](impala/CompileTimeHardening.md) both explicitly ban, and it was corrected on
2026-08-02: an identifier cannot carry spaces, so it could not state the two counts that make the message
actionable, and it read like an internal error. The `.g<N>` skip label needs a counter that does NOT
reset per function, which is the only cost the one-line form was avoiding.

**Position is load-bearing.** GAZL checks the WHOLE allocation, not the field, so the guard must sit
above the `DATA` rows: below them a total overflow reports the coarser `Not enough space in data section`
first, and a field spill that still fits the total reports nothing at all. Verified against GAZLCmd
2026-08-02, boundary included: `words == extent` passes, and both over-fill shapes stop with the
`! FAIL` text.

**Its prerequisite landed first: a struct field extent now has a durable name.** It did not before - the
`.z.` rework named array VARIABLE extents (`.z.plain: ! DEFi #<A>`, then `GLOB *.z.plain`) but a struct
array field folded its extent into a bare scratch (`! ADDi <a> #<a> #<A>`) that `endStruct` handed
straight back, so the next struct re-used `<A>` and there was nothing stable to compare against. The
layout block now mints `.z.<Struct>.<field>` for every array field while the scratch is still live and
advances by the SYMBOL, so the extent outlives the borrow. It briefly carried its own `.x.` tag, because
`.z.<owner>.<name>` was also a local array's extent and a struct could then share a function's name. That
clash is gone: `claimTopName` now rejects every top-level name collision (`E401`), so one owner name has
exactly one kind and a single `.z.` covers both - see [`SymbolNamespace.md`](gazl/SymbolNamespace.md), and
`tests/impala/sources/structFieldExtents.impala` for the pinned layout.

**Still open, and now cheap**, because they want the same symbol: `sizeof` of an array field, and
gazl-validate checking a host-supplied extent for an `extern struct` array field (an unstated wildcard
today). The second needs the host to publish `.z.E.field` the way it already publishes `.o.E.field`,
since an extern struct mints no layout of its own.

Note the two checks are complementary, not redundant. Define-each-word-once catches an over-run only when
it lands on a word something else initialized; an array over-running into an *uninitialized* neighbour is
a legal, in-bounds, non-overlapping write, and the assembler has no notion of a struct's interior to
judge it by. Over-filling a whole section already self-reports as `Not enough space in data section`.

Rejected alternatives: **assemble-time loops** (you would emit N zero words into a region that already
zero-fills them, N can be host-supplied so module size becomes a function of a load-time define, and
assembly time stops being bounded) - the requirement is to SKIP, not to emit; and **emitting the trailing
initializers as runtime stores in a startup function**, which breaks the static-data model, costs code and
time, and cannot work for `readonly`/`CNST` at all.

Retires `E454` and `E459`, and shortens `DATA` rows for sparse initializers even with literal extents.
`E459` is the one that unlocks something genuinely new: a host-owned struct could carry static contents
for the first time, since every word would be placed at a `.o.*` the host defined rather than at a
position Impala assumed.

### Tail calls

**No park branch - never built.** Recursion works but is never eliminated, so a tail-recursive
accumulator traps on frame depth. It needs a new GAZL instruction (no existing form enters a function
without pushing a frame) *and* Impala syntax, which is why it is neither a peephole nor a grammar tweak.
Designed in [`TailCalls.md`](gazl/TailCalls.md), including a cheaper self-recursion-only first increment that
needs no ISA change.

### Full import-cycle resolution (collect mode)

**No park branch - this one was never built.** Unlike everything above, collect mode is a design that was
written and deferred, not code that was removed, so there is nothing to check out. The architecture is
`design/impala/Impala2Slices.md` Step 5 (still the plan of record) and `docs/impala/Impala2.md` "Deferred to 3.0: collect
mode"; both are complete enough to implement from.

Deferred 2026-07-29 rather than held up 2.0. Impala 2.0's answer to a backwards cross-cycle reference is a
forward `extern`, which covers the function and global cases, is checked against the real definition since
E437, and is what 1.0 users already write. Only a cross-cycle *struct type* has no workaround short of
breaking the cycle. Adding collect mode later **removes** the need for those externs without invalidating
them - an existing `extern` stays correct and keeps compiling - so this is a pure relaxation, not a
breaking change, and nothing written against 2.0 has to be revisited.

Its precondition is worth doing regardless: finishing the thinning of fat inline actions into `$$parser`
methods (`design/jspeg/RefactorPlan.md` is the adjacent cleanup on the same surface) also shrank the migration
the value model faced (`design/jspeg/JSPEGFuture.md` Problem 2, done). Do not treat collect mode as gated on that
or on the body-level AST rework - it is gated on neither.

### Precompiled `.gazl` blob imports

**No park branch - never built.** `import "lib.gazl"` parses as Impala source and fails there, because the
closure walker has exactly one way to consume a unit. Deferred to 3.0 on 2026-08-04: nothing needs it. The
import builder concatenates its units and compiles them in one pass, so an already-assembled blob has no
seam to enter through, and every use a blob would serve - sharing declarations, linking units, hiding
internals - is already served by source imports plus `export` and `--dead-strip`.

It is a relaxation, like collect mode: `import "lib.impala"` written today keeps compiling unchanged if
blob imports arrive later. The real precondition is the same architectural one - emitting units separately
instead of concatenating - so if collect mode is ever built, this becomes small. On its own it is not
worth that rework.
