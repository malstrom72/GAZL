# Plan: Make `impala.jspeg` Helpers Feel Like Real JavaScript (While Keeping `$$parser`)

## Context and Constraints
- Grammar actions must continue to target ES3, so no ES5+ constructs (`let`, `const`, arrow functions, etc.).
- JSPEG strips the `$$` sigil when emitting the generated compiler. The helpers therefore act as a namespace shim: `$$parser.emit` becomes `emit` in `impalaCompiler.js`.
- We deliberately keep the `$$parser` container for now; the plan focuses on making the code inside the namespace look and behave like idiomatic JavaScript instead of PikaScript.
- All changes must preserve the behaviour validated by `node impala/jspeg/jspegCompilerTests.js` and the repo-wide `timeout 180 ./build.sh` regression run.

## Milestones

### Milestone 0 — Rebuild Understanding Before Touching Code
- [x] Re-read `JSPEG.md`, `impala/jspeg/jspeg.jspeg`, and `impala/jspeg/jspegCompiler.js` to refresh how header code is tokenised, how `var $$parser.foo` is rewritten, and why the scope hack works.
- [x] Document the current `$$parser` handling (where the sigil is stripped, where it is kept) in a short note within this plan so future steps keep the behaviour intact.
- [x] Diff the last known-good `impala.jspeg` against the current tree to identify any accidental regressions (e.g. removed `var` prefixes) that must be reverted before proceeding.
- [x] Run `node impala/jspeg/jspegCompilerTests.js` and `timeout 180 ./build.sh` to reconfirm the untouched baseline after the review.

> **Milestone 0 findings**
> - The compiler front-end strips only the `$$parser.` prefix via `$b.replace(/\x24\x24parser\./g, '')`, so a bare `var $$parser = {};` declaration survives intact in generated output while the helpers become ordinary locals.
> - Action blocks apply the same prefix-stripping logic: when the tokenizer encounters `$$parser.`, it drops the sigil but leaves other `$$` shorthands intact (`$$s` → `_s`, etc.), ensuring helper calls stay local while grammar intrinsics map to runtime variables.
> - Comparing `impala/jspeg/impala.jspeg` at `489b2fb` with HEAD shows the newer tree converts each `var $$parser.name = …;` pseudo-declaration into `var name = …; $$parser.name = name;`, keeping helper scope local and leaving only `map()`-driven initialisation as the remaining PikaScript artefact.

### Milestone 1 — Baseline Snapshot (Post-Review)
- [x] Capture the generated baseline by running `node impala/jspeg/updateJSPEG.js --check` and saving the resulting `output/impalaCompiler.js` snapshot for diffs.
- [x] Inventory which helpers (`emitMeta`, `declareLocal`, tables, etc.) are called from grammar actions so we understand downstream impact before refactoring.
    - Grammar actions hit 55 helpers/exports today: `CASTS_TO_TYPES`, `IMPALA_VERSION`, `META_TO_GAZL`, `SUPPORTED_OPS`, `TYPE_SUFFIXES`, `UNARY_OPS`, `VERBOSE_TYPES`, `ZEROES`, `absFloor`, `assign`, `binaryOp`, `borrow`, `borrowForCall`, `counters`, `debugPrintMeta`, `declare`, `dereference`, `dropHash`, `dry`, `dumpString`, `emit`, `emitMeta`, `end`, `fail`, `floatToIntConvert`, `flushMetaCode`, `intToFloatConvert`, `labelCounter`, `lookup`, `makeArgValue`, `makeConstant`, `makeMeta`, `makeRValue`, `makeString`, `metacode`, `minus`, `mulDivOp`, `newLabel`, `noForward`, `not`, `printable`, `processBranches`, `randomId`, `reference`, `releaseMeta`, `returnBack`, `start`, `stock`, `strings`, `subConstInt`, `switchStack`, `symbols`, `typeError`, `unaryOp`, `validateStock`.
- [x] Annotate which helpers rely on `var $$parser.*` for function scoping so we avoid breaking encapsulation in later steps.
    - Every export is either a hoisted function declaration or a `var`-declared constant that is immediately mirrored onto `$$parser`, so the generated compiler still sees plain locals after the sigil is stripped. Any future restructuring must retain a local declaration before the alias write.
- [x] Run `node impala/jspeg/jspegCompilerTests.js` and `timeout 180 ./build.sh` to confirm the starting state stays green.

> **Milestone 1 findings**
> - `node impala/jspeg/updateJSPEG.js --check` captured the clean baseline and reconfirmed that the recorded compiler artefacts match the grammar.
> - Both the standalone JSPEG regression runner and the full `./build.sh` pipeline still pass with no diffs, giving us a solid before-state for subsequent refactors.

### Milestone 2 — Make Helpers Valid ES3 Without Losing Local Scope
- [x] Lock in the replacement pattern—real locals declared with `var name = …;` immediately mirrored via `$$parser.name = name;`—and document why JSPEG still strips the sigil while keeping the locals scoped.
- [x] Prototype the change in a scratch branch or gist, confirming that `var $$parser.foo` still compiles into local declarations in `impalaCompiler.js` and nothing leaks globally.
- [x] Update the helper section to use the new pattern one group at a time (constants, lookup tables, functions) with explicit notes on why each export remains local.
- [x] Prefer dot notation for export writes and reserve bracket notation for keys that are not valid identifiers.
- [x] Re-run `node impala/jspeg/updateJSPEG.js --check`, `node impala/jspeg/jspegCompilerTests.js`, and `timeout 180 ./build.sh` to finish the milestone.

> **Milestone 2 notes**
> - The JSPEG compiler continues to apply `$b.replace(/\x24\x24parser\./g, '')`, so the `var name; $$parser.name = name;` alias pattern still compiles down to ordinary locals in `impalaCompiler.js` while leaving the `var $$parser = {};` declaration intact for future container-based packaging.
> - Helper groups will keep this local-first rule; any export that must stay container-scoped will be documented alongside the alias so we can audit scope expectations during later refactors.
> - Regenerating `impalaCompiler.js` after the change confirmed that the emitted helpers remain plain locals with identical bodies, proving the alias pattern survives the prefix-stripping pass unchanged.

### Milestone 3 — Replace Lookup Tables and Clarify Shared State
- [x] Rewrite every `map()`-built lookup table (`META_TO_GAZL`, `SUPPORTED_OPS`, `CASTS_TO_TYPES`, `ZEROES`, `TYPE_SUFFIXES`, `VERBOSE_TYPES`, handler arrays) as explicit object or array literals without trailing commas.
- [x] Remove the `map()` helper once literals cover all callers, updating any code that relied on it mutating existing objects.
- [x] Name shared mutable state (`metacode`, `strings`, `switchStack`, counters) explicitly and provide helper accessors or mutators instead of direct field pokes.
- [x] Adjust grammar actions to call the new helper surface so state changes happen through a single pathway, documenting side effects inline where necessary.
- [ ] Re-run `node impala/jspeg/updateJSPEG.js --check`, `node impala/jspeg/jspegCompilerTests.js`, and `timeout 180 ./build.sh` before advancing.

> **Milestone 3 notes**
> - Literal tables now live in plain `var` declarations, so the helper header is valid ES3 without relying on the custom `map()` helper.
> - The standalone `map()` utility has been deleted because every table is initialised declaratively; no other helpers depended on its mutating behaviour.
> - Shared state now funnels through helpers such as `appendMetacode`, `resetStock`, `resetCounters`, `resetStrings`, and the switch-stack push/pop pair so grammar actions no longer reach into the buckets directly.

### Milestone 4 — Adopt an ES3-friendly Module Layout and Finalise
- [ ] Remove the leading/trailing bare block so the helper section becomes standard top-level JavaScript *only after* the new scoping approach is proven safe.
- [ ] Evaluate a lightweight IIFE versus keeping the existing wrapper, ensuring whichever option we choose still lets JSPEG drop the sigil while preserving the namespace and local scope of helpers.
- [ ] Place `'use strict';` inside a reachable scope (IIFE body or top-level) without leaking it globally, and verify no ES5-only constructs are introduced.
- [ ] Confirm all helpers continue exporting through `$$parser`, no implicit globals remain after the refactor, and the generated compiler matches the baseline except for intentional stylistic changes.
- [ ] Re-run `node impala/jspeg/updateJSPEG.js --check`, `node impala/jspeg/jspegCompilerTests.js`, and `timeout 180 ./build.sh` as the closing checks.

## Additional Insights and Opportunities
- [ ] Introduce sub-objects such as `$$parser.symbols` or `$$parser.codegen` to clarify helper responsibility once the base refactor lands.
- [ ] Document helper side effects inline (e.g. mutations of shared arrays) to guide future contributors.
- [ ] Explore richer parser packaging (like exposing individual rule entry points) after the helpers read like idiomatic JavaScript.

## Deliverables
- [ ] Updated `impala.jspeg` helper section that reads as idiomatic ES3 JavaScript while still exporting through `$$parser`.
- [ ] Documentation adjustments (e.g. `ImpalaJS.md`) describing the helper layout changes, if necessary.
- [ ] Verified, unchanged behaviour in `impalaCompiler.js`, supported by the JSPEG regression suite and the repository build.
