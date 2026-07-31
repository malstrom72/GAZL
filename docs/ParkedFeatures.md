# Parked features and where to find them

Status: INDEX. Start here when you wonder "didn't we already build that?"

Some Impala 2.0 features were designed, implemented, reviewed, and then deliberately taken back out. The
work is not lost - each one is preserved on its own branch. This file says what is parked, where it lives,
and why it was parked.


## The parking convention

1. Create a branch at the LAST commit that still contains the feature:

        git branch <park-branch> <commit>

2. Land a rollback commit on the working branch whose message names the branch.

So a park branch is normally an ancestor of the working branch (the removal happened afterwards on the same
line). To see the feature, check the branch out; to see how it was removed, read the rollback commit.

Do not delete these branches. They are the only copy.


## Parked: multidimensional arrays / arrays as values

    branch:   Impala2-multidim-arrays        (tip 4cd52f2)
    removed:  dda4129  "Roll back multidim arrays (slices 1-2) to pre-multidim compiler"
    target:   Impala 3.0

Contains slices 1-2 of multidimensional array support: shape types, multidim subscript lowering (each index
walking by pointee size rather than stride-1), untyped multidim element typing, and a long design thread on
array-dimension TYPE IDENTITY.

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

1. Expression extents in struct array fields were BROKEN (see the ordering trap in
   [[docs/StructLayoutConstants.md]]) - a prerequisite fix, not part of the feature.
2. An `extern struct` array field states no extent (E430). An inner extent IS the stride, so a sizeless
   field cannot be indexed at rank >= 2. "Extern array fields are sizeless" and "a matrix can be a
   struct field" are mutually exclusive for host-owned structs.
3. Therefore the whole selling point - "put a matrix in a struct and pass it that way" - cannot reach
   host-owned structs. The feature costs grammar, place-model, validator and fuzzer work while
   delivering materially less than proposed.

Same call as by-value: the reach does not justify the surface. Do not treat multi-dim arrays as
"nearly done" because the milder design looked clean on paper.


## Parked: by-value struct params/returns, multi-return, destructuring

    branch:   Impala3-byvalue-multireturn    (tip 985bdcc)
    removed:  e6ad36d  "Park by-value struct params/returns, multi-return and destructuring"
    target:   Impala 3.0

Contains three entangled features, which is why they share one branch:

- **By-value struct parameters and returns.** `function bump(V x) returns V y`. Implemented as a transient
  CALL window: the struct's words ARE frame slots, so fields are reached frame-relative (`$x:.o.V.a`) with
  no indirection. Includes `copyStructArg`, `freeStructWindow`, struct-sized `PARA *N`, the return-window
  `ADRL`, `winBase`/`winWords` place state, and the nested-call window adoption trick (an inner call's
  output window is placed exactly where the outer argument belongs, so the return needs no copy-out).
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
frame or hit `E433`. See [`docs/TwoStageConstants.md`](TwoStageConstants.md) for why that class of
assumption keeps failing.

The park branch is not an ancestor: `GAZL2` forked from the last Impala 2 commit that still had the
feature and carried it forward rather than freezing it. It is a working line, not an archive.

Rejected now with `E439`, whose hint points at GAZL 2 and at dropping the keyword. The feature's own
codes are retired with it and must not be reused: `E432` recursive expansion, `E433` non-literal local
extent (which the `.x.` rework deleted outright), `E434` exported inline, `E436` redeclared inline.

Retired with it: fixtures `inlineEquivalence`, `inlineEquivalenceCall`, `inlineFunctions`,
`inlineReviewArgs`, `inlineReviewCompose`, `inlineReviewControl`, `inlineReviewLocals`,
`inlineReviewTypes`; the `inlineCases` diagnostic table in `jspegCompilerTests.js`; and - the real loss
- the fuzzer's INLINE DIFFERENTIAL, the only oracle there with a reference build to compare against
(same program, keyword stripped, must print the same thing). Three silent miscompiles were caught by it.
`tests/impala/sources/import/mathlib.impala` kept its cross-unit helper as an ordinary function.

What did NOT go with it: `.x.` extent constants (`docs/SymbolNamespace.md`) and `SCOP` / `ENDS` are
worth having on their own, and the call-window-in-a-hole guard in `borrowForCall` is an Impala 1.0 bug
fix that stays.


## Impala 3.0 wishlist

The first three belong together, because they are all changes to the same calling convention. Doing them in
one pass is much cheaper than three separate ABI migrations. Multidimensional arrays and collect mode are
independent of the ABI work and of each other, and can land on their own.

### Restore by-value structs, multi-return and destructuring

Restore from `Impala3-byvalue-multireturn`. The implementation there worked and was well tested; it is the
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

See `docs/GAZLSymbolicWindows.md`. GAZL already supports everything needed (symbolically-indexed transients
`%<A>`, symbolic `PARA`/`CALL` extents); the blocker is that Impala's transient allocator keys slots by
integer index. Making it symbolic would let by-value structs adapt to a re-packed layout, lift `E425` so
extern (host-owned) structs can be passed and returned by value, and make the modifiable-layout promise
hold end to end instead of stopping at the call boundary.

Note the dependency: this only matters once by-value structs are back. And the current numeric ABI is
CORRECT, not a stopgap - see that document before "fixing" any by-value size to `*.z.V`.

### Multidimensional arrays

Restore from `Impala2-multidim-arrays`. Independent of the ABI work above - it needs no calling-convention
change. Two things must be settled FIRST, and neither is part of the feature itself: array-dimension type
identity (a constant evaluator is load-bearing here), and the expression-extent ordering trap in struct
array fields. The 2026-07-26 re-evaluation above also stands: the "matrix as a struct field" shortcut does
not reach `extern struct`, because a sizeless host-owned array field has no stride to index by.

### Full import-cycle resolution (collect mode)

**No park branch - this one was never built.** Unlike everything above, collect mode is a design that was
written and deferred, not code that was removed, so there is nothing to check out. The architecture is
`impala/Impala2Slices.md` Step 5 (still the plan of record) and `docs/Impala2.md` "Deferred to 3.0: collect
mode"; both are complete enough to implement from.

Deferred 2026-07-29 rather than held up 2.0. Impala 2.0's answer to a backwards cross-cycle reference is a
forward `extern`, which covers the function and global cases, is checked against the real definition since
E437, and is what 1.0 users already write. Only a cross-cycle *struct type* has no workaround short of
breaking the cycle. Adding collect mode later **removes** the need for those externs without invalidating
them - an existing `extern` stays correct and keeps compiling - so this is a pure relaxation, not a
breaking change, and nothing written against 2.0 has to be revisited.

Its precondition is worth doing regardless: finishing the thinning of fat inline actions into `$$parser`
methods (`impala/RefactorPlan.md` is the adjacent cleanup on the same surface) also shrinks the migration
that "JSPEG 2" would face (`docs/JSPEGFuture.md` Problem 2). Do not treat collect mode as gated on JSPEG 2
or on the body-level AST rework - it is gated on neither.
