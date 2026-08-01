# Impala 2.0 review pass (2026-07-29)

Status: REVIEW NOTE. A full-language review of 2.0 against 1.0, against the planned 3.0, against itself,
and against its toolchain. Everything marked **[V]** was reproduced on this machine by compiling and, where
relevant, running under `output/GAZLCmd.exe`. Unverified claims are marked as such and should be treated as
leads, not facts.


## A. What changed from Impala 1.0 (raw material for a What's New)

Baseline for the diff is `main:impala/impala.jspeg` (2938 lines) vs HEAD (5022).

### New features

| Feature | 1.0 | 2.0 |
|---|---|---|
| Typed pointers | `pointer v` + `(int) v[i]` casts | `int pointer v`, `v[i]` |
| Typed arrays | `array a[4]` | `int array a[4]`, `int pointer array t[8]` |
| Structs | hand-rolled `const int Filter_cutoff = 0` offsets | `struct Filter { float cutoff; ... }`, `.` / `->` |
| Struct layout | n/a | symbolic `.o.Struct.field` / `.z.Struct` GAZL constants |
| `extern struct` | n/a | host-owned layout, supplied at load, no recompile |
| Typed funcptrs | `funcptr f` (unchecked) | `functype Fn(int) returns int` + `Fn f` |
| Modules | manual `.gazl` concatenation + hand-copied `extern` | `import "x.impala"`, `export`, `--dead-strip` |
| Extern prototypes | `extern native printInt` (asserts nothing) | `extern native printInt(int n)` (arity + type checked) |
| Inlining | n/a | `inline function` — **undocumented in both language docs** |
| `sizeof` | n/a | `sizeof(Type)`, in WORDS, type-name form only |
| Strict expressions | `a \| 250 & 120` silently mis-parses | E101 / E102, `--legacy` lowers to warning |
| Diagnostics | bare messages | `path:line:col: error[Ennn]:` + caret + fix-it note |

### Breaking changes

1. **Six new reserved words**: `export`, `functype`, `import`, `inline`, `sizeof`, `struct`. Not restored by
   `--legacy`.
2. **E101** mixed bitwise operators at one parenthesization level.
3. **E102** unparenthesized bitwise directly against a comparison.
4. **E201/E202/E203** element-type mismatches — reachable in old code because `&` became type-producing and
   string literals became `int pointer`.
5. **E437/E438** extern prototypes are now checked against a definition in the same closure.

`--legacy` gates **exactly two** things: E101 and E102. `strictError` (`impala/impala.jspeg:1259-1266`) has
precisely two call sites; every other diagnostic calls `$$parser.fail` directly. **[V]**

### Deliberately NOT changed

No compound assignment or `++`. Comparisons are still not values. Bitwise/shift still share one precedence
level looser than `+`/`-`. Arrays still one-dimensional. `for (v = a to b)` and `switch (e == lo to hi)`
unchanged. Four built-ins only (`abs`, `floor`, `itof`, `ftoi`). Casts still reinterpret, never convert.
`global` prefix still mandatory at every global access.


## B. Biggest shortcomings vs the planned Impala 3.0

Ranked by pain. All parked work is *additive* in 3.0 except item 5 — nothing here says "avoid a pattern
today" except that one.

1. **No by-value structs, no multi-return, no destructuring** (E426-E429). You write pointer
   out-parameters: `addVec(&s, &p, &q)` instead of `s = addVec(p, q)`. Cost is inside the callee — the
   golden diff in `e6ad36d` shows `addVec` going from 2 instructions to 8, because by-value fields are free
   window operands and by-pointer fields are a `PEEK`/`POKE` each. Implemented and VM-verified on
   `Impala3-byvalue-multireturn` (an ancestor of this branch), then parked by `e6ad36d`; `Impala2` has since
   drifted 96 commits / +1031-360 lines in the grammar, so restoring it is a re-implementation against a
   working reference, not a cherry-pick.
2. **Import cycles only half-resolve.** A backwards cross-cycle reference needs a forward `extern`, and the
   *same two files* build or fail depending only on which is the root. A cross-cycle **struct type** has no
   workaround at all — there is no forward-`extern` form for a type. Collect mode (the fix) is designed but
   never built; done-when is pinned (`importcycle/odd.impala` builds as root with its `extern` deleted).
3. **No multidimensional arrays.** You hand-stride flat arrays (`grid[y * W + x]`), and the stride becomes a
   duplicated, unchecked invariant. The obvious escape — put the matrix in a struct — does not reach
   `extern struct`, because a host-owned array field must state no extent (E430) and the inner extent *is*
   the stride.
4. **The diagnostics backlog** (`docs/CompileTimeHardening.md`) — five shapes the compiler accepts that the
   assembler then rejects, naming a compiler-minted GAZL symbol instead of the `.impala` line.
5. **Implicit array→pointer decay is still live and it is decided to go.** Write `&a[0]` today — it is
   correct under both rules and costs nothing now. (An earlier draft of this line said `&a`; that is wrong,
   `&a` is `E404 Invalid lvalue` today, which also means blocking decay is not a pure removal. Recorded in
   `docs/ParkedFeatures.md`.) See section C for why this is the only forward-compat item.
6. Numeric-only by-value call windows (E425 — not a dead diagnostic but a never-written one), the mandatory reserved return
   transient (an ABI change), and dead-arm elimination after a compile-time branch. None reachable today.

### Undocumented 3.0 direction

`docs/ParkedFeatures.md` does **not** record the decay decision — it lives only in
`docs/MultidimensionalArrays.md` on the parked branch, a file deleted from `Impala2`. The one item a 2.0
user should act on today is invisible from the index. Same for `docs/Impala2OpenItems.md`, an entire backlog
that survives only on that branch.


## C. Things that will trip humans and AI agents

Ordered by danger. Silent wrong behaviour first.

### C1. `--dead-strip` silently corrupts retained array data **[V]** — CRITICAL

`impala/impalaImportClosure.js:216-231` treats a labelled data definition as a block of **exactly one line**.
The unlabelled `DATA` continuation rows carrying an initializer become `loose` blocks, which are kept
unconditionally (`:267`). Strip a dead array's header and its rows are adopted by the preceding block:

```impala
readonly int array KEPT[5] = { 9, 9 }     // trailing zero-fill
readonly int array DEAD[3] = { 1, 2, 3 }  // unreferenced
```
`KEPT` reads `9 9 0 0 0` normally and **`9 9 1 2 3` under `--dead-strip`**, with no error at any stage.
No fixture covers it: `tests/impala/sources/deadstrip/striplib.impala` has two functions and no data.

### C2. `--dead-strip` breaks the canonical firmware idiom **[V]** — HIGH

`collectRefs` (`impalaImportClosure.js:194`) is `/[&^#]([A-Za-z_]\w*)/g`. It does not recognise the `*size`
operand form, so a constant used only as an array extent looks unreachable:

```impala
const int N = 16
global int array buf[N]      // --dead-strip: Symbol not previously defined: N
```

`global int array params[PARAM_COUNT]` is exactly the shape `docs/Impala2.md:207-210` recommends.

### C3. `p + 1` does not stride but `p[1]` does **[V]** — CRITICAL

`SUPPORTED_OPS` has a single `'+pi' -> 'p'` rule (`impala/impala.jspeg:187`) with no element-size scaling,
while `subscriptStruct` folds `! MULi <A> #1 #.z.S`. On a `S pointer` into `bank[3]`:

| expression | result |
|---|---|
| `p[1].a` | `bank[1].a` — correct |
| `q = p + 1; q->a` | **`bank[0].b`** |
| `q - p` across two structs | word distance, not element distance |
| `for (p = &bank[0] to &bank[2])` | `sizeof(S)` times too many iterations |

`docs/Impala2.md:159-164` promises the opposite, and `docs/Impala.md:451` states `p[i]` is equivalent to
`*(p + i)`. Either scale by `.z.T` or hard-error on arithmetic over a struct pointer.

### C4. Shift-vs-additive precedence — INVESTIGATED, NOT A BUG **[V]**

Kept in the list because it looks like one, was reported as one, and cost a round trip to disprove.

`x = a << n + 1` compiles as `a << (n + 1)`:

```gazl
ADDi %0 $n #1      ; x = a << n + 1
SHLi $x $a %0
```

**That is what C does too.** C ranks additive (level 4) *tighter* than shift (level 5), so `a << n + 1` is
`a << (n + 1)` there as well. Verified on both sides: Impala prints 64 for `a = 8, n = 2`, and so do C and
JS, which share the precedence table. `1 << 2 + 3 == 32` is a well-known C pitfall precisely because the
language really does bind it that way.

So `docs/Impala.md:375` and `docs/Impala2.md:1044` ("Arithmetic-vs-bitwise actually agrees with C") are
**correct as written** - do not "fix" them.

The whole picture, since it is easy to half-check and conclude wrongly: Impala puts `<< >> >>> & ^ |` at
ONE level below `AddSub`; C spreads them across levels 5, 8, 9 and 10, all still below additive. The
relationship *to arithmetic* is therefore identical. Where the two differ is *among themselves* - C reads
`a | b & c` as `a | (b & c)` where Impala left-associates - and that is exactly what **E101** already
rejects: any two DIFFERENT bitwise operators at one parenthesization level. A same-operator chain
left-associates identically in both. On precedence Impala therefore either matches C or hard-errors, which
is the design goal met rather than missed.

Lesson worth keeping: the codegen above was verified, and the *conclusion drawn from it* was not. `[V]`
on an observation does not transfer to the claim built on top of it.

### C5. `E403 Undeclared identifier` on a global that plainly exists **[V]** — HIGH FREQUENCY

`x = G;` for a `global int G` reports `Undeclared identifier: G` with no note. The mandatory `global` prefix
is a sound marker-discipline device (one `global` = one `PEEK`), but this is the first error a newcomer or a
code-generating agent hits and the message points the wrong way. The symbol table already knows; one note
(`note: G is a global - write "global G"`) fixes it. Same for `global x` on a local.

### C6. Other silent-wrong shapes **[V]**

- **A declared return value never assigned returns stale frame garbage.** No definite-assignment analysis;
  decidable single-pass for the trivial case.
- **`copy` bypasses the pointer type system entirely.** `copy(8 from &intSrc[0] to &floatDst[0])` and a
  4-word overrun of the destination both compile silently, even with both extents known at compile time.
- **Locals are not zero-filled, and `docs/Impala2.md:430` says they are** ("Uninitialized struct storage is
  zero-filled" — true for globals, false for locals, stated without qualification).
- **`&local` returned from a function** points into the next callee's frame. No diagnostic, no trap.
- **`extern struct` host layout is essentially unchecked**: overlapping offsets, offsets past `.z.Name`, and
  int↔float retypes are all undetected. Only a renamed/removed field fails, at load.
  `docs/ExternPrototypes.md:26-28` claims gazl-validate catches drift; it is name-presence-only and does not
  run at all in the motivating use case. `docs/StructLayoutConstants.md:227-229` correctly calls it deferred.
- **Constant OOB through a pointer** (`p[9].a`) is caught nowhere. Through an array — local or global — the
  assembler does catch it, which makes `CompileTimeHardening.md:80` backwards on which case is uncovered.
- **`for`'s upper bound is live or frozen depending on the shape of the bound expression** — a plain local
  emits `FORi $i $n` (re-read each iteration); anything else is snapshotted into a scratch.
- **Typed-array initializers are not element-checked.** `global int array T[4] = { 1, 2.0, 3, 4 }` compiles,
  contradicting `docs/Impala2.md:260` which states it is an error. `InitList` (`:4042-4079`) uses each
  item's own type and never consults `$a.elem`. Global initializers likewise bypass the pointer-element,
  funcptr-signature and inline-address checks that the same statement gets inside a function — and
  `global funcptr fp = inlineFn` emits `DATp &f` for a symbol that is never emitted.
- ~~**A brace initializer over a symbolically-sized struct field emits a short, misaligned DATA row.**~~
  FIXED (`E454`). `struct S { int a; int array v[N]; int z }` with `N` a const gave `DATA #1 #2` where the
  literal `[3]` gave `DATA #1 #7 #8 #9 #2` — the `2` meant for `z` landed in `v[0]`, with the `GLOB` still
  correctly sized `*.z.S`. Root cause: `fieldWords` multiplied the extent OPERAND (`'N'`) by a number and
  returned NaN, which flowed unchecked into `s.words`; `buildStructInit`'s `e < f.size` then compared
  against NaN and ran zero times. Note the *layout* was never wrong — `emitStructLayout` emits
  `! ADDi <a> #<a> #N` and the assembler resolves it, so allocation, nesting, struct arrays, locals, field
  access, `COPY` and `sizeof` were all correct throughout. `fieldWords` returns `undefined` rather than
  NaN, and the two checks that read `structWords(...) === undefined` as "incomplete" now ask
  `structDefined` instead — otherwise a symbolically-sized struct could not be nested by value or passed
  to `sizeof`.
  **A symbolic extent can still be initialized**, and the first fix wrongly rejected that. `DATA` may
  define FEWER words than its region holds, with the remainder zeroed (`docs/InstructionSet.md:96-99`), so
  the values given simply land at the front: `struct S { int a; int array v[N] }` with
  `{ a: 1, v: {7,8,9} }` emits `DATA #1 #7 #8 #9` and assembles. What has no answer at Impala compile
  time is the offset of a field placed *after* the symbolic one. Verified against the assembler, three ways: there is no fill or repeat
  directive in `docs/InstructionSet.md`; a FORWARD `! GOTO` assembles and can even skip a `DATA` line; a
  BACKWARD one does not assemble at all (`Compile time label not found`), so no assemble-time loop can
  emit a symbolic number of words. Hence `E454` — but only when a later field is given a NON-ZERO value.
  Zero costs no words, so `{ a: 1, v: {7,8,9}, z: 0 }` is accepted and simply stops the row early, landing
  exactly where the zero-fill would; omitted fields were always fine. The hint leads with omitting those
  fields or moving the symbolic array last, NOT with "give it a literal size" — per
  `docs/TwoStageConstants.md`, steering a user toward pinning an extent numerically is the anti-pattern,
  not the fix. Over-filling is left to the assembler, per rule 2 there — Impala does not know `N`, so it
  must not guess. Note the assembler does NOT catch it on its own: over-running the whole section is
  named (`Not enough space in data section: s`), but over-running one field INTO the next is a legal,
  in-bounds write it has no notion of. So Impala emits the comparison for it (`! GRTi #3 #.x.S.v @.ERROR...`,
  2026-08-01), which is the only thing standing between that shape and a silent spill.
- **Duplicate `case` labels** and **out-of-range `case` labels** are both accepted silently; a case *below*
  the low bound emits `.s0.-6` and the module will not load at all.
- **`switch (x == lo to hi)`: `hi` is exclusive**, and `docs/Impala.md:328` says inclusive.

### C7. C reflexes that are rejected with a bare `E001: syntax error`

No expected-set, no note. For a language whose stated audience is strangers and AI agents, this is the
largest DX defect: an agent iterating against diagnostics gets a caret and nothing else.

| You write | Reality |
|---|---|
| `if (x)`, `while (1)`, `if (!x)` | conditions require a `COMP_OP`; write `if (x != 0)` |
| `flag = (a < b)` | comparisons and `&&`/`\|\|` are not values, anywhere |
| `return r;` / `break;` / `continue;` | not keywords → `E403 Undeclared identifier: return` |
| `int i;` inside a body | no declaration statement; all locals in the `locals` clause |
| `copy(dst, src, n)` | `copy(N from SRC to DST)` — count first, and src/dst reversed vs `memcpy` |
| `abs x - 1` read as `abs(x - 1)` | `abs`/`floor`/`itof`/`ftoi` are prefix operators, so this is `(abs x) - 1`. (`abs(x)` does compile — the parens are just a parenthesized expression — but `abs()` and `abs(x, 2)` do not.) |
| `function f(int array a)` | array parameters do not exist (but `locals` accepts arrays — the two lists differ) |
| `1e6` | `FloatLiteral` requires `DIGIT+ "." DIGIT+` first |
| `&arrayName`, `&funcName` | `E404 Invalid lvalue`; write `a` / `&a[0]` and `fp = g` |
| `(StructName) expr` | `E403 Undeclared identifier: StructName` — write `(S pointer)` |

One bright spot worth advertising: `if (x = 1)` is structurally impossible, because assignment is an `Expr`
and conditions need a `COMP_OP`.

### C8. Internal inconsistencies a reader must simply memorize

- **Three signature grammars.** `function` and `extern function` require parameter names *and* a return name
  (a meaningless dummy for an extern); only `functype` allows types-only.
  `docs/Impala2.md:659` claims they mirror each other — **[V]** false.
- **Separators are inverted**: struct fields take `;`, `locals` takes `,`. Neither accepts the other.
- **Semicolons**: top-level declarations take none, every statement takes one, `do {...} while (c)` takes
  none.
- **Three declaration keywords (`global`/`readonly`/`temporary`), one access keyword (`global`).**
- **Modifier order is rigid and undocumented**: `extern global int X`, `readonly global int X`,
  `function inline g()`, `export struct S` are all `E001`.
- **`assert` defers its own prerequisites to the assembler** — with no `const int DEBUG` and no `assertFail`
  in the link set the Impala compile succeeds and the failure surfaces at load.
- **Initializer errors are anchored on the *next* declaration** **[V]** — `$$i` has already skipped
  whitespace. When the bad declaration is last in the file the caret lands past EOF and no source line
  prints at all. The grammar already has the fix for the metadata path (`$$parser.declOffset`, described
  verbatim at `:3349-3352`); it was not applied to the error path.
- **`docs/Impala.md:261` says forward references work.** They do not — but the E403 note is excellent, and
  even finds the later definition.
- **`docs/Impala.md:316-324`'s documented `goto`-out-of-loop idiom does not compile** — a trailing label
  needs `finished: ;`. It is the only documented way to break out of a loop.
- **`docs/Impala.md:28-30`'s reserved-word list is missing all six new keywords**, so it gives a false
  all-clear on exactly the names most likely to collide.
- **Every `.gazl` still says `; Compiled with Impala version 1.0`** (`impala/impala.jspeg:132`).

### C9. Documented-but-absent, and absent-but-shipped

- `docs/StructLayoutConstants.md:220-226` lists **E418, E424, E425** as implemented extern-struct guards.
  All three have zero fail sites **[V]** — array fields, by-value nested fields, nested access at non-zero
  offset, and by-value extern instances all compile clean.
- `(funcptr array) table` casts (`docs/Impala2.md:218`) do not parse — the cast grammar has no `array`.
- `impala build` does not exist; the subcommands are `compile` and `run`. There is no `impala` binary at
  all — every doc example that writes `impala compile ...` is aspirational.
- `--json`, `--emit-metadata`, `--no-metadata` do not exist. The complete flag set is `--legacy` and
  `--dead-strip`.
- `import "x.gazl"` blob imports do not work — the closure walker parses every import as Impala source.
- **The E-code registry in `docs/Impala2.md:1136-1160` is ~29 codes behind** (E410-E442 are all
  implemented and undocumented).
- **`inline function` is shipped and appears in neither language doc.** It is a real optimization pass with
  an aliasing analysis and argument-move deletion, which qualifies both design principle #1 ("2.0 adds no
  hidden optimization passes") and the "instruction count = marker count" cost model. Opt-in by keyword, so
  not hidden — but the headline invariants are stated without the exception.
- **The legacy manual-concatenation struct model no longer links** **[V]**. Two byte-identical `struct`
  declarations in two units now collide (`Symbol already defined: .o.Filter.mode`), because Phase 2a made
  every struct emit `! DEFi` layout rows. The whole "Identity across concatenation" section of
  `docs/Impala2.md:579-632` is dead text; the working pattern (one unit `struct`, the rest
  `extern struct` with a body) is documented nowhere.
- `docs/StructLayoutConstants.md:198-209` *understates* the design: it claims nested/array field access
  costs an `ADDp`/`ADRL` per level. **[V]** It emits assemble-time folds and one `GETL`. Dots really are free.


## D. Toolchain clarity

### D1. The README's Getting Started command is wrong and fails silently **[V]**

`README.md:49-52` passes `0x4d2` where `impala.nuxjs.js` expects the **output path**. It exits 0, writes the
real GAZL to a file named `0x4d2`, and leaves `demo.gazl` empty — then `GAZLCmd` reports
`Code size: 0 ... Could not locate function: main`. `docs/UsageExample.md` has the correct 4-argument form.
Compounding it, `impala.nuxjs.js` prints errors to **stdout**, so a `>` redirect captures diagnostics into
the "compiled" file.

### D2. "gazl-validate is not the assembler" is documented only in a source comment

The best explanation in the repo is `impala/gazlAssembleCheck.js:1-10`, a file mentioned in **zero** `.md`
files. `docs/Impala.md:530+` describes gazl-validate at length without ever saying GAZLCmd is the real one.
Same for **GAZLCmd having no assemble-only mode** and the bogus-entry-point workaround — documented only at
`gazlAssembleCheck.js:78-81`. A newcomer who "just wants to check it assembles" runs the program, which for
the `Priyome` fixture is an interactive chess game.

### D3. `build.sh` and `build.cmd` run different gate sets **[V]**

`AGENTS.md:28` and `:55` claim they are mirrored and identical. `build.cmd` runs `importBuildTests.js` and
`fuzzImpala.js 3000 1`; `build.sh` runs neither. CI runs `.cmd` on Windows and `.sh` on macOS/Linux, so two
of five gates are Windows-only — and `AGENTS.md` tells every contributor and agent to run the weaker one.

### D4. There is no way to run only the JS gates

`AGENTS.md` says run `./build.sh`, which needs a C++ toolchain. The four JS gates
(`updateJSPEG --check`, `jspegCompilerTests`, `runJspegTests`, `importBuildTests`) total **under 10 seconds**
and there is no single command for them. Largest missed opportunity in the repo.

### D5. Smaller, all verified

- `node tools/gazl-validate.js` crashes with `ReferenceError: print is not defined` — it is a NuXJS script
  with a bare `.js` extension, while `tools/` already has a `*.nuxjs.js` convention it does not follow. Even
  the correct `tools/gazl-validate.sh` with no args prints usage *and then* an internal stack trace.
- `regen-jspeg-fixtures.cmd` validates all fixtures in one call and fails on duplicate `main`; its `.sh`
  twin carries a comment explaining exactly why they must go one at a time.
- `output/impala.nuxjs.js` is a **stale copy** of `impala/impala.nuxjs.js` (5 days behind), and every doc
  example points at the stale one.
- `impala/impalaCompiler.js` has **no generated-file banner** — line 1 is `var $$parser = {};`. It is
  git-tracked, so it looks hand-maintained. `tools/genbench.sh:58` already emits such a banner into its
  outputs.
- **Only 2 of 12 entry points self-document.** `runJspegTests.js --help` silently runs the entire golden
  gate; `importBuildTests.js --help` runs the whole suite; `fuzzImpala.js --help` prints `NaN programs` and
  **exits 0**; `GAZLCmd` with no args exits 0 and with `--help` errors.
- **Two independent golden-fixture systems with near-identical names**: `tests/impala/{sources,golden}`
  (owned by `runJspegTests --makegold`) and `impala/testdata/*.expected.gazl` (owned by
  `tools/regen-jspeg-fixtures`). Adding a fixture to the wrong one silently gets you no coverage. The split
  is explained in `jspegCompilerTests.js:677-681` — again, a source comment.
- **`impala/` has no README**, and `docs/Overview.md` — billed as general architecture — contains the word
  "impala" exactly once.


## Decision: pointer arithmetic scales by element size (2026-07-29) - SUPERSEDED

> **Superseded 2026-07-30** by "the scaled subscript is spelled `[[ ]]`" at the end of this document.
> This decision was reached without knowing F1 and F2 below: the elements rule leaks into pointer
> comparison, where there must be no unit at all, and cannot reach `for`, where `FORp` has no room for a
> stride. It is kept in full because its reasoning about the *unit* still holds and the replacement builds
> on it.

Settled after arguing it in both directions. **One rule, no exceptions: pointer arithmetic is in
elements.** `p + n`, `p - n` and `p - q` all scale by the pointee size. Untyped `pointer` and word-sized
element types have stride 1, so nothing about them changes; struct pointers go from silently wrong (C3) to
correct. No new diagnostic - nothing is rejected.

The three arguments that were tried and abandoned, recorded so they are not re-litigated:

1. *"Scaling hides a `MULi`, breaking instruction count = marker count."* Dead - `&a[b]` on a struct array
   already emits one. The rule was traded away when structs got subscripts.
2. *"Scaling unlocks the cheap pointer walk."* Dead - `! MULi` is an assemble-time fold, so `p = &p[1]`
   already compiles to a single `ADDp`. Both spellings produce identical codegen; there is no performance
   argument in either direction.
3. *"Accept `MULi` for `+` but reject `p - q` because `DIVi` is expensive."* Dead - cost is not the rule,
   predictability is, and a division is exactly as predictable as a multiply. Rationing exceptions by
   how expensive they feel is not a principle.

What actually decides it: **rejecting creates a special case.** `int pointer p; p = p + 1` works today, so
rejecting the same expression on a struct pointer would mean arithmetic works on some typed pointers and
not others. Scaling removes that asymmetry instead of adding one.

On non-multiple `p - q`: C makes it impossible rather than defining it (C17 6.5.6p9 requires both operands
to point into the same array, where the distance is necessarily a whole multiple), and real compilers
optimize the division by modular inverse, so a non-multiple yields an unrelated number rather than a
truncated one. Impala's `DIVi` truncates deterministically inside the sandbox, which is strictly better
than C, from pointers that were meaningless anyway.

**Consequence for the docs:** `docs/Impala2.md:472-485` ("instruction count = marker count") is already
false - struct subscripting broke it and `inline function` broke it harder. Reword it to what is actually
true and worth defending: *cost is predictable from the declared types*. Scaling satisfies that completely,
since `p` being an `S pointer` is visible at the declaration. Do not carve further exceptions into the
language to protect a sentence that is not true.

### No GAZL instruction is missing here

Considered and rejected: a scale operand on `ADDp`/`DIFp` (`ADDp ptr(d) ptr int *stride`). It does not fit -
**GAZL is a 3-operand ISA** (173 of 174 instruction forms take 3 or fewer; the single 4-operand line is a
data directive), and `d = base + index*stride` needs four values. `MULi %0 $n #.z.S` is already the minimal
3-operand form of the scale, so there is nothing to fuse. Verified: `ADDp $p $p #.z.S` assembles and runs
with a symbolic stride, so the constant-stride step is already a single instruction. The residual cost of
scaling is one `MULi` on runtime-indexed access and one `DIVi` on pointer difference - the same cost `a[i]`
already pays today.


## The plan

Five batches, executed in the order **4 - 1 - 2 - 3 - 5**. Batch 4 goes first because `test-js` makes every
later batch cheaper to verify, and the gate drift (D3) means batches 1-3 would otherwise be validated by the
weaker script. Each batch ends green on all four JS gates plus a commit.

### Batch 4 - toolchain
`tools/test-js.{sh,cmd}` running every JS gate, called by **both** `build.sh` and `build.cmd` so D3 is fixed
by construction rather than by discipline. Fix `README.md:49-52`. Send `impala.nuxjs.js` errors to stderr.
Emit a generated-file banner from `updateJSPEG.js` into `impalaCompiler.js`. Rename
`tools/gazl-validate.js` to `.nuxjs.js`. Fix the `regen-jspeg-fixtures.cmd` loop.

### Batch 1 - `--dead-strip` (C1, C2)
Data-bearing fixture first, then the block extent and `collectRefs` fixes, then an assert that no `DATA` row
is ever loose. Verify by revert.

### Batch 2 - the silent divergence
**2a** scale `+`, `-`-with-int and pointer difference by element size (see the decision above); fixtures
walking a struct array by pointer vs by index, which must agree.

> **Shipped and superseded.** 2a landed in `a7fac42` and introduced F1 while leaving F2 unfixed. Replaced
> by the `[[ ]]` decision at the end of this document.

**2b was dropped**: the reported shift-precedence divergence does not exist (see C4). Batch 2 is 2a only.

### Batch 3 - one diagnostics pass
Duplicate `case`, `case` outside the range, `goto` to an undefined label, `readonly` array element write,
constant index OOB **on a dereference only, never on address formation - see F3**, plus the `E403` fix-it
note for globals. Reject only on values Impala genuinely knows;
stay silent on symbolic ones. Payoff is measurable: `switchtest` comes off `KNOWN_UNLOADABLE`
(`impala/runJspegTests.js:59`) and `CompileTimeHardening.md` closes.

Deliberately **not** in this batch: definite-assignment on named returns, and `copy` element/extent
checking. Both are real silent-garbage classes and both are decidable, but each is its own analysis rather
than a symbol-table lookup. They deserve their own batch.

### Batch 5 - docs (DONE)
Everything in section C9 and D, plus the "which tool does what" box promoted out of
`impala/gazlAssembleCheck.js:1-10`, the reworded cost principle, and a new `impala/README.md`.

Re-verifying the C9/D claims against the tree as it stood AFTER batches 1-4 changed four of them:

- **`abs(x)` is legal** (C7 above, corrected). The operator-vs-function distinction is real but the
  paren spelling the docs use is not an error.
- **`docs/Impala.md`'s `p[i]` text was already correct** - there is no stale "not equivalent" note to
  remove; Batch 2a made the existing sentence true.
- **`&a` does not compile** (`E404`), so B5's "write `&a` today" was wrong advice - `&a[0]` is the
  forward-proof spelling, and blocking decay is not a pure removal. Recorded in `ParkedFeatures.md`.
- **The hardening E-codes map differently than assumed**: item 3 is E444 and item 5 is E443, not the
  other way round. Item 1 is still open, and `&a[k]` out of bounds is unchecked by compiler *and*
  assembler - a sharper edge than the doc's original claim about locals vs globals, which was false.

Also found while checking: a named `const` upper bound defeats the E444 range check, E445's caret
pointed one statement late (since fixed, along with E403/E305/E422 - see `docs/SyntaxConsistency.md`
§5), and `docs/StructLayoutConstants.md` understated its own design so badly that it claimed a
per-level `ADDp` cost for what is actually assemble-time folding.


# Follow-up (2026-07-30)

Batch 2a shipped (`74f6862`..`a7fac42`) and is wrong in two ways that the `pointerStride` fixture does not
reach. Both were reproduced on this machine against the committed `Impala2` branch.

## F1. Every comparison between two struct pointers emits invalid GAZL **[V]** - CRITICAL - REGRESSION

The scaling branch (`impala/impala.jspeg:2463-2468`) keys on "the left operand is a struct pointer" and
fires for **every** operator, not just `+` and `-`. A comparison has no unit, so scaling either side is
meaningless, and the result is a `MULi` on a pointer operand that the assembler rejects:

```impala
struct S { int a; int b; int c }
global S array bank[3]
export function main() locals S pointer p, S pointer q, int n {
	p = &global bank[0]; q = &global bank[2];
	if (p < q) { n = 1; }
}
```

```gazl
MULi %0 $q #.z.S        ; <- pointer operand
```

`GAZLCmd` refuses to load it: `Incompatible types: $q`. Reproduced for all six of `<`, `<=`, `>`, `>=`,
`==`, `!=`. **Comparing two struct pointers is currently impossible in Impala.** The JS gates are green
only because no fixture compares two struct pointers - `pointerStride.impala` covers `+`, `-` and
difference, and nothing else does.

Loud rather than silent, since the assembler catches it, but it removes the operation that the
pointer-walk idiom is built on.

## F2. `for` over a struct pointer is silently wrong, and `FORp` cannot express the fix **[V]** - CRITICAL

```impala
struct S { int a; int b; int c }
global S array bank[3] = { { 10, 0, 0 }, { 20, 0, 0 }, { 30, 0, 0 } }
for (p = &global bank[0] to &global bank[3]) { printInt(p->a); printLF(); }
```

prints `10 0 0 20 0 0 30 0 0` - nine iterations, not three. The bound scales correctly to
`&bank + 3*.z.S`, and then `FORp` steps one word:

```gazl
! MULi <A> #3 #.z.S
ADDp %0 &bank #<A>
.l1:	PEEK %2 $p #.o.S.a
	FORp $p %0 @.l1
```

This is the same row the C3 table listed as broken, and batch 2a did not fix it. It **cannot** be fixed
under the elements rule: `FORp ptr(d) &address @label` is already three operands, and GAZL is a 3-operand
ISA (see "No GAZL instruction is missing here" above, which established the same constraint for
`ADDp`/`DIFp`). The only lowerings available are to reject the construct or to abandon `FORp` for
`ADDp`+`LSSp`+`GOTO`, which is three instructions where the language promises one.

F1 and F2 together mean the 2026-07-29 decision does not describe a reachable design. "Elements
everywhere, one rule, no exceptions" leaks into comparison, where there must be no unit, and cannot reach
`for`, where the unit cannot be represented.

## F3. The constant-index OOB check must be scoped to dereference, not address formation

Planned in batch 3 above as a bare "constant index OOB", and in `CompileTimeHardening.md:75-84` as task
#22. As worded it would reject `&a[N]`, and that is wrong on principle and stricter than the target
machine. Verified on `global int array a[4]`:

```gazl
MOVp $e &a:4        ; e = &global a[4]   one past the end
MOVp $f &a:9        ; f = &global a[9]   far past the end
```

Both assemble and run. GAZL's `Offset out of bounds` check (`MemorySafetyModel.md:78`, cpp:619) fires on
constant-offset **access** operands, not on address-taking. An out-of-range address is a value like any
other; the guarantee comes from every dereference being bounds-checked at run time (`PEEK` against
`memorySize`, `POKE` against `rwMemorySize`, `GETL`/`SETL` against `dataStackEnd`). A transliterator must
not invent a restriction its target does not have.

Correct scope, which neither write-up states:

- **Dereference with a known out-of-range constant index** (`a[7]`, `a[7] = 1`, `p[9].a` as a value) -
  compile error. It is a guaranteed trap, so catching it early loses nothing.
- **Address formation** (`&a[7]`, `&a[N]`, `&p[[i]]`) - always legal, never checked.

`CompileTimeHardening.md`'s motivating example is `a[7] = 1`, a store, so the check as *motivated* is
already correctly scoped; only the wording generalises past it. Note this leaves Impala **simpler** than
C, not looser: C needs an explicit carve-out making one-past-the-end valid and two-past undefined, while
Impala needs no carve-out at all.


# Decision: the scaled subscript is spelled `[[ ]]` (2026-07-30)

Supersedes the 2026-07-29 decision. **A subscript that scales by the element size is written `[[i]]`, and
it is the only construct that moves a struct pointer.** Arithmetic on struct pointers is rejected.

| form | on unit-stride (`int pointer`, untyped `pointer`, `int array`) | on struct array / struct pointer |
|---|---|---|
| `a[i]` | one instruction, stride 1 | **error**, fix-it to `a[[i]]` |
| `a[[i]]` | error, fix-it to `a[i]` | one instruction + one `MULi`, stride `sizeof(elem)` |
| `p + i`, `p += i`, `p - i` | unchanged, stride 1 | **error**, fix-it to `&p[[i]]` |
| `q - p` | unchanged, bare `DIFp` | **error**, fix-it to `((pointer)q - (pointer)p) / sizeof(S)` |
| `for (p = a to b)` | unchanged, `FORp` | **error**, fix-it to the `while` form |
| `p < q` and the other five | bare comparison | bare comparison (F1 fix) |

```impala
struct Voice { int note; float phase; float gain }
global Voice array voices[16]

v = &global voices[[0]];
end = &global voices[[16]];         // one past the end - legal, see F3
while (v < end) {
	v->gain = 0.0;                  // 1 instruction, marked by ->
	v = &v[[1]];                    // 1 ADDp, stride folded at assemble time
}
voices[[i]].gain = 0.0;             // 2 instructions, marked by [[ ]]
```

## Why this and not the alternatives

Four designs were weighed. All four respect `p + (q - p) == q` and `p[i] == *(p + i)`, which are not
negotiable: pointer arithmetic is an affine space, so `+`, `-`, difference and `[]` share one unit or the
language is incoherent.

1. **Elements everywhere** (the superseded decision). Dead on F1 and F2 - not a reachable design.
2. **Words everywhere, `[]` included.** Coherent and perfectly 1:1, but `[]` must then also be words, so
   `bank[1].a` means the second *word* and every struct access becomes `bank[i * sizeof(S)].a`.
3. **Reject arithmetic, keep plain `[]` on struct arrays.** Coherent, no division anywhere, and the
   identities hold vacuously because the expressions do not exist. This was the frontrunner and `[[ ]]` is
   a strict improvement on it, at the cost of one new spelling.
4. **`[[ ]]`, this decision.** 3, plus the cost is visible at the use site.

What decides it in favour of 4 over 3: `docs/Impala2.md:472-485` promised **instruction count = marker
count**, and struct subscripting broke it. The 2026-07-29 decision responded by retreating to "cost is
predictable from the declared types". `[[ ]]` makes the original sentence true again - `[[ ]]` is the
marker for the multiply, exactly as `->` is the marker for the load - instead of weakening it. Restoring
a principle beats rewording it.

The objection that killed this idea on the first pass was that a second subscript operator means two
spellings for one idea, which `docs/Impala2.md:1102` rejects elsewhere. It does not apply: `[]` and
`[[ ]]` are **not** interchangeable. Each is an error where the other is correct, so there is exactly one
legal spelling per access and nothing to dilute.

The second objection was breaking existing code. There is none - `struct` appears zero times as a language
feature in the 1.0 spec (`docs/Impala.md`; its four "struct" hits are substrings of "constructs" and
"instructions"), and exactly 24 files in `tests/impala/sources` carry a struct declaration, all of them
2.0 fixtures written for this feature.

## What it costs

A non-C spelling on the most common struct operation. C programmers and LLM-authored code will write
`[]` and `p + 1`, and will keep doing so. Every one of those is a compile error with a fix-it, never wrong
behaviour, which is the trade this language makes everywhere else - but it is permanent friction and it is
the honest price.

## What it buys

No hidden multiplication and no hidden division anywhere in the language. Every `MULi` sits behind a
bracket pair the author typed; every `DIVi` is one the author wrote. The residual multiply on `a[[i]]` is
the **floor**, not overhead: the address of element `i` of a 3-word struct genuinely is `base + i*3`, and
no language or ISA can produce it without a multiply. That is the principled place to stop, and it is a
lower bound rather than a preference:

> **Impala emits no arithmetic beyond what the operation mathematically requires, and where it emits any,
> a marker in the source says so.** `[[ ]]` costs one load plus one multiply; `[]` and `.` cost no
> arithmetic; `->` costs one load.

Two footnotes shrink the residual further: a constant index folds at assembly time (`! MULi <A> #1 #.z.S`,
`pointerStride.gazl:37-39`), so it costs nothing at run time, and the degenerate one-word struct case
(`MULi %0 $i #1`) is already queued as `IDENTITY_OP` in `GAZLAssemblerOptimizations.md`.

## When to revisit

If Impala ever gains a non-word-sized **scalar** element type, scaling stops being struct-only and becomes
pervasive, and the balance between `[]` and `[[ ]]` should be re-argued from scratch. Same if the parked
multidim-array work (`docs/ParkedFeatures.md`) lands, since a 2-D subscript scales on both axes.
