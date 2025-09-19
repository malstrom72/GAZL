# Impala JSPEG Refactor Plan — Return‑Style Helpers (No Shim)

This plan migrates the Impala JSPEG grammar and helper API from PikaScript‑style "output by reference" to JS‑style return values, so actions never pass a non‑existing output (`$$`/`$name`) to helpers. The milestones are incremental, each runnable and testable. Check items as you complete them.

## Baseline

- [ ] Verify repo builds and tests are green from a clean state
  - [ ] `node impala/jspeg/updateJSPEG.js --check`
  - [ ] `timeout 180 ./build.sh`
  - [ ] Note current result and commit hash as rollback point

## Milestone 1 — Context guard (holder vs. value) in codegen

Goal: Remove the VM shim without changing helper APIs yet, by ensuring helpers receive holders (not values) from action code.

- [ ] Implement a targeted codegen tweak in `impala/jspeg/jspeg.jspeg` Action rewriter:
  - [ ] Detect when generating arguments to `$$parser.*( … )` calls
  - [ ] In that context, emit holders for `$$`/`$name` (no `._`) when the token is a full argument (followed by `,` or `)`)
  - [ ] Leave all other action uses unchanged (still append `._` for value semantics)
- [ ] Regenerate and test
  - [ ] `node impala/jspeg/updateJSPEG.js` (should pass self‑hosting + regression)
  - [ ] `timeout 180 ./build.sh`
- [ ] Remove the VM shim code from `impala/jspeg/impalaJsCompilerRunner.js`
  - [ ] Keep the runner minimal; no `Object.prototype` mutations or compiler source patching
  - [ ] Re‑run the tests above

Outcome: Shim‑free build where helpers get holders directly; no more fragile `._` pre‑init at call sites.

## Milestone 2 — Introduce return‑style helper wrappers

Goal: Add new helpers that return meta nodes instead of mutating output arguments, while preserving current internal behavior (emits, borrow/release, etc.).

- [ ] Add wrappers alongside existing helpers in `impala/jspeg/impala.jspeg`:
  - [ ] `lookupId(name, isGlobal, s, i) -> meta`
  - [ ] `assignRet(left, right, s, i) -> meta` (mirrors `assign`)
  - [ ] `binaryRet(op, left, right, s, i) -> meta` (mirrors `binaryOp`)
  - [ ] `unaryRet(op, expr, s, i) -> meta` (mirrors `unaryOp`)
  - [ ] `callRet(callee, args, s, i) -> meta` (mirrors FuncCall path)
  - [ ] `switchStart/switchCase/switchEnd` returning context/meta where appropriate
- [ ] Each wrapper should:
  - [ ] Use existing helpers internally to preserve semantics
  - [ ] Return the meta node so actions can write `$$ = …`
- [ ] Add minimal unit coverage in `impala/jspeg/jspegCompilerTests.js` invoking wrappers via a tiny grammar, if feasible
- [ ] Regenerate + run tests
  - [ ] `node impala/jspeg/updateJSPEG.js`
  - [ ] `timeout 180 ./build.sh`

Outcome: Parallel, return‑style API validated without changing grammar actions yet.

## Milestone 3 — Migrate Variable + Assignment rules to returns

Goal: First, visible user‑facing simplification with small blast radius.

- [ ] Update Variable rule to use `$$ = $$parser.lookupId(…)`
- [ ] Update `Expr '=' r:Expr` action to use `$$ = $$parser.assignRet($$, $r, $$s, $$i)`
- [ ] Remove any local pre‑inits introduced to work around `._`
- [ ] Regenerate + run tests
  - [ ] `node impala/jspeg/updateJSPEG.js`
  - [ ] `timeout 180 ./build.sh`

Outcome: Basic assignments work purely by return values; no mutation of an output arg.

## Milestone 4 — Migrate Binary and Unary ops

- [ ] Replace `binaryOp` calls with `binaryRet` assigned to `$$`
- [ ] Replace `unaryOp` calls with `unaryRet` assigned to `$$`
- [ ] Regenerate + test as above

Outcome: Expressions become consistently return‑assigned.

## Milestone 5 — Migrate FuncCall

- [ ] Replace in‑rule borrow/call/release logic with `$$ = $$parser.callRet($$, args, $$s, $$i)`
- [ ] Ensure wrappers preserve temp borrowing and release order internally
- [ ] Regenerate + test

Outcome: Function calls simplified; side effects encapsulated.

## Milestone 6 — Migrate Switch/Case

- [ ] Use `switchStart/switchCase/switchEnd` helpers to encapsulate switch scaffolding
- [ ] Keep comments/labels identical (parity with fixtures)
- [ ] Regenerate + test

Outcome: Control‑flow helpers become return‑ or context‑based; clearer actions.

## Milestone 7 — Remove legacy patterns and cleanup

- [ ] Remove VM shim code paths if any still linger (runner stays minimal)
- [ ] Remove Impala‑side `._` pre‑init workarounds introduced earlier
- [ ] Drop unused mutating helpers after all sites migrated
- [ ] Regenerate + test

Outcome: A clean, shim‑free design, helpers return results, actions assign values.

## Milestone 8 — Docs and examples

- [ ] Update `impala/jspeg/JSPEG.md` to describe holder/value model and new return‑style helpers
- [ ] Update `impala/jspeg/ImpalaJS.md` with examples using the new API
- [ ] Add a small example grammar in `impala/jspeg/testdata` demonstrating assignment/binary/call with return helpers

## Rollback/Guardrails

- Keep commits small per milestone and ensure `updateJSPEG.js` + full build stay green after each step.
- If a regression appears, revert the last step and add a targeted test before retrying.

