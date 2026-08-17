# Future optimizations in the GAZL assembler (design note)

Status: DESIGN NOTE. Candidate optimizations that belong in the **assembler**, not in Impala. Nothing
here is implemented. Sized for its own branch.

The dividing line is what each layer knows. Impala must keep struct sizes and offsets SYMBOLIC
(`.z.Name`, `.o.Name.field`) because an `extern struct` layout is host-owned and supplied at load - see
[`design/impala/StructLayoutConstants.md`](../impala/StructLayoutConstants.md). The assembler is the first layer that holds
the resolved value, so any transform keyed on it can only happen here. That single fact drives the list.

Related but separate: [`design/FutureOptimizations.md`](../FutureOptimizations.md) covers Impala-side
candidates (dead-arm elimination after a compile-time branch, and the `expandInline` folding restriction).
[`design/gazl/TailCalls.md`](TailCalls.md) covers the one case that needs a NEW instruction rather than a
peephole - `CALL f; RETU` cannot be collapsed here, because no GAZL form can enter a function without
pushing a frame.


## What the assembler already does

From the operator table in `src/GAZL.cpp:299-306`:

| Flag | Transform |
|---|---|
| `SWAP_0_AND_1` / `SWAP_1_AND_2` | operand canonicalization for commutative ops, to shrink the effective instruction set |
| `YIELDS_CONST` | **all** source operands constant -> fold to a constant |
| `YIELDS_GOTO` | constant comparison -> `GOTO` or `NOOP` |
| `LOCAL_BOUNDS` | frame-bounds bookkeeping |
| `CHECK_DIV_BY_0` | reject a constant zero divisor |

Two gaps follow from that table:

- **Every fold requires ALL sources to be constant.** There is no transform for a variable-plus-constant
  instruction whose constant happens to be an identity. Items 1-3 below.
- **There is no control-flow peephole at all.** `GOTO_b__` carries a flag word of `0`, and there is no
  threading, chaining or collapsing logic anywhere in `src/GAZL.cpp`. Items 4-5 below.


## 1. Identity folding on `_vvc` forms

The concrete case. Impala emits, for a runtime index into a struct array:

```gazl
MULi %0 $i #.z.S        ; scale an element index to words
ADDp $q $p %0
```

`.z.S` is a `! DEFi` constant, so after resolution the assembler knows it. When the struct is one word
(`struct One { int a }`), that is `MULi %0 $i #1` - a real `MULI_VVC` at run time, multiplying by one,
on every single access. `YIELDS_CONST` does not fire because `$i` is a variable.

**Correctness.** Unconditional for the integer forms; these are exact identities on two's-complement
words, with no overflow or NaN subtlety (`imul` at `src/GAZL.cpp:1332`):

| Instruction | Constant | Becomes |
|---|---|---|
| `MULi d v #1` | 1 | `MOVi d v` |
| `MULi d v #0` | 0 | `MOVi d #0` |
| `ADDi d v #0` / `ADDp d v #0` | 0 | `MOVi` / `MOVp d v` |
| `SUBi d v #0` | 0 | `MOVi d v` |
| `DIVi d v #1` | 1 | `MOVi d v` |
| `SHLi` / `SHRi` / `SHRu` `d v #0` | 0 | `MOVi d v` |
| `IORi d v #0` / `XORi d v #0` | 0 | `MOVi d v` |
| `ANDi d v #-1` | -1 | `MOVi d v` |

(Mnemonics checked against the operator table: bitwise-or is `IORi`, and Impala's `>>>` lowers to `SHRu`
while `>>` lowers to `SHRi` - `impala/impala.jspeg:172`.)

**Float forms are NOT included.** `MULf x #1.0` is not an identity: it flushes a signalling NaN to quiet
and, on some hosts, normalizes a denormal. `ADDf x #0.0` changes `-0.0` to `+0.0`. Leave floats alone.

**Shape.** A new operator-table flag - say `IDENTITY_OP` - alongside `YIELDS_CONST`, carrying which
constant value collapses the instruction and what it collapses to. It generalizes across the ISA and
helps hand-written GAZL as much as compiler output.

**Scope of the win, honestly.** Narrow. The *constant* index case already costs nothing: `p + 1` folds to
`! MULi <A> #1 #.z.S` at assembly time, so no multiply survives. Only a **runtime** index into a
**one-word** struct array pays, which is the degenerate-struct case. It is pre-existing rather than new -
`subscriptStruct` has always emitted the same multiply for `a[i]` - and Impala reaches it from `p + i` too
since pointer arithmetic started scaling (see [`design/impala/Impala2Review.md`](../impala/Impala2Review.md)).

Do not "fix" this in Impala. The compiler must not bake `.z.S`; a normal struct's word count is known at
compile time, but an `extern struct`'s is not, and special-casing one would put a size in the output that
a host layout change could invalidate.


## 2. Dead-store and redundant-move elimination after identity folding

Item 1 turns instructions into moves, which tends to expose `MOVi %0 $i` followed by a single use of
`%0`. Coalescing that away is the natural follow-on, but it needs liveness, which the assembler does not
currently compute. Worth measuring before building: the transient slots Impala uses are short-lived and
mostly already tight.

Not a candidate on its own - only worth considering once item 1 exists and there is a benchmark showing
the moves are material.


## 3. Strength reduction on constant multiply

`MULi d v #8` -> `SHLi d v #3` for a positive power-of-two constant. Correct for the integer form, and a
plain win on hosts without a fast multiplier. Interacts with item 1 (which is the `#1` special case of the
same idea), so design them together.

Note the sign: `DIVi d v #8` -> `SHRi d v #3` is **wrong** for negative `v` under truncating division, so
the division direction is not symmetrical with multiplication. Multiply only, unless the arithmetic-shift
semantics are pinned down first.

Note also that `SHLi_vvc` / `SHRi_vvc` take `CONST_INT_P` - a POSITIVE constant - where `MULi_vvc` takes a
plain `CONST_INT`, so the rewrite has to respect the narrower operand class, not just the value.


## 4. Branch threading

`GOTO @a` where `a:` holds `GOTO @b` becomes `GOTO @b`, applied transitively to a fixpoint.

**Every hop is a real dispatch today.** Measured on this tree, 20M iterations of a loop whose body is a
chain of three `GOTO`s versus one:

| Body | Instructions/iteration | Median |
|---|---|---|
| `GOTO`->`GOTO`->`GOTO`->`FORi` | 4 | 126.4 ms |
| `GOTO`->`FORi` | 2 | 63.9 ms |

1.97x against an instruction-count ratio of exactly 2. Nothing is threaded; the cost is linear in the
chain length and is paid on every execution.

**Population is non-zero and structural.** Impala's `processBranches` does collapse chains, but the alias
map is resolved at visit time during a single bottom-to-top walk and is NOT transitive, so a chain the
walk meets in the wrong order survives into the output:

```gazl
        GOTO @second        ; goto first    <- followed one hop, then stopped
second: GOTO @done
first:  GOTO @second
done:   ...
```

`aliases[first] = second` is recorded before `aliases[second] = done` exists, and nothing re-follows.

## 5. Return duplication

`GOTO @a` where `a:` holds `RETU` becomes `RETU`. This is NOT item 4 - the target is not a branch, so
threading does not reach it.

**This one is guaranteed to occur in every program with an early exit.** Impala has no `return`
statement, so the sanctioned early-exit idiom is a `goto` to a label at the end of the body, which is
exactly where the function's `RETU` sits:

```gazl
        NEQi $r #0 @.f0     ; if (v == 0) goto out
        GOTO @out           ; goto out          <- should be RETU
.f0:    MOVi $r $v
out:    RETU
```

Correctness is unconditional: `RETU` takes no operands and does the same frame work wherever it appears,
so duplicating it changes nothing but the dispatch count. After the rewrite the label may become
unreferenced, which is a separate (and optional) cleanup.

Note this one has a competing fix at the other end: a bare `return;` statement in Impala would lower
straight to `RETU` and never emit the `GOTO` (see [`ParkedFeatures.md`](../ParkedFeatures.md)). The two are
complementary rather than alternatives - the peephole also improves every program already written, and
every hand-written GAZL module.

**Why items 4 and 5 belong here and not in Impala.** Impala's own pass can and should emit fewer of
these (see the rejected fixpoint restructure noted below), but the assembler is the layer that resolves
labels, it is the only layer hand-written GAZL passes through, and there is no layer after it - a chain
Impala leaves behind is currently paid forever. Both transforms also satisfy this document's own
admission criterion: neither changes the number of memory accesses nor the order of side effects.

**Related work on the Impala side. The NOOP half is BUILT BUT OPT-IN; the `GOTO` chains are still yours.**

A prototyped `processBranches` restructure (record aliases during the walk, then a forward pass resolving
every label operand to a fixpoint and dropping the unreferenced ones) cut corpus NOOPs 523 -> 379 but made
`Priyome.impala` fail to assemble (`Symbol not found: .f5`, cause not isolated) and was rejected.

What shipped instead is smaller and sits at the END of `processBranches`, after every alias and deletion
that pass makes has settled - so nothing re-points afterwards, which is what sank the prototype. A run of
`<--` records with nothing emitted between them all names ONE address, and only one LINE can carry a name,
so each of the others was spent on a `NOOP` that existed for no other reason (`adventCode` had seven in a
row). The run collapses onto one survivor - a user label in preference to a minted one - and every
reference is rewritten to it.

**It is `--collapse-labels`, and OFF by default (2026-08-17).** "A user label survives in preference to a
minted one, so a `goto` target never leaves the listing" holds only while at most ONE label in the run is
user-written. `calc.impala` has two - `wasFunction: ;` immediately before `repeat: {` - so `repeat`
vanished from the listing entirely and `goto repeat` emitted as `GOTO @wasFunction`. A `NOOP` costs no
cycles, so the default now spends one per label and keeps Impala:GAZL as close to 1:1 as it can; the
collapse is a size win for a `.gazl` that SHIPS and is asked for by name, beside `--dead-strip`.

Measured when it shipped, across `tests/impala/golden` (89 files, 36102 code lines): **NOOPs 516 -> 255**,
261 lines of text. What remains is not recoverable this way and should not be chased:

| remaining NOOP | count | why it stays |
|----------------|-------|--------------|
| label lands on a `!` line | 203 | a RUNTIME branch target - the line folds away and would take the label |
| switch table entry (`.sN#k`) | 52 | the case VALUE is part of the name; two entries are two addresses |

The other half of the old measurement - 99 `COMP ->A; GOTO B; A:` sites that should be `!COMP ->B` - is
untouched, and is what items 4 and 5 recover from below.

### The `.f5` failure, isolated (2026-08-03) - and it is a warning for item 4

Both attempts at threading inside Impala died on `Priyome` with `Symbol not found (in expected scope):
.f5`, and neither found the cause. It is not what the earlier notes guessed. Minimising it gives one
changed line in `checkInvariant`:

    good:  ! EQUi #DEBUG #0 @.a7
    bad:   ! EQUi #DEBUG #0 @.a14      (because `.a7:` holds `GOTO @.a14`)

**An assemble-time branch is not control flow.** `! EQUi #DEBUG #0 @L` tells the assembler to stop
emitting until it reaches `L`; the target does not mean "continue here", it delimits a REGION OF TEXT
that will not exist. Threading it to a later label silently WIDENS the skipped region, taking the label
definitions inside it - so a symbol goes missing while sitting in plain sight in the listing, and the
complaint surfaces against the NEXT function, where the scope closes with the reference still open.

Excluding those records fixes it completely (Priyome assembles, corpus 0/87 goldens). It is still not
shipped, because it then changes exactly ONE line across the whole corpus and that line is in a synthetic
fixture: Impala's backward walk already collapses every chain running in the direction it scans.

**For an assembler implementation this hazard cannot arise** - by `finalize` the `!` directives have
already been consumed and every remaining target is an instruction index. That is a second, better reason
to do item 4 here rather than in the compiler.

## 6. Not candidates

- **Anything requiring the assembler to know Impala's type model.** It sees words.
- **Reassociation or factoring across instructions.** GAZL is a transliteration target; the instruction
  sequence is the programmer's (or the compiler's) statement of intent, and the cost model that makes GAZL
  predictable depends on not silently rewriting it. Peephole identities are acceptable because they never
  change the number of memory accesses or the order of side effects; general reassociation would.
- **Anything that changes float results.** See the note under item 1.


## Measuring first

Before any of this, get a number. `tools/bench.sh` / `.cmd` and `tools/genbench.sh` exist; the honest
first step is to count how often the affected shapes actually occur in `tests/impala/golden/*.gazl` and in
real firmware, rather than assuming the multiply matters. Item 1's whole reachable surface is one-word
structs with runtime indices, which may well be zero programs today.

**Items 4 and 5 are the exception - their populations are counted.** Across the 94 fixtures in
`tests/impala/golden/*.gazl`:

| | count | share of all `GOTO`s |
|---|---|---|
| `GOTO` instructions | 1174 | - |
| target is another `GOTO` (item 4) | 34 | 2.9% |
| target is a `RETU` (item 5) | **271** | **23.1%** |
| **removable** | **305** | **26.0%** |

**A quarter of every `GOTO` in the corpus is removable, and item 5 is almost all of it** - which is the
forced early-exit idiom showing up exactly as predicted. If only one thing on this page gets built, build
item 5. It is the simplest transform here, it is unconditionally correct, and it is the only one whose
benefit is already demonstrated on real programs rather than argued.
