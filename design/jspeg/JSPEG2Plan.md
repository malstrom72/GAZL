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

This is the working decision; the next implementation step is to prototype the `_val`-register emission in the
generator and prove byte-identical output. **Generator-surgery hazard noted:** the action rewriter processes `$`
even inside emitted string literals (that is why the current generated `Definition` carries a stray `var $` from
its `'($){'` literal), so emitting a literal `$$`/`_val=$$` from the generator's own actions needs care —
concatenation tricks or a rewriter tweak. The bootstrap wrinkle to settle there: regenerating `jspegCompiler.js`
with the new emission self-hosts the *self-grammar*, so its downward-context uses must be handled (migrate them
to the `$` context param, or keep the generator's own actions holder-style behind a mode) — a concern isolated
to the generator, not to `impala.jspeg`.

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
