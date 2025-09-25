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
1. **Emit Call-Site Expectations**
   Extend the signature helpers that already power `formatCallExpectationComment` inside `FuncCall` (see `impala/jspeg/impala.jspeg` around the call to `emit(';', undefined, callComment, ...)`) so they also emit a structured metadata row such as `; signature expect func xorShiftRandom() -> float @ file.impala:12:9` whenever `makeMeta` infers a concrete return type. The emitted row should reuse the existing `appendOrigin(...)` helper so file/offset data mirrors other signature comments.

2. **Annotate Function Definitions**
   Update the `FuncDecl` lowering (generated in both `impala.jspeg` and `impalaCompiler.js`) to guarantee that the `entry.signature` record contains `returnResolved` and `returns` whenever the body writes `return` metadata, then emit an explicit `; signature define func foo() -> int @ ...` comment immediately after `emitFunctionSignature($id._);`. This makes concrete return evidence available even if the implementation lacks an explicit `return` statement.

3. **Collect Metadata During Validation**
   Teach `tools/gazl-validate.js` to recognize the new `signature expect` and `signature define` rows inside `parseSignatureComment`. Store expectations in `context.calls` keyed by function name, and store definitions in `context.exports.functions`. Each entry should record `{ type, origin }` so later diagnostics can pinpoint the mismatch source.

4. **Cross-Check Across Units**
   After `scanFile` populates the context, add a reconciliation step that iterates over `context.calls` and `context.exports.functions`, compares concrete types, and raises a descriptive error (e.g., `xorShiftRandom expected float at caller.gazl:12:9 but returns int at callee.gazl:5:1`). Allow multiple agreeing expectations but fail on the first conflict unless `--warn-only` is set.

5. **Fail Early When Evidence Exists**
   When `resolveFunctionReturnType` (invoked during `FuncDecl`) sees a concrete return type that disagrees with `signature.expectedReturn`, emit a `typeError` immediately so single-source mismatches remain compile-time errors.

6. **Regression Coverage**
   Extend `tests/impala/` and `impala/jspeg/testdata/` with paired fixtures: `externReturnMismatch` (caller vs. callee conflict -> expect validation failure), `externReturnAgreement` (multiple units agree -> expect success), and `externReturnUnknown` (no expectation -> expect success). Regenerate goldens via `tools/regen-jspeg-fixtures.sh` and wire a validator invocation into the test harness so mismatches surface in CI.

## Open Questions / Follow-Ups
- Decide how to represent “unknown” or “void” in the metadata so the validator can distinguish intentional omissions.
- Confirm whether metadata should be optional (to avoid bloating `.gazl`) or always emitted when type inference runs.
- Explore emitting similar metadata for argument categories once return types are stable.

## Rollout Notes
- Update the documentation to describe how cross-unit type mismatches are reported.
- Ensure fixture-regeneration scripts keep the metadata comments intact when retabulating.

## Step-by-Step Implementation Plan
- [ ] Extend the existing call-site path (`expectFunctionReturnType` → `formatCallExpectationComment` in `impala/jspeg/impala.jspeg` and the generated `impalaCompiler.js`) so, whenever a concrete expectation is recorded, we also emit a `signature expect` metadata row via a new `emitCallExpectationSignature` helper that relies on `appendOrigin` for file/offset data.
- [ ] Emit explicit `signature define` comments after each function declaration by enhancing `emitFunctionSignature` to include the resolved return type (or `unknown` when unresolved).
- [ ] Update `tools/gazl-validate.js` to parse the new `expect`/`define` comment kinds, accumulate expectations vs. definitions, and report descriptive diagnostics.
- [ ] Add validator-driven regression fixtures covering matching, conflicting, and unconstrained extern return types; ensure the JSPEG regeneration scripts include the validator step so the goldens stay in sync.

## Newly Observed Issues
- The implicit-return branch inside `FuncDecl` currently calls `declare('PARA', 'locals', ...)` before `emitFunctionSignature`, which prints the placeholder row ahead of the `FUNC` declaration (showing up as `PARA *1` preceding the function label). Confirm whether GAZL technically allows this ordering and, if not, reorder the emissions so the `FUNC` symbol leads the block while keeping the placeholder available for callers that expect a one-word return slot.
- The reason for emitting the dummy `PARA *1` sentinel is not documented in the JSPEG grammar, making it unclear why void functions still declare a return-sized slot.

## Additional Action Items
- [ ] Rework the implicit-return path so the dummy `PARA *1` is declared after the `FUNC` line (or otherwise ensure assemblers see the `FUNC` declaration first) while preserving return-type inference bookkeeping.
- [ ] Add an explanatory comment in `impala/jspeg/impala.jspeg` (and regenerate the compiler) that spells out why void functions emit a placeholder `PARA *1`—namely, to give call sites a predictable one-word return area and to keep legacy PPEG output identical.
