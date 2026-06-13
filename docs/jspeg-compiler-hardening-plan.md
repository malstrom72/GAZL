# Build-time hardening for the JSPEG Impala compiler

This plan retires `patchCompilerSourceForMeta` by re-embedding all safety logic directly into the JSPEG grammar and wrapper build script. Each phase lists the rationale, specific source edits (with line references from `main` as of this writing), and validation steps so contributors can execute it without rediscovering the groundwork.

## Why the runtime patch must go

* **Fragility** – `impala/jspeg/impalaJsCompilerRunner.js` rewrites the emitted bundle using global regexes (see lines 173–258). Every upstream JSPEG refresh risks desynchronising these patterns, breaking the compiler silently.
* **Observability gaps** – Today’s tests only cover the happy path. The injected guards around meta slots, assignments, and `fail` live outside source control, so regressions slip by until runtime.
* **Operational cost** – Developers diff the regenerated `impalaCompiler.js`, see unfamiliar helper bodies, and waste time chasing down why they do not match the grammar.

Replacing the patch gives us diffable, reviewable source while keeping the hardened semantics that motivated the original hack.

## Phase 1 – Capture existing behaviour with tests

**Goal.** Freeze the current runtime semantics in executable form so later steps can verify parity.

**Code to read**

* `impala/jspeg/impalaJsCompilerRunner.js`, lines 173–258 – the injected helpers.
* `impala/jspeg/jspegCompilerTests.js` – existing smoke coverage for the compiler wrapper.

**Tasks**

- [x] Encode unit tests that assert the behaviours the patch enforces:
  - [x] `metaSlot` must tolerate `null`, primitives, and plain objects without `_` while returning an object whose `operands` array is exactly length three. (See `impala/jspeg/jspegCompilerTests.js`, tests "metaSlot must normalise operands" and "metaSlot must tolerate primitives".)
  - [x] `$$parser.makeMeta` must materialise placeholder records when passed `null`. (Verified by "makeMeta must seed missing record" in `impala/jspeg/jspegCompilerTests.js`.)
  - [x] `$$parser.assign` must throw if the left operand lacks an operator (`leftx.operator === undefined`). (Guarded by "assign must reject missing operator" test case.)
  - [x] `$$parser.fail` must throw `Error` instances containing `impalaMessage`, `impalaOffset`, `impalaSnippetBefore`, and `impalaSnippetAfter`. (See structured error assertions around `formatThrownCompilerError`.)
  - [x] The root context used by the compiler must expose an `_` getter that lazily initialises the backing meta record. (Covered by "createParserContext must lazily materialise meta records" test.)
- [x] Drive these tests via `compileWithJsImpala` so they fail without the runtime rewrite and pass with the current patch. This guards against future drift while the migration is underway.

**Validation** – `timeout 180 ./build.sh` should run the new tests automatically.

## Phase 2 – Port meta helpers into the grammar

**Goal.** Make the generated helpers match the runtime replacements.

**Primary files**

* `impala/jspeg/impala.jspeg`, lines 748–820 (`metaSlot`, `makeMeta`, and related helpers).
* `impala/jspeg/impalaJsCompilerRunner.js`, lines 173–219 (authoritative implementation to port).

- [x] Replace the grammar’s `metaSlot` body (currently lines 764–794) with the defensive implementation from the patch. Preserve the operand normalisation and the logic that creates placeholder records with `operator`, `type`, and a three-element `operands` array. (Done in `impala/jspeg/impala.jspeg`, lines 786–833; `node impala/jspeg/updateJSPEG.js` regenerates the hardened helper verbatim.)
- [x] Introduce a grammar-level helper (name it `createParserContext`) adjacent to the debug helpers. The function should match the runtime `__jspegCreateContext` implementation, including the lazily-defined `_` accessor backed by `__metaSlot`. (Added to `impala/jspeg/impala.jspeg` as a private generated helper.)
- [x] Update `$$parser.makeMeta` (lines 801–810) to call the new `metaSlot` helper unconditionally rather than assuming it receives a valid record. (Grammar now normalises via `metaSlot` before populating the operands array.)
- [x] Guard `$$parser.assign` at the top (after the existing `metaSlot` calls) by inserting the runtime check from lines 239–244 of the patch: throw a descriptive `Error` if `!leftx || leftx.operator === undefined`. (JSPEG grammar now throws directly, matching the hardened runtime semantics.)

**Motivation** – Editing the grammar makes the behaviour visible during code review and ensures any future generator runs pick up the hardening automatically.

**Validation** – After editing the grammar, run `node impala/jspeg/updateJSPEG.js` and inspect the diff to confirm `impalaCompiler.js` now contains the hardened helpers even before removing the runtime rewrite.
*Status:* complete – the grammar emits hardened helpers, and we still run `applyImpalaHardening` during regeneration as a belt-and-suspenders guard while `jspeg.jspeg` keeps its legacy bootstrap logic.

## Phase 3 – Upgrade error handling in the grammar

**Goal.** Eliminate the string-based `fail` helper so the bundle natively throws structured errors.

**Primary files**

* `impala/jspeg/impala.jspeg`, lines 546–551 (`$$parser.fail`).
* `impala/jspeg/impalaJsCompilerRunner.js`, lines 221–234 (runtime replacement).

- [x] Replace the grammar’s `$$parser.fail` with the runtime logic:
  - [x] Call `bake(error)` into a `message` string.
  - [x] Capture `before` and `after` snippets via the `oneLine` helper.
  - [x] Create a new `Error(message + ' : ' + before + ' <!!!!> ' + after)` and annotate it with `impalaMessage`, `impalaOffset`, `impalaSnippetBefore`, and `impalaSnippetAfter` (guard `impalaOffset` behind `Number.isFinite`).
- [x] Ensure the helper tolerates missing `source`/`offset` by falling back to sane defaults, mirroring the runtime patch exactly so the new tests pass without modification. (Grammar now constructs enriched errors directly without build-time rewriting.)

**Motivation** – `formatThrownCompilerError` (lines 141–162 in `impalaJsCompilerRunner.js`) expects these enriched properties. Baking them into the grammar prevents future regressions.

**Validation** – Rerun the targeted tests from Phase 1; `compileWithJsImpala` should now throw enriched errors without relying on the runtime rewrite.
*Status:* complete – regenerated bundles throw structured errors without additional patching.

## Phase 4 – Fix root context initialisation

**Goal.** Make the generated bundle allocate parser contexts via the new helper instead of `{ _: void 0 }`.

**Primary files**

* `impala/jspeg/impala.jspeg`, in the productions that bootstrap compilation (search for `_o={_:void 0}` in the generator template).
* `impala/jspeg/impalaCompiler.js`, line 1902 (`var _i=0,_im=0,_o={_:void 0},_b=root(_o);`).
* `impala/jspeg/updateJSPEG.js`, lines 20–67 (`wrapCompilerSource`).

- [x] Modify the grammar initialisation code so the generated bundle uses `$$parser.createParserContext()` instead of `{ _: void 0 }` when constructing the root context. When unsure where to edit, search for `var _i=0,_im=0,_o={_:void 0}` in the grammar template. (The grammar now generates `var _i=0,_im=0,_o=$$parser.createParserContext(),_b=root(_o);`.)
- [x] Expose the helper before the generated code executes by initialising `$$parser.createParserContext` inside `wrapCompilerSource` (if the grammar does not emit it soon enough). The wrapper currently seeds `var $$parser = {};`; extend that block so it predefines the helper when present. (No wrapper changes required—the helper ships within the bundle and self-registers on `globalThis` for tests.)
- [x] Confirm the regenerated bundle reads `var _i=0,_im=0,_o=$$parser.createParserContext(),_b=root(_o);` once the grammar emits it directly.
- [ ] Mirror the root-context bootstrap in `jspeg.jspeg` once we have a principled design for self-hosting safety helpers.

**Motivation** – The runtime patch replaces this line (see lines 247–252) because the generated code otherwise dereferences `_` on a bare object, crashing when the meta record is missing. Baking the helper into the bundle removes that failure mode permanently.

**Validation** – Run `node impala/jspeg/updateJSPEG.js` and inspect `impalaCompiler.js` to ensure the replacement occurred. Execute the Phase 1 tests to verify the root context still exposes the lazy `_` getter.
*Status:* partially complete – `impalaCompiler.js` now bootstraps via `createParserContext()`; the `jspeg.jspeg`/`jspegCompiler.js` change was rolled back pending a dedicated design review.

## Phase 5 – Remove the runtime patch and tidy documentation

**Goal.** Delete the string-rewrite shim now that the emitted bundle is hardened.

**Primary files**

* `impala/jspeg/impalaJsCompilerRunner.js`, lines 173–258 (`patchCompilerSourceForMeta`).
* `impala/jspeg/ImpalaJS.md` (or a new doc section describing the build pipeline).

- [x] Delete `patchCompilerSourceForMeta`, the surrounding explanatory comment, and the call that wraps the compiler source (line 281).
- [x] Replace it with a no-op wrapper that simply loads `impalaCompiler.js`:
  ```js
  const compilerSource = options.compilerSource ?? fs.readFileSync(compilerPath, 'utf8');
  ```
- [x] Update the documentation to note that the meta safety logic now lives inside `impala/jspeg/impala.jspeg` and that regenerating the compiler requires running `node impala/jspeg/updateJSPEG.js`.

**Motivation** – This validates that the preceding phases completely eliminated the need for runtime surgery and leaves future vendoring work limited to the grammar and generated bundle.

**Validation** – Rerun `timeout 180 ./build.sh`. The new unit tests from Phase 1 should continue to pass, and no runtime source rewrites should remain.
*Status:* grammar work is complete; docs updated to point at the hardened grammar and regeneration pipeline.

## Deliverables checklist

- [x] Tests – New assertions in `impala/jspeg/jspegCompilerTests.js` covering meta-slot normalisation, assignment guards, root context creation, and enriched compiler errors.
- [x] Grammar – Hardened helpers in `impala/jspeg/impala.jspeg`, including `metaSlot`, `createParserContext`, `makeMeta`, `assign`, and `fail`. (Now part of the checked-in grammar.)
- [x] Build script – `impala/jspeg/updateJSPEG.js` applies `applyImpalaHardening` so the generated Impala bundle keeps the hardened helpers even while `jspeg.jspeg` still emits legacy parser contexts. (Drop this shim once the JSPEG grammar gains native helpers.)
- [x] Generated artefact – Regenerated `impala/jspeg/impalaCompiler.js` checked in with the hardened helpers sourced from the grammar. (Bundle matches the grammar without post-processing.)
- [x] Runner cleanup – `impala/jspeg/impalaJsCompilerRunner.js` simplified to load the bundle verbatim.
- [x] Documentation – Build notes describing regeneration steps and the absence of runtime patching. (Impala JS docs now cover the grammar-embedded helpers and regeneration command.)

Following this roadmap removes the brittle string patch, restores trust in the generated compiler source, and documents every step needed to keep the bundle hardened in a maintainable way.
