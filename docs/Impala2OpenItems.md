# Impala 2.0 open items (review findings)

A living list of everything flagged during the multidim/struct review pass, with status. Resume point.
Tasks referenced by #N are in the session task list. Commits are on branch `Impala2`.

## Fixed this pass (committed)

- Shape-pointer subscript stride. `p[2]` on `int array[3] pointer` now strides by the pointee size ->
  the 3rd `int[3]` at offset 6 (a sub-array), and `p[2, x]` is the scalar at 6+x. Was wrongly stride-1
  scalar. Also revised the `matsum` example to a row pointer (`&a[0, :]`).
- Untyped multidim element type. `global array t[3, 4]; t[1, 2] = 5` gave a bogus
  `E303: Incompatible types (, = int)` because the multidim path derived the element type from the
  (empty) descriptor. Now uses the wildcard `?`, matching untyped 1-D arrays.
- Array-shape dimension must be a numeric literal. `int array[3*2] pointer` / `int array[SIX] pointer`
  used to leak a const-pool label into the type (`expected int array[<A>] elements`). Now a clear E414.
  Full support (evaluate `3*2` / `SIX`) is #20.
- (Earlier, same session, also done: funcptr-pointer `&funcptr` E201 fix; playground now reports syntax
  errors + Tab key + localStorage persistence; multidim global signatures show the real shape.)

## Open bugs (compile-time checks that are missing)

| Item | Detail | Task |
|------|--------|------|
| readonly array write not caught | `readonly int array a[3]; a[1] = 5` compiles (then fails at GAZL assembly). Scalars ARE caught (E404) - the array lookup (`type 'A'`) ignores the readonly flag, so element writes slip through. Fix: carry readonly onto the array ref, propagate to the element/place, reject the write. | #21 |
| const index out of bounds | A constant index `< 0` or `>= dimension` (`int array a[3]; a[5]`, `a[0 - 1]`) is not caught at compile time; GAZL catches static OOB at assembly. Add a compile-time bounds check for constant indices in the subscript paths (named 1-D via binaryOp, multidim `arraySubscriptFlat`, slices `arraySlice`). | #22 |

## Open features

| Item | Detail | Task / doc |
|------|--------|-----------|
| Destructure into lvalue targets | `a[1], b[2] = f()` is a syntax error - destructure targets are plain identifiers only. Extend `DestTarget` to accept array-element / field lvalues and have `finishDestructure` store through them. | #23 |
| Return arrays by value | Functions cannot return an array; structs can. This is the deferred value-semantics work (arrays behave like structs). Doc section 7b of MultidimensionalArrays.md. | #18 |
| Pass arrays by value | Cannot pass an array by value (copy) as a parameter; passing by POINTER works (`int array[W] pointer`, pass `&a` / `&a[0, :]`). By-value is the same value-semantics slice as returns. | #18 |
| Constant-expression array dims | `3*2`, named consts, in ANY dimension (1-D `&a` descriptor, multidim, shape pointers). Needs a constant evaluator that folds a const int expression to a plain integer. | #20 |
| Extern prototypes / multi-return externs | Externs are name-only; cannot declare or destructure a multi-value extern. Allow (do not demand) full signatures, validated against the definition/manifest. Workaround today: out-parameters `foo(in, &a, &b)`. | #19, docs/ExternPrototypes.md |
| Struct layout as GAZL constants | Emit `.offset.<Struct>.<field>` / `.sizeof.<Struct>` and reference them instead of baked immediates (macro-assembler; conditional fields adapt). Decouples struct INTERFACE (compile-time) from LAYOUT (assemble-time). Naming decided. | #24, docs/StructLayoutConstants.md |

## Design notes

- docs/MultidimensionalArrays.md - multidim arrays (slices 1-2 implemented; value semantics, decay
  `--legacy`, nested initializers, shape-aware `sizeof` still open).
- docs/ExternPrototypes.md - extern signatures: allow + validate, drift vs verifiability.
- docs/StructLayoutConstants.md - struct layout as GAZL constants; interface/layout decoupling.

## Not yet re-derived from the review, worth a look later

- `&a` of a 1-D array declared with an expression size (`int array a[3*2]`) compiles/allocates via the
  `<A>` label but its whole-array pointer descriptor becomes `[<A>]:i` - same root as #20.
- The fuzzer only generates TYPED arrays, so untyped-multidim and readonly/OOB gaps were not caught by
  fuzzing - consider adding untyped + readonly + const-OOB shapes to fuzzImpala.js.
- Single-index-on-shape-pointer ergonomics vs C: `p[2]` = within-walk (a sub-array), while C's `p[2]`
  would be the same block but people expect chained `p[2][x]`; we use comma `p[2, x]`. Documented, but
  worth a second look for footgun-ness.
