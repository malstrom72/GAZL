# Impala 2 Implementation Plan: the Remaining Slices

Investigation notes and the settled approach for the work that was flagged high-risk: struct
values (2.2–2.4), by-value passing/returns (2.5 + Step 4), and import-as-linking with
`--dead-strip` (Step 5). Every load-bearing mechanism below was **verified by experiment on the
built VM** before being chosen; experiment sources are reproduced inline so they can be re-run.

## Finding 0: the risk was in the wrong place

The original fear — "struct values break the compiler's one-word-per-value/temporary assumption" —
dissolves under the right architecture: **struct-typed expressions never enter expression
temporaries at all.** Every struct-typed expression is a compile-time *place*; only terminal
scalar field accesses emit value instructions, and whole-struct operations (`a = b`, arguments,
returns) are `COPY`/window operations at statement and call boundaries — which is exactly what the
already-shipped spec decisions (statement-level `=`, no struct expressions, no whole-struct
compare) were protecting. The one-word temporary invariant is never touched. Risk drops from
"rework the bedrock" to "add a place representation and lower it correctly".

## Slices 2.2 + 2.3: the place architecture — IMPLEMENTED

> Done and VM-verified (`structValues.impala`, `structAssign.impala`): struct-value locals as
> `LOCA *sizeof`; the place representation (`@place` meta with `baseKind` local|pointer, base,
> compile-time offset, struct); `lookup` → place; nested inline chains via offset accumulation
> (`v.lo.z1` → direct `$v:2`; `p->lo.z1` → `PEEK $p #2`); `*structPtr` → pointer-base place;
> whole-struct `a = b` → one `COPY *sizeof` with `placeAddress` (ADRL for locals + optional ADDp
> for a field offset; pointer base used directly). Slice 4: `&structValue` → typed struct pointer
> (materializes the place address; `&v.field` too), enabling `f(&v)` by-pointer calls. Slice 5:
> struct-value **globals** (`global Voice voice` → zeroed `GLOB *sizeof`; `globalAddr` base kind —
> field access `&name:off` in global memory, `&global v` addresses it, global↔local `COPY`). The
> place model now covers all three base kinds. Slice 6: struct **arrays** (`Voice array bank[8]` →
> `GLOB/LOCA *(count*sizeof)`; a struct array decays to a struct pointer, and subscripting a struct
> pointer (`structSubscript`) yields a place — constant index folds `C*sizeof` into the offset,
> dynamic index emits one `MULi` stride; global bases use `&name:off`, runtime pointers use
> PEEK/POKE; struct-array size must be a numeric literal for now, E414). Slice 7: **array fields**
> inside a struct (`struct Filter { float array state[4] }` → `f.state[i]`) — the array field
> decays to a typed pointer at base+offset (global `&v:off`, local ADRL+add, pointer base+add),
> subscript handles the rest incl. arrays-of-structs inside a struct. Deferred still: brace
> initializers (E421), by-VALUE params/returns. Original design notes retained below.


A **place** is carried on the expression meta record:

```
{ baseKind: 'local' | 'globalAddr' | 'pointer',
  base:     '$f' | '&g' | <pointer operand>,
  offset:   <accumulated compile-time word offset>,
  struct:   <struct name> }
```

- `lookup` of a struct-typed local/global produces a place (type `'S'`), mirroring how array
  lookups already produce `'=&'`/`':='` address metas.
- `fieldAccess` on a place with a **struct-typed field** just adds `field.offset` — nested chains
  (`v.low.z1`, `fp->sub.field`) fall out for free, zero instructions, "dots are free" holds
  by construction.
- `fieldAccess` terminating on a **scalar field** lowers by `baseKind` — each form verified:

| baseKind | lowering | verified by |
|---|---|---|
| local | direct operand `$f:7` (`'='`/`':='` meta) | `MOVi $x $s:1` — runs (experiment 1); same form `binaryOp`'s const-index array path already emits |
| globalAddr | `'=*'`/`'*='` with `&g:7` | corpus: `PEEK %0 &initedArray:2`, `COPY %0 &initedArray:1 *3` |
| pointer | `PEEK`/`POKE ptr #7` | shipping slice-1 behavior |

- Whole-struct `a = b`: both sides places, same struct required; local places take one borrowed
  pointer temp for `ADRL`, global places use `&g:off` directly (`COPY` accepts address-with-offset
  operands — corpus-verified); emit exactly one `COPY *sizeof`. Statement-level: a struct-typed
  assign result is a no-value marker; use in a nested expression is an error.
- `releaseMeta` on places is naturally safe (`returnBack` ignores non-`%`/`<` operands), but the
  borrowed `ADRL` temps in whole-struct assign must be returned — same discipline as `copy()`.

**Testing:** golden files cannot catch struct-lowering bugs (no legacy coverage), so every behavior
is asserted by **running fixtures on GAZLCmd** with expected output — the `structPointers.impala`
pattern: locals, globals, nesting three deep, mixed `.`/`->` chains, whole-struct assignment in
all base-kind combinations, and read-back verification after every write.

## Slice 2.4: struct arrays + brace initializers

- `Filter array banks[4]` = `LOCA`/`GLOB *(4*sizeof)`; constant index → place with
  `offset = i*sizeof + …` (still free); dynamic index → stride `MULi`, base becomes a computed
  pointer temp, terminal access is `PEEK`/`POKE`/`GETL`-style — the Step 1 dynamic-index shapes.
  Lifts error E414.
- Brace initializers recurse the existing `InitList` machinery with a field cursor: each value
  checked against the field type, nested `{}` descends into struct/array fields, trailing
  omission zero-fills. Lowering is the flat `DATA` rows the braces describe.

## Step 4 + slice 2.5: returns and by-value — one window convention

**Experiment 1 (decisive):** a labeled `PARA` section works as a first-class local:

```gazl
helper: FUNC
    $s: PARA *3         ; labeled 3-word parameter window
    MOVi $x $s:1        ; direct :offset read — works
    MOVi $s:0 #99       ; write — works (read-only is compiler-enforced)
    GETL %11 $s:1 %12   ; runtime offset off a :const base — works
```

So: **a by-value struct parameter is one labeled `PARA *sizeof`** declared in parameter order;
fields are free direct operands in the callee (the perf win the spec records). **A by-value struct
return is a leading labeled `PARA *sizeof`** the callee writes as `$out:off`. Scalar multi-return
(Step 4) is the same convention with N scalar `OUT`s. (One small pre-Step-4 experiment remains:
two scalar `OUT`s + caller reading `%b..%b+1` — the docs say `*size` counts both, and the layout
is declaration-order, so this is expected to pass.)

**Experiment 2 (decisive):** caller-side copying into the argument window works:

```gazl
    ADRL $q %0 *3       ; address of the transient call window — legal
    COPY $q $p *3       ; copy the struct value in
    CALL &sum3 %0 *3    ; callee sees 7,8,9 — verified (prints 24)
```

Caller-side implementation: claim `sizeof` consecutive window slots (the existing `makeArgValue`
slot-claiming loop, run per word), then `ADRL` + `COPY`. Call `*size` counts **words**, which the
`CALL` documentation already specifies. Receiving a struct return: `v = f(...)` is statement-level;
after `CALL`, `ADRL` the destination place + `COPY` from the window.

Signature entries gain `words`; arity checking counts words; by-value vs by-pointer mismatch keeps
the two fix-it notes from the spec.

## Step 5: import-as-linking + `--dead-strip`

**Division of labor: the compiler gains a collect-only mode; the builder owns the closure.**

**Architecture (revised 2026-07-20 per the thin-action/handler discussion): `$$parser` already IS
the semantic-handler object the grammar dispatches to.** Finish thinning the remaining fat inline
actions into `$$parser` methods, then give `$$parser` two modes:

- **emit mode** — today's codegen.
- **collect mode** — the import/interface parser: declarations register in the symbol/struct/type
  tables **with type references recorded by NAME, not eagerly resolved**; function bodies are
  parsed but their codegen calls no-op (or blocks are brace-skipped). Emits nothing.

**This is declaration-level two-phase, and it is all import cycles need** — distinct from the
expensive body-level two-phase (the AST rework that fixes `dry`/backtracking). Two independent
moves:

- *Declaration-level two-phase* (gather decls across the closure, then resolve names): cheap,
  bounded, unlocks cycles. Delivered by collect-mode + deferred resolution.
- *Body-level two-phase* (AST of expressions, resolve/emit later): the JSPEG 2 rework; cycles do
  **not** need it; still deferred.

- Grammar gains only: `import "path"` and the `export` declaration modifier (`export` emitted as a
  role prefix in the `; signature` rows; validator's `classifyRole` extended to accept it).
- The **builder** (`impala build` in `impala.node.js`): (1) **gather** — walk the import closure
  (visited-set by canonical path), parse every unit in collect mode, merge declarations into one
  closure-wide interface with names still symbolic; (2) **resolve** — resolve all type/name
  references against the merged interface (by-value containment cycles caught here as infinite
  size); (3) **codegen** — compile each unit in emit mode against the complete tables, concatenate
  in closure order.
- **Per-unit seeds are mandatory** — experiment 3 proved it: two units compiled with the same
  `randomId` that contain the same string constant collide at link
  (`Symbol already defined: .s_shared_4d2`). This is a *pre-existing* landmine of the manual
  workflow. The builder derives each unit's seed deterministically (user seed ⊕ hash of canonical
  unit path), making collisions impossible by construction and builds reproducible.
- **Cycles are legal** (revised — earlier draft here retreated to a build error). The gather →
  resolve → codegen order means A↔B mutual references resolve regardless of order: gather records
  `Node.partner : ptr-to-"B"` and `B.partner : ptr-to-"Node"` unresolved, resolve links them, and
  bodies codegen against complete tables. The single-pass *body* compiler is never asked to
  forward-reference a type. (Bonus: cross-unit forward function references also resolve without
  `extern`, byte-safe — existing externs become redundant, not wrong. Whether to also make the
  standalone single-unit path gather-first is a separate, byte-safe option, not required here.)
- **`--dead-strip` is a text-level `.gazl` transform in the builder, not compiler logic.** The
  output is line-structured: labeled `FUNC` blocks, labeled `GLOB`/`CNST`/`TEMP` data blocks,
  `! DEF` rows. Build a reference graph from operands (`&name`, `^name`, `#name`), roots from
  `; signature export …` rows, mark-and-sweep, drop dead blocks plus their metadata rows. Zero
  compiler risk, independently testable against artifacts, and testable *without* the language
  feature by hand-writing inputs.

## Order of work

1. **2.2+2.3** — the place architecture (locals, globals, nesting, whole-struct `=`), VM fixtures.
2. **2.4** — struct arrays + initializers.
3. **Step 4** — multi-`OUT` experiment, then scalar multi-return + destructuring.
4. **2.5** — by-value params/returns on the window convention.
5. **Step 3** — `functype` (independent, low risk).
6. **Step 5** — builder + import + `export` + `--dead-strip`, with the cycle amendment.

Each lands as a separate commit behind the full gate (regenerate → jspegCompilerTests →
runJspegTests golden → full build → VM-run fixtures).
