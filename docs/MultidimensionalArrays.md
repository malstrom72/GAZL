# Multidimensional Arrays in Impala 2.0 (design draft)

Status: DRAFT / design discussion. Nothing here is implemented yet. The one-dimensional
`type array name[N]` form and the jagged `type pointer array` form described under "What Impala
already has" DO exist today; contiguous multidimensional arrays do not.

## 1. Goal and non-goals

Goal: support contiguous, rectangular, fixed-size multidimensional arrays (a matrix), declared and
indexed in a way that is clear about what the memory actually is, and that does NOT reproduce C's
array/pointer confusion.

Non-goals for the first cut:
- Function-parameter syntax for passing a matrix (explicitly deferred; see section 8).
- Runtime-shaped (variable-length) arrays.
- Slices / views with independent stride.
- Whole-array assignment and pass/return by value (discussed as an open question, section 9).

## 2. What Impala already has

Impala 2.0 is already clearer than C in the relevant places, and we should build on that rather than
import C's rules:

- Explicit keywords: `int array a[N]` (not `int a[N]`) and `int pointer p` (not `int *p`).
- Typed pointers with element checking: assigning across element types is a hard error (E201/E202),
  so pointers of different element type never silently interconvert.
- Structs are first-class by value: they can be assigned, passed, and returned whole.
- Subscript is a grammar postfix `value[expr]`, NOT surface-level `*(value + expr)`. So the C oddity
  `3[a]` does not exist and never will (lesson 10 in the C notes: already handled).
- A jagged 2-D structure already works today:

      locals int pointer array rows[3], int array r0[4], int array r1[4], int array r2[4]
      rows[0] = &r0[0]; rows[1] = &r1[0]; rows[2] = &r2[0];
      rows[y][x] = 1;                 // two dependent loads: *(*(rows + y) + x)

  This is a real, distinct memory model (rows may live anywhere), and it is NOT what a contiguous
  matrix is. Keeping the two visibly distinct is a central design goal (see section 7).

## 3. What we are deliberately avoiding from C

Condensed from the C peculiarities notes. Each C wart maps to a design rule for Impala:

| C behavior (to avoid)                                                        | Impala rule                                    |
|------------------------------------------------------------------------------|------------------------------------------------|
| Array silently decays to a bare pointer, losing shape (notes 2, 15)          | No silent decay. Shape is part of the type.    |
| `a[3]` (a row) has type `int[10]` but usually acts as `int *` (notes 1-3)    | `a[y]` is a typed row, not a bare pointer.     |
| Same address, different pointer stride for `a`, `&a`, `a[0]`, `&a[0]` (11)   | Type carries stride; address alone never implies type. |
| Array parameters silently rewritten to pointers; `sizeof` lies (notes 5, 6) | No silent rewrite. `sizeof`/shape are predictable. |
| `int a[10][10]` confused with `int **` (notes 7, 16)                          | Matrix, jagged, and pointer-to-pointer are three distinct types. |
| Per-row object bounds make row-crossing arithmetic UB despite contiguity (8) | One flat rectangular object; row-crossing is defined (section 6). |
| Commutative indexing `3[a]` (note 10)                                        | Not expressible in the grammar. |

Design principles, in order of priority:
1. Shape lives in the type. Indexing peels one dimension; nothing is silently lost.
2. A matrix is ONE contiguous object. `a[y]` is a view into it, not a separate object.
3. No implicit conversion between matrix, jagged array, and raw pointer. Conversions are explicit.
4. `sizeof` / length / shape are predictable and identical for a local, and (later) a parameter.
5. Raw pointer arithmetic stays an explicit low-level escape hatch, not the mechanism behind `a[y][x]`.

## 4. The model: array of arrays, contiguous, row-major

A multidimensional array is an array whose element is itself an array (the C mental model in note 1,
kept honestly). For `int array a[H][W]`:

- `a` is an array of `H` rows; each row is an `int array [W]`.
- Storage is one contiguous block of `H * W` ints, row-major:
  `a[0][0], a[0][1], ..., a[0][W-1], a[1][0], ...`.
- `a[y]` is the y-th row: a value of type `int array [W]` (a shape-carrying reference into the block),
  addressed at `base + y * W`. It is NOT an `int pointer`.
- `a[y][x]` is an `int`, at `base + y * W + x`.

Indexing rule (uniform, and it already matches how struct arrays decay today): subscripting an array
peels its outermost dimension and scales the index by the size of one element at that level:

    stride(a)      = W          (one row = W ints)
    a[y]           = row at base + y*W,   type int array [W]
    stride(a[y])   = 1          (one int)
    a[y][x]        = int at base + y*W + x

For N dimensions `a[D0][D1]...[Dk]`, `a[i]` scales by `product(D1..Dk) * sizeof(element)` and yields
an `(N-1)`-dimensional row; the last subscript yields the scalar (or struct) element. This is exactly
the existing "struct array decays to a struct pointer with stride = sizeof(struct)" path generalized,
so the compiler already contains the shape of this logic.

Internally a matrix value (and each intermediate row) carries its remaining dimension list as part of
its type. That list is the moral equivalent of C's `int (*)[W]` stride, but it is part of the visible
type rather than hidden in declarator syntax.

## 5. Syntax (DECIDED: comma shape)

Declaration and indexing use a comma-separated dimension list inside a single bracket:

    int array a[H, W]           // declaration: one shaped object
    a[y, x] = 1                 // use: one indexing operation
    a[y]                        // a partial index yields a row (see section 6)

This was chosen over the C-style bracket chain (`a[H][W]` / `a[y][x]`) deliberately: the bracket chain
is visually identical to the jagged `rows[y][x]`, which is a completely different operation (two
dependent pointer loads vs one computed offset). The comma form makes a contiguous matrix read as one
shaped object indexed once, and keeps it distinct from the jagged form at both declaration and use.
Grammar cost is small: subscript becomes `'[' Expr (',' Expr)* ']'`.

## 6. Semantics of each form

For `int array a[H, W]` (using Option B spelling):

Each array SHAPE is its own type, exactly like each struct definition is its own type. `int[H, W]`,
`int[W]`, and `int[V]` are three distinct types; taking the address of any of them yields a distinct
pointer type whose pointer arithmetic strides by that whole object. This is the "shape lives in the
type" principle (section 3) made concrete, and it is what makes an array feel like a struct: a fixed
shape is a nominal type.

| Expression   | Meaning                        | Type                    | Pointer arithmetic stride    |
|--------------|--------------------------------|-------------------------|------------------------------|
| `a`          | the whole matrix               | `int array [H, W]`      | -                            |
| `a[y]`       | row y                          | `int array [W]`         | -                            |
| `a[y, x]`    | element                        | `int`                   | -                            |
| `&a`         | address of the whole matrix    | `int array[H, W] pointer` | one whole matrix (H*W ints) |
| `&a[y]`      | address of a row               | `int array[W] pointer`  | one row (W ints)             |
| `&a[y, x]`   | address of an element          | `int pointer`           | one int                      |

So `&a`, `&a[y]`, and `&a[y, x]` normally hold the same address for `y = x = 0`, but are three
DIFFERENT pointer types with different strides - and the existing typed-pointer element checks
(E201/E202) already refuse to cross element types, so they cannot be silently confused (C notes 3, 11).

Explicit flattening: to obtain a plain `int pointer` walking the whole contiguous block, write
`&a[0, 0]` (an element pointer; row-crossing within the one block is defined, section 6). There is no
implicit decay from `a`, `a[y]`, or `&a` to a bare `int pointer`; the programmer asks for the exact
pointer they want. This resolves the confusion about "taking `a[3]` from `a[10, 10]`": `a[3]` is a row
of type `int array [10]`, `&a[3]` is an `int array[10] pointer`, and if you want a flat element pointer
you write `&a[3, 0]` and you have said so.

Note (1-D case): for `int array a[N]`, `a[0]` is a scalar, so `&a[0]` is a plain `int pointer` and `&a`
is an `int array[N] pointer`. The ladder above degenerates correctly to one rung.

Row-crossing arithmetic: because a matrix is ONE flat object (not C's per-row objects), a pointer
obtained via `&a[y, 0]` that walks past index `W-1` is DEFINED to reach `a[y+1, 0]`, as long as it
stays within the whole block. We can afford this because GAZL is a flat memory machine and the
compiler performs no C-style per-object aliasing optimization. (Contrast C note 8, where this is UB.)
Out-of-block access remains the programmer's responsibility; the GAZL runtime does not bounds-check
dynamic indices (small overruns read/write adjacent memory; large or negative offsets can fault).

## 7. Distinct types: matrix vs jagged vs pointer-to-pointer

These are three different things and Impala keeps them three different types with no implicit
conversion between them:

| Declaration                     | Layout                              | `x[y][z]` / `x[y, z]` cost      |
|---------------------------------|-------------------------------------|---------------------------------|
| `int array a[H, W]`             | one contiguous H*W block            | one computed offset             |
| `int pointer array rows[H]`     | H row pointers, rows anywhere       | two dependent loads (jagged)    |
| `int pointer pointer p`         | raw double pointer                  | two dependent loads (raw)       |

A matrix does not implicitly become a jagged array or a raw pointer, and vice versa. This is the
anti-C invariant (C notes 7 and 16): the representation is always visible in the type.

## 7b. Value vs reference semantics (the central question)

The goal is for a multidimensional array to feel like a struct: one contained object. Today Impala's
arrays are C-like reference values - a bare array name decays to a pointer, and passing an array to a
function passes the pointer, not a copy. That is the single decision most in tension with the "contained
object" goal, and it is worth revisiting rather than building matrices on top of it.

Two consistent models exist; Impala already implements BOTH, just for different aggregates:

- Value aggregate (structs today): assigned, passed, and returned whole (copied). A struct parameter
  copies; to avoid the copy you pass a struct pointer explicitly. No silent decay.
- Reference aggregate (arrays today): the name decays to a pointer; passing an array passes a pointer;
  `sizeof` / shape is easily lost. This is exactly the C behavior the notes flag as lessons 1 and 6.

Was the C-like array default a mistake? For the "contained object" goal, yes in one specific sense: the
silent array-to-pointer decay (and the resulting shape loss) is a known C wart, and it is why arrays do
not already feel like structs. The underlying capability - passing a large buffer by reference without
copying - is necessary, but it should be the EXPLICIT case, not the silent default.

DECIDED direction: remove the implicit array-to-pointer decay, gated exactly like the strict-expression
change (strict by default, `--legacy` lowers it). In 1.0 an array is NOT a pointer - it merely
downcasts implicitly to one; 2.0 removes that implicit downcast:

- 2.0 default (strict): a bare array does not implicitly convert to a pointer. To get a pointer you
  write `&a` (whole array), `&a[y]` (row), or `&a[y, x]` / `&a[0, 0]` (element). Passing a bare array
  where a pointer is expected is a coded error with a fix-it note ("take its address: `&a`").
- `--legacy`: the old implicit downcast is allowed, emitted as a warning in the same diagnostic shape.

Why this is the chosen approach:
- Backwards compatible: 1.0 code compiles under `--legacy`, identical to how mixed-bitwise was handled.
- Uniform: one rule for ALL array ranks - no dimensionality split, no per-file dialects.
- It is a type-system tightening (remove an implicit coercion), NOT a representation change: an array
  is still N contiguous words; it just stops silently acting as a pointer. Localized to the coercion
  path plus the diagnostic and its `--legacy` lowering.
- Migration: fixtures that pass a bare array either add `&` or compile under `--legacy` - the same
  rollout the bitwise change used.

SEPARATE, ADDITIVE, non-breaking (still open): whether fixed-shape arrays also become copyable VALUES
like structs - `b = a` copies, and a by-value array parameter copies while a pointer parameter does not
(so big buffers are never force-copied; the programmer chooses via the parameter type). This reuses the
struct-by-value machinery (an array value is N contiguous words, like an anonymous struct). It does not
gate multidimensional arrays and can be picked up independently. Removing implicit decay (above) is the
load-bearing decision; copyable value semantics is a later enhancement.

## 8. Function parameters (DEFERRED - do not implement yet)

Per the current decision, parameter syntax is NOT being changed in this pass. This section records the
problem and the options so the eventual choice is deliberate.

The problem (C notes 5, 6): to index a matrix parameter, the callee must know the INNER dimensions
(the stride), but not the outer one. C solves this by silently rewriting `int a[H][W]` to
`int (*a)[W]` and by making `sizeof` inside the callee lie. We will not do the silent rewrite.

Options to evaluate later (all keep the inner shape in the parameter's type, none silently rewrite):
1. Shaped array parameter: `function f(int array a[, W])` - outer dimension omitted, inner dims
   required. The argument must be a matrix whose inner dims match `W`.
2. Named row-pointer parameter: `function f(int array[W] pointer a)` - C's `int (*)[W]`, but the row
   type is spelled out. Most explicit about what is passed.
3. Pass-by-reference of the whole fixed-shape matrix (requires deciding array value semantics first,
   section 9).

Until one is chosen, a matrix can be passed the same way arrays are passed today: as a flat
`int pointer` (via `&a[0, 0]`) plus the width as a separate argument, with the callee doing
`p[y * W + x]`. This is explicit and honest, if verbose. The jagged `int pointer array` form is also
available when the caller genuinely has row pointers.

Requirement for whatever we pick: `sizeof` / shape of a parameter must behave the same as for a local
of the same type (C note 6 is explicitly rejected).

## 9. Open questions

1. Syntax: DECIDED - comma shape `a[H, W]` / `a[y, x]` (section 5).
2. `sizeof` and shape: current `sizeof` takes a TYPE (`sizeof(int)`, `sizeof(Struct)`), not an
   expression. Do we (a) extend `sizeof` to accept an array-typed expression so `sizeof(a)` = H*W and
   `sizeof(a[y])` = W, and/or (b) add a `length(a, dim)` or shape intrinsic? Predictability (principle
   4) argues for doing at least one.
3. `&a` / `&a[y]` / `&a[y, x]` types: DECIDED - each yields a distinct shape-carrying pointer type
   (`int array[H, W] pointer` / `int array[W] pointer` / `int pointer`), striding by the whole object
   at that rung (section 6). Each array shape is its own type. (Whether the compiler's FIRST
   implementation slice emits the row/matrix pointer TYPES or only supports `&a[0, 0]` element pointers
   is an implementation-sequencing detail, not a semantic question.)
4. Array value semantics: THE central decision - see section 7b. Make fixed-shape arrays value types
   like structs (recommended), and if so, with what migration appetite (full switch / scoped /
   struct-wrap only)?
5. Element types: allow struct-element matrices (`Pixel array img[H, W]`)? The stride math already
   generalizes (element size = structWords); mainly an initializer and type-check concern.
6. Initializers: flat row-major (`int array a[2, 3] = { 1, 2, 3, 4, 5, 6 }`) is trivial to support and
   probably enough for v1. Nested-brace per-row initializers are a later nicety.

## 10. Implementation sketch (for when syntax is settled)

The machinery is mostly a generalization of paths that already exist (see the array/subscript map):

- Declaration: `ArrayDecl` accumulates a dimension list; allocation words = product(dims) *
  element words. (Single-dimension output must stay byte-identical to today.)
- Type: carry the remaining dimension list on the array/row meta record (the shape). A 1-D array
  behaves exactly as today (empty remaining-dim list after one subscript).
- Subscript: when more than one dimension remains, peel the outermost, scale the index by
  product(remaining) * elementWords (mirror `structSubscript`'s `ptr + i*sizeof`), and produce a row
  that carries the remaining dims; when one dimension remains, hand off to the existing scalar `=[]` /
  struct-place path so the final index is unchanged from today.
- `sizeof` / shape: per open question 2.
- Parameters: per section 8 (deferred).

This keeps the 73 golden fixtures untouched for all existing (single-dimension) code, because the
multi-dimension paths only engage when a second dimension is present.
