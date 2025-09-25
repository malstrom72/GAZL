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
   In `impala/jspeg/impala.jspeg`, the `FuncCall` lowering currently emits an `; expects …` comment via `formatCallExpectationComment` and records the inferred type on the caller through `makeMeta`. Introduce a sibling helper (defined next to `formatCallExpectationComment`) that formats `signature expect func NAME(...) -> TYPE` using `appendOrigin`. Call it from the same block that already emits the `expects` comment (just before the `makeRValue` call) whenever `callResultType` is a concrete value. Regenerate `impala/jspeg/impalaCompiler.js` so the runtime compiler mirrors the change.

2. **Guarantee definition-side metadata**  
   Still inside `FuncDecl`, ensure `$$parser.resolveFunctionReturnType` (and the existing implicit-return path) populate `entry.signature.returns` with `'?'` for unknown/void and preserve `returnResolved` for later checks. Extend `$$parser.emitFunctionSignature` so, immediately after the `FUNC` declaration, it emits `; signature define func …` using a new formatter that wraps `formatFunctionSignatureComment` and forces a return category (defaulting to `unknown`). Regenerate `impalaCompiler.js` and confirm the generated output keeps the dummy `PARA *1` slot but moves the `signature define` comment into place.

3. **Record expectations and definitions during validation**  
   Augment `parseSignatureComment` in `tools/gazl-validate.js` so it recognizes both `signature expect func …` and `signature define func …`. Expectations should be recorded in a new `ctx.calls` map keyed by function name; definitions should flow into a new `ctx.definitions` map (or extend `ctx.exports.functions`) with `{ type, location }` records. Leverage the existing `location()` helper so diagnostics cite both the `.gazl` file and any embedded origin.

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

## Rollout Notes
- Update the documentation to describe how cross-unit type mismatches are reported.
- Ensure fixture-regeneration scripts keep the metadata comments intact when retabulating.

## Step-by-Step Implementation Plan
- [ ] Update `impala/jspeg/impala.jspeg`:
  - [ ] Add `formatCallExpectationSignature` (or equivalent) beside `formatCallExpectationComment`, using `appendOrigin` to produce `signature expect func …` rows.
  - [ ] Invoke the new formatter from the `FuncCall` lowering when `callResultType` is concrete, and emit the resulting comment with `$$parser.emit(';', …)` immediately after the existing `expects` comment.
  - [ ] Extend `$$parser.emitFunctionSignature` so that, after emitting the `FUNC` declaration, it outputs a `signature define func …` line derived from `entry.signature` (fallback to `unknown` when no return was resolved).
  - [ ] Re-run `node impala/jspeg/updateJSPEG.js` so `impalaCompiler.js` matches the grammar changes.
- [ ] Amend `tools/gazl-validate.js`:
  - [ ] Teach `parseSignatureComment` to parse `signature expect func …` and distinguish them from `signature define` entries.
  - [ ] Maintain new context maps (`ctx.calls`, `ctx.definitions`) to store `{ category, location }` records.
  - [ ] Add a reconciliation pass that compares each expected category with every discovered definition, reusing `typesCompatible` for the comparisons and issuing structured diagnostics on conflicts or missing definitions.
- [ ] Expand the regression suite:
  - [ ] Author the three `.impala` samples (agreement, mismatch, unknown) and regenerate their `.gazl` outputs under both `tests/impala/golden/` and `impala/jspeg/testdata/`.
  - [ ] Wire `tools/regen-jspeg-fixtures.{sh,cmd}` to invoke `node tools/gazl-validate.js` on the freshly generated fixtures so signature mismatches fail regeneration.
- [ ] Validate the dummy return-slot work:
  - [ ] Adjust the implicit-return branch in `FuncDecl` so the `PARA *1` emission follows the `FUNC` declaration while keeping legacy semantics.
  - [ ] Document the rationale for the placeholder within `impala.jspeg` (and refresh `impalaCompiler.js`) to keep future contributors aligned.

## Newly Observed Issues
- The implicit-return branch inside `FuncDecl` currently calls `declare('PARA', 'locals', ...)` before `emitFunctionSignature`, which prints the placeholder row ahead of the `FUNC` declaration (showing up as `PARA *1` preceding the function label). Confirm whether GAZL technically allows this ordering and, if not, reorder the emissions so the `FUNC` symbol leads the block while keeping the placeholder available for callers that expect a one-word return slot.
- The reason for emitting the dummy `PARA *1` sentinel is not documented in the JSPEG grammar, making it unclear why void functions still declare a return-sized slot.

## Additional Action Items
- [ ] Rework the implicit-return path so the dummy `PARA *1` is declared after the `FUNC` line (or otherwise ensure assemblers see the `FUNC` declaration first) while preserving return-type inference bookkeeping.
- [ ] Add an explanatory comment in `impala/jspeg/impala.jspeg` (and regenerate the compiler) that spells out why void functions emit a placeholder `PARA *1`—namely, to give call sites a predictable one-word return area and to keep legacy PPEG output identical.
