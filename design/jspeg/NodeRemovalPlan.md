# Node Removal Plan

Goal: remove Node as a required dependency for the GAZL/Impala build,
validation, regeneration, and regression workflows. The deployed Impala compiler
and `gazl-validate` already run through NuXJS; this plan finishes the remaining
developer/test paths.

## 1. Define the target

- `bash build.sh` and `build.cmd` run without `node`.
- Impala compile, GAZL validation, fixture regeneration, JSPEG checks, and unit
  tests run through `output/NuXJS`.
- The only JavaScript runtime required by the repository is NuXJS.
- Any remaining `module.exports` guards in generated compiler files are harmless
  compatibility output, not an active Node dependency.

## 2. Inventory NuXJS utilities

- Pull or inspect current upstream NuXJS.
- Review `tools/systools.js` and decide which helpers to vendor or stage.
- Confirm the exact available runtime primitives:
  - `read`
  - `write`
  - `load`
  - `system`
  - `getenv`
  - `arguments`
- Decide whether fixture lists can be explicit or whether `systools.js` provides
  enough filesystem support to replace directory scans.

## 3. Port JSPEG regeneration

- Convert `impala/updateJSPEG.js` to a NuXJS script.
- Replace Node APIs:
  - `fs.readFileSync` -> `read`
  - `fs.writeFileSync` -> `write`
  - `path` helpers -> local path helpers or `systools.js`
  - `child_process.spawnSync` -> `system`
  - `Module` loading -> `load` or controlled eval-style loading
- Keep regenerated `jspegCompiler.js` and `impalaCompiler.js` byte-for-byte
  identical.
- Preserve `--check` behavior.

## 4. Port JSPEG regression tests

- Convert `impala/jspegCompilerTests.js` to NuXJS.
- Replace Node-only dependencies:
  - `require`
  - `vm`
  - `child_process`
  - `fs`
  - `path`
- Use `load()` and scoped/global test harness helpers instead of Node modules.
- Keep coverage for:
  - JSPEG self-hosting
  - generated Impala compiler parity
  - NuXJS command wrapper behavior
  - validator fixture checks
  - random-id behavior
  - compiler error hardening

## 5. Port golden fixture runner

- Convert `impala/runJspegTests.js` to NuXJS.
- Replace directory scanning with either:
  - explicit fixture manifest, or
  - filesystem helpers from `systools.js` if suitable.
- Replace Node file I/O with `read` and `write`.
- Preserve normal compare mode and `--makegold`.

## 6. Remove Node Impala CLI

- Delete `impala/impala.node.js` after fixture regeneration no longer uses it.
- Make `impala/impala.nuxjs.js` the only command-line Impala compiler.
- Update `tools/regen-jspeg-fixtures.sh` and `.cmd` to invoke:
  - `output/NuXJS impala/impala.nuxjs.js ...`

## 7. Update build scripts

- Build NuXJS before any JavaScript-based tooling runs.
- Replace these build calls:
  - `node impala/jspegCompilerTests.js`
  - `node impala/runJspegTests.js`
  - any remaining Node validator/test invocation
- Use NuXJS scripts for all JavaScript execution.

## 8. Clean documentation

- Remove Node CLI instructions from:
  - `impala/ImpalaJS.md`
  - `impala/JSPEG.md`
  - related plan docs
- Document NuXJS-only workflows for:
  - compile
  - validate
  - regenerate compilers
  - regenerate fixtures
  - run regression tests

## 9. Delete Node-only files

Delete or replace these after equivalent NuXJS paths are passing:

- `impala/impala.node.js`
- `impala/impalaJsCompilerRunner.js`
- `tests/gazl-validator-tests.js` if folded into NuXJS tests

Keep generated compiler `module.exports` guards only if they remain harmless in
NuXJS and useful for external consumers.

## 10. Verify

Run:

- `bash tools/BuildNuXJS.sh`
- NuXJS JSPEG regeneration check
- NuXJS JSPEG regression suite
- NuXJS golden fixture tests
- `bash tools/gazl-validate.sh ...`
- `timeout 180 ./build.sh`

Then scan for remaining Node dependencies:

```bash
rg -n "node|require\\(|process\\.|fs\\.|path\\.|child_process|module\\.exports" .
```

Classify every hit as either removed, replaced, or intentionally harmless
generated compatibility text.
