# JSPEG 2 — Value-Returning Rules (dropping the `$` container)

Status: **LIVE, NOT STARTED — plan only** (written 2026-08-28). This is the "JSPEG 2" end-state named in
[`JSPEGFuture.md`](JSPEGFuture.md) Problem 2: the one breaking change to the JSPEG code generator itself, and
it is about `$$`/holders, nothing else. It is independent of the two-phase AST rework (Problem 1) and of the
`$$parser`-state-on-an-object step; none of them gate this one.

## What is being removed

Today every generated rule is `function Rule($){ … }` where `$` is a **holder object** and the rule's value
is `$._`. Two mechanisms sit on that, both in [`impala/jspegCompiler.js`](../../impala/jspegCompiler.js) (the
generated generator) and defined in [`impala/jspeg.jspeg`](../../impala/jspeg.jspeg):

- **Tag holders.** `$x:Rule` allocates a fresh holder — `Tagged` emits `($t.vr=$.vr)[$v._]='={}'`, so `$x`
  becomes `$x={}` and its value is `$x._`. `Definition` then declares `var $x={},…` at the top of the rule.
- **The `._` rewriter heuristics** in `Action`: `$$`→`$._`, `$$.`→the holder object, and bare `$name`→
  `$name._` *unless* the name is not a holder or is immediately followed by `.`. The consequence, and the reason
  this is worth removing, is that **the meaning of `$name` in an action depends on distant grammar context**
  (whether that name was ever introduced as a tag) — the "holder/value duality."

Dropping the container means: a rule *produces* its value, tags become **plain local variables**, `$$` becomes
an **ordinary variable**, and the `._` heuristics plus the `={}` holder allocation are deleted from the
generator. Object-valued results still support field mutation (`$$.count = 0` on a plain object), so
container-style rules (e.g. `FuncCall`) migrate by initializing `$$ = {…}` instead of relying on a pre-existing
holder.

## The load-bearing design question (settle in M1 before anything else)

The current model carries **two** channels: the rule's boolean return is *parse success*; the value rides
*out of band* in the caller's holder (`$._`). Value-returning rules collapse the value channel into the return
— which forces a decision about how *success* is then signalled, because the sequence/choice combinators are
built on `&&`/`||` over booleans (`p1 && p2 && … || (backtrack)`) and you cannot `&&` values.

Candidate protocols, to be chosen and prototyped in M1:

- **FAIL sentinel.** A rule returns its value on success and a module-level `FAIL` object on failure;
  combinators become `v = Sub(); if (v === FAIL) backtrack`. Clean, but every rule site tests the sentinel and
  "matched and produced `FAIL`-shaped value" must be impossible by construction.
- **Boolean return + assigned value var.** Keep the boolean combinator chain exactly as today, but replace the
  holder with a plain local the action assigns and the rule returns via a trailing slot the generator threads.
  Smallest diff to the combinator codegen; the question is where the returned value is stashed without a holder.
- **Position-delta success + value return.** Success = `_i` advanced / no error flag set; value = the return.
  Decouples the two channels entirely but changes how zero-width matches report success.

M1's job is to pick one, prove it on the self-grammar, and record why. Everything downstream assumes the choice.

## M1 findings (2026-08-28): the holder's dual role, and why the self-grammar is the *hard* case

Reading the generator source surfaced a subtlety Problem 2 understates: the holder `$` carries **two** channels,
not one.

- **Upward — the value.** `$._` is the rule's semantic result. This is the channel JSPEG 2 is about.
- **Downward — scope context.** In the self-grammar, `Definition` sets `$$.tag/vi/vr` and the `Expression`
  sub-rule *reads them back* from the same holder it is handed ([`jspeg.jspeg:46-47,67`](../../impala/jspeg.jspeg)) —
  the holder is threaded *down* to pass the current variable-numbering scope into nested expressions.

Measured on the two grammars (2026-08-28):

- **`impala.jspeg` (the real target) uses only the upward channel.** Zero `$tag.field = $$.field` downward-copy
  sites; every `$$.field` is result-record building (`$$.words`, `$$.retSlots`, `$$.elems`, `$$.type`, …), and
  all cross-rule state lives on `$$parser.*` globals (`fail`×87, `emit`×70, `metacode`, `symbols`, `declare`,
  `newLabel`, `counters`, …). So `$$` there is a plain bottom-up record and nothing is threaded through holders.
- **Only the self-grammar threads context down** (`vi/tag/vr`). It is therefore the *atypical, hard* case — the
  opposite of the plan's original "prototype on the self-grammar first (easy)" assumption.

**Consequence for the protocol.** Split the holder's two roles instead of collapsing them:

- **`$$` becomes the value** — a plain rule-local, object-valued where used as a container (initialize `$$ = {}`
  before the first `$$.field =`), scalar otherwise. It flows *up* via a shared return register (`_val`): a rule
  keeps its boolean combinator return (so the `&&`/`||` chains are unchanged) and sets `_val` to its result;
  tags capture `_val` **eagerly** in the same `&&` step as the call (`Sub() && ($x = _val, true)`), so the single
  register is never clobbered before capture. This deletes holder allocation and the `._` convention.
- **`$` (single) becomes the downward context param**, kept only for rules that thread scope. In a migrated
  self-grammar the `vi/tag/vr` uses move from `$$.` to `$.`; **`impala.jspeg` needs none of this**, so its
  migration is uniform and mechanical.

**The passthrough refinement (found stress-testing `_val` against `jspegTest.jspeg`).** A shared register has a
clobbering hazard the holder model does not: in a passthrough rule like `group <- number _ / '(' _ expr ')' _`,
`number` sets the value but the trailing `_` (whitespace, no action) runs *after* it. With a naive
"set `_val = $$` at every rule end," `_`'s call would overwrite `_val` with junk and `group` would lose
`number`'s value. The holder model is immune because `number` and `_` share `group`'s one holder and only
`number` writes it. Resolution, and the precise emission rule:

> A rule sets `_val` **only if it assigns `$$`** (via an action, a `$$:` tag, or a `$$=` capture). Value-less
> rules (`_`, pure token rules) leave `_val` exactly as their sub-calls left it. So `_val` is touched only by
> value-producing rules, and passthrough works: after `number() && _()`, `_val` still holds `number`'s value,
> and a `group` with no `$$` action forwards it untouched.

The generator must therefore track a per-rule "assigns `$$`" flag (a small addition alongside the existing
`vars` tracking) and emit the trailing `&& (_val = $$, true)` conditionally.

**Protocol empirically validated (2026-08-28).** [`valueReturningProto.js`](valueReturningProto.js) is a
hand-generated value-returning parser for `jspegTest.jspeg` (built by applying the emission rules above by hand)
that passes all 9 arithmetic cases — passthrough (`root`, `group`), the accumulator loops (a tagged sub-call
sets `_val` but does **not** clobber the parent's `$$` local), operator precedence, parens, whitespace, and
division. So the `_val`-register model computes correctly end to end; what remains for M1 is making the
*generator* emit this shape (not whether the shape is right).

This is the working decision.

## M1 emission prototype — WORKS, and it found the migration's real boundary (2026-08-28)

The value-returning *generator* is prototyped in [`impala/jspeg2.jspeg`](../../impala/jspeg2.jspeg) (a copy of
`jspeg.jspeg` with ~7 emission actions rewritten). Driven meta-circularly — current `compileJSPEG` compiles
`jspeg2.jspeg` into a new-emission generator, which compiles `jspegTest.jspeg` — it produces exactly the target
shape (param-less rules, `var _v` value locals, eager `($x=_val,true)` captures, a conditional
`&&(_val=_v,true)` suffix, **no holders, no `._`**) and **passes all 9 arithmetic cases**. The generator surgery
is real, not hand-waved. Two hazards from the M1 note were handled cleanly:

- **The string-literal `$` hazard** (the rewriter processes `$` even inside emitted string literals — it is why
  the old `Definition` carries a stray `var $` from its `'($){'` literal): dodged by naming the value local `_v`
  (no `$`) and going **param-less** in the prototype (rule refs emit `ID()`, no `$` literal anywhere).
- The generator's own actions stay holder-style (interpreted by the current compiler); only the emitted strings
  changed — so the bootstrap is clean.

**The boundary it found — the shared-downward-container idiom does NOT survive.** `tagCaptureTest.jspeg` builds a
map with `root <- _ {$$={}} pair …` where the untagged `pair` does `$$[$key]=$value` **expecting `$$` to be
root's map** (holder model: `pair` shares root's holder). Under value-returning, generated `pair()` gets its own
`var _v` and writes `_v[$key]` on an *uninitialized* local — it crashes, and even if it didn't it would fill the
wrong object. So a rule that mutates an **inherited** `$$` container via an untagged call is not mechanically
migratable; such grammars must be rewritten (sub-rule *returns* a value, parent merges) or use an explicit
accumulator (a `$$parser` global). This is exactly the kind of thing M3's "tag rebinding / container audit" must
catch, now with a concrete failure signature to grep for.

**Does `impala.jspeg` (the real target) use that idiom? — inconclusive, audit required.** Counts: 20 `$$ =`
initializations vs 75 `$$.field`/`+=` writes. That ratio fits *either* "init once, write many fields locally"
(safe) *or* some rules writing an inherited `$$` (unsafe). The dominant patterns — per-rule `makeMeta` records
and `$$parser.*` globals for accumulation (`emit`, `metacode`, `symbols`) — lean strongly toward local records +
global accumulation (safe), but this must be **proven per-rule in M3** before the migration is declared
mechanical. The go/no-go for the whole refactor rests here: if impala.jspeg avoids the inherited-container idiom,
the migration is uniform; if a few rules use it, they get the return-and-merge rewrite.

**Generator self-hosting** remains its own step (regenerating `jspegCompiler.js` with the new emission requires
the self-grammar's downward-`vi/tag/vr` context to move onto the `$` param) — isolated to the generator, not to
`impala.jspeg`.

## Go/no-go audit result (2026-08-28): NOT uniform — the postfix-chain/accumulator cluster is the real work

The audit (`auditContainer.js`: per-rule, flag any rule that mutates `$$` as a container without a direct init)
found **6 of 93 rules** using the pattern: `TypeDeclr`, `ExternDecl`, `VarDecl`, `ArrayDecl`, `FuncCall`,
`Argument`. Inspecting the two load-bearing ones settles the question — **the answer is that `impala.jspeg` DOES
use the inherited-container idiom**, in its call machinery:

- **`FuncCall`** receives `$$` = the *callee's* meta record (built earlier in the postfix chain, the documented
  "`$` shared by uncaptured sub-rules" pattern) and adds `.count/.retSlots/.words/.base/.types/.elems/…` to it.
  It never inits `$$` because `$$` is inherited — so the JSPEGFuture claim "FuncCall migrates by initializing
  `$$ = {}`" is **wrong**: that would destroy the inherited callee record.
- **`Argument`** (referenced untagged ×4 inside `FuncCall`) accumulates into that shared `$$`: `++$$.count`,
  `$$.types.push(meta.type)`, `$$.words += w`. This is exactly the tagCaptureTest crash pattern — an untagged
  child mutating the parent's container.

So dropping the `$` container is **not** a mechanical migration of 125 sites. There is a **hard core**: the
postfix-chain expression builder plus the call/declaration accumulators (the 6 rules above, at least
`FuncCall`+`Argument` confirmed as inherited-accumulators; the declaration rules — some referenced tagged, e.g.
`v:VarDecl`, `out:TypeDeclr` — need per-rule triage into "self-container, add init = easy" vs
"inherited-accumulator = rewrite"). These rules keep a *mutable record threaded across sibling rules*, which the
per-rule-`_v` model does not provide.

**This is the real design decision the refactor turns on** — how to carry that threaded record without the
holder. Options (to weigh before M3):

1. **Explicit accumulator off `$$`.** Move the call/decl record onto `$$parser` state (a small stack of
   in-progress records), the way impala.jspeg already accumulates emitted code via `$$parser.emit`/`metacode`.
   The postfix chain pushes/reads the current record; `$$` stays a plain value. Most in keeping with the
   codebase's existing "state lives on `$$parser`" style; the tradeoff is a manual push/pop discipline.
2. **Return-and-merge.** `Argument` *returns* its `{type, elem, words, …}` contribution; `FuncCall` folds the
   list. Purely functional, no shared state, but a larger rewrite of the argument loop and the close-of-call
   checks that currently read the accumulated fields.
3. **A context object threaded down the chain** (the `$` param this plan already reserves for the self-grammar's
   scope) — carry the in-progress record on `$` for exactly these rules. Smallest diff to the existing actions,
   but reintroduces a downward container for one cluster (a scoped, honest version of what we're removing).

Recommendation leans (1): it matches how impala.jspeg already threads cross-rule state and keeps `$$` a pure
value everywhere. The choice gates M3; the other ~119 sites remain the mechanical migration the plan assumed.

### DECISION (2026-08-28): return-and-merge (option 2) for the hard core

`Argument` *returns* its contribution instead of mutating a shared record; `FuncCall` folds the list. No shared
mutable state — `$$` is a pure value everywhere, the strongest end-state. Concrete shape:

- **`Argument`** returns a record `{ meta: $a, type, elem, opnd, structWords }` (structWords = 1 for a scalar,
  `structWords(struct)` for a by-value struct) and does **not** place the value or touch a shared `$$`. The
  `E406`/index-use checks it does today stay in `Argument` (they are per-arg and need no window offset).
- **`FuncCall`** collects the returned records into a local array (the `(Argument (','_ Argument)*)?` loop tags
  each and pushes), then **after the loop folds once**: `words = 0; for each arg { winSlot = base + retSlots +
  words; place(arg) /* makeArgValue or copyStructArg at winSlot */; words += arg.structWords; }`, then runs the
  existing count/type/signature checks against the collected list and closes the call. `$$` for `FuncCall` stays
  the inherited callee meta; the accumulation lives in locals.

**The load-bearing risk is emission ORDER.** Today placement (`makeArgValue`/`copyStructArg`, which *emit*)
happens *during* the argument loop; return-and-merge moves it into the post-loop fold. The fold must emit in the
same left-to-right arg order so the generated `.gazl` is **byte-identical** — the corpus parity gate is the
proof. Two things to verify when it lands: (a) the by-value-struct `svals` LAST-door check at close still sees
the same list, and (b) the scratch-pool ownership of each `$a` survives being held in the array until the fold
(see the `<X>` scratch-ownership note in memory) — i.e. the args' meta slots must not be released until placement.

`Argument`+`FuncCall` are the confirmed inherited-accumulators. The four declaration rules (`TypeDeclr`,
`ExternDecl`, `VarDecl`, `ArrayDecl`) get the M3 triage: a tagged-only, self-contained record just gains a
`$$ = {}` init (mechanical); any that accumulate across an untagged call get the same return-and-merge treatment.

## Status after M1 (2026-08-28)

M1 is effectively complete: protocol decided **and** validated (hand-parser + real generator, `jspegTest` 9/9),
value-returning generator prototyped in `jspeg2.jspeg`, the migration boundary found and the go/no-go answered
(not uniform; hard core identified), and the hard-core strategy chosen (return-and-merge).

## M3 triage complete (2026-08-28)

**A key mechanics correction confirmed during triage:** `$$.field` maps to `$.field` — a field on the **holder
object itself** — while bare `$$` is `$._` (the value slot). So "container-style" rules store their record as
holder FIELDS, and the holder is fresh-per-tag (`x:Rule` allocates `$x = {}`) or shared when the rule is called
untagged. That is the precise dividing line:

| Rules | Holder | `$$.field` uses | Migration |
|---|---|---|---|
| `FuncCall`, `Argument` | inherited/shared across the postfix chain | `++`/`+=` **accumulate** | **return-and-merge** (hard core) |
| `TypeDeclr`, `ExternDecl`, `VarDecl`, `ArrayDecl` | own record (fresh, captured by a tagged parent) | all `=` **set** | **add `$$ = {}`** (mechanical) |

**Both migration paths are now validated on the real generator**, not asserted:

- Self-container path: [`selfContainerProto.jspeg`](selfContainerProto.jspeg) (`item <- key:Id '=' val:Num
  { $$ = {}; $$.k = $key; $$.v = $val; … }`) compiled by the value-returning generator returns
  `{"k":"foo","v":42,"tag":"rec"}` — `$$ = {}` init + `$$.field =` on the local + tagged captures all work.
- Core protocol path: `jspegTest` 9/9 (arithmetic, accumulator loops, passthrough).

The return-and-merge path for the hard core is designed (above) but not yet built — that is the first coding step
of M3 proper.

## M3 is a big-bang, not incremental — the toolchain constraint

Important sequencing reality found in M1: `impala.jspeg` is compiled by the **holder-based** production generator
(`jspegCompiler.js` via `updateJSPEG.js`). A value-returning rewrite of any rule miscompiles under that generator,
so rules **cannot** be migrated one-at-a-time against the production toolchain. M3 therefore switches the
generator (use the value-returning `jspeg2.jspeg`-derived generator for `impala.jspeg`) and migrates **all** sites
in one gated step — verifiable but not incrementally reviewable — **unless** a dual-mode generator (understands
both `._` and value styles) is built first to allow staged migration. Decide dual-mode-vs-big-bang at the top of
M3; the byte-identical corpus parity gate makes either safe to *verify*.

Order for M3 proper: (0) decide dual-mode vs big-bang; (1) self-host the value-returning generator (move the
self-grammar's `vi/tag/vr` onto the `$` context param) so it can be the toolchain generator; (2) `Argument`/
`FuncCall` return-and-merge; (3) the four declaration rules gain `$$ = {}`; (4) the ~119 mechanical sites;
(5) switch `updateJSPEG.js` and prove byte-identical `.gazl` on the full corpus.

## DECISION: dual-mode — and it is validated, so M3 IS incrementally reviewable (2026-08-28)

Dual-mode chosen, and the enabling insight is now proven: **the holder's `._` field already IS the transfer
register.** Every generated rule (holder OR value style) writes its value to `$._`; captures read `$._`. So a
value-style rule needs only to (a) use a local `_v` and plain-local tags internally, (b) suppress the `._`
heuristic in its actions, and (c) **bridge `$._ = _v` on success**. Holder-style rules are completely unchanged.
Both styles then interoperate through `._` with zero glue.

**Validated** by [`dualModeProto.js`](dualModeProto.js): a hand-written parser mixing holder-style
(`root`/`product`/`group`/`_`) and value-style-bridged (`expr`/`number`) rules computes all 9 arithmetic cases,
with the style boundary crossed in **both** directions (holder `product`→value `number`; value `expr`→holder
`product`). So a rule can be migrated to value style **independently**, and it keeps working alongside unmigrated
rules — the migration is rule-by-rule under the byte-identical parity gate, not a big-bang. `_val` (the global
register) is not needed for the transition; it becomes a *final* mechanical step (rename `$._`→`_val`, drop the
holder param) once every rule is value style.

Revised M3 order (dual-mode): (1) extend the generator with a **per-rule value-style flag** (a marker or a
name-set) that switches action-rewriting to the value style and appends the `$._ = _v` bridge — a small addition
to `jspeg2.jspeg`'s Definition/Action/Tagged/Capture; (2) migrate rules to value style in reviewable batches
(mechanical self-containers, then the `Argument`/`FuncCall` return-and-merge as one unit), each behind
`node impala/updateJSPEG.js` + `tools/test-js` byte-identical; (3) when the last rule flips, do the final
`$._`→`_val` + drop-holder mechanical pass and delete the holder machinery (M4).

Everything through here is de-risked with runnable proofs; the next coding step is the per-rule value-style flag
in the generator.

## Relationship to the other jspeg work

- **[`RefactorPlan.md`](RefactorPlan.md) (return-style helpers) becomes moot for its stated goal.** That plan
  reduces the number of *holder-touching* action sites while **keeping** the container; JSPEG 2 removes the
  container outright. If JSPEG 2 lands we do not also need RefactorPlan — but its milestone discipline
  (behavior-preserving, fixture-gated, one reviewable step at a time) is the model this plan follows. Do not
  invest in RefactorPlan's `binaryRet`/`lookupRet` wrappers as a prerequisite; they would be migrated away
  again.
- **Two-phase AST (Problem 1) is orthogonal.** It pairs *naturally* with JSPEG 2 (in two-phase style `$$` is
  just the node under construction and the holder question evaporates), but neither waits for the other and this
  plan does not touch the `dry`/`FuncCall` speculation patches.

## Current state (verified 2026-08-28)

- **125** `$$.` holder-escape sites in `impala/impala.jspeg` — the mechanical migration surface.
- **0** return-style `Ret(` helpers present (RefactorPlan is genuinely 0/8, nothing to unwind).
- `impala/impala.node.js` (fast dev compiler) exists; `tools/test-js.{sh,cmd}` is the parity gate.

## Guardrails

- **The parity fixtures are the spec.** Generated `.gazl` output must stay **byte-identical** across every
  milestone — `tools/test-js.{sh,cmd}` runs the corpus byte-compare plus the NuXJS parity set. A milestone is
  not done until that gate is green. Behavior change, if any is ever intended, is a separate reviewed step.
- Regenerate generated files with `node impala/updateJSPEG.js` after any grammar/generator edit; run
  `node impala/updateJSPEG.js --check` to confirm the checked-in generator is current.
- **NuXJS-safe by construction.** The generator and the compiler it emits must run under NuXJS; avoid JS
  features NuXJS lacks (no `.map`, ES3-level). The NuXJS parity leg of the gate enforces this.
- The generator change and the `impala.jspeg` action migration are **coupled** (the generator decides how an
  action's `$name`/`$$` is read). M1 must decide whether a **dual-mode generator** (understands both the old
  `._` style and the new value style during transition, enabling incremental site migration) is feasible, or
  whether the switch is **atomic** (generator + all 125 sites in one gated step). Prefer dual-mode if it is not
  hacky; fall back to atomic, which the byte-identical gate still makes verifiable even if it is a large review.
- Keep the old holder machinery in the generator until every site is migrated and the gate proves it unused.

## Milestones

### M1 — Protocol design + generator prototype
Goal: settle the value/success protocol (**done — the `_val`-register split, see M1 findings above**) and prove a
value-returning *emission* on throwaway grammars before any `impala.jspeg` exposure.
- ~~Choose among the candidate protocols~~ — **done**: `_val` return-register for the value, boolean success
  unchanged, `$` retained as the downward context param. Recorded above.
- Prototype the new emission in the generator and prove it compiles `impala/jspegTest.jspeg` and
  `impala/tagCaptureTest.jspeg` to working parsers. **Note the inversion (M1 findings): the self-grammar is the
  *hard* case (it threads `vi/tag/vr` down), so it is not the right first prototype — validate on the small
  grammars and an `impala.jspeg` subset first, and treat self-hosting `jspegCompiler.js` as its own step.**
- Decide dual-mode-vs-atomic (see guardrails) and record it.
- Exit: a working value-returning emission for the small grammars + a written protocol/transition decision
  (protocol done; emission prototype + transition decision remain).

### M2 — Generator self-hosting fixed point
Goal: the new generator regenerates itself stably and behaves identically on the small grammars.
- Feed the new `jspeg.jspeg` through the new generator; confirm a fixed point (regenerating again is a no-op).
- Confirm byte-identical parser behavior on `jspegTest` / `tagCaptureTest` outputs.
- Exit: `node impala/updateJSPEG.js` is stable under the new generator; self-grammar tests pass.

### M3 — Migrate `impala.jspeg` actions to the value style
Goal: convert the 125 `$$.` sites and the implicit `$name._` reads to plain-variable form.
- Migrate in reviewable batches by rule family (expressions → assignment → calls → control flow), mirroring
  RefactorPlan's risk ordering, if dual-mode (M1) allows incremental migration; otherwise stage the edits and
  land them together with the generator switch.
- Audit tag rebinding (a name reused as a tag in more than one alternative) — the one place plain locals differ
  from per-alternative holders.
- Keep generated `.gazl` byte-identical after each batch: `node impala/updateJSPEG.js` then
  `node impala/jspegCompilerTests.js` and the full `tools/test-js` gate.
- Exit: no `$$.` holder-escape sites remain; the corpus and NuXJS parity are byte-identical.

### M4 — Delete the holder machinery from the generator
Goal: remove the now-dead container support.
- Remove the `={}` holder allocation (`Tagged`/`Definition`) and the `._` heuristics (`Action`) from
  `jspeg.jspeg`; regenerate.
- Search generated + source for any residual `._`/holder use; the gate must stay green with the machinery gone.
- Exit: `node impala/updateJSPEG.js --check` clean; no holder/`._` codegen remains; gate byte-identical.

### M5 — Documentation
Goal: describe the final value model for future grammar authors.
- Rewrite the holder/`._` semantics section of [`JSPEG.md`](JSPEG.md) as the value-returning model; delete the
  duality material.
- Fold the outcome back into [`JSPEGFuture.md`](JSPEGFuture.md) (Problem 2 resolved) and retire this plan's
  "not started" status.
- Exit: docs describe only the value-returning model; no obsolete holder/`._` references remain.

## Risks / open questions

- **The protocol choice (M1) is the whole risk.** If none of the candidates yields byte-identical output
  without ugly special-casing, stop and reconsider scope before touching `impala.jspeg`.
- **Zero-width and falsy values.** Any protocol must distinguish "matched, produced a falsy/empty value" from
  "failed" — the FAIL-sentinel and position-delta options handle this differently; the prototype must cover a
  rule that legitimately yields `''`, `0`, or `undefined`.
- **Dual-mode feasibility** decides whether M3 is incremental or a single large gated commit; the byte-identical
  fixtures make either safe to *verify*, but not equally easy to *review*.
- **NuXJS.** Any new generator idiom must clear the NuXJS parity leg; the safest posture is to reuse constructs
  the current generator already emits.

## M3 IN PROGRESS — dual-mode + two-pass generator built, all self-containers migrated (2026-08-28)

The dual-mode generator (option B) is **built, in the production `jspeg.jspeg`/`jspegCompiler.js`, and gate-green**:

- **Dual-mode:** a rule marked `<=` emits value-style (`var _v`, plain-local tags, no `._` heuristic, bridge
  `$._ = _v` at the end); `<-` rules are unchanged. Both interoperate through `$._`. Byte-identical with no rule
  marked.
- **Two-pass:** the generator prelude pre-scans the grammar for `Name <=` (`_vsRules`); a capture of a value-style
  rule is marked so the Action rewriter emits `._` on it (`$x.field` → `$x._.field`), letting a holder-style
  caller read a value rule's fields. Detection scans **all** rule references in a tagged Suffix, so a value rule
  inside a tagged group (`v:(VarDecl / ArrayDecl)`) is found too.

**Self-containers migrated (each: flip `<-`→`<=`, add `$$ = {}`, regenerate, byte-identical `.gazl`, full gate):**
`TypeDeclr`, `VarDecl`, `ArrayDecl`, `ExternDecl` — **all four done.**

**Two interop wrinkles the gate caught and were fixed locally (both byte-identical):**
1. **Tagged groups** — the first cut of the two-pass only saw a bare rule ref; extended to scan every rule ref in
   the Suffix (fixes `v:(VarDecl / ArrayDecl)`).
2. **Untagged-sharing site** — `ExternDecl` had a bare `VarDecl` filling its shared holder (the inherited-container
   idiom). Tagged it (`d:VarDecl { $$.type = $d.type; … }`) mirroring the existing `ArrayDecl` branch. This is the
   same class of thing the `FuncCall`/`Argument` hard core needs, in miniature — expect a few more such sites.

**Remaining M3:** the `FuncCall`/`Argument` return-and-merge hard core; the ~119 mechanical value sites (each a
flip + gate, watching for untagged-sharing sites to tag); then **M4** — once every rule is `<=`, delete the whole
dual-mode/two-pass/holder scaffolding and collapse `$._`→`_val`, param-less.

## M3 mechanical batch (2026-08-28): 9 leaf rules + the shape of what's left

Migrated (byte-identical, full gate): the scalar-capture leaves `ADDSUB_OP`, `MULDIV_OP`, `BITWISE_OP`,
`PREFIX_OP`, `BUILT_IN`, `IntegerLiteral`, `FloatLiteral`, `StringLiteral`, and `TypeBase`. **13 rules total now
value-style** (these + the 4 self-containers).

**The "~119 mechanical sites" are not uniformly a flip.** Trying `Identifier` (22 errors, reverted) showed the
remaining rules split three ways:
1. **Clean leaves** — scalar/record consumed via tags. Flip + gate (done above).
2. **Untagged-sharing rules** — used bare somewhere, so a consumer reads them via `$$`-sharing. Each needs its
   untagged sites tagged first (the `ExternDecl`-`VarDecl` fix). `Identifier` is this, with *many* consumers.
3. **The expression precedence chain** (`Expr`/`Bitwise`/`AddSub`/`MulDiv`/`PrePost`/`Comp`/`Value`/`Subscript`/
   `FieldAccess`) — shares `$$` up the chain like the `FuncCall` postfix chain: a **second cluster** to migrate as
   a unit, not rule-by-rule.

So the real remaining M3 is: a few more clean leaves; then per-rule tagging for the untagged-sharing rules; then
the two clusters (expression chain, call chain incl. `FuncCall`/`Argument` return-and-merge). The
dual-mode/two-pass machinery handles all of it — it is surgery, not trivial flips. Then M4 deletes the scaffolding
and collapses `$._`→`_val`.

## M3 batch 2 (2026-08-28): the ~v/explicit-`._` guard, then 30 more rules

**The "Identifier is untagged-sharing" diagnosis in batch 1 was wrong.** The real failure was a
double-`._`: three actions write `$label._` explicitly (`Statement`, `Goto`), and once `Identifier`
is value-style the two-pass `~v` marker *also* auto-appends `._` on that same capture → `$label._._`
→ `undefined` label (E446). Fixed at the source — the generator's Action rewriter — not per site:
the `~v`/heuristic `._`-append is now guarded by `$b.slice(0, 2) !== '._'`, so an action that already
wrote `._` is left alone. One token in `jspeg.jspeg`, byte-identical for every rule already passing,
and it is the general fix (any future value-style capture that a legacy action deref'd with `._` now
just works). With that guard, `Identifier` migrates clean — it was never an untagged-sharing rule.

**Lesson:** the "three-way split" over-counted category 2. Most rules are category 1 once the guard
is in. The genuine shared-container rules are far fewer than feared — so far only `ExternDecl`'s
`VarDecl` and (still ahead) the `Switch`/`CaseExpr` + expression/call chains that carry `$$` up.

**Also found — the `_v` register clash.** Actions wrap in an IIFE, so an author-local `var _v`
shadows the value register *inside* the action; a value-style `$$ =` there would write the IIFE-local,
not the register, and the bridge would read `undefined`. Only `BracedEntry` had one; renamed its
local `_v`→`_e` (byte-identical). Grep `\b_v\b` before migrating any rule with hand-written locals.

**Migrated this batch (each byte-identical, full gate green):**
- `Identifier` (+ the generator guard).
- 14 self-contained statement rules: `Assert` `Block` `Goto` `Return` `Break` `Continue` `If`
  `DoWhile` `Loop` `For` `Copy` `Destructure` `DestTarget` `While`.
- 6 array-builders: `Braced` `BracedEntry` `BracedItem` `InitList` `ArgsDecl` `LocalsDecl`.
- 7 self-contained decl rules: `ImportDecl` `ExportDecl` `StructDecl` `FuncTypeDecl` `ConstDecl`
  `GlobalDecl` `FuncDecl`.

**43 rules now value-style.** The recipe for a self-contained rule (no `$$` read that a *caller*
depends on) is a pure arrow flip: `grep '\$\$([^psi]|$)'` a rule's body (minus `$$parser/$$s/$$i`);
if it never touches the holder value, `<-`→`<=` is byte-identical. Container-builders that assign
`$$` and are read *via a tag* also just flip (the `~v` marker feeds the caller). What is left needs
real design: `root`/`Variable`; `Switch`/`CaseExpr`; the expression chain
(`Expr`/`Bitwise`/`AddSub`/`MulDiv`/`PrePost`/`Subscript`/`FieldAccess`/`FuncCall`/`Argument`/`Group`/
`BoolGroup`/`And`/`Comp`/`Value`) which threads the result register through a shared `$$`; and the
trivial token/keyword leaves (no `$$` at all) which are a mechanical M4-sweep flip.

## M3 COMPLETE (2026-08-28): every impala.jspeg rule is value-style — and the hard core was a non-event

**The whole grammar is now `<=`, byte-identical throughout.** The "FuncCall/Argument return-and-merge
hard core" and the expression precedence chain — the parts flagged as the risky, design-heavy cluster —
turned out to need **zero special handling**. They are pure arrow flips.

**The one insight that dissolved the hard core: initialize the value register to the incoming slot.**
The generator now emits `var _v = $._;` (was `var _v;`) for every value rule. Why this is the whole game:

- Meta slots are **mutated in place, never reassigned**. `makeMeta`/`binaryOp`/`lookup`/`fieldAccess`/…
  all receive the slot object (`$._`) and fill it via `metaSlot(rec)`; the slot at `$._` is allocated
  once (`createParserContext`) and its identity is stable for the rule's lifetime.
- So aliasing `_v = $._` at entry means: a rule that only **threads** an accumulator (the expression
  chain folding `binaryOp($op, $$, $r)` in place; the postfix chain where `FuncCall`/`Subscript`/
  `FieldAccess` hang fields on the shared slot; `Switch`/`CaseExpr` reading `metaSlot($$)`) has `_v`
  pointing at the same object every helper mutates, and the bridge `$._ = _v` is a **no-op**.
- A rule that **builds a fresh value** (`$$ = {}`, a literal, a fold seed) simply overwrites `_v`, and
  the bridge publishes it. Byte-identical to the old `var _v;` for every such rule (they assigned `_v`
  before use anyway).

This one line retired every seed hack tried along the way (`$$ = $l` tagged-child seeds, `$$ = $._`
per-alt seeds). It also fixed a real clobber it exposed: a multi-alt rule (`Comp`) where only some alts
set `_v` had the bridge writing `$._ = undefined` on the other alts, corrupting a slot a later stage
(the assert message via `endDebugGuard`) reused. With the init, unset `_v` == the incoming slot, so the
bridge is always safe.

**Also required:** the two-pass `~v` marker + the `._`-guard (batch 2) for value-rule captures a holder
reads; and `updateJSPEG.js`'s `KEYWORD` scan now accepts either arrow (`<[-=]`).

**Migration order that worked:** self-containers/statements/decls/array-builders (flip) → arithmetic
chain via seed-and-fold (later simplified to flips by the init) → the init → everything else as flips,
incl. `FuncCall`/`Argument`/`Switch`/`CaseExpr`/`Statement`. All 101 goldens byte-identical at every step.

## What M4 is now

M4 = **delete the dual-mode scaffolding** and make value-style the only mode: drop the `<-` path and the
`.vs` threading in `jspeg.jspeg`; drop the two-pass `_vsRules` prescan + `~v` marker (with every rule
`<=`, every capture is a value capture); collapse `$._`→a single return register and make rules
param-less (`_val`), which is the actual "drop the `$` container" end-state. The `impala.jspeg` grammar
does **not** change in M4 — only the generator and the shape of the emitted functions. `jspeg.jspeg`'s
OWN rules are still holder-style and must migrate too before the `<-` path can be deleted (the generator
self-hosts). Bar stays byte-identical `.gazl`, plus NuXJS parity + fuzz.

## M4 first attempt (2026-08-28): why `jspeg.jspeg` cannot self-migrate under the alias scheme

Flipping all 40 of `jspeg.jspeg`'s own rules to `<=` fails (`TypeError: Cannot set properties of
undefined (setting 'tag')`). Reverted; tree green. Two blockers, the second fundamental:

1. **Uninitialized value slot.** `Definition` writes `$$.tag/.vi/.vr/.vs` as *context*. In value mode
   that is `_v.tag` with `_v = $._`, and a fresh sub-holder (`$x = {}`) has `$._ === undefined`. Fixable
   the usual way (`$$ = {}` first), same as the impala self-containers.

2. **The alias goes stale when a child REPLACES the value (fundamental).** `var _v = $._` works only
   because impala's meta slots are *mutated in place* — helpers fill the same object, so the alias stays
   valid. `jspeg.jspeg` builds **strings**: every rule does `$$ = '…'` / `$$ += '…'`, which **rebinds**.
   An untagged child (e.g. `Definition`'s bare `Expression`) sets its own `_v` to a new string and
   bridges `$._ = _v`; the parent's `_v`, aliased at entry, still points at the *old* value. Holder mode
   worked because parent and child literally shared one `$._` cell.

**So the `_v=$._` init is not general — it is exactly a mutate-in-place optimization.** It carried all of
impala.jspeg because that grammar threads meta-slot objects; it cannot carry a string-building grammar.

**This reorders M4.** The collapse must come first, not last:

- **Option A — tag-and-thread `jspeg.jspeg` first.** Tag every untagged value-producing child
  (`e:Expression { $$ = $e }`) so the value arrives through a capture instead of a shared cell, and move
  the threaded context (`$$.vi/.tag/.vr/.vs`) onto those sub-holders. Mechanical but invasive, and it
  churns the rules M4 is about to delete anyway.
- **Option B (recommended) — do the `_val` collapse first.** Replace the per-rule holder with a single
  return register: a rule sets `_val`, the caller reads `_val`, a tagged capture saves it to a local
  right after the call. "Child replaces the value" then works by construction (no aliasing), which
  dissolves blocker 2 *and* blocker 1, and `jspeg.jspeg` migrates without the Option-A churn. It is the
  actual "drop the `$` container" end-state, so the work is not throwaway.

Bar is unchanged: byte-identical `.gazl` (0/101) + NuXJS parity + fuzz. Note the collapse changes the
*shape* of generated `impalaCompiler.js`, which is allowed — only its **output** must be identical.

## M4 design resolved (2026-08-28): seed `_val` at tagged captures — the piece the M1 prototype lacks

`impala/jspeg2.jspeg` (the M1 emission prototype) already has the target shape: param-less rules,
`var _v`, `Primary` emitting `Sub()`, eager `($x=_val,true)` captures, conditional `&&(_val=_v,true)`,
no `._`. **But porting it wholesale would break `impala.jspeg`**, because it was validated only on
`jspegTest.jspeg`, whose rules *create* their values. impala's rules do the opposite: helpers
(`makeMeta`, `binaryOp`, `lookup`, `fieldAccess`, …) **fill a caller-provided slot in place** and their
return value is discarded. Under the prototype's emission a callee starts with `_v === undefined`, so
`makeMeta($$,…)` fills a throwaway slot (`metaSlot(undefined)` → fresh, unstored) and the rule publishes
`undefined`. Today that works only because hardening pre-allocates a slot per capture
(`$x={}` → `createParserContext()` = `{_: newMetaSlot()}`).

**Resolution — seed the register from the capture's own slot, then read it back:**

```
tagged capture x:Sub   →   (_val = $x, Sub() && ($x = _val, true))
rule entry             →   var _v = _val;
rule exit (if assigns) →   && (_val = _v, true)
```

with `$x` still declared as a **pre-allocated fresh slot** (the existing `={}` → hardening path, now
yielding the bare slot rather than a `{_:}` holder). This satisfies every case at once:

- **Fill-in-place callee** (`Value`, `Subscript`, …): inherits the seeded slot via `var _v = _val`, so the
  helpers mutate exactly the object the caller will read back. No grammar change.
- **Value-replacing callee** (scalar rules: `BASE_TYPE`, literals, `$$ = {}` builders): overwrites `_v`,
  publishes `_val = _v`, and `$x = _val` picks up the new value. Fixes the stale-alias bug that blocks
  `jspeg.jspeg` (strings), because the value now travels through the register, not a captured alias.
- **Untagged child** (the shared accumulator): inherits `_val` unseeded, i.e. the parent's own `_v`
  object — the current sharing semantics, preserved.
- **Isolation**: one pre-allocated slot per capture var (unchanged from today, including reuse across
  loop iterations).
- **Passthrough**: unchanged — publish only if the rule mentions `$$`.

**So M4 needs no `impala.jspeg` edits at all** — it is generator surgery plus bootstrap plumbing:

1. `jspeg.jspeg`, value-mode branches only (holder mode untouched, since `jspeg.jspeg` is still `<-`):
   `Definition` (param-less + `_v=_val` init + `_val=_v` publish), `Primary` (`Sub()`), `Tagged` (the
   seed/read-back above, plus the `$$:` form → `(_val=_v,(…)&&(_v=_val,true))`), and the Action rewriter
   (**no `._` append in value mode** — captures hold values directly, which retires the heuristic, the
   `~v` marker and its guard for migrated rules).
2. `root`'s emitted preamble becomes `var _i=0,_im=0,_val,…,_b=root();` / `return [_b,_val,…]` when
   `_vsRules['root']`, keeping `_o` + `_o.options=_hostOptions` for the host.
3. `updateJSPEG.js` hardening: `rootInitPattern` must match the new preamble, and `$x={}` should harden
   to a bare meta slot rather than a `{_:}` holder.

Only after that does `jspeg.jspeg` migrate (blocker 2 above is gone), and only then can the `<-` path,
`.vs` threading and the `_vsRules` prescan be deleted. Bar unchanged: byte-identical `.gazl` + parity +
fuzz. **Not yet implemented** — the bootstrap (a self-hosting generator whose emitted preamble and
hardening must change together) makes this a focused push, not a tail-end edit.
