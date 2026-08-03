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


## GAZL 2 wishlist

**The criterion: does it fit `struct Instruction`?** (`src/GAZL.h:87`)

```cpp
struct Instruction { Int opcode; Value p0; Value p1; Value p2; };
```

Opcode plus exactly THREE operand slots, one fixed record for every instruction in every program. A
candidate needing a fourth operand does not cost "one more field" - it widens every instruction ever
assembled, to serve whichever one form asked. So sort candidates by which side of that line they fall on:

- a NEW OPCODE reusing the three slots is cheap (an enum entry and a table row);
- an ASSEMBLE-TIME directive or `!` meta-instruction is cheaper still - it never reaches the VM;
- a fourth operand is a redesign, and needs a reason that outweighs the size of every other instruction.

The items below are ordered by that rule, cheapest first. One rejected candidate is kept at the end
because the reasoning is the useful part.

### A region directive for static data at a symbolic offset

`DATA` is positional and append-only, and GAZL 1 has no fill, repeat or origin directive - verified
against `docs/InstructionSet.md`, plus: a FORWARD `! GOTO` assembles and can even skip a `DATA` line, but
a BACKWARD one does not assemble at all (`Compile time label not found`), so there are no assemble-time
loops either. There is therefore no way to emit *or* skip a symbolic number of words.

That blocks initializing anything whose layout is only known at assembly - a host-owned (`extern`) struct,
or a field sitting behind a struct array field with a symbolic extent. The words such a field does not
fill are a symbolic count, so nothing after it has a known position.

**Take a REGION, not a cursor.** An `ORG *offset` that merely moves a write cursor RELOCATES the problem
instead of solving it: it can place a field, but the words it skipped over are still undefined, and a
symbolic gap cannot be enumerated to fill them. Declaring a region of `*size` words at `*offset`, ZERO
unless written, gets three things a cursor does not:

1. **Fill falls out.** Untouched words in the region are defined without anyone counting them. This is the
   only part that makes a symbolic gap expressible at all, and it is the whole reason the cursor form is
   not enough.
2. **Overlap is an interval test.** A monotonic cursor is not sufficient - host offsets need not follow
   declaration order, so out-of-order placement has to stay legal, which otherwise forces a written-WORD
   set to stay safe. A region declaration IS that bookkeeping, and it also catches an overlapping host
   layout, which nothing detects today.
3. **Total accounting.** The whole struct is a region of `.z.Name` words, so every placement is checked
   against the real size rather than each field independently landing somewhere.

Cost: the assembler must hold interval state across the data section, where `DATA` is append-only today.
That is the same cost the written-word set would have charged.

> The compiler-side analysis (which shapes are refused today, and why naming the fields fixed how the
> source READS but not where the words LAND) lives on the `Impala2` branch, which is ahead of this one -
> it arrives at the next merge.

### `! FAILIF` - one meta-instruction for a deferred assertion

Every assertion the compiler defers to assembly is the same three lines - compare, fail, skip label:

```gazl
! LSSi #K #.z.Test.b @.g0
! FAIL index K outside Test.b
.g0:
```

Collapsing that to `! FAILIF <cmp> a b "text"` is not just a third of the shipped text. The LABEL is what
costs: it forces the emitter to decide whether a label may sit on a line that folds away (an assemble-time
branch resolves against it, a runtime `GOTO` reports `Symbol not found` once the line is gone), and
whether one that may not needs a `NOOP` to land on. A form with no label retires that whole question.
Nothing reaches the VM, so this is the cheapest item here.

### Unsigned comparisons (`LSSu` and friends)

There are none: every comparison is `f`/`i`/`p` only, and `SHRu` is the single `u` in the instruction set,
so the suffix convention exists but was never applied to comparisons. `(unsigned)k < extent` tests BOTH
ends of a range in one comparison, halving every bounds check - the compiler currently emits two, one per
end, because it has no other way to say it. New opcodes reusing the three slots, so it fits the record.

Note the VM already relies on exactly this trick internally: `GETL_VVV` (`src/GAZL.cpp`) compares the
index unsigned so a negative one wraps huge and traps rather than stepping backwards.

### A distinct function-pointer type (suffix `t`)

`docs/InstructionSet.md` already states the contract: a function pointer is an opaque ordinal, and only
equality and calling are defined on it. GAZL 1 cannot ENFORCE that, because `p` covers both data pointers
and function pointers - so `ADDp` on a function pointer assembles, and does not trap, and silently calls a
different function (`&one + 1` is a valid ordinal). A `t` type with no arithmetic or ordering forms makes
those operations unrepresentable instead of undefined.

Written up in [`docs/GAZL2FunctionPointers.md`](GAZL2FunctionPointers.md), with the runtime demonstration,
the ~14 table entries needed, and the suffix letters ruled out. The sites that must change carry
`GAZL 2:` comments pointing back at it.

### REJECTED: bounding `GETL`/`SETL` by the object instead of the stack

Kept because the premise is genuinely tempting and the refutation is the useful part.

`docs/MemorySafetyModel.md` notes that a dynamic index is bounded by `dataStackEnd`, NOT by the object it
indexes - which is why a struct array field overrun stays inside the frame and nothing traps. The compiler
therefore emits its own comparisons to approximate a check the VM is already performing. Worse, the VM's
bound is not even a constant:

```cpp
case GETL_VVV: if ((ui = V2.i) < (UInt)(dataStackEnd - dsp - C1.i)) { V0 = (dsp + C1.i)[ui]; ... }
```

That subtraction runs on EVERY access. Handing the instruction the object's extent - which the assembler
knows, as a `.z.` symbol - would be strictly cheaper at run time AND correct, so it reads like a free win.

**It is not, because the extent has nowhere to live.** `GETL` already uses all three slots: `V0` dest,
`C1` constant base offset, `V2` index. An extent is a fourth, and by the criterion at the top of this
section that widens every instruction in every program to bounds-check dynamic indices. Packing base and
extent into `p1` as two halves is the only form that fits, and it caps both to half a `Value`.

If this is ever revisited, the question to answer FIRST is where the extent lives - not whether the check
is worth having. The check is obviously worth having; the encoding is the entire problem.

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
