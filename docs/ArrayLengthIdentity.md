# Array length: value vs type (DECISION)

Status: DECIDED (design). Not yet implemented. This is the resolution of the long "type identity when a
dimension is a calculated / assembler-resolved constant" thread. It supersedes the "single named symbol
for ALL symbolic dims" and "nominal-syntactic expression tokens" ideas, both of which were wrong (see
"Rejected alternatives").

## The rule

An array length can appear in two roles, and the requirement differs by role:

- LENGTH-AS-A-VALUE: the length becomes a number that gazl resolves (allocation size, an index stride,
  a copy word-count). Any expression is allowed here: `int array a[c1 * c2]`. This is exactly what
  Impala 1.0 did and it is unchanged.
- LENGTH-AS-A-TYPE: the length participates in comparing one array shape against another. Here each
  dimension must be a SINGLE named constant (or a literal / frozen const that folds to a value). An
  expression is NOT allowed directly; name it first (`const int N = c1 * c2`).

Put plainly: you can always DECLARE and USE an array with a calculated length. You just cannot put a
calculated length INTO A TYPE without naming it first.

## Which operations are which

Length-as-a-value (any expression, 1.0-compatible):
- Declaration / allocation: `int array a[c1 * c2]`. gazl resolves the size.
- Indexing, including multi-dim: `a[y, x]` -> `y * innerStride + x`. Needs the stride VALUE (a
  symbolic / foldable operand), not a shape comparison.
- The COPY word-count itself: a computed value (product of dims), gazl resolves it.

Length-as-a-type (each dimension must be a single named constant or a folded value):
- Array-by-value copy, pass-by-value, return-by-value.
- Shape-carrying pointer parameters (`int array[N] pointer`) and `&a` flowing into them.
- Slice copy `a[y, :] = b[y, :]`.

The distinguishing act is SHAPE COMPARISON ("is the source the same shape as the destination / the
parameter?"). Only these operations need a stable, comparable identity, so only these carry the
single-named-constant requirement.

## Why this is sound AND non-breaking

Non-breaking: Impala 1.0 had NO array-typed parameters, only pointers. So no 1.0 code sits in a
length-as-a-type position. The requirement can therefore only ever apply to constructs that did not
exist in 1.0 (array value semantics, shaped pointers, slices). `int array a[c1 * c2]`, indexing it, and
`&a` decaying to a plain `int pointer` are all length-as-a-value and stay exactly as 1.0.

Sound: with each dimension a lone symbol, shape comparison is pairwise-nominal - `[H, N]` matches
`[H, N]` by comparing `H`==`H` and `N`==`N`. No expression ever enters a type, so the commutativity
footgun (`c1*c2` vs `c2*c1`) cannot arise; there is nothing to reorder. Under an assembler-variable
constant it is sound for the same reason structs are: if `N` changes at assembly time, both sides
reference `N` and move together, so they still match. The compiler never needs the value; the assembler
owns it. This is the struct model exactly (a size in a type is one symbol, `.sizeof.Voice`, never an
expression).

Multi-dim detail: each AXIS is a single identifier (or folded value) so the shape compares pairwise;
the overall stride / count / total size is a VALUE computed from those axes (`H * N`), handled as a
length-as-a-value (fold if frozen, else a symbolic operand or a materialized `MULi`). So a multi-dim
copy is fine: pairwise-nominal type check on the axes, computed word-count for the COPY.

## The error is local and actionable

    a = b;   // a, b : int array[c1 * c2]
             // error: array-by-value copy needs each dimension as a single named
             //        constant; 'c1 * c2' is an expression. Name it: const int N = c1 * c2

A one-line fix at a brand-new-in-2.0 operation, not a migration tax on existing code.

## Consequences for open items

- #20 (constant-expression array dims) drops from LOAD-BEARING to an ERGONOMIC SOFTENER. Folding a
  FROZEN expression (`3 * 2` -> `6`, a frozen `const`) is only needed so such an array can be used in a
  length-as-a-type position WITHOUT naming it. It is a safe convenience, never required for soundness:
  the assembler-variable case never folds, it names. So the constant evaluator is optional and scoped to
  frozen consts.
- Slice 2 (shape-carrying pointers) STAYS. Its descriptor comparison (E201/E202 string compare) is
  already the pairwise-nominal check. The only fix is to ensure a dimension in a type descriptor is a
  single symbol or a folded value, never a generated const-pool label (`[<A>]`) or a raw expression.
  The current E414 ("must be a numeric literal") becomes "must be a numeric literal, a folded frozen
  const, or a single named constant" - i.e. reject only multi-token expressions in type positions,
  with the "name it" hint above.
- The StructLayoutConstants.md "type identity" section argued value-fold vs single-symbol as a global
  choice; that framing is replaced by this value-vs-type split. See the pointer there.

## Rejected alternatives (why)

- Value-fold everything: unsound the moment a dimension is assembler-variable (the compiler type-checks
  against a value the assembler can override).
- Single named symbol for ALL symbolic dims (even in declarations): breaks 1.0. `int array a[c1 * c2]`
  was legal and common; demanding it be renamed everywhere is a rewrite, not a migration. The fix is to
  scope the requirement to length-as-a-type positions, which no 1.0 code occupies.
- Nominal-syntactic (allow expressions in a type as a compared source token): lets `[c1*c2]` into a
  type and then must live with the `[c1*c2]` vs `[c2*c1]` false-negative footgun. Banning expressions
  from types outright (this decision) removes the footgun entirely and matches structs.
- C-style decay only (drop shape typing): loses the compile-time catch for calling a function with the
  wrong dimensions, which is a real hard-bug source we want caught. This decision keeps that catch while
  staying 1.0-compatible.

## Related

- docs/MultidimensionalArrays.md - the multidim design (slices 1-2).
- docs/StructLayoutConstants.md - the struct nominal-identity precedent this reuses.
- docs/Impala2OpenItems.md - #20 downgraded per the above.
