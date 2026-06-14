# NuXJS-compatible Impala compiler plan

This plan tracks the work needed to make the generated Impala compiler runnable in NuXJS without a Node.js runtime. The
target is the generated compiler artefact, not the generator or developer tooling: `updateJSPEG.js`,
`impalaJsCompilerRunner.js`, and build orchestration may remain Node-based.

## Target runtime contract

`impala/impalaCompiler.js` should be runnable as plain JavaScript in a NuXJS runtime after the host supplies the
source text and host services explicitly. Normal compilation must not depend on `require`, `process`, `Buffer`,
`globalThis`, ambient `output`, or ambient `impalaRandomId`.

NuXJS provides the ES3 standard library plus focused ES5 additions we can rely on for this target:

- `JSON.parse` and `JSON.stringify`
- `Array.isArray`
- string indexing
- `Object.defineProperty` with NuXJS descriptor semantics

The likely compatibility risk is not the presence of these APIs, but whether the generated compiler relies on descriptor
features that NuXJS does not implement, especially getter/setter descriptors in `createParserContext`.

## Phase 1 - Audit actual NuXJS blockers

**Goal.** Classify every non-trivial host/runtime assumption in `impalaCompiler.js` against NuXJS instead of generic ES3.

**Primary files**

- `impala/impalaCompiler.js`
- `impala/impala.jspeg`
- `impala/updateJSPEG.js`

**Tasks**

- [x] Scan the generated compiler for `require`, `process`, `Buffer`, `globalThis`, `module.exports`, `console`, and
      accessor-style `Object.defineProperty`.
- [x] Treat `Array.isArray`, `JSON.parse`, `JSON.stringify`, and string indexing as allowed NuXJS APIs.
- [x] Confirm whether the guarded `module.exports` block parses and executes harmlessly in NuXJS when `module` is
      undefined.
- [x] Remove the accessor descriptor dependency from `createParserContext`.

**Validation.** Add or update a test that loads the compiler in a minimal context without Node globals and verifies that
the only failing assumptions are the ones listed in this phase.

## Phase 2 - Rewrite parser context creation for NuXJS

**Goal.** Preserve the `$._` meta-record semantics without relying on getter/setter descriptors.

**Primary files**

- `impala/impala.jspeg`
- `impala/updateJSPEG.js`
- `impala/impalaCompiler.js`

**Tasks**

- [x] Replace the accessor-descriptor `createParserContext` implementation with a NuXJS-compatible implementation.
- [x] Preserve lazy/default meta-record behaviour:
  - [x] missing metadata normalises to a record with `operator`, `type`, and three `operands`
  - [x] assignments to `$._` still pass through the same normalisation path
  - [x] operand arrays are still normalised to exactly three slots
- [x] Regenerate `impalaCompiler.js` from the grammar and hardening script.
- [x] Keep the helper private to the generated compiler; do not reintroduce `globalThis.createParserContext`.

**Validation.** Existing meta-slot, assignment, and root-context tests in `impalaCompilerTests.js` must still
pass after the rewrite.

## Phase 3 - Keep host services explicit

**Goal.** Make the generated compiler usable from Node, NuXJS, or another host through the same explicit options object.

**Tasks**

- [x] Keep `impalaCompiler(source, options)` as the public entry point.
- [x] Continue passing these services through `options`:
  - [x] `output`
  - [x] `randomId`
  - [x] `sourceName`
- [x] Ensure normal compilation does not read ambient `output`, `impalaRandomId`, or parser context globals.
- [x] Keep the guarded CommonJS export in the default artefact because it is harmless when `module` is undefined.
- [x] Replace the generated `KEYWORD` alternation with an equivalent loop so NuXJS can compile the generated compiler
      without hitting its internal complexity limit.

**Validation.** Add a regression test that compiles through `impalaCompiler(source, options)` in a context with no
ambient host globals.

## Phase 4 - Add a NuXJS smoke harness

**Goal.** Provide a direct compatibility check that runs the generated compiler with NuXJS when an executable is
available.

**Tasks**

- [x] Add a small smoke script that combines:
  - [x] `impalaCompiler.js`
  - [x] a tiny Impala source string
  - [x] host-provided `output`, `randomId`, and `sourceName`
  - [x] an assertion/reporting footer
- [x] Add a wrapper script that locates NuXJS through a simple variable such as `NUXJS`.
- [x] Make the smoke script skip clearly when NuXJS is unavailable, unless build policy later requires NuXJS.
- [x] Keep the script portable and runnable from any directory if it becomes user-facing.
- [x] Add a matching `.cmd` script if the smoke path becomes part of the documented workflow.

**Validation.** The smoke harness should fail if `impalaCompiler.js` requires Node globals or unsupported descriptor
features.

## Phase 5 - Document the workflow

**Goal.** Make the supported runtime split clear.

**Tasks**

- [x] Update `impala/ImpalaJS.md` to state that:
  - [x] generator and build scripts use Node
  - [x] the generated compiler is intended to be host-neutral
  - [x] NuXJS hosts must supply source text and host services explicitly
  - [x] file I/O is a host responsibility because NuXJS is sandboxed
- [x] Document the NuXJS smoke command and the `NUXJS` executable variable if added.
- [x] Document that `Array.isArray` and `JSON.parse` are allowed for NuXJS compatibility.

## Phase 6 - Verification

Run the full verification sequence after implementation:

- [x] `node impala/updateJSPEG.js`
- [x] NuXJS compatibility smoke test, if `NUXJS` is available. The wrapper was run and skipped cleanly because no NuXJS
      executable is configured in this checkout.
- [x] `timeout 180 ./build.sh`

## Deliverables checklist

- [x] NuXJS blocker audit captured in tests or docs.
- [x] NuXJS-compatible parser context implementation.
- [x] Generated `impalaCompiler.js` regenerated and checked in.
- [x] Explicit-host-service regression coverage.
- [x] Optional NuXJS smoke harness.
- [x] NuXJS command-line compiler script using global `arguments`; `arguments[0]` supplies the script path used to locate
      `impalaCompiler.js` in the same directory.
- [x] Documentation explaining the Node tooling / host-neutral compiler split.
