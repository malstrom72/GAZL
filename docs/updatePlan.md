# Impala Return-Type Contract Enforcement Plan

## Problem Statement
Compiling callers and callees in different units allows mismatched return types to slip through. The caller infers a `float` (because the assignment target is typed), but the callee later materializes as an `int` function in another compilation. Each unit succeeds in isolation and the existing `gazl-validate` comparison still reports success because the generated assembly lacks explicit return-type metadata.

## Goals
- Preserve the relaxed assignment behavior so callers can store results of extern functions without casting.
- Surface an error when separately compiled units disagree about a function's return category.
- Keep `.gazl` output and regression tests deterministic, with failures pointing back to the offending symbol.

## Non-Goals
- Changing Impala source syntax to carry explicit return types in `extern function` declarations.
- Introducing a full linker; validation should stay a metadata comparison step.

## Proposed Approach
1. **Persist call expectations in `.gazl`**
   In `impala/jspeg/impala.jspeg`, the `FuncCall` lowering already emits `; expects function(...) -> …` comments through `formatCallExpectationComment` and records the inferred return category on the caller via `makeMeta`. Tighten the call path so whenever `callResultType` is concrete (e.g., inferred from the assignment target or a prior definition), that comment renders the resolved category instead of falling back to `unknown`. Regenerate `impala/jspeg/impalaCompiler.js` so the runtime compiler shares the same behavior.

2. **Guarantee definition-side metadata**
   Still inside `FuncDecl`, ensure `$$parser.resolveFunctionReturnType` (and the implicit-return path) populate `entry.signature.returns` with `'?'` for `void`/unknown while preserving `returnResolved` for later checks. The existing `$$parser.emitFunctionSignature` already appends `; signature func … -> TYPE` to each `FUNC` line—verify that it always has a concrete `returns` value (defaulting to `unknown`) so downstream tooling has a stable definition record.

3. **Record expectations and definitions during validation**
   Augment `parseSignatureComment` in `tools/gazl-validate.js` so it captures both call-site comments (`; expects function(...) -> TYPE`) and definition comments (`; signature func NAME(...) -> TYPE`, plus the analogous `; signature extern native …`). Expectations should be collected in a new `ctx.calls` map keyed by function name; definitions (including extern natives) should flow into a `ctx.definitions` map (or extend `ctx.exports.functions`) with `{ type, location, kind }` records. Reuse the existing `location()` helper so diagnostics cite both the `.gazl` file and any embedded origin. For native callbacks, load the manifest described in Step 5 (`docs/nativeCallbackSignatures.json`) before scanning files so that the validator treats those entries as pre-declared definitions.

4. **Reconcile contracts across compilation units**  
   After `processFile` finishes scanning each file, walk the aggregated call expectations and ensure there is a compatible definition. Emit an error when multiple expectations disagree (`typesCompatible` is already available) or when a concrete expectation conflicts with a concrete definition. Add a dedicated diagnostic constructor so failures read like `xorShiftRandom expected float at caller.gazl:12 but returns int at callee.gazl:6`.

5. **Tighten compile-time feedback**  
   In `$$parser.resolveFunctionReturnType` and `$$parser.expectFunctionReturnType`, keep today’s single-unit safeguards but update the messages to reference “previous expectation from signature metadata” when applicable. This ensures forward declarations inside the same translation unit still produce immediate errors.

6. **Regression coverage and tooling hooks**
   Add three Impala samples under `tests/impala/sources/` (agreement, mismatch, unknown) plus their `tests/impala/golden/` counterparts. Mirror each sample in `impala/jspeg/testdata/` for the JSPEG unit tests. Update `tools/regen-jspeg-fixtures.sh`/`.cmd` to call `node tools/gazl-validate.js` on the regenerated `.gazl` pairs so fixture drift immediately surfaces.

## Open Questions / Follow-Ups
- Decide how to represent “unknown” or “void” in the metadata so the validator can distinguish intentional omissions.
- Confirm whether metadata should be optional (to avoid bloating `.gazl`) or always emitted when type inference runs.
- Explore emitting similar metadata for argument categories once return types are stable.
- Derive the native callback manifest from the existing host registration tables once the quick-stop solution below proves itself.

## Rollout Notes
- Update the documentation to describe how cross-unit type mismatches are reported.
- Ensure fixture-regeneration scripts keep the metadata comments intact when retabulating.

## Step-by-Step Implementation Plan
- [ ] Update `impala/jspeg/impala.jspeg`:
  - [ ] Ensure the `FuncCall` lowering passes resolved return categories into `formatCallExpectationComment` so the emitted `; expects function(...) -> TYPE` row reflects inference instead of defaulting to `unknown`.
  - [ ] Guarantee `$$parser.resolveFunctionReturnType` / `$$parser.emitFunctionSignature` always assign a concrete `returns` value (defaulting to `unknown`) before appending the existing `; signature func …` comment.
  - [ ] Re-run `node impala/jspeg/updateJSPEG.js` so `impalaCompiler.js` matches the grammar changes.
- [ ] Amend `tools/gazl-validate.js`:
  - [ ] Teach `parseSignatureComment` to parse the call-site `; expects function(...) -> TYPE` rows alongside `; signature func …` and `; signature extern native …` definitions.
  - [ ] Maintain new context maps (`ctx.calls`, `ctx.definitions`) to store `{ category, location, kind }` records.
  - [ ] Add a reconciliation pass that compares each expected category with every discovered definition, reusing `typesCompatible` for the comparisons and issuing structured diagnostics on conflicts or missing definitions.
- [ ] Expand the regression suite:
  - [ ] Author the three `.impala` samples (agreement, mismatch, unknown) and regenerate their `.gazl` outputs under both `tests/impala/golden/` and `impala/jspeg/testdata/`.
  - [ ] Wire `tools/regen-jspeg-fixtures.{sh,cmd}` to invoke `node tools/gazl-validate.js` on the freshly generated fixtures so signature mismatches fail regeneration.
- [ ] Validate the dummy return-slot work:
  - [ ] Adjust the implicit-return branch in `FuncDecl` so the `PARA *1` emission follows the `FUNC` declaration while keeping legacy semantics.
  - [ ] Document the rationale for the placeholder within `impala.jspeg` (and refresh `impalaCompiler.js`) to keep future contributors aligned.
- [ ] Verify native callback coverage:
  - [ ] Create `docs/nativeCallbackSignatures.json` containing `{ "name": string, "return": string }` entries seeded from today’s `NATIVE_NAMES` tables and any additional host registrations that ship with the repo.
  - [ ] Extend `tools/gazl-validate.js` to load this JSON at startup (with graceful fallback when the file is missing) and to treat every manifest entry as a definition when reconciling `extern native` call expectations.
  - [ ] Follow up by scripting a generator that rehydrates the JSON from the actual host registration tables so the manifest stays in sync without manual edits.

## Newly Observed Issues
- The implicit-return branch inside `FuncDecl` currently calls `declare('PARA', 'locals', ...)` before `emitFunctionSignature`, which prints the placeholder row ahead of the `FUNC` declaration (showing up as `PARA *1` preceding the function label). Confirm whether GAZL technically allows this ordering and, if not, reorder the emissions so the `FUNC` symbol leads the block while keeping the placeholder available for callers that expect a one-word return slot.
- The reason for emitting the dummy `PARA *1` sentinel is not documented in the JSPEG grammar, making it unclear why void functions still declare a return-sized slot.

## Formatting Requirements
- All JavaScript files in this repository must be formatted with Prettier using tab indentation (`useTabs: true`) and a 140-character print width.
- Install Prettier locally when needed:
  ```sh
  npm install --no-save prettier
  ```
- Format sources before committing:
  ```sh
  npx prettier --write .
  ```
- Prettier should pick up the following configuration (store it in the project root as needed):
  ```json
  {
	"useTabs": true,
	"printWidth": 140
  }
  ```

## Additional Action Items
- [ ] Rework the implicit-return path so the dummy `PARA *1` is declared after the `FUNC` line (or otherwise ensure assemblers see the `FUNC` declaration first) while preserving return-type inference bookkeeping.
- [ ] Add an explanatory comment in `impala/jspeg/impala.jspeg` (and regenerate the compiler) that spells out why void functions emit a placeholder `PARA *1`—namely, to give call sites a predictable one-word return area and to keep legacy PPEG output identical.
