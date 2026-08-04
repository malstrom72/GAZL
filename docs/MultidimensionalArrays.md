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

`.d` is unused in [`SymbolNamespace.md`](SymbolNamespace.md) and must be registered there before minting.
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

**Slice 2 - standalone arrays.** `global int array grid[H, W]` and locals. Object-keyed `.d.`, so checking
covers the owning scope only. Note these lower through `binaryOp('=[]')`, a *different* path that never
builds a place - the Horner reduction has to be shared between the two rather than written twice.

**Slice 3 - shape identity across standalone arrays.** The deferred `! EQUi` compare. Needs a position
where two shapes meet, which needs shape-carrying pointer types or a named shape - the largest step, and
the only one that touches the descriptor grammar.

**Not in scope at all:** arrays by value, array parameters, `:` open axes and slices, `&a` as an lvalue,
nested-brace initializers, C-style `a[y][x]` chains.

## Decisions (2026-08-04)

**1. Row-crossing is REJECTED, not defined.** A matrix is one flat object, so `cells[0, W]` addresses
`cells[1, 0]`, and the park design chose to define that as legal on the grounds that GAZL is a flat machine
with no aliasing optimisation. Bounds checking rejects it instead: the check is **per axis**, not against
the flat total. Checking only the total would let `x >= W` through, which is exactly the row-overflow bug
the feature exists to catch - a shape that cannot catch it buys syntax and nothing else.

**2. An `extern struct` matrix field names its axes with host-supplied consts.**

```impala
const int W;
const int H;
extern struct Grid { int array cells[H, W]; int tag }
```

`E430` relaxes from a blanket ban to "no LITERAL extent". Its own rationale is what licenses this - the
rule reads *"a number here would be an unverifiable claim Impala never reads (offsets are host-supplied
`.o.` symbols, 1-D stride is 1)"*, and both clauses are conditional. A host-supplied `const int W;` is not
a number Impala knows, it is a reference to the host's own symbol exactly like `.o.` and `.z.`, so no claim
is made and nothing is unverifiable. And "1-D stride is 1" is precisely *why* the extent was never needed;
at rank 2 the stride IS the inner extent, so the premise expires. For a multi-dim extern field, naming the
axes is therefore not merely permitted but **required** - without the inner extent the stride cannot be
formed at all.

The machinery is already in place: a normal struct field with a symbolic extent emits `.z.S.b: ! DEFi #W`
and accumulates `#.z.S.b` into the layout, all resolved by the host at load. So the host ends up owning the
layout AND the shape, consistently, and the per-axis bounds checks defer to assembly (`! LSSi #k #W` /
`! FAIL`) on the tier that already exists.

**3. Axes are separated by `x` in metadata, never by a comma.** `cells : int[4x5]`.
`tools/gazl-validate.nuxjs.js:299` splits struct fields on `,`, so `cells : int[4, 5]` would parse as two
bogus fields and report a spurious cross-unit conflict. The park branch used `4x5` for this reason. Its
`arraySignaturesCompatible` also compares extents as raw strings, which is worth fixing on its own merits
and independently of this feature.
