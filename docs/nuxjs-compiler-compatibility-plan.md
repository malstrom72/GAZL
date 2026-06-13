# NuXJS-compatible Impala compiler plan

This plan tracks the work needed to make the generated Impala compiler runnable in NuXJS without a Node.js runtime. The
target is the generated compiler artefact, not the generator or developer tooling: `updateJSPEG.js`,
`impalaJsCompilerRunner.js`, and build orchestration may remain Node-based.

## Target runtime contract

`impala/jspeg/impalaCompiler.js` should be runnable as plain JavaScript in a NuXJS runtime after the host supplies the
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

- `impala/jspeg/impalaCompiler.js`
- `impala/jspeg/impala.jspeg`
- `impala/jspeg/updateJSPEG.js`

**Tasks**

- [ ] Scan the generated compiler for `require`, `process`, `Buffer`, `globalThis`, `module.exports`, `console`, and
      accessor-style `Object.defineProperty`.
- [ ] Treat `Array.isArray`, `JSON.parse`, `JSON.stringify`, and string indexing as allowed NuXJS APIs.
- [ ] Confirm whether the guarded `module.exports` block parses and executes harmlessly in NuXJS when `module` is
      undefined.
- [ ] Confirm whether NuXJS accepts accessor descriptors. If not, mark `createParserContext` as the first required
      rewrite.

**Validation.** Add or update a test that loads the compiler in a minimal context without Node globals and verifies that
the only failing assumptions are the ones listed in this phase.

## Phase 2 - Rewrite parser context creation for NuXJS

**Goal.** Preserve the `$._` meta-record semantics without relying on getter/setter descriptors.

**Primary files**

- `impala/jspeg/impala.jspeg`
- `impala/jspeg/updateJSPEG.js`
- `impala/jspeg/impalaCompiler.js`

**Tasks**

- [ ] Replace the accessor-descriptor `createParserContext` implementation with a NuXJS-compatible implementation.
- [ ] Preserve lazy/default meta-record behaviour:
  - [ ] missing metadata normalises to a record with `operator`, `type`, and three `operands`
  - [ ] assignments to `$._` still pass through the same normalisation path
  - [ ] operand arrays are still normalised to exactly three slots
- [ ] Regenerate `impalaCompiler.js` from the grammar and hardening script.
- [ ] Keep the helper private to the generated compiler; do not reintroduce `globalThis.createParserContext`.

**Validation.** Existing meta-slot, assignment, and root-context tests in `impala/jspeg/jspegCompilerTests.js` must still
pass after the rewrite.

## Phase 3 - Keep host services explicit

**Goal.** Make the generated compiler usable from Node, NuXJS, or another host through the same explicit options object.

**Tasks**

- [ ] Keep `impalaCompiler(source, options)` as the public entry point.
- [ ] Continue passing these services through `options`:
  - [ ] `output`
  - [ ] `randomId`
  - [ ] `sourceName`
- [ ] Ensure normal compilation does not read ambient `output`, `impalaRandomId`, or parser context globals.
- [ ] Decide whether to keep the guarded CommonJS export in the default artefact or emit a separate NuXJS/plain wrapper.

**Validation.** Add a regression test that compiles through `impalaCompiler(source, options)` in a context with no
ambient host globals.

## Phase 4 - Add a NuXJS smoke harness

**Goal.** Provide a direct compatibility check that runs the generated compiler with NuXJS when an executable is
available.

**Tasks**

- [ ] Add a small smoke script that combines:
  - [ ] `impalaCompiler.js`
  - [ ] a tiny Impala source string
  - [ ] host-provided `output`, `randomId`, and `sourceName`
  - [ ] an assertion/reporting footer
- [ ] Add a wrapper script that locates NuXJS through a simple variable such as `NUXJS`.
- [ ] Make the smoke script skip clearly when NuXJS is unavailable, unless build policy later requires NuXJS.
- [ ] Keep the script portable and runnable from any directory if it becomes user-facing.
- [ ] Add a matching `.cmd` script if the smoke path becomes part of the documented workflow.

**Validation.** The smoke harness should fail if `impalaCompiler.js` requires Node globals or unsupported descriptor
features.

## Phase 5 - Document the workflow

**Goal.** Make the supported runtime split clear.

**Tasks**

- [ ] Update `impala/jspeg/ImpalaJS.md` to state that:
  - [ ] generator and build scripts use Node
  - [ ] the generated compiler is intended to be host-neutral
  - [ ] NuXJS hosts must supply source text and host services explicitly
  - [ ] file I/O is a host responsibility because NuXJS is sandboxed
- [ ] Document the NuXJS smoke command and the `NUXJS` executable variable if added.
- [ ] Document that `Array.isArray` and `JSON.parse` are allowed for NuXJS compatibility.

## Phase 6 - Verification

Run the full verification sequence after implementation:

- [ ] `node impala/jspeg/updateJSPEG.js`
- [ ] NuXJS compatibility smoke test, if `NUXJS` is available
- [ ] `timeout 180 ./build.sh`

## Deliverables checklist

- [ ] NuXJS blocker audit captured in tests or docs.
- [ ] NuXJS-compatible parser context implementation.
- [ ] Generated `impalaCompiler.js` regenerated and checked in.
- [ ] Explicit-host-service regression coverage.
- [ ] Optional NuXJS smoke harness.
- [ ] Documentation explaining the Node tooling / host-neutral compiler split.
