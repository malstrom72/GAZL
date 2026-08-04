# Multidimensional arrays (Impala 2.0 design)

Status: **DESIGN, targeting 2.0.** Supersedes the 2026-07-26 "still OUT of 2.0" re-evaluation in
[`ParkedFeatures.md`](ParkedFeatures.md), whose central objection - that array-dimension type identity is
unsolved - does not apply to the design below. A same-named document existed on the park branch
`Impala2-multidim-arrays` for the 3.0 design; that one required numeric literal dimensions and predates
the current place model, so it is history, not a plan. Read it with `git show
Impala2-multidim-arrays:docs/MultidimensionalArrays.md` for the questions it already answered (slice
semantics, shape-pointer typing, `:` open axes) - none of which are in scope here.

Every GAZL fragment quoted below was produced by the shipping compiler, not written by hand.

## The rule

One subscript, a comma list, striding by the declared element - the same rule the single subscript already
follows since `[[ ]]` was removed:

```
struct Grid { int array cells[H, W]; int tag }

function get(Grid pointer g, int y, int x) returns int v
{
	v = g->cells[y, x];
}
```

`cells[y, x]` lowers to the linear index `y * W + x` (Horner over the axes) and then to exactly the access
a hand-written `cells[y * W + x]` emits today. **No new VM instructions, no new lowering path** - the
existing subscript machinery takes one linear index and always has.

## `.d.` - one constant per axis

An array's extents become named assemble-time constants, one per axis, beside the `.z.` that already names
its total size in words:

    .d.Grid.cells.0     ! DEFi #W       axis 0 - stride 1, the fastest-varying
    .d.Grid.cells.1     ! DEFi #H       axis 1 - stride .d.Grid.cells.0
    .z.Grid.cells       ! DEFi #<a>     total words, as today = product of all axes

**Axes are numbered by stride, innermost first.** Axis *k*'s stride is the product of axes `0..k-1`, so
the numbering is independent of how the dimension list is written and the stride formula is uniform. For
`cells[30, 20, 10]`, axis 0 is 10, axis 1 is 20, axis 2 is 30, and `.z.` is their product.

This is the same move `.o.` and `.z.` already make for struct layout: *if the compiler cannot decide it,
name it and let the assembler decide it.* A dimension may be a literal, a named `const`, a host-supplied
valueless `const int W;`, or any expression - the constant is what makes it referenceable more than once.
Where an axis is already a single referenceable operand (a literal, or a named const) there is nothing to
mint and the extent is referenced directly.

`.d` is registered in [`SymbolNamespace.md`](SymbolNamespace.md), in all three of its path shapes
(`.d.<Struct>.<field>.<k>`, `.d.<global>.<k>`, `.d.<function>.<local>.<k>` - the same three `.z.` has).
It inherits `.z.`'s soundness condition, which is enforced and now has tests: a top-level name has exactly
one kind, so `.d.S.*` can never mean two things. Verified - `struct S` + `global int array S[4]`,
`struct main` + `function main`, `functype F` + `global F`, `const K` + `global array K` are all `E401`.

## Object-keyed versus type-keyed, and why struct fields come first

This is the distinction the whole design turns on, and it is inherited from `.z.`, which is *already* both:

| Symbol | Keyed on | Shared by |
|---|---|---|
| `.z.Voice` | a TYPE | every `Voice` |
| `.z.Grid.cells` | a TYPE (the struct owns the field) | every `Grid` |
| `.z.grid` | an OBJECT (a global array) | that array only |
| `.z.main.buf` | an OBJECT (a local array) | that local only |

So for a **struct field**, per-axis dimensions are type-keyed - `.d.Grid.cells.0` belongs to `Grid`, not to
any particular `Grid`. Two `Grid pointer`s trivially agree on the shape at compile time, by name, with no
comparison to make. For a **standalone array**, they are object-keyed: `.d.a.0` and `.d.b.0` are different
symbols even when both are 4, so shape *identity* between two standalone arrays can only be decided by
comparing values at assembly time (`! EQUi` / `! FAIL`, see
[`deferredShapeCheck.gazl`](deferredShapeCheck.gazl)) - which is sound, complete and free, but is a later
slice and is not needed for anything below.

**The consequence that orders the work: an extent already survives a call boundary when, and only when, it
is type-keyed.** Verified against the shipping compiler:

```
struct Grid { int array cells[12]; int tag }
function readBad(Grid pointer g) returns int v { v = g->cells[99]; }
```
```
error[E461]: Index 99 is outside the extent 12 of this array
```

That fires inside a function which received nothing but a pointer, and `--range-checks` guards the runtime
index against `.z.Grid.cells` in the same position. Meanwhile a bare array parameter does not exist at all
(`int array a[4]` as a parameter is `E001`, with or without an extent), and a plain `int pointer p`
parameter carries no extent and gets no guard, by design.

So a matrix **inside a struct** is checked across a call boundary, where a standalone 1-D array today is
not. Multi-dim as a struct field is not a compromise shape - it is the shape with the strongest guarantee.

## Slices

**Slice 1 - struct fields.** `int array cells[H, W]` as a struct field; `g->cells[y, x]` and
`s.cells[y, x]`; `.d.<Struct>.<field>.<k>` minted in the layout block; per-axis `E461`, per-axis deferred
`! FAIL`, per-axis `--range-checks`. Self-contained: it lowers through `subscriptStruct`, which already has
the `offParts`/`dynIndex` machinery a Horner fold needs, and it needs no type-system change whatsoever.

**Slice 2 - standalone arrays.** IMPLEMENTED. `global int array grid[H, W]` and locals, with the same
three tiers and the same `x`-joined metadata; `.d.` is object-keyed here (`.d.grid.0`, `.d.main.b.0`), so
checking covers the owning scope only. These lower through `binaryOp('=[]')`, a *different* path that
never builds a place, and the Horner reduction is shared rather than written twice: `foldAxes` produces
the one linear index both paths already took, and the only thing each path adds is draining the per-axis
findings. Verified end to end - the deferred `! FAIL` on `sym[2, 5]` fires under `GAZLCmd`, the in-range
control runs, and `tests/impala/sources/multidimArrays.impala` covers a global shape, a local shape,
symbolic axes and a 1-D array beside them.

The slice turned up a hole that was in slice 1 too, and it is the more important half of the work: **the
rank check only ran when a comma was written**, so `cells[11]` on a `[3, 4]` compiled silently as a flat
word offset. That is row-crossing walking back in through the spelling with no comma in it - decision 1
below rejects `cells[0, W]` and said nothing about `cells[11]`, which is the same address. A subscript now
states every axis on both paths and for one index as much as for several (`E206`); an array with no axes
is rank 1, so ordinary arrays and bare pointers are untouched.

Making that mandatory is also what retired two *duplicate* checks a shape was carrying. The per-axis
checks strictly imply the flat one - `.z.` is the product of the axes, so no index that cleared every axis
can fail against `.z.` - so a shaped subscript no longer emits the flat `--range-checks` compare pair, nor
the flat deferred `! LSSi` / `! FAIL` triple and the `! MOVi` guard copy that fed it. Four lines of shipped
GAZL per shaped access, restating a settled question. This was sound only once every axis had to be
stated; before the rank fix, `cells[11]` cleared no axis at all.

**Slice 3 - shape identity across standalone arrays. DEFERRED TO 3.0, and it is not a gap.** The deferred
`! EQUi` compare in [`deferredShapeCheck.gazl`](deferredShapeCheck.gazl) works exactly as documented -
re-run all three ways on 2026-08-04: `[4][3]` against `[N][W-1]` passes with `N 4 W 4`, and each mismatch
names its own axis, at zero runtime cost. **What it has no caller.** Its scenario is "`f` takes an
`int[4][3]`, the call passes `b`", and Impala cannot express that: an array parameter does not exist
(`int array a[4]` as a parameter is `E001`, with or without an extent), a pointer parameter carries no
extent, and arrays by value are on the not-in-scope list below.

So there is **no position in 2.0 where two standalone shapes meet.** The one position where two shapes do
meet - a shape reached through a `Grid pointer` - is answered for free by being type-keyed: both sides
name `Grid`, so there is nothing to compare. Slice 3 is a sound mechanism waiting on a prerequisite that
is a language change rather than a slice: shape-carrying pointer TYPES (`int array[W] pointer m`, which
the park branch built as slice 2c/2d, `3781f8c`). Treat this section as a conditional design - *if* 3.0
adds those types, this is the sound way to check them, with a running proof - and not as pending work.

**Not in scope at all:** arrays by value, array parameters, `:` open axes and slices, `&a` as an lvalue,
nested-brace initializers, C-style `a[y][x]` chains.

## Decisions (2026-08-04)

**1. Row-crossing is REJECTED, not defined.** A matrix is one flat object, so `cells[0, W]` addresses
`cells[1, 0]`, and the park design chose to define that as legal on the grounds that GAZL is a flat machine
with no aliasing optimisation. Bounds checking rejects it instead: the check is **per axis**, not against
the flat total. Checking only the total would let `x >= W` through, which is exactly the row-overflow bug
the feature exists to catch - a shape that cannot catch it buys syntax and nothing else.

Rejecting `cells[0, W]` is only half of it, and the half that was easy to see. `cells[W]` is the SAME
ADDRESS written without a comma, and it compiled silently until the rank check was made unconditional (see
slice 2 above). A per-axis rule that any subscript can opt out of by dropping a comma is not a rule.

**2. An `extern struct` matrix field states its RANK; the host supplies every axis. `E430` stays.**

An earlier draft of this section had it backwards and proposed relaxing `E430` so the field could name its
axes with host-supplied consts. That was built, tested, and **reverted the same hour**, because the premise
was false. Recording why, since it is the whole answer:

A sizeless `extern struct` array field is **already bounds-checked**. Impala emits the deferred guard and
references a `.z.<Struct>.<field>` that the HOST supplies, exactly as it supplies `.o.` and `.z.`:

```
! LSSi #9999 #.z.H.b @.g0
! FAIL index 9999 outside H.b (resolved only at assembly)
```

Verified end to end - with `.z.H.b 4` the assembler rejects `h->b[9999]`; with `.z.H.b 20000` the same
module assembles and runs. So the extent is not missing, it is host-owned and already published, and
`E430`'s "the host owns this layout" is exactly right. Letting Impala also define `.z.H.b` produces
`Symbol already defined: .z.H.b` - measured, not predicted.

The multidimensional form therefore follows the contract that already exists. The declaration states how
many axes there are and nothing else; the host publishes `.d.H.cells.0`, `.d.H.cells.1` and `.z.H.cells`
together, and the per-axis checks defer against those symbols:

```impala
extern struct Grid { int array cells[,]; int tag }     /* spelling TBD - two axes, host-supplied */
```

Open sub-question: the spelling for "rank 2, extents host-supplied". `[,]` reads as an empty comma list and
needs no new token, but it is easy to misread. It must not state numbers, since that is the claim `E430`
correctly refuses.

**The lesson, worth more than the feature:** "compiles clean" was measured by counting `error` lines, and a
deferred `! FAIL` is not an error line. A check that lives in the emitted GAZL is invisible to that test.
Read the output, not the exit status.

**3. Axes are separated by `x` in metadata, never by a comma.** `cells : int[4x5]`. IMPLEMENTED.
`tools/gazl-validate.nuxjs.js:299` splits struct fields on `,`, so `cells : int[4, 5]` would parse as two
bogus fields and report a spurious cross-unit conflict. The park branch used `4x5` for this reason.

`x` also keeps a shape one `\S+` token, which is what let the validator take it **with no change at all**:
`arraySignaturesCompatible` compares extents as RAW STRINGS, so `int[3x4]` matches `int[3x4]` and conflicts
with both `int[2x6]` and `int[12]` - the last being exactly right, since two shapes over the same 12 words
are not interchangeable. An earlier draft of this file called that raw-string comparison "worth fixing on
its own merits"; it is the opposite. Normalizing extents numerically would have to teach the validator what
a shape is, and would silently equate `[3x4]` with `[12]`. Verified against both spellings.

A shape is stated ALL-OR-NOTHING, as a single extent already is: one axis Impala cannot name as a constant
and the whole field renders `int[]`, "cannot state this". Half a shape read as a whole one is a false
claim, and `[]` is the one the validator already skips rather than assumes equal.
