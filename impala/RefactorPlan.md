# Impala JSPEG Return-Style Helper Plan

This plan tracks a cleanup of the Impala JSPEG grammar actions from
holder-mutating helpers to return-style helpers.

Current action style:

```js
$$parser.binaryOp($$, "+", $left, $right, $$s, $$i);
```

Target action style:

```js
$$ = $$parser.binaryRet("+", $left, $right, $$s, $$i);
```

The goal is readability and fewer holder/value mistakes, not different compiler
output. Every completed step must keep generated `.gazl` output equivalent and
must keep the compiler runnable in NuXJS.

## Guardrails

- Keep each milestone small enough to review as one behavior-preserving change.
- Regenerate generated files with `node impala/updateJSPEG.js` after grammar
  edits.
- Run `timeout 180 ./build.sh` before considering a milestone complete.
- Keep NuXJS compatibility explicit. Avoid new JavaScript features unless NuXJS
  is known to support them.
- Do not remove existing mutating helpers until all call sites are migrated and
  tests prove they are unused.

## Milestone 1 - Audit Current Helper Shapes

Goal: document the exact helper calls before changing behavior.

- List every `$$parser.*` helper in `impala/impala.jspeg` that writes into `$$`,
  `$name`, or another holder argument.
- Classify each helper by risk:
  - simple expression metadata (`lookup`, unary, binary)
  - assignment and copies
  - function calls and temporary ownership
  - control-flow scaffolding such as switch/case
- Confirm there is no remaining runner-side shim in `impala/impalaJsCompilerRunner.js`
  that this refactor is expected to remove.
- Run:
  - `node impala/updateJSPEG.js --check`
  - `timeout 180 ./build.sh`

Exit criteria: a short checked-in audit note or comments in this plan identifying
the migration order and any helpers that should not be converted.

## Milestone 2 - Add Return-Style Wrappers

Goal: add return-style APIs beside the existing mutating APIs without changing
grammar actions.

- Add wrappers in `impala/impala.jspeg` for low-risk expression helpers first:
  - `lookupRet(...)`
  - `unaryRet(...)`
  - `binaryRet(...)`
  - `assignRet(...)`
- Each wrapper should call the existing implementation internally, then return
  the produced meta record.
- Add targeted coverage in `impala/jspegCompilerTests.js` if a wrapper can be
  exercised without broad fixture churn.
- Regenerate and run:
  - `node impala/updateJSPEG.js`
  - `timeout 180 ./build.sh`

Exit criteria: generated compiler changes only by adding wrapper support, and
all existing fixtures remain unchanged.

## Milestone 3 - Migrate Variables And Simple Expressions

Goal: convert the lowest-risk grammar actions to assignment from return values.

- Migrate variable lookup actions to `$$ = lookupRet(...)`.
- Migrate unary and binary expression actions to `$$ = unaryRet(...)` and
  `$$ = binaryRet(...)`.
- Keep output text byte-for-byte equivalent after regeneration.
- Run:
  - `node impala/updateJSPEG.js`
  - `node impala/jspegCompilerTests.js`
  - `timeout 180 ./build.sh`

Exit criteria: expression fixtures remain unchanged and NuXJS smoke still
passes through the full build.

## Milestone 4 - Migrate Assignment And Copy-Like Operations

Goal: convert actions that produce assignment metadata while preserving existing
diagnostics and source locations.

- Migrate assignment actions to `$$ = assignRet(...)`.
- Review copy/borrow/release helpers before converting; only migrate them if the
  returned value model is clearer than the current mutation.
- Preserve type-error messages and source offsets.
- Run:
  - `node impala/updateJSPEG.js`
  - `node impala/jspegCompilerTests.js`
  - `timeout 180 ./build.sh`

Exit criteria: assignment and type-checking tests pass unchanged.

## Milestone 5 - Evaluate Function Calls

Goal: decide whether function-call lowering benefits from return-style helpers.

- Audit `FuncCall` temporary borrowing, call emission, return value handling, and
  release order.
- If a wrapper improves clarity, add `callRet(...)` and migrate one call path.
- If the current structure is clearer because of side effects, document that and
  leave it mutating.
- Run:
  - `node impala/updateJSPEG.js`
  - `node impala/jspegCompilerTests.js`
  - `timeout 180 ./build.sh`

Exit criteria: either function calls use a proven return-style wrapper, or the
plan records why this area should stay mutation-based.

## Milestone 6 - Evaluate Switch And Control Flow

Goal: avoid inventing abstractions unless they make control-flow actions simpler.

- Review switch/case helpers and branch processing.
- Add return/context helpers only if they reduce real action complexity.
- Preserve labels, comments, branch ordering, and generated output.
- Run:
  - `node impala/updateJSPEG.js`
  - `node impala/jspegCompilerTests.js`
  - `timeout 180 ./build.sh`

Exit criteria: control-flow fixtures remain unchanged, or the plan documents why
no migration was made.

## Milestone 7 - Remove Proven-Dead Mutating Helpers

Goal: clean up only after call sites have moved.

- Search generated and source grammar code for each old mutating helper.
- Remove helpers only when no source or generated call sites remain.
- Regenerate and run:
  - `node impala/updateJSPEG.js`
  - `node impala/updateJSPEG.js --check`
  - `timeout 180 ./build.sh`

Exit criteria: no unused return-migration scaffolding remains, and the generated
compiler is still current.

## Milestone 8 - Update Documentation

Goal: make the final holder/value model clear to future grammar authors.

- Update `impala/JSPEG.md` with:
  - when `$$` means the value
  - when `$$.` reaches the holder
  - when helpers should return values versus mutate holders
- Update `impala/ImpalaJS.md` only if user-facing compiler behavior changed.

Exit criteria: docs describe the final action style without mentioning obsolete
PikaScript files or the removed `impala/jspeg/` layout.
