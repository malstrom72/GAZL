# Future optimizations in the GAZL assembler (design note)

Status: DESIGN NOTE, plus one item now BUILT. Optimizations that belong in the **assembler**, not in
Impala. Items 1-3 are candidates; item 4 (branch threading and return duplication) shipped 2026-08-04.

The dividing line is what each layer knows. Impala must keep struct sizes and offsets SYMBOLIC
(`.z.Name`, `.o.Name.field`) because an `extern struct` layout is host-owned and supplied at load - see
[`docs/StructLayoutConstants.md`](StructLayoutConstants.md). The assembler is the first layer that holds
the resolved value, so any transform keyed on it can only happen here. That single fact drives the list.

Related but separate: [`docs/FutureOptimizations.md`](FutureOptimizations.md) covers Impala-side
candidates (dead-arm elimination after a compile-time branch, and the `expandInline` folding restriction).


## What the assembler already does

From the operator table in `src/GAZL.cpp:299-306`:

| Flag | Transform |
|---|---|
| `SWAP_0_AND_1` / `SWAP_1_AND_2` | operand canonicalization for commutative ops, to shrink the effective instruction set |
| `YIELDS_CONST` | **all** source operands constant -> fold to a constant |
| `YIELDS_GOTO` | constant comparison -> `GOTO` or `NOOP` |
| `LOCAL_BOUNDS` | frame-bounds bookkeeping |
| `CHECK_DIV_BY_0` | reject a constant zero divisor |

The gap: **every fold requires ALL sources to be constant.** There is no transform for a
variable-plus-constant instruction whose constant happens to be an identity.


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
since pointer arithmetic started scaling (see [`docs/Impala2Review.md`](Impala2Review.md)).

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


## 4. Branch threading and return duplication - IMPLEMENTED 2026-08-04

Two control-flow peepholes, in `Assembler::threadBranches()`, called from `finalize` once every symbol is
resolved:

- **Threading.** A branch whose target leads to a `GOTO` adopts that `GOTO`'s target, followed to a
  fixpoint. Every hop is a real dispatch: measured 20M iterations of a loop body of three chained `GOTO`s
  against one, 126.4 ms vs 63.9 ms - 1.97x against an instruction ratio of exactly 2. Nothing short-
  circuits a chain at run time.
- **Return duplication.** `GOTO @a` where `a:` holds `RETU` becomes `RETU`. Not the same transform - the
  target is not a branch, so threading cannot reach it. Unconditional: `RETU` takes no operands and does
  the same frame work wherever it stands.

**Why it is safe, and why it is cheap.** Both rewrite an opcode or a displacement IN PLACE. No address
moves, so nothing needs re-patching - which is equally the reason the removal variants (dropping the now-
dead `GOTO`, or a label left unreferenced) are NOT done: those shift every later address and invalidate
every resolved branch and the function table.

The branch operand is read from the same `OPERATORS` table the assembler parses with, so a future
branching opcode is covered by declaring its operand `BRANCH` and nothing else; all 35 carry it in one
consistent slot. **`SWCH` is excluded by name**: its third operand looks identical to the table but is a
jump TABLE base in memory (`ip += mb[C2.p + index].i`), not a displacement.

**Why here rather than in Impala.** Impala tried this twice and failed both times, on the same fixture
with the same symbol (`Priyome`: `Symbol not found (in expected scope): .f5`). The cause is that
`! EQUi #DEBUG #0 @L` is not control flow at all - it tells the assembler to stop EMITTING until it
reaches `L`, so its target delimits a region of text rather than naming a continuation. Threading it
widens the skipped region and swallows the label definitions inside, which surfaces as a symbol that is
missing while plainly present in the listing. Down here that hazard cannot arise: those directives are
consumed while assembling and never reach the code array. The assembler is also the only layer hand-
written GAZL passes through, and there is no layer after it.

**Verified**: 34 branch opcodes recognised (35 less `SWCH`); a hand-written 3-hop chain collapses to two
threads plus one `GOTO`->`RETU`; goldens 0/94 with 26 fixtures assembled AND run.

## 5. Not candidates

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
