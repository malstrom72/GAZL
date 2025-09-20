# Plan: Restore PikaScript Semantics and Gradually Modernise Helpers

## Context
- `$$parser.` is a compile-time sigil that JSPEG strips while keeping the identifier local; there is no runtime container to instantiate.
- The helpers must remain valid for ES3-era syntax once emitted, but the source `.jspeg` file can keep its historical `var $$parser.foo` pattern.
- We deliberately avoid declaring `var $$parser = {}` so the generated compiler stops emitting the unused container stub.
- Every milestone ends with `node impala/jspeg/jspegCompilerTests.js` and `timeout 180 ./build.sh` to preserve behaviour.

## Milestone 0 — Roll Back the Alias Experiment
- [x] Restore the helper block to use the classic `var $$parser.foo` declarations without a `parser` alias.
- [x] Delete the stray `var $$parser = {};` header assignment so no container leaks into generated output.
- [x] Regenerate `impalaCompiler.js` via `node impala/jspeg/updateJSPEG.js` once the helpers match the original layout.
- [ ] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [ ] ✅ `timeout 180 ./build.sh`

## Milestone 1 — Re-document the Baseline
- [ ] Summarise how the tokenizer handles `$$parser.` stripping and why helpers stay local in the generated compiler.
- [ ] Inventory helper exports (constants, tables, functions) and note which grammar actions rely on them.
- [ ] Capture baseline artefacts with `node impala/jspeg/updateJSPEG.js --check` for future diffs.
- [ ] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [ ] ✅ `timeout 180 ./build.sh`

## Milestone 2 — Make Helpers Look Like ES3 Without Changing Scope
- [ ] Convert obvious constant tables to `var NAME = { ... };` while keeping `var $$parser.NAME = ...;` declarations so JSPEG still strips correctly.
- [ ] Replace ad-hoc helpers like `map()` with explicit literals or loops, documenting any behavioural nuances.
- [ ] Ensure each helper that mutates shared state documents the side effects in comments for easier auditing.
- [ ] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [ ] ✅ `timeout 180 ./build.sh`

## Milestone 3 — Organise Mutable Buckets
- [ ] Group related exports (e.g. `metacode`, `strings`, `switchStack`) under comments and keep mutation helpers nearby.
- [ ] Add small wrapper helpers if grammar actions repeatedly poke the same structure, improving readability without changing APIs.
- [ ] Re-run the generator and regression suite after each grouping change to ensure behaviour matches the baseline.
- [ ] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [ ] ✅ `timeout 180 ./build.sh`

## Milestone 4 — Re-evaluate Module Layout (Optional)
- [ ] Investigate whether removing the outer braces or adopting an IIFE keeps `'use strict';` effective without altering grammar semantics.
- [ ] Consider future packaging modes that could reinterpret `$$parser.` into real container properties once the helpers are fully idiomatic.
- [ ] Only attempt structural changes after all previous milestones stay green across the full build.
- [ ] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [ ] ✅ `timeout 180 ./build.sh`

## Additional Notes
- Track any insights about parser packaging separately so they do not interfere with the baseline cleanup.
- Document regressions immediately if a milestone fails; revert quickly rather than layering more changes.
