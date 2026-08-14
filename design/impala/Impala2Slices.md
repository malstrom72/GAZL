# Impala 2 Implementation Plan: the Remaining Slices

Status: **COMPLETE for 2.0 - design record, not a plan** (checked 2026-08-05). Every item on the
"Order of work" list at the foot of this file is resolved, and each is already annotated inline with a
date: the place architecture and struct arrays are IMPLEMENTED; multi-return and by-value params were
implemented, VM-verified, then PARKED to 3.0 at `Impala3-byvalue-multireturn-park` (`E426`-`E429`); `functype`
and `import`/`export`/`--dead-strip` are IMPLEMENTED, the last with an honest carve-out; collect mode is
DEFERRED to 3.0 and struck through in place. Nothing here is open work.

Read it for **why** those mechanisms were chosen - every one was settled by experiment on the built VM,
and the experiment sources are reproduced inline so they can be re-run.

Investigation notes and the settled approach for the work that was flagged high-risk: struct
values (2.2-2.4), by-value passing/returns (2.5 + Step 4), and import-as-linking with
`--dead-strip` (Step 5). Every load-bearing mechanism below was **verified by experiment on the
built VM** before being chosen; experiment sources are reproduced inline so they can be re-run.

## Finding 0: the risk was in the wrong place

The original fear - "struct values break the compiler's one-word-per-value/temporary assumption" -
dissolves under the right architecture: **struct-typed expressions never enter expression
temporaries at all.** Every struct-typed expression is a compile-time *place*; only terminal
scalar field accesses emit value instructions, and whole-struct operations (`a = b`, arguments,
returns) are `COPY`/window operations at statement and call boundaries - which is exactly what the
already-shipped spec decisions (statement-level `=`, no struct expressions, no whole-struct
compare) were protecting. The one-word temporary invariant is never touched. Risk drops from
"rework the bedrock" to "add a place representation and lower it correctly".

## Slices 2.2 + 2.3: the place architecture - IMPLEMENTED

> Done and VM-verified (`structValues.impala`, `structAssign.impala`): struct-value locals as
> `LOCA *sizeof`; the place representation (`@place` meta with `baseKind` local|pointer, base,
> compile-time offset, struct); `lookup` → place; nested inline chains via offset accumulation
> (`v.lo.z1` → direct `$v:2`; `p->lo.z1` → `PEEK $p #2`); `*structPtr` → pointer-base place;
> whole-struct `a = b` → one `COPY *sizeof` with `placeAddress` (ADRL for locals + optional ADDp
> for a field offset; pointer base used directly). Slice 4: `&structValue` → typed struct pointer
> (materializes the place address; `&v.field` too), enabling `f(&v)` by-pointer calls. Slice 5:
> struct-value **globals** (`global Voice voice` → zeroed `GLOB *sizeof`; `globalAddr` base kind -
> field access `&name:off` in global memory, `&global v` addresses it, global↔local `COPY`). The
> place model now covers all three base kinds. Slice 6: struct **arrays** (`Voice array bank[8]` →
> `GLOB/LOCA *(count*sizeof)`; a struct array decays to a struct pointer, and subscripting a struct
> pointer (`structSubscript`) yields a place - constant index folds `C*sizeof` into the offset,
> dynamic index emits one `MULi` stride; global bases use `&name:off`, runtime pointers use
> PEEK/POKE; a struct-element array takes a named `const` extent whether or not it is initialized, so
> `const int N = 4; global S array bank[N] = { { n: 1 } }` compiles clean - the 2026-08-04 narrowing of
> the INITIALIZED case to a numeric literal (E414) was LIFTED 2026-08-13, see Slice 2.4). Slice 7: **array fields**
> inside a struct (`struct Filter { float array state[4] }` → `f.state[i]`) - the array field
> decays to a typed pointer at base+offset (global `&v:off`, local ADRL+add, pointer base+add),
> subscript handles the rest incl. arrays-of-structs inside a struct. Slice 8: **brace
> initializers** for struct globals/readonly and struct arrays via recursive `buildStructInit` over
> a `Braced` tree (per-field type check, trailing zero-fill, emits GLOB/CNST + DATA; InitList tried
> first, backtracks on nested braces). **Structs feature-complete except by-value params/returns**
> (returns need Step 4 first). Original design notes retained below.


A **place** is carried on the expression meta record:

```
{ baseKind: 'local' | 'globalAddr' | 'pointer',
  base:     '$f' | '&g' | <pointer operand>,
  offset:   <accumulated compile-time word offset>,
  struct:   <struct name> }
```

- `lookup` of a struct-typed local/global produces a place (type `'S'`), mirroring how array
  lookups already produce `'=&'`/`':='` address metas.
- `fieldAccess` on a place with a **struct-typed field** just adds `field.offset` - nested chains
  (`v.low.z1`, `fp->sub.field`) fall out for free, zero instructions, "dots are free" holds
  by construction.
- `fieldAccess` terminating on a **scalar field** lowers by `baseKind` - each form verified:

| baseKind | lowering | verified by |
|---|---|---|
| local | direct operand `$f:7` (`'='`/`':='` meta) | `MOVi $x $s:1` - runs (experiment 1); same form `binaryOp`'s const-index array path already emits |
| globalAddr | `'=*'`/`'*='` with `&g:7` | corpus: `PEEK %0 &initedArray:2`, `COPY %0 &initedArray:1 *3` |
| pointer | `PEEK`/`POKE ptr #7` | shipping slice-1 behavior |

- Whole-struct `a = b`: both sides places, same struct required; local places take one borrowed
  pointer temp for `ADRL`, global places use `&g:off` directly (`COPY` accepts address-with-offset
  operands - corpus-verified); emit exactly one `COPY *sizeof`. Statement-level: a struct-typed
  assign result is a no-value marker; use in a nested expression is an error.
- `releaseMeta` on places is naturally safe (`returnBack` ignores non-`%`/`<` operands), but the
  borrowed `ADRL` temps in whole-struct assign must be returned - same discipline as `copy()`.

**Testing:** golden files cannot catch struct-lowering bugs (no legacy coverage), so every behavior
is asserted by **running fixtures on GAZLCmd** with expected output - the `structPointers.impala`
pattern: locals, globals, nesting three deep, mixed `.`/`->` chains, whole-struct assignment in
all base-kind combinations, and read-back verification after every write.

## Slice 2.4: struct arrays + brace initializers

- `Filter array banks[4]` = `LOCA`/`GLOB *(4*sizeof)`; constant index → place with
  `offset = i*sizeof + …` (still free); dynamic index → stride `MULi`, base becomes a computed
  pointer temp, terminal access is `PEEK`/`POKE`/`GETL`-style - the Step 1 dynamic-index shapes.
  **Lifts error E414 - DONE 2026-08-13, by unifying the fill.** There were three array-fill loops - a
  struct FIELD's, a standalone array's, and the shaped walk - each re-deciding what the extent meant, and
  E414 was one of them deciding it needed a compile-time count. They are now ONE `fillArray`/`fillAxis`
  taking a slot record (`{ name, elem, size, dims }`, which a field entry and an array declarator already
  share); a 1-D array is just the rank-1 shape `[size]`, so there is no separate 1-D path left to drift.

  **The extent is a CHECK, never a fill bound**, and the two checks a symbolic extent needs differ because
  the layout consequences do: the OUTERMOST axis may stop short (a prefix - the region zero-fills the
  rest), so only `words <= .z.<name>` must hold; an INTERIOR axis must be exactly full, because a short
  group there leaves a gap of unknown size that positional `DATA` cannot skip, so it is asserted against
  `.d.<name>.k` with `! EQUi`. At rank 1 there are no interior axes and the rule degenerates to "place the
  prefix, assert it fits" - which is E414's case, now with nothing special written down for it. A symbolic
  OUTERMOST axis gained a prefix fill for free; it previously demanded an exact count.

  **Short fill is decided once, in `emitInitData`**, which drops a trailing run of zero words - exactly
  what the region supplies (verified on GAZLCmd for a `CNST` section as well as a `GLOB` one). That is what
  lets every fill loop pad honestly to its axis, which is what keeps a short INTERIOR group from shifting
  everything after it, while a `[100]` array given 14 entries still emits 56 words rather than 400.
  Covered by `tests/impala/sources/structArrayGiven.impala`.

  **`InitList` STAYS A SEPARATE PATH, and the reason is the scratch pool - do not re-attempt the merge
  without answering it.** The flat list writes its `DATA` rows WHILE PARSING; everything else buffers the
  words and writes them at the end. Merging the flat list into the buffered filler was built and reverted
  2026-08-13: an entry like `STRIDE + 1` on a named const is folded by emitting `! ADDi <A> ...` and using
  `<A>` as the word, and `declare` returns a scratch to the pool only when it writes the row quoting it. The
  pool is `<A>`..`<Z>`. Collecting entries before placing them therefore holds one scratch per computed
  entry and aborts at 27 with "compile-time scratch pool exhausted"; measured exactly - 26 entries compile,
  30 do not - while the streaming form has no limit. `chess.impala` and `Priyome.impala` are the corpus
  programs that prove it. The merge is possible only by making the shared writer EMIT AS IT FILLS, which
  additionally needs a pending-zero run (so a trailing run can still be dropped, and an interior one still
  written) - a real design, not a rename. An emit-as-you-fill sink WAS built and reverted the same day: it
  is byte-identical and costs 20 lines, but it does not lift the cap, because the scratches are borrowed
  while the tree is PARSED, before any placement can free one.

  **The CAP itself is gone, separately (`holdConstant`, `tests/impala/sources/initSpill.impala`).** Once the
  pool is empty the next computed entry is captured into a named define and the scratch handed straight
  back, so at most one is ever tied up and a nested initializer has no entry limit (200 verified). It looks
  unsound and is not: the emitted line naming `<A>` is its DEFINITION, which is what gives the capture its
  value, while the only pending reference is an operand string still held in the entry - so redirecting it
  costs nothing. This is the same move `.z.`/`.d.` already make (`! MULi <A> ...` then `.z.g: ! DEFi #<A>`).
  Only entries past the pool spill, so every initializer that already fitted emits exactly what it did
  before - no golden moved.

  What the merge attempt also landed is the rule it exposed: a scratch gets a row to itself in the buffered
  writer too, without which `readonly S s = { x: 1 << P }` never compiled at all
  (`tests/impala/sources/structInitScratch.impala`).
- Brace initializers recurse the existing `InitList` machinery with a field cursor: each value
  checked against the field type, nested `{}` descends into struct/array fields, trailing
  omission zero-fills. Lowering is the flat `DATA` rows the braces describe.

> **Step 4 DONE (VM-verified, `multiReturn.impala`).** 4a: comma `returns` -> N `OUT`
> slots, `signature.returnList/returnCount`, `-> (t1,t2)` metadata, E430/E431 guards. 4b:
> caller side - `a, b = f(...)` destructuring. The call reserves a leading N-slot output
> window (`claimSlot` past `borrowForCall`); args land after it (`base + retSlots-1 + i`),
> `CALL ... *(count+retSlots)`; each target read out via `assign` (`_` discards, `global g`
> targets supported), single-return path byte-identical (golden 0/67).
>
> **Slice 2.5 DONE too - by-value struct params + returns.** Struct param -> input `PARA
> *sizeof` (place, read-only-ish); struct return -> leading output `PARA *sizeof`. Caller arg
> machinery moved from per-arg count to a running-WORD counter (`copyStructArg` = reserve slots
> + ADRL window + ADRL source + COPY; scalar-only path byte-identical). Struct return = a window
> place (`winBase`/`winWords`) freed by `freeStructWindow` once consumed (assign / arg / discard);
> nested struct-return-as-arg is adopted in place (windows slide, no self-copy). E421/E423/E430.
> VM-verified: structByValue, structReturn. gate 0/69.
>
> **The machinery described above was REMOVED from the compiler on 2026-08-07** (`winBase`/`winWords`,
> `freeStructWindow`, the return-window `ADRL`, the multi-return window, and `E423`, which no longer
> exists as a diagnostic). E427 rejects a struct return and E428 a second return value, both at the
> DECLARATOR, so no call could reach any of it. Kept here as the design a 3.0 revival restarts from -
> not as a description of code that is present.

## Step 4 + slice 2.5: returns and by-value - one window convention

> **PARKED for Impala 3.0.** This slice shipped and was then removed; the work is preserved at the
> `Impala3-byvalue-multireturn-park` tag. Impala 2.0 rejects multi-value returns (`E428`),
> destructuring (`E429`) and by-value struct params/returns (`E426`, `E427`). Kept as the design and
> experiment record. See `design/ParkedFeatures.md`.

**Experiment 1 (decisive):** a labeled `PARA` section works as a first-class local:

```gazl
helper: FUNC
    $s: PARA *3         ; labeled 3-word parameter window
    MOVi $x $s:1        ; direct :offset read - works
    MOVi $s:0 #99       ; write - works (read-only is compiler-enforced)
    GETL %11 $s:1 %12   ; runtime offset off a :const base - works
```

So: **a by-value struct parameter is one labeled `PARA *sizeof`** declared in parameter order;
fields are free direct operands in the callee (the perf win the spec records). **A by-value struct
return is a leading labeled `PARA *sizeof`** the callee writes as `$out:off`. Scalar multi-return
(Step 4) is the same convention with N scalar `OUT`s. (One small pre-Step-4 experiment remains:
two scalar `OUT`s + caller reading `%b..%b+1` - the docs say `*size` counts both, and the layout
is declaration-order, so this is expected to pass.)

**Experiment 2 (decisive):** caller-side copying into the argument window works:

```gazl
    ADRL $q %0 *3       ; address of the transient call window - legal
    COPY $q $p *3       ; copy the struct value in
    CALL &sum3 %0 *3    ; callee sees 7,8,9 - verified (prints 24)
```

Caller-side implementation: claim `sizeof` consecutive window slots (the existing `makeArgValue`
slot-claiming loop, run per word), then `ADRL` + `COPY`. Call `*size` counts **words**, which the
`CALL` documentation already specifies. Receiving a struct return: `v = f(...)` is statement-level;
after `CALL`, `ADRL` the destination place + `COPY` from the window.

Signature entries gain `words`; arity checking counts words; by-value vs by-pointer mismatch keeps
the two fix-it notes from the spec.

## Step 5: import-as-linking + `--dead-strip`

> **PARTLY IMPLEMENTED (status added 2026-07-28).** `import`, `export`, `--dead-strip`, the closure
> walk and per-unit dedup all shipped. **Collect mode did not.** The builder took a shortcut the plan
> below does not describe: it concatenates the closure into one source and compiles it *once*, in
> emit mode, rather than gather -> resolve -> codegen. That gets correct cross-unit codegen for free
> and moots the per-unit seed requirement, but it leaves the compiler single-pass over the
> concatenation - so the cycle claims further down are **not** true today. A backwards reference
> across a cycle fails (`E403` for a function, `E413` for a struct type), and which direction fails
> depends only on which unit is named as root. Fixture: `tests/impala/sources/importcycle/`, pinned
> in `impala/importBuildTests.js`. Current behaviour and the route out are written up in
> `docs/impala/Impala2.md` under "Cycles" and "Deferred to 3.0: collect mode"; the architecture in this
> section is still the plan of record for that work.
>
> **Collect mode DEFERRED to Impala 3.0 (2026-07-29)** - see `design/ParkedFeatures.md`. Half-resolved
> cycles are the shipped 2.0 rule, not a pending fix: a backwards cross-cycle reference takes a
> forward `extern`, which covers functions and globals (a cross-cycle struct type does not, and has
> no workaround short of breaking the cycle). Landing the pre-pass later makes those externs
> unnecessary without making them wrong, so the deferral raises no compatibility question.

**Division of labor: the compiler gains a collect-only mode; the builder owns the closure.**

**Architecture (revised 2026-07-20 per the thin-action/handler discussion): `$$parser` already IS
the semantic-handler object the grammar dispatches to.** Finish thinning the remaining fat inline
actions into `$$parser` methods, then give `$$parser` two modes:

- **emit mode** - today's codegen.
- **collect mode** - the import/interface parser: declarations register in the symbol/struct/type
  tables **with type references recorded by NAME, not eagerly resolved**; function bodies are
  parsed but their codegen calls no-op (or blocks are brace-skipped). Emits nothing.

**This is declaration-level two-phase, and it is all import cycles need** - distinct from the
expensive body-level two-phase (the AST rework that fixes `dry`/backtracking). Two independent
moves:

- *Declaration-level two-phase* (gather decls across the closure, then resolve names): cheap,
  bounded, unlocks cycles. Delivered by collect-mode + deferred resolution.
- *Body-level two-phase* (AST of expressions, resolve/emit later): an `impala.jspeg` action rewrite
  (`design/jspeg/JSPEGFuture.md` Problem 1), not a JSPEG change; cycles do **not** need it; still deferred.

- Grammar gains only: `import "path"` and the `export` declaration modifier (`export` emitted as a
  role prefix in the `; signature` rows; validator's `classifyRole` extended to accept it).
- The **builder** (the closure walk behind `impala compile`): (1) **gather** - walk the import closure
  (visited-set by canonical path), parse every unit in collect mode, merge declarations into one
  closure-wide interface with names still symbolic; (2) **resolve** - resolve all type/name
  references against the merged interface (by-value containment cycles caught here as infinite
  size); (3) **codegen** - compile each unit in emit mode against the complete tables, concatenate
  in closure order.
- **Per-unit seeds are mandatory** - experiment 3 proved it: two units compiled with the same
  `randomId` that contain the same string constant collide at link
  (`Symbol already defined: .s_shared_4d2`). This is a *pre-existing* landmine of the manual
  workflow. The builder derives each unit's seed deterministically (user seed ⊕ hash of canonical
  unit path), making collisions impossible by construction and builds reproducible.
- **Cycles are legal** (revised - earlier draft here retreated to a build error). The gather →
  resolve → codegen order means A↔B mutual references resolve regardless of order: gather records
  `Node.partner : ptr-to-"B"` and `B.partner : ptr-to-"Node"` unresolved, resolve links them, and
  bodies codegen against complete tables. The single-pass *body* compiler is never asked to
  forward-reference a type. (Bonus: cross-unit forward function references also resolve without
  `extern`, byte-safe - existing externs become redundant, not wrong. Whether to also make the
  standalone single-unit path gather-first is a separate, byte-safe option, not required here.)
  **Unbuilt - this paragraph describes the target, not today.** Without collect mode the body
  compiler *is* asked to forward-reference, and refuses; the `extern` is required rather than
  redundant. See the status note at the top of this section.
- **`--dead-strip` is a text-level `.gazl` transform in the builder, not compiler logic.** The
  output is line-structured: labeled `FUNC` blocks, labeled `GLOB`/`CNST`/`TEMP` data blocks,
  `! DEF` rows. Build a reference graph from operands (`&name`, `^name`, `#name`), roots from
  `; signature export …` rows, mark-and-sweep, drop dead blocks plus their metadata rows. Zero
  compiler risk, independently testable against artifacts, and testable *without* the language
  feature by hand-writing inputs.

## Order of work

1. **2.2+2.3** - the place architecture (locals, globals, nesting, whole-struct `=`), VM fixtures.
2. **2.4** - struct arrays + initializers.
3. **Step 4** - multi-`OUT` experiment, then scalar multi-return + destructuring.
4. **2.5** - by-value params/returns on the window convention.
5. **Step 3** - `functype` (independent, low risk).
6. **Step 5** - builder + import + `export` + `--dead-strip`, with the cycle amendment.
7. **Collect mode** - ~~the one piece of Step 5 still outstanding~~ **DEFERRED to Impala 3.0**
   (2026-07-29, `design/ParkedFeatures.md`); half-resolved cycles are the 2.0 rule and `extern` is the
   answer. Kept here as the plan of record. Precondition: finish thinning the fat inline actions into
   `$$parser` (see the architecture note above, and `design/jspeg/RefactorPlan.md` for the adjacent
   return-style cleanup on the same surface) - worth doing on its own, since it also shrinks what a
   later "JSPEG 2" would have to migrate. Not gated on the body-level AST rework - the split is in
   "declaration-level vs body-level two-phase" above. Done when
   `tests/impala/sources/importcycle/odd.impala` builds as a root with its `extern` deleted.

Each lands as a separate commit behind the full gate (regenerate → jspegCompilerTests →
runJspegTests golden → full build → VM-run fixtures).
