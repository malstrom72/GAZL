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

Contains slices 1-2 of multidimensional array support: shape types, multidim subscript lowering (each index
walking by pointee size rather than stride-1), untyped multidim element typing, and a long design thread on
array-dimension TYPE IDENTITY.

Parked because the type-identity question has no clean answer. An array's dimensions want to be part of its
type so calls can be checked, but dimensions can be arbitrary expressions resolved by the assembler. That
forces either syntactic (form-dependent, incomplete) identity or numeric-literal-only dimensions. The
branch's commit messages walk through the reasoning; the decided rule was "any expression as a value,
single named const as a type", which is non-breaking against 1.0 but still unsatisfying.

Related open item: a constant evaluator is load-bearing for any 2.0 array identity.


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


## Impala 3.0 wishlist

These belong together, because they are all changes to the same calling convention. Doing them in one pass
is much cheaper than three separate ABI migrations.

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
