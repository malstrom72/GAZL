# Impala 2.0 review pass (2026-07-29)

Status: REVIEW NOTE. A full-language review of 2.0 against 1.0, against the planned 3.0, against itself,
and against its toolchain. Unverified claims are marked as such and should be treated as leads, not facts.

**Read `[V]` as "reproduced on the date stamped on that entry", NOT as "true now".** Every entry in
sections C, D and F carries a date; a `[V]` with an old date means it was real then, and says nothing about
today. An entry that has since been closed is struck through and stamped **FIXED `<date>`**, with the
description kept so the shape stays searchable. Re-verify before acting on anything here, and when you do,
move the stamp. Full re-audit of every open item: **2026-08-04**.


## A. What changed from Impala 1.0 (raw material for a What's New)

Baseline for the diff is `main:impala/impala.jspeg` (2938 lines) vs HEAD (6214, measured 2026-08-05).

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
| `sizeof` | n/a | `sizeof(Type)`, in WORDS, type-name form only |
| Strict expressions | `a \| 250 & 120` silently mis-parses | E101 / E102, `--legacy` lowers to warning |
| Diagnostics | bare messages | `path:line:col: error[Ennn]:` + caret + fix-it note |

### Breaking changes

1. **Six new reserved words**: `export`, `functype`, `import`, `inline`, `sizeof`, `struct`. Not restored by
   `--legacy` — all six are a hard `E001` as an identifier, verified 2026-08-04. (`return`, `break` and
   `continue` also became reserved, but through `E449`, which *is* a `--legacy` warning.)
2. **E101** mixed bitwise operators at one parenthesization level.
3. **E102** unparenthesized bitwise directly against a comparison.
4. **E201/E202/E203** element-type mismatches — reachable in old code because `&` became type-producing and
   string literals became `int pointer`.
5. **E437/E438** extern prototypes are now checked against a definition in the same closure.

`--legacy` gates **six** diagnostics, not the two this section originally claimed: E101, E102, E103, E449,
E452 and E455. `strictError` (`impala/impala.jspeg:1650`) has exactly those six call sites; every other
diagnostic calls `$$parser.fail` directly. **[V] 2026-08-04** — confirmed behaviourally, not just by
call-site count.

### Deliberately NOT changed

No compound assignment or `++`. Comparisons are still not values. Bitwise/shift still share one precedence
level looser than `+`/`-`. Arrays were one-dimensional when this was written and became multidimensional
on 2026-08-04 (`docs/impala/MultidimensionalArrays.md`). `for (v = a to b)` and `switch (e == lo to hi)`
unchanged. Four built-ins only (`abs`, `floor`, `itof`, `ftoi`). Casts still reinterpret, never convert.
`global` prefix still mandatory at every global access.


## B. Biggest shortcomings vs the planned Impala 3.0

Ranked by pain. All parked work is *additive* in 3.0 except item 4 — nothing here says "avoid a pattern
today" except that one.

The list originally carried a sixth entry, the diagnostics backlog: five shapes the compiler accepted and
the assembler then rejected, naming a compiler-minted GAZL symbol instead of the `.impala` line. **CLOSED
2026-08-03**, verified item by item on 2026-08-04 — E461, E404, E445, E444, E443, and `KNOWN_UNLOADABLE` in
`impala/runJspegTests.js:61` is now `{}`. See `design/impala/CompileTimeHardening.md:15`.

1. **No by-value structs, no multi-return, no destructuring** (E426-E429). You write pointer
   out-parameters: `addVec(&s, &p, &q)` instead of `s = addVec(p, q)`. Cost is inside the callee — the
   golden diff in `e6ad36d` shows `addVec` going from 2 instructions to 8, because by-value fields are free
   window operands and by-pointer fields are a `PEEK`/`POKE` each. Implemented and VM-verified on
   `Impala3-byvalue-multireturn-park` (an ancestor of this branch), then parked by `e6ad36d`; `Impala2` has since
   drifted **236 commits / +2425 -544** lines in the grammar (measured 2026-08-05; it was 96 / +1031 -360
   when this was first written), so restoring it is a re-implementation against a working reference, not a
   cherry-pick. The figure only grows, and with it the argument.
2. **Import cycles only half-resolve.** A backwards cross-cycle reference needs a forward `extern`, and the
   *same two files* build or fail depending only on which is the root. A cross-cycle **struct type** has no
   workaround at all — there is no forward-`extern` form for a type. Collect mode (the fix) is designed but
   never built; done-when is pinned (`importcycle/odd.impala` builds as root with its `extern` deleted).
3. ~~**No multidimensional arrays.**~~ **RESOLVED 2026-08-04.** Implemented in all three positions this
   entry called impossible - `global` arrays, locals and struct fields - with per-axis bounds checking in
   all three tiers. The escape-hatch argument fell with it: an `extern struct` field states its RANK
   (`int array cells[,]`), the host supplies each axis as `.d.Grid.cells.<k>`, and `E430` still refuses an
   extent, so nothing had to be relaxed. See `docs/impala/MultidimensionalArrays.md`.
4. **Implicit array→pointer decay is still live and it is decided to go.** Write `&a[0]` today — it is
   correct under both rules and costs nothing now. (An earlier draft of this line said `&a`; that is wrong,
   `&a` is `E404 Invalid lvalue` today, which also means blocking decay is not a pure removal. Recorded in
   `design/ParkedFeatures.md`.) See section C for why this is the only forward-compat item.
5. Numeric-only by-value call windows (E425 — not a dead diagnostic but a never-written one), the mandatory reserved return
   transient (an ABI change), and dead-arm elimination after a compile-time branch. None reachable today.

### ~~Undocumented 3.0 direction~~ — FIXED 2026-08-04

*(Was true on 2026-07-29.)* `design/ParkedFeatures.md:294` now carries the decay decision under its own
heading, "Block implicit array->pointer decay", marked as a decided restriction rather than a parked
feature — the one item a 2.0 user should act on today, and now reachable from the index.
`design/ParkedFeatures.md` also names the document that survives only on the park branch
(`docs/Impala2OpenItems.md`), which is what this entry asked for. It used to name
`docs/impala/MultidimensionalArrays.md` alongside it; that one is a live doc on THIS branch describing the design
that was actually built, so pointing a reader at the park branch's copy sent them to a superseded 3.0
design for an implemented feature.


## C. Things that will trip humans and AI agents

Ordered by danger. Silent wrong behaviour first. Each heading carries the date its state was last checked.

### C1. ~~`--dead-strip` silently corrupts retained array data~~ — FIXED 2026-08-04

*(Was **[V]** CRITICAL on 2026-07-29.)* Both shapes below now run correctly with and without
`--dead-strip`, verified on a 5-element and a 24-element array: `KEPT` reads `9 9 0 0 0` either way, and
the strip drops exactly `DEAD`'s three words from the consts region. Description kept as the record:

`impala/impalaImportClosure.js:216-231` treated a labelled data definition as a block of **exactly one line**.
The unlabelled `DATA` continuation rows carrying an initializer became `loose` blocks, which were kept
unconditionally (`:267`). Stripping a dead array's header let its rows be adopted by the preceding block:

```impala
readonly int array KEPT[5] = { 9, 9 }     // trailing zero-fill
readonly int array DEAD[3] = { 1, 2, 3 }  // unreferenced
```
`KEPT` read `9 9 0 0 0` normally and **`9 9 1 2 3` under `--dead-strip`**, with no error at any stage. No
fixture covered it at the time; `tests/impala/sources/deadstrip/stripmain.impala` now carries array data.

### C2. ~~`--dead-strip` breaks the canonical firmware idiom~~ — FIXED 2026-08-04

*(Was **[V]** HIGH on 2026-07-29.)* The program below compiles, strips and runs. Fixed by `2852625`,
"Dead-strip missed a symbol used as an offset or starting with a dot". Description kept as the record:

`collectRefs` (`impalaImportClosure.js:194`) was `/[&^#]([A-Za-z_]\w*)/g`. It did not recognise the `*size`
operand form, so a constant used only as an array extent looked unreachable:

```impala
const int N = 16
global int array buf[N]      // used to be: --dead-strip: Symbol not previously defined: N
```

`global int array params[PARAM_COUNT]` is exactly the shape `docs/impala/Impala2.md:207-210` recommends.

### C3. ~~`p + 1` does not stride but `p[1]` does~~ — RESOLVED 2026-07-30, re-verified 2026-08-04

*(Was **[V]** CRITICAL on 2026-07-29.)* Resolved not by making the table's rows correct but by making
every one of them **unwritable**. The rule is that scaling is confined to the SUBSCRIPT - `[[ ]]`, the
spelling the end of this document proposed for it, was reversed on 2026-08-04 and the ordinary `[ ]`
carries it. Re-checked on 2026-08-05:

| expression | today |
|---|---|
| `bank[0]` on a struct array | scales by `.z.` - no diagnostic (`E204` was retired 2026-08-04) |
| `p = p + 1` on a struct pointer | `E307`, fix-it "a struct pointer moves by scaled subscript only - write `&p[i]`" |
| `q - p` on two struct pointers | `E308`, fix-it to `((pointer)q - (pointer)p) / sizeof(S)` |
| `for (p = ... to ...)` on a struct pointer | `E309` — see F2 |

So there is no silently-wrong arithmetic over a struct pointer left; every shape in the original table is
a diagnostic with a fix-it.

### C4. Shift-vs-additive precedence — INVESTIGATED, NOT A BUG **[V] 2026-07-29**

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

So `docs/impala/Impala.md:375` and `docs/impala/Impala2.md:1044` ("Arithmetic-vs-bitwise actually agrees with C") are
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

### C5. ~~`E403 Undeclared identifier` on a global that plainly exists~~ — FIXED 2026-08-04

*(Was **[V]** HIGH FREQUENCY on 2026-07-29.)* `x = G;` for a `global int G` still reports E403 — that part
is by design, the `global` prefix is mandatory — but it now carries exactly the note this entry asked for:

```
error[E403]: Undeclared identifier: G
note: G is a global - write `global G`
```

The mandatory `global` prefix remains a sound marker-discipline device (one `global` = one `PEEK`); it was
only ever the message that pointed the wrong way.

### C6. Other silent-wrong shapes — bullet-by-bullet dates below

- ~~**A flat array initializer ignored the declared element type.**~~ FIXED 2026-08-02. `InitList`
  checked each entry against its OWN type — a comparison no value can fail — so nothing enforced the
  array's element type. `int array A[2] = { 1, "s" }` stored a POINTER in an int slot, and
  `float array F[2] = { 1, 2 }` stored the INTEGER bit pattern, so `F[0]` read back as `1.4013e-45`
  (verified by running it). The assembler cannot catch either: `DATA` is untyped words. The scalar
  paths were always this strict — `global float f = 1` is E407 — so this was the array path missing a
  check the rest of the language had, not a new rule. Untyped `array A[2]` (Impala 1) states no element
  type and is still unchecked, deliberately.
- **A declared return value never assigned returns stale frame garbage.** No definite-assignment analysis;
  decidable single-pass for the trivial case. **[V] still open 2026-08-04.**
- ~~**`copy` bypasses the pointer ELEMENT type.**~~ FIXED 2026-08-04. `copy(8 from &intSrc[0] to
  &floatDst[0])` compiled silently: the rule checked that both operands are pointers (`E301`) and never
  asked what they point AT, leaving it the one door that reads a typed pointer and enforces nothing. It now
  runs `checkPtrAssign` — with one deliberate difference from an assignment: **only on a contradiction**,
  when both sides know their element and disagree. An assignment rejects untyped → typed because the
  VARIABLE must keep that promise for every later deref; a copy consumes both addresses on the spot, so an
  untyped source claims nothing to break, and reading an Impala 1 `array` blob into typed storage is the
  1.0 idiom (one corpus fixture, `patch.impala:234`, does exactly that). The cast is the escape hatch.
  The check exposed a second bug underneath it: `&x` on an element of an untyped `array` stamped
  `elem = '?'` while a bare untyped array stamped `undefined` — two spellings of the same non-knowledge,
  which made comparing them say "expected untyped elements, got untyped elements". Fixed at the source
  (`&x` now normalizes `'?'` to `undefined`), which also collapsed the emitted `unknown-ptr` metadata token
  onto `ptr`, the spelling the code already documented as canonical — the only corpus effect, 7 comment
  lines across 3 goldens, no instruction changed.
  *The length half of this finding was withdrawn 2026-08-04: it originally also called a 4-word overrun of
  the destination a defect. A pointer has no extent, so past `&a[0]` there is nothing to check against, and
  extents are not tracked through pointer values by design. Only the literal `&arr[const]` spelling could
  ever be caught, which would make the diagnostic a property of how the address was WRITTEN rather than of
  the program — worse than uniform silence. Length is the programmer's, exactly as with `memcpy`.*
- **Locals are not zero-filled** **[V] 2026-08-04** — still worth stating, because globals are, so the two
  storage classes differ. The doc half of this finding is FIXED: `docs/impala/Impala2.md:522` now reads
  "Uninitialized **global** struct storage is zero-filled", qualified exactly as the finding asked.
- **`&local` returned from a function** points into the next callee's frame. No diagnostic, no trap.
  **[V] 2026-07-29.**
- **`extern struct` host layout is essentially unchecked**: overlapping offsets, offsets past `.z.Name`, and
  int↔float retypes are all undetected. Only a renamed/removed field fails, at load. **[V] still open
  2026-08-04.** (`design/impala/ExternPrototypes.md` used to claim gazl-validate catches drift; it now says plainly
  that the scanner records field names only and spells out what that does and does not catch.
  `design/impala/StructLayoutConstants.md` correctly calls the rest deferred.)
- ~~**Constant OOB through a pointer** (`p[[9]].a`) is caught nowhere.~~ NOT A DEFECT — filed here in
  error. A pointer has no extent, so there is nothing to check against, and a language at this level hands
  that to the programmer exactly as C does. Pointer indexing is unchecked at every tier, permanently and by
  design; it is not a gap and not future work. The ARRAY half of this bullet was real and is closed: a
  constant index past a known extent is `E461` at Impala compile time. See `docs/impala/MemorySafetyModel.md`
  for the tiers and what each one covers.
- **`for`'s upper bound is live or frozen depending on the shape of the bound expression** — a plain local
  emits `FORi $i $n` (re-read each iteration); anything else is snapshotted into a scratch.
  **[V] 2026-07-29.**
- ~~**Global initializers bypass checks their in-function twins get.**~~ FIXED 2026-08-04, in three steps
  worth reading together, because the first two each closed one half and left the shape of the bug behind.
  The element-type half (`global int array T[4] = { 1, 2.0, 3, 4 }`) closed first, as `E407`. The funcptr
  half (`global Fn bp = g` with a mismatched signature) closed next as `E441` (`0682046`) — but through a
  wrapper calling *half* of `checkPtrAssign`, so `global int pointer p = &global f[0]` off a
  `float array f[4]` still compiled clean. `ed1b879` deleted the wrapper: every FLAT initializer door runs
  `checkPtrAssign` whole, so a check added there cannot be right for an assignment and missing at a
  declaration. Ordering was the subtlety — `global int pointer p = 1` is not an element mismatch but a
  non-pointer, and stays `E407`, exactly as its assignment twin stays `E303`.
  **Step four, 2026-08-04 (`63bd4ff`): there was a FIFTH door, and it was the one that mattered.** A struct
  FIELD does not reach the row through those four — it goes through `pushInitScalar`, which was handed the
  field's `type` and not its `elem`, and asked only the coarse question. So `struct P { int pointer p }`
  took `{ p: &global floatArr[0] }` and a funcptr field took a mismatched function, both silently, while
  assigning either to the same field inside a function is `E201`/`E441`. Struct fields are where typed
  pointers and funcptrs actually live in this codebase, so the four doors closed first were the *less*
  important ones. `BracedItem` now carries the value's `elem`, and `pushInitScalar` runs the same check —
  a braced entry is already reduced to its operand, so it rebuilds the one-operand shape the check reads,
  which is what keeps `null`/`nullfunc` holes legal. Found by a cleanup review, not by the fix that
  claimed to have closed this; the earlier wording above ("every initializer door") was itself the tell.
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
  define FEWER words than its region holds, with the remainder zeroed (`docs/gazl/InstructionSet.md:96-99`), so
  the values given simply land at the front: `struct S { int a; int array v[N] }` with
  `{ a: 1, v: {7,8,9} }` emits `DATA #1 #7 #8 #9` and assembles. What has no answer at Impala compile
  time is the offset of a field placed *after* the symbolic one. Verified against the assembler, three ways: there is no fill or repeat
  directive in `docs/gazl/InstructionSet.md`; a FORWARD `! GOTO` assembles and can even skip a `DATA` line; a
  BACKWARD one does not assemble at all (`Compile time label not found`), so no assemble-time loop can
  emit a symbolic number of words. Hence `E454` — but only when a later field is given a NON-ZERO value.
  Zero costs no words, so `{ a: 1, v: {7,8,9}, z: 0 }` is accepted and simply stops the row early, landing
  exactly where the zero-fill would; omitted fields were always fine. The hint leads with omitting those
  fields or moving the symbolic array last, NOT with "give it a literal size" — per
  `design/impala/TwoStageConstants.md`, steering a user toward pinning an extent numerically is the anti-pattern,
  not the fix. Over-filling is left to the assembler, per rule 2 there — Impala does not know `N`, so it
  must not guess. Note the assembler does NOT catch it on its own: over-running the whole section is
  named (`Not enough space in data section: s`), but over-running one field INTO the next is a legal,
  in-bounds write it has no notion of. So Impala emits the comparison for it (`! LEQi` + `! FAIL` above
  the rows, 2026-08-02), which is the only thing standing between that shape and a silent spill.
- ~~**Duplicate `case` labels** and **out-of-range `case` labels** are both accepted silently; a case
  *below* the low bound emits `.s0.-6` and the module will not load at all.~~ FIXED: `E443` (duplicate,
  every range as of 2026-08-02) and `E444` (outside a numeric range). A symbolic range leaves the window
  check off deliberately - see S5/S6 in `CompileTimeHardening.md`.
- ~~**`switch (x == lo to hi)`: `hi` is exclusive**, and `docs/impala/Impala.md` says inclusive.~~ DOC FIXED
  2026-08-04 — `docs/impala/Impala.md:354` now says "the upper bound is **exclusive**, exactly as in
  `for (i = 0 to N)`". The language behaviour never changed; only the doc was wrong.

### C7. C reflexes that are rejected with a bare `E001: syntax error` — **[V] 2026-08-04**

No expected-set, no note. For a language whose stated audience is strangers and AI agents, this is the
largest DX defect: an agent iterating against diagnostics gets a caret and nothing else. Rows below
re-checked 2026-08-04 and still accurate; the `return`/`break`/`continue` row came off the list, since
those are now reserved words with dedicated messages (`E448`/`E450`) — see `design/impala/SyntaxConsistency.md:205-217`.

| You write | Reality |
|---|---|
| `if (x)`, `while (1)`, `if (!x)` | conditions require a `COMP_OP`; write `if (x != 0)` |
| `flag = (a < b)` | comparisons and `&&`/`\|\|` are not values, anywhere |
| `int i;` inside a body | no declaration statement; all locals in the `locals` clause |
| `copy(dst, src, n)` | `copy(N from SRC to DST)` — count first, and src/dst reversed vs `memcpy` |
| `abs x - 1` read as `abs(x - 1)` | `abs`/`floor`/`itof`/`ftoi` are prefix operators, so this is `(abs x) - 1`. (`abs(x)` does compile — the parens are just a parenthesized expression — but `abs()` and `abs(x, 2)` do not.) |
| `function f(int array a)` | array parameters do not exist (but `locals` accepts arrays — the two lists differ) |
| `1e6` | `FloatLiteral` requires `DIGIT+ "." DIGIT+` first |
| `&arrayName`, `&funcName` | `E404 Invalid lvalue`; write `a` / `&a[0]` and `fp = g` |
| `(StructName) expr` | `E403 Undeclared identifier: StructName` — write `(S pointer)` |

One bright spot worth advertising: `if (x = 1)` is structurally impossible, because assignment is an `Expr`
and conditions need a `COMP_OP`.

### C8. Internal inconsistencies a reader must simply memorize — re-checked 2026-08-04

- **Three signature grammars.** `function` and `extern function` require parameter names *and* a return name
  (a meaningless dummy for an extern); only `functype` allows types-only. ~~`docs/impala/Impala2.md` claims they
  mirror each other~~ — DOC FIXED: `docs/impala/Impala2.md:810` now says "The three signature grammars do **not**
  mirror each other."
- **Separators are inverted**: struct fields take `;`, `locals` takes `,`. Neither accepts the other.
- **Semicolons**: top-level declarations take none, every statement takes one, `do {...} while (c)` takes
  none.
- **Three declaration keywords (`global`/`readonly`/`temporary`), one access keyword (`global`).**
- **Modifier order is rigid and undocumented**: `extern global int X`, `readonly global int X`,
  `function inline g()`, `export struct S` are all `E001`.
- **`assert` defers its own prerequisites to the assembler** — with no `const int DEBUG` and no `assertFail`
  in the link set the Impala compile succeeds and the failure surfaces at load.
- ~~**Initializer errors are anchored on the *next* declaration** **[V]** — `$$i` has already skipped
  whitespace. When the bad declaration is last in the file the caret lands past EOF and no source line
  prints at all.~~ FIXED 2026-08-02. Four scalar paths kept `$$i` while the brace path beside them had
  already moved to a saved start offset: the global initializer (E407/E421), the const initializer
  (E407) and the array extent (E407). Each now saves `$$i` straight after the `'='` or `'['`, before
  `Expr` (or `']' _`) eats the trailing space — the same one-line pattern `$initStart` and `$cStart`
  already used. Carets are column-accurate, not merely on the right line, and pinned in `caretCases`
  including a deliberately last-in-file case for the past-EOF shape.
- ~~**`docs/impala/Impala.md` says forward references work.**~~ DOC FIXED 2026-08-04 — `docs/impala/Impala.md:308` now
  tells you to "declare a forward `extern function` above the use". The E403 note remains excellent, and
  even finds the later definition.
- ~~**`docs/impala/Impala.md`'s documented `goto`-out-of-loop idiom does not compile.**~~ DOC FIXED 2026-08-04 —
  `docs/impala/Impala.md:378-385` now writes `finished: ;` and explains that a bare `finished:` is `E001`.
- ~~**`docs/impala/Impala.md`'s reserved-word list is missing all six new keywords.**~~ DOC FIXED 2026-08-04 —
  the list at `docs/impala/Impala.md:32-36` carries all six, plus a note on `return`/`break`/`continue`.
- ~~**Every `.gazl` still says `; Compiled with Impala version 1.0`**~~ FIXED 2026-08-04 (`3d1975e`).
  `IMPALA_VERSION` is `'2.0'`; all 93 recorded artifacts were regolded, and the whole regold diff is that
  one banner line — `tests/impala/golden/*.gazl` via `runJspegTests --makegold`, `impala/testdata/*.expected.gazl`
  by replaying the harness's own options, and `importMain.gazl`/`stripped.gazl` via `importBuildTests makegold`
  (a third set `--makegold` does not reach).

### C9. Documented-but-absent, and absent-but-shipped — CLOSED by Batch 5, re-checked 2026-08-04

This section was Batch 5's input list. Every doc-side item below has since been written; what remains is
the two entries still marked open.

- ~~`design/impala/StructLayoutConstants.md` lists **E418, E424, E425** as implemented extern-struct guards, and all
  three have zero fail sites.~~ DOC FIXED. That section now carries a "Superseded scope note"
  (`design/impala/StructLayoutConstants.md:268`) saying the three were reserved and never fired;
  `docs/impala/Impala2.md:1573` says the same in the registry.
- ~~`(funcptr array) table` casts do not parse — the cast grammar has no `array`.~~ DOC FIXED.
  `docs/impala/Impala2.md:248` now states it: "**`array` is not a cast modifier.** `pointer` is the only one, so
  `(funcptr array) table` is `E001`."
- ~~`impala build` does not exist; the subcommands are `compile` and `run`, and there is no `impala` binary
  at all — every doc example that writes `impala compile ...` is aspirational.~~ DOC FIXED 2026-08-04. The
  last such example (`docs/impala/Impala2.md:967`) now spells the real invocation, `node impala/impala.node.js
  compile`. Packaging a launcher is a separate wish, not a doc defect: no doc promises one now.
- ~~`--json`, `--emit-metadata`, `--no-metadata` do not exist. The complete flag set is `--legacy` and
  `--dead-strip`.~~ The three phantom flags are still absent, but the flag set is **three**, not two:
  `--legacy`, `--dead-strip`, `--range-checks` and `--collapse-labels` (corrected twice, 2026-08-04 and
  2026-08-18 - the lesson is to cite the usage text rather than copy it; `--range-checks` shipped with
  E461).
- ~~`import "x.gazl"` blob imports do not work — the closure walker parses every import as Impala source.~~
  RESOLVED 2026-08-04 by **deferring the feature to Impala 3.0**, not by building it: nothing needs it, and
  the builder's concatenate-then-compile shape leaves a blob no seam to enter through. The Step 5 bullet in
  `docs/impala/Impala2.md` is struck through and points at
  [`ParkedFeatures.md`](../ParkedFeatures.md#precompiled-gazl-blob-imports), which records why and what it
  would cost. Like collect mode it is a pure relaxation — source imports written today keep compiling.
- ~~**The legacy manual-concatenation struct model no longer links**~~ DOC FIXED. `docs/impala/Impala2.md:696`
  now opens "the 1.0 copy-paste model ... is dead", shows the collision, and documents the working pattern
  (one unit `struct`, the rest a body-carrying `extern struct`).
- ~~`design/impala/StructLayoutConstants.md` *understates* the design: it claims nested/array field access costs an
  `ADDp`/`ADRL` per level.~~ DOC FIXED — it now describes the assemble-time fold and the single `GETL`.
  Dots really are free.


## D. Toolchain clarity — Batch 4 closed most of this, re-checked 2026-08-04

### D1. ~~The README's Getting Started command is wrong and fails silently~~ — FIXED 2026-08-04

*(Was **[V]** on 2026-07-29.)* `README.md` now passes the output path second and spells out the failure
mode in prose right under the command: "The output path is the *second* argument. Passing the random id
there instead writes the GAZL to a file named `0x4d2` and leaves `demo.gazl` empty, which `GAZLCmd` then
reports as `Code size: 0 ... Could not locate function: main`."

### D2. ~~"gazl-validate is not the assembler" is documented only in a source comment~~ — FIXED 2026-08-04

`README.md:42` carries a **"Which tool does what"** table naming `output/GAZLCmd` as the only real
assembler-and-VM, `tools/gazl-validate.sh`/`.cmd` as a `; signature` metadata linter, and
`impala/gazlAssembleCheck.js` as the gates' helper — plus the no-assemble-only-mode note and the
bogus-entry-point workaround, both promoted out of the source comment.

### D3. ~~`build.sh` and `build.cmd` run different gate sets~~ — FIXED 2026-08-04

*(Was **[V]** on 2026-07-29.)* Both now call the same script: `build.sh:15` runs `bash tools/test-js.sh`
and `build.cmd:22` runs `CALL tools\test-js.cmd`. The drift is fixed by construction rather than by
discipline, which was Batch 4's stated goal.

### D4. ~~There is no way to run only the JS gates~~ — FIXED 2026-08-04

`tools/test-js.sh` / `tools/test-js.cmd` exist and are listed in the README's Helper Scripts section:
"every gate that needs only node (~1-1.5 min, most of it a 3000-program fuzz run; no C++ toolchain); run this before committing a compiler-only
change".

### D5. Smaller — re-checked 2026-08-04

Four of the original bullets are now false and are dropped: `tools/gazl-validate.nuxjs.js` exists with no
bare `.js` twin; `output/impala.nuxjs.js` is byte-identical to `impala/impala.nuxjs.js`;
`impala/impalaCompiler.js:1` carries `/* GENERATED from impala/impala.jspeg by ... -- do not edit by
hand. */`; and `impala/README.md` exists. What is left:

- **Entry points still do not self-document.** **[V] still open 2026-08-04**: `fuzzImpala.js --help` prints
  `fuzz: NaN programs, 0 compiled, ...` and **exits 0**; `runJspegTests.js --help` and
  `importBuildTests.js --help` each run the whole suite instead of printing usage.
- `regen-jspeg-fixtures.cmd` validates all fixtures in one call and fails on duplicate `main`; its `.sh`
  twin carries a comment explaining exactly why they must go one at a time.
- **Two independent golden-fixture systems with near-identical names**: `tests/impala/{sources,golden}`
  (owned by `runJspegTests --makegold`) and `impala/testdata/*.expected.gazl` (owned by
  `tools/regen-jspeg-fixtures`). Adding a fixture to the wrong one silently gets you no coverage. The split
  is explained in `jspegCompilerTests.js:677-681` — a source comment.


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

**Consequence for the docs:** `docs/impala/Impala2.md:472-485` ("instruction count = marker count") is already
false - struct subscripting broke it. (This line used to add "and `inline function` broke it harder";
`inline function` is **parked on `Impala2`** (`E439`) and lives on `GAZL2`, so it breaks nothing here -
noted 2026-08-04.) Reword it to what is actually
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

**All five batches are DONE** (last confirmed 2026-08-04). Their input lists — C1, C2, C3, C9 and D — are
struck above accordingly.

### Batch 4 - toolchain (DONE)
`tools/test-js.{sh,cmd}` running every JS gate, called by **both** `build.sh` and `build.cmd` so D3 is fixed
by construction rather than by discipline. Fix `README.md:49-52`. Send `impala.nuxjs.js` errors to stderr.
Emit a generated-file banner from `updateJSPEG.js` into `impalaCompiler.js`. Rename
`tools/gazl-validate.js` to `.nuxjs.js`. Fix the `regen-jspeg-fixtures.cmd` loop. All shipped except the
`regen-jspeg-fixtures.cmd` loop, which stays open under D5.

### Batch 1 - `--dead-strip` (C1, C2) (DONE)
Data-bearing fixture first, then the block extent and `collectRefs` fixes, then an assert that no `DATA` row
is ever loose. Verify by revert.

### Batch 2 - the silent divergence (DONE, then superseded)
**2a** scale `+`, `-`-with-int and pointer difference by element size (see the decision above); fixtures
walking a struct array by pointer vs by index, which must agree.

> **Shipped and superseded.** 2a landed in `a7fac42` and introduced F1 while leaving F2 unfixed. Replaced
> by the `[[ ]]` decision at the end of this document.

**2b was dropped**: the reported shift-precedence divergence does not exist (see C4). Batch 2 is 2a only.

### Batch 3 - one diagnostics pass (DONE)
Duplicate `case`, `case` outside the range, `goto` to an undefined label, `readonly` array element write,
constant index OOB **on a dereference only, never on address formation - see F3**, plus the `E403` fix-it
note for globals. Reject only on values Impala genuinely knows;
stay silent on symbolic ones. Payoff was measurable and was collected: `KNOWN_UNLOADABLE`
(`impala/runJspegTests.js:61`) is now `{}`, and `CompileTimeHardening.md:15` reports every item closed.

Deliberately **not** in this batch: definite-assignment on named returns, and `copy` element/extent
checking. Both are real silent-garbage classes and both are decidable, but each is its own analysis rather
than a symbol-table lookup. They deserve their own batch.

### Batch 5 - docs (DONE)
Everything in section C9 and D, plus the "which tool does what" box promoted out of
`impala/gazlAssembleCheck.js:1-10`, the reworded cost principle, and a new `impala/README.md`.

Re-verifying the C9/D claims against the tree as it stood AFTER batches 1-4 changed four of them:

- **`abs(x)` is legal** (C7 above, corrected). The operator-vs-function distinction is real but the
  paren spelling the docs use is not an error.
- **`docs/impala/Impala.md`'s `p[i]` text was already correct** - there is no stale "not equivalent" note to
  remove; Batch 2a made the existing sentence true.
- **`&a` does not compile** (`E404`), so B5's "write `&a` today" was wrong advice - `&a[0]` is the
  forward-proof spelling, and blocking decay is not a pure removal. Recorded in `ParkedFeatures.md`.
- **The hardening E-codes map differently than assumed**: item 3 is E444 and item 5 is E443, not the
  other way round. Item 1 is still open, and `&a[k]` out of bounds is unchecked by compiler *and*
  assembler - a sharper edge than the doc's original claim about locals vs globals, which was false.

Also found while checking: a named `const` upper bound defeats the E444 range check, E445's caret
pointed one statement late (since fixed, along with E403/E305/E422 - see `design/impala/SyntaxConsistency.md`
§5), and `design/impala/StructLayoutConstants.md` understated its own design so badly that it claimed a
per-level `ADDp` cost for what is actually assemble-time folding.


# Follow-up (2026-07-30)

Batch 2a shipped (`74f6862`..`a7fac42`) and was wrong in two ways that the `pointerStride` fixture did not
reach. Both were reproduced on 2026-07-30 against the committed `Impala2` branch; **both are closed as of
2026-08-04** by the `[[ ]]` decision at the end of this document. F3 is a scoping note, not a defect, and
carries its own amendment.

## F1. ~~Every comparison between two struct pointers emits invalid GAZL~~ - FIXED 2026-08-04

*(Was **[V]** CRITICAL REGRESSION on 2026-07-30.)* Fixed by confining scaling to the subscript, whose last
table row is "`p < q` and the other five: bare comparison". This entry's own program, with named-field
initializers, **compiles, loads and runs**, printing `1 / 2 / 20` (re-verified 2026-08-05):

```impala
struct S { int a; int b; int c }
global S array bank[3] = { { a: 1, b: 0, c: 0 }, { a: 2, b: 0, c: 0 }, { a: 20, b: 0, c: 0 } }
export function main() locals S pointer p, S pointer q, int n {
	p = &global bank[0]; q = &global bank[2];
	if (p < q) { n = 1; }
	printInt(p->a); printLF(); printInt(global bank[1].a); printLF(); printInt(q->a); printLF();
}
```

The code block above was written with `[[ ]]`, which was REVERSED on 2026-08-04 and is now `E404 Invalid
lvalue` - so the evidence for a "FIXED" entry did not compile. Respelled and re-run.

Description kept as the record: the scaling branch keyed on "the left operand is a struct pointer" and
fired for **every** operator, not just `+` and `-`. A comparison has no unit, so scaling either side was
meaningless, and the result was `MULi %0 $q #.z.S` — a `MULi` on a pointer operand, which `GAZLCmd`
refused to load with `Incompatible types: $q`. The JS gates were green only because no fixture compared
two struct pointers.

## F2. ~~`for` over a struct pointer is silently wrong~~ - FIXED 2026-08-04, by rejection

*(Was **[V]** CRITICAL on 2026-07-30.)* No longer silent and no longer wrong: the construct is now
`E309 For variable must not be a struct pointer`, with the note "FORp cannot stride by a struct - use
`while (p < end) { ...; p = &p[1]; }`". `design/impala/SyntaxConsistency.md:24-31` already records this
correctly, including that the earlier stride-by-hand fix was implemented and then reverted.

Description kept as the record:

```impala
struct S { int a; int b; int c }
global S array bank[3] = { { 10, 0, 0 }, { 20, 0, 0 }, { 30, 0, 0 } }
for (p = &global bank[0] to &global bank[3]) { printInt(p->a); printLF(); }
```

printed `10 0 0 20 0 0 30 0 0` - nine iterations, not three. The bound scaled correctly to
`&bank + 3*.z.S`, and then `FORp` stepped one word:

```gazl
! MULi <A> #3 #.z.S
ADDp %0 &bank #<A>
.l1:	PEEK %2 $p #.o.S.a
	FORp $p %0 @.l1
```

It could not be fixed under the elements rule: `FORp ptr(d) &address @label` is already three operands,
and GAZL is a 3-operand ISA (see "No GAZL instruction is missing here" above, which established the same
constraint for `ADDp`/`DIFp`). The only lowerings available were to reject the construct or to abandon
`FORp` for `ADDp`+`LSSp`+`GOTO`, which is three instructions where the language promises one. Rejection
won.

F1 and F2 together meant the 2026-07-29 decision did not describe a reachable design. "Elements
everywhere, one rule, no exceptions" leaks into comparison, where there must be no unit, and cannot reach
`for`, where the unit cannot be represented. Both are closed by the `[[ ]]` decision that replaced it.

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
- **Address formation PAST THE END** (`&a[7]`, `&a[N]`, `&p[i]`) - always legal, never checked.

`CompileTimeHardening.md`'s motivating example is `a[7] = 1`, a store, so the check as *motivated* is
already correctly scoped; only the wording generalises past it. Note this leaves Impala **simpler** than
C, not looser: C needs an explicit carve-out making one-past-the-end valid and two-past undefined, while
Impala needs no carve-out at all.

> **AMENDED 2026-08-03, when this shipped as `E461`.** A NEGATIVE constant index is rejected in every
> context, address included - the one exception to the second bullet, and the paragraph above overstates
> "no carve-out at all". It is not a distance rule: a negative offset is not an address the target machine
> will take either. `MOVp $p &g:-1` and `ADRL $p $a:-1` are both rejected at assembly ("Negative value not
> accepted"), and on a struct field `.o.S.pad + (-1)` folds to a perfectly good POSITIVE offset naming the
> previous field, so `&s.pad[-1]` assembles, runs and silently aliases a neighbour - the very bug the check
> exists for. So the reasoning above still holds ("do not invent a restriction the target does not have");
> it is just that for a negative index the target does have one. See `docs/impala/Impala2.md` "Array bounds" for
> the shipped rule, including the `--range-checks` runtime tier for indices no constant check can see.


# ~~Decision: the scaled subscript is spelled `[[ ]]`~~ — REVERSED 2026-08-04

> **REVERSED. `[[ ]]` no longer exists; `a[i]` strides by the declared element size, whatever it is, and
> `E204`/`E205` are gone.** The section below is kept as the record of why it was adopted and is accurate
> about the 2026-07-30 reasoning - do not read it as current syntax. `docs/impala/Impala2.md`, "One subscript",
> is normative.
>
> Three things decided it. (1) The marker carried **no information**: because each bracket form was an
> error where the other belonged, the compiler always knew which was meant and the fix-it was purely
> mechanical - the "not interchangeable, so nothing to dilute" defence below argues for removal as easily
> as against it. (2) The marker was **usually wrong**: 35 of the 49 uses in this repo's corpus were
> constant indices, where the stride folds to an assemble-time `!` line and costs nothing at run time, so
> `bank[[2]]` announced a multiply that was not there. "What it buys" below rests on `[[ ]]` meaning one
> `MULi`, and for constant indices it does not. (3) **"When to revisit" called it**: that clause, at the
> foot of this section, names the multidim-array case as a trigger - `a[3, 5, 6]` has to scale on every
> axis with no marker available to say so, so the notation was one feature away from breaking regardless.
> Pointer arithmetic had meanwhile settled on silent element-size scaling, which this decision
> contradicted.
>
> Removing it changed no emitted code: the corpus regolded with byte-identical instruction streams, only
> the echoed source in trailing comments and the column numbers moving. Arithmetic on a struct pointer is
> still `E307` - the reasoning for that is independent, and `&p[i]` still spells the move.

Supersedes the 2026-07-29 decision. **A subscript that scales by the element size is written `[[i]]`, and
it is the only construct that moves a struct pointer.** Arithmetic on struct pointers is rejected.

| form | on unit-stride (`int pointer`, untyped `pointer`, `int array`) | on struct array / struct pointer |
|---|---|---|
| `a[i]` | one instruction, stride 1 | **error**, fix-it to `a[[i]]` |
| `a[[i]]` | error, fix-it to `a[i]` | one instruction + one `MULi`, stride `sizeof(elem)` |
| `p + i`, `p += i`, `p - i` | unchanged, stride 1 | **error**, fix-it to `&p[i]` |
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

What decides it in favour of 4 over 3: `docs/impala/Impala2.md:472-485` promised **instruction count = marker
count**, and struct subscripting broke it. The 2026-07-29 decision responded by retreating to "cost is
predictable from the declared types". `[[ ]]` makes the original sentence true again - `[[ ]]` is the
marker for the multiply, exactly as `->` is the marker for the load - instead of weakening it. Restoring
a principle beats rewording it.

The objection that killed this idea on the first pass was that a second subscript operator means two
spellings for one idea, which `docs/impala/Impala2.md:1102` rejects elsewhere. It does not apply: `[]` and
`[[ ]]` are **not** interchangeable. Each is an error where the other is correct, so there is exactly one
legal spelling per access and nothing to dilute.

The second objection was breaking existing code. There is none - `struct` appears zero times as a language
feature in the 1.0 spec (`docs/impala/Impala.md`; its four "struct" hits are substrings of "constructs" and
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
pervasive, and the balance between `[]` and `[[ ]]` should be re-argued from scratch.

The other trigger this clause named - multidimensional arrays - **fired on 2026-08-04**, and it is why
`[[ ]]` was reversed rather than revisited: `a[3, 5, 6]` scales on every axis with no marker available to
say so. The reversal preamble in section F records it. Nothing is pending here.
