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
   Extend the compiler to serialize return-type expectations into the generated `.gazl` whenever a call result flows into a typed context (assignment, argument, or expression). The metadata can stay in the comment channel we already use for signature notes, e.g. `; @call expect xorShiftRandom: float`.

2. **Annotate Function Definitions**  
   When lowering a function definition, add a matching metadata record describing the actual return type that the implementation produces (if any). This piggybacks on the existing signature helper infrastructure that records parameters.

3. **Collect Metadata During Validation**  
   Teach `gazl-validate.js` to parse the new metadata from each `.gazl` input and build two maps: expected return types (from call sites) and actual return types (from function bodies).

4. **Cross-Check Across Units**  
   During validation, compare the aggregated expectation map against the definition map. If a function has conflicting return categories, raise a descriptive error that names the symbol, the expected type(s), and where the evidence came from (caller vs. callee files).

5. **Fail Early When Evidence Exists**  
   If a callee implementation is compiled in the same unit as a mismatched call-site expectation, the compiler already has enough information to throw immediately. Keep that fast-path so single-file errors remain crisp.

6. **Regression Coverage**  
   Add multi-unit test cases where:
   - A caller expects `float` but the callee returns `int` (should fail).
   - Multiple callers agree on a type and the implementation matches (should succeed).
   - One caller never constrains the return type (should remain accepted until evidence appears).

## Open Questions / Follow-Ups
- Decide how to represent “unknown” or “void” in the metadata so the validator can distinguish intentional omissions.
- Confirm whether metadata should be optional (to avoid bloating `.gazl`) or always emitted when type inference runs.
- Explore emitting similar metadata for argument categories once return types are stable.

## Rollout Notes
- Update the documentation to describe how cross-unit type mismatches are reported.
- Ensure fixture-regeneration scripts keep the metadata comments intact when retabulating.
