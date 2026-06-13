# Improved Type Checking for Impala

Impala developers enjoy being able to compile individual sources to textual `.gazl` units and then assemble a program by concatenating those files. The goal of this specification is to tighten cross-unit type checking—primarily around ints, floats, pointers, function pointers, and void returns—without disturbing that lightweight workflow.

## Background

The shipping Impala compiler is generated from `impala/jspeg/impala.jspeg` and mirrors the original PPEG grammar in JavaScript. During a compile it builds meta-instructions, tracks transient pools, and flushes them to GAZL text via helpers such as `$$parser.emitMeta` and `$$parser.flushMetaCode`. Types are already tracked per expression and declaration using single-character codes (`i`, `f`, `p`, `F`, `N`, `A`, `?`) that drive code generation and validation.

* `$$parser.SUPPORTED_OPS` enumerates the legal operand combinations for each arithmetic and comparison operator. Attempting `+` on incompatible types raises a diagnostic before code is emitted.【F:impala/jspeg/impala.jspeg†L96-L152】【F:impala/jspeg/impala.jspeg†L562-L600】
* `$$parser.declare` records every symbol in scoped tables (`locals`, `globals`, `functions`, `defines`). It reuses `TYPE_SUFFIXES` to stamp assembly directives such as `INPi`, `LOCp`, or `FUNC` while enforcing that redeclarations match the previous type.【F:impala/jspeg/impala.jspeg†L1006-L1070】
* Lookups convert declarations into meta-code with the correct operator: locals produce `=` or `:=`, globals emit `=*/:=*`, functions collapse to `&name` with type `F`, and natives map to `^name` with type `N`. Function calls only verify that the callee itself has type `F` or `N`; they do **not** check the number or types of arguments being supplied.【F:impala/jspeg/impala.jspeg†L1128-L1198】【F:impala/jspeg/impala.jspeg†L1789-L1831】
* Argument and local lists preserve declaration order and store per-slot type codes via `ArgsDecl` and `LocalsDecl`, ensuring helpers like `makeArgValue` can generate correct temporaries; array initialisers enforce element constants through `makeConstant` before `DATA` directives are emitted.【F:impala/jspeg/impala.jspeg†L1680-L1718】【F:impala/jspeg/impala.jspeg†L1699-L1774】

However, those `ArgsDecl` entries are discarded once `declare` runs, so `FuncCall` only checks that the callee resolves to `F` or `N` and emits code without verifying argument lists.【F:impala/jspeg/impala.jspeg†L1006-L1049】【F:impala/jspeg/impala.jspeg†L1800-L1823】 Inside a single translation unit, type enforcement covers operator compatibility and redeclarations, but it stops short of validating call sites. Developers can invoke any function with any argument list and the compiler still emits code. The generated `.gazl` text only contains the final instructions, globals, and section headers. Once the files are concatenated the assembler has no notion of argument counts or the type codes that guarded the source, so mismatches across units (wrong extern prototypes, globals with different categories, accidental reuse of function names) become runtime problems. Capturing the recorded parameter arrays in the function symbol table gives us enough information to abort mismatched calls during the same compile before relying on post-hoc validation.【F:impala/jspeg/impala.jspeg†L1680-L1718】【F:impala/jspeg/impala.jspeg†L1800-L1823】

## Pain Points Today

1. **Extern drift.** Two units can declare the same `extern function` with incompatible signatures. Each unit type-checks internally, but the assembler concatenation never catches the disagreement and whichever definition is loaded first wins at runtime.
2. **Global shape mismatches.** Nothing prevents a unit from exporting a pointer but another importing it as a float. The compiler enforces the type consistently inside each unit, yet the `.gazl` blobs have no metadata to cross-check the expectations.
3. **Argument arity.** Calls into other units or native shims rely on the callee obeying an implied argument window. The compiler never verifies arity, so even inside a single unit a function can be invoked with any number of arguments and code still emits; when a callee changes from two parameters to three, existing call sites continue to emit two words and the VM happily consumes garbage.
4. **Void versus valued functions.** Functions without an explicit `returns` clause are implemented by declaring an implicit one-word slot with type `?`. Callers expect to read from that slot. A callee switching from void to returning an int (or vice versa) should be flagged, but the current pipeline cannot compare them after concatenation.【F:impala/jspeg/impala.jspeg†L1489-L1524】

## Goals

* Capture and compare return categories and argument counts for the primitive Impala types (int, float, pointer, funcptr, implicit void) whenever units are combined.
* Fail fast inside a unit when a call to a known function uses the wrong primitive categories or argument count by reusing the parser’s existing parameter records.【F:impala/jspeg/impala.jspeg†L1680-L1718】【F:impala/jspeg/impala.jspeg†L1800-L1823】
* Preserve the existing “compile Impala to `.gazl` independently, then concatenate” workflow. No additional linker or ordering constraints should be imposed by default.
* Keep metadata compact enough that legacy tooling that only understands raw `.gazl` can continue to operate unchanged.
* Allow developers to opt into strict validation gradually so legacy units without metadata still build.

## Constraints and Observations

* The compiler already records accurate type codes during parsing; the missing piece is exporting that knowledge. Hooking the metadata capture into `$$parser.declare`, `ArgsDecl`, and `FuncDecl` avoids duplicating logic and stays aligned with the self-hosted compiler.【F:impala/jspeg/impala.jspeg†L1006-L1070】【F:impala/jspeg/impala.jspeg†L1466-L1546】
* `.gazl` text is intentionally order-independent. We must embed metadata in a way that survives plain-text concatenation, hence the signature comments ride directly alongside the existing instructions.
* Build automation (`build.sh`) already runs `PikaCmd runTests.pika` and the JSPEG regression harness. Any additional validation step must fit into that ecosystem without making the pipeline brittle.【F:build.sh†L18-L21】
* Native entry points declared with `extern native` have type `N` and typically follow VM-defined signatures. The validator should recognise `N` as opaque so host applications can continue providing implementations without extra signature metadata.【F:impala/jspeg/impala.jspeg†L1554-L1563】【F:impala/jspeg/impala.jspeg†L1128-L1157】

## Proposed Approach

### 1. Annotate `.gazl` With Signature Comments

* While walking declarations the compiler already knows each function’s return category and ordered parameter list. Reuse that data so `FuncDecl` and `ExternDecl` attach a compact prototype comment directly to the emitted instruction. Instead of inventing a `;SIG:` prefix, reuse Impala’s existing comment style and keep the metadata readable: `FUNC foo    ; signature func foo(int a, ptr b) -> int`. Parameter names are included when available (falling back to `arg0`, `arg1`, …) so developers can line the comment up with the source.【F:impala/jspeg/impala.jspeg†L1466-L1546】【F:impala/jspeg/impala.jspeg†L1636-L1683】

  The file only needs to advertise the metadata schema once. A header such as `; signatures version=1` at the top of the `.gazl` unit tells downstream tools that signature comments follow; the rest of the file can focus on the readable one-liners.

  An example `.gazl` fragment after enabling metadata might look like:

  ```
  ; signatures version=1
  FUNC foo    ; signature func foo(int a, ptr b) -> int @ foo.impala:5:1
  ...
  LOC sharedBuffer    ; signature global sharedBuffer : ptr @ foo.impala:2:1
  ...
  CALL foo    ; expects foo(int, ptr) -> int @ foo.impala:12:9
  ; signature extern func bar() -> unknown @ foo.impala:1:1
  ```

  Signature annotations follow a compact grammar so tooling can parse them deterministically:

  * **Header.** Each unit advertises support once via `; signatures version=<n>`.
  * **Function definitions and prototypes.** Any line that emits `FUNC name` (or an implicit forward declaration) appends `; signature func name(<params>) -> <return>` with an optional trailing `@ <origin>` token identifying the Impala source location. Extern shims use the same shape with an `extern func` or `extern native` prefix to distinguish host-provided entry points.
  * **Call sites.** Every `CALL` instruction gains `; expects <label>(<arg-types>) -> <return>` (likewise with an optional `@ <origin>` suffix) describing the caller’s view.
  * **Globals and constants.** `LOC`, `DATA`, and directive-driven constants append `; signature global name : <type>` (or `readonly`/`temporary` depending on section). Array declarations render as `; signature array name[size] : <type>`.
  * **Standalone externs.** Declarations that produce no GAZL directive still emit their own comment block, allowing validators to collect imports even when no code follows the declaration.

  Parameter entries are rendered as `type name` pairs (falling back to `argN` when the source omits a name), and all primitive types are normalised to the `{int, float, ptr, funcptr, void}` vocabulary. When the compiler knows the originating source buffer and offset, it appends `@ <path>:<line>:<column>` (using 1-based line and column numbers) to the same comment so validators and editors can surface precise diagnostics; callers that cannot supply a filename emit just the line/column pair. Comments are attached through the existing `$$parser.emit(';', …)` path so they obey the same indentation and spacing rules as other debugger notes.

  The comments describe the definition’s signature, the exported global, the caller’s expectations, and now even bare extern declarations using the same primitive categories (rendered as `int`, `float`, `ptr`, `funcptr`, `void`). Because they are ordinary comments, concatenating multiple `.gazl` files simply produces a larger stream of inline signature notes for the validator to consume without affecting assemblers that ignore comments.
  Standalone `extern` declarations that produce no GAZL directive still emit a one-line annotation (for example, `; signature extern func bar() -> unknown`) so validators can record imports alongside executable code.
  The call-site comment rides on the `CALL` instruction itself, so there is no need for a `caller->callee` shorthand—the surrounding assembler already makes the callee obvious.

  #### Source span capture and formatting

  * **Record filenames.** Compiler runners already know which file they are compiling. Thread a `sourceName` (or equivalent) option into `impalaCompiler`, cache it on `$$parser`, and copy it into each symbol’s signature record alongside the existing `sourceCode` and `sourceOffset` data.
  * **Retain offsets.** `FuncDecl`, `ArgsDecl`, `ExternDecl`, `GlobalDecl`, `ConstDecl`, and `FuncCall` all receive `$$s`/`$$i` pairs today. Persist those offsets on the metadata objects we already store so the emitter can recover precise origin information.
  * **Normalise coordinates.** Introduce a helper such as `formatSignatureSourceOrigin(sourceCode, offset, filename)` that converts offsets to 1-based `line:column` coordinates, collapses Windows-style newlines, and falls back to just `line:column` when no filename is provided. The helper returns strings like `voice.impala:42:9` or `42:9`.
  * **Emit `@ origin` markers.** Extend `formatFunctionSignatureComment`, `formatGlobalSignatureComment`, `formatConstSignatureComment`, and `formatCallExpectationComment` to append `@ <origin>` whenever metadata includes a computed span. Standalone extern annotations use the same helper so imports carry comparable location data.

* Extend `FuncCall` so that when the callee resolves to a definition or prototype inside the current unit it consults the stored signature, compares the number of arguments and each `{int, float, ptr, funcptr, void}` category (derived from the internal `i/f/p/F/?` codes), and calls `typeError` if they disagree. Extern-only calls continue to defer to metadata validation, preserving today’s lax behaviour for units that lack full signatures.【F:impala/jspeg/impala.jspeg†L1006-L1049】【F:impala/jspeg/impala.jspeg†L1680-L1718】【F:impala/jspeg/impala.jspeg†L1800-L1823】
* Each call site (`makeCall`/`callExpr`) should stamp the expected signature for the callee based on the parser’s current knowledge. Placing the comment after the emitted instruction (`CALL foo; expects foo(int, ptr) -> int`) keeps the metadata adjacent to the code that depends on it while still being trivial to strip. This captures the caller’s view even when the callee lives in another unit, letting a validator spot mismatched assumptions once files are combined.【F:impala/jspeg/impala.jspeg†L1789-L1831】
* Globals, constants, and arrays get analogous annotations (`LOC sharedBuffer; signature global sharedBuffer : ptr`, `DATA sineTable; signature array sineTable[8] : float`). Because the comments live inside the `.gazl`, legacy assemblers continue to run unmodified while newer tooling can parse the extra metadata.【F:impala/jspeg/impala.jspeg†L1086-L1114】
* Use the `; signature` / `; expects` tokens to keep the format machine-readable without forcing every line to carry a redundant schema or version tag.

### 2. Provide a `gazl-validate` Utility

* Build a small Node or C++ tool that accepts a list of `.gazl` files, scans for the signature comments, and resolves imports against exports.
* Matching rules:
  * Functions must agree on argument count and return type. All arguments must match within the `{int, float, ptr, funcptr, void}` set (mapped back to the compiler’s `i/f/p/F/?` codes). Caller annotations that mention symbols lacking a matching callee annotation in the concatenated stream produce warnings (upgradeable to errors).
  * Globals/constants must align on category (`int`, `float`, `ptr`, `array`, encoded as `i/f/p/A`). Array sizes must also match.
  * Function pointers (`F`) can bind to `extern function` exports by erasing the pointer level, mirroring how `lookup` rewrites function declarations into address expressions today.【F:impala/jspeg/impala.jspeg†L1128-L1157】
  * Native (`N`) exports satisfy any matching import but are never type-checked, deferring to host integration.
* When mismatches are found, emit actionable diagnostics that cite both the importer and exporter source locations. The validator should parse the optional `@ <origin>` suffix on each comment and, when absent, fall back to the legacy `$$s`/`$$i` offsets that are already threaded through declarations for this purpose.【F:impala/jspeg/impala.jspeg†L1466-L1546】
* Provide `--warn-only` and `--force` flags so teams can adopt strictness gradually.
* `tools/gazl-validate.js` (with a Windows shim in `tools/gazl-validate.cmd`) accepts `.gazl` files or directories, downgrades errors to warnings when `--warn-only` is supplied, and promotes missing-definition warnings to hard failures when `--force` is used.【F:tools/gazl-validate.js†L1-L71】【F:tools/gazl-validate.js†L636-L668】

### 3. Surface Metadata to Developers

* Update Impala documentation (`docs/Impala.md` and related guides) with a brief explanation of signature comments, how void returns are represented, and how to run the validator.
* Teach the existing regression harness to run `gazl-validate` after it regenerates fixtures (`tools/regen-jspeg-fixtures.{sh,cmd}`) so we maintain parity between the PPEG and JSPEG toolchains.
* Offer a compiler flag (for example `--no-metadata`) for scenarios where the output must be byte-for-byte identical to the legacy build, keeping it off by default once the feature stabilises.

### 4. Preserve Backward Compatibility

* Units without signature comments are treated as having all exports/imports in the `unknown` category. The validator warns but does not fail unless `--strict` is enabled.
* When reading concatenated `.gazl`, the validator should tolerate signature comments appearing anywhere—multiple concatenated blocks simply append to the same stream.
* Version the signature comment schema from the start via the file header (`; signatures version=1`). Future expansions (e.g., distinguishing pointer depth or struct shapes) can bump the header while older validators fall back to permissive behaviour.

## Implementation Plan

- [x] **Compiler plumbing**
  - [x] Modify the generated compiler (`impala/jspeg/impala.jspeg`) so `FuncDecl`, `ExternDecl`, `GlobalDecl`, `ConstDecl`, and call emission helpers append inline signature comments (`; signature …` / `; expects …`) directly to the output buffer. Reuse the existing type codes and argument arrays so no additional inference is required.
    - [x] Attach signature metadata for function definitions, globals, constants, and call sites.
    - [x] Extend extern declarations to emit or preserve compatible signature comments.
  - [x] Persist the ordered parameter list from `ArgsDecl` in each function symbol, compare it against the call-site operands inside `FuncCall`, and invoke `typeError` when intra-unit calls disagree on category or arity before emitting the `()` instruction.【F:impala/jspeg/impala.jspeg†L1006-L1055】【F:impala/jspeg/impala.jspeg†L1719-L1738】【F:impala/jspeg/impala.jspeg†L1834-L1916】
  - [x] Teach the emitter to format argument lists and return categories using the `{int, float, ptr, funcptr, void}` vocabulary (while preserving the internal `i/f/p/F/?` mapping) and to include positional source spans where available for better diagnostics.
    - [x] Format function signatures, call expectations, and global annotations using the shared vocabulary helpers and stable parameter ordering.
    - [x] Thread explicit source-span markers through the emitted comments so downstream tooling can cite the original Impala locations when reporting validation errors.
      - [x] Expose a stable `sourceName` option on the compiler entry points and store it alongside the existing `sourceCode`/`sourceOffset` metadata for declarations and call sites.
      - [x] Derive 1-based line/column coordinates from the stored offsets and stash a formatted origin string on each signature or call expectation.
      - [x] Extend the signature-formatting helpers to append `@ <origin>` tokens to inline and standalone comments when origin data exists.
- [x] **Comment schema**
  - [x] Lock down the comment grammar (for example, `FUNC foo    ; signature func foo(int a, ptr b) -> int` and `CALL foo    ; expects foo(int, ptr) -> int`) and document it alongside the Impala assembly reference so downstream tooling knows how to parse it.
  - [x] Ensure comments never break layout-sensitive sections by routing them through the same helpers that already insert `;`-prefixed notes when `-g` is enabled today.
- [x] **Validator tool**
  - [x] Create `tools/gazl-validate.{js,cmd}` (mirroring existing script conventions) that parses the signature comments, performs the matching described above, and exits non-zero on fatal mismatches unless `--warn-only` is passed.【F:tools/gazl-validate.js†L1-L338】【F:tools/gazl-validate.js†L486-L679】
  - [x] Run `tools/gazl-validate.js` from the default build on explicit JSPEG fixture file sets, while keeping arbitrary linked-unit validation as a direct command.
  - [x] Parse optional `@ <origin>` markers so mismatch diagnostics can cite both the importer and exporter spans when metadata is available.
- [x] **Documentation & onboarding**
  - [x] Update `docs/Impala.md` and add a quickstart snippet showing how to run the validator on two sample units.
  - [x] Document how implicit void returns (`?`) in the compiler map to the signature comment’s `void` category so users understand the translation.【F:impala/jspeg/impala.jspeg†L1489-L1524】
- [x] **Testing**
  - [x] Extend the JSPEG regression suite to regenerate `.gazl` fixtures with signature comments in `impala/jspeg/testdata/` and assert that the validator reports expected errors when fixtures are intentionally mis-typed.
  - [x] Add new unit tests under `tests/` that feed crafted comment snippets to the validator, covering success, warnings, and hard failures.

## Adoption Strategy

* Ship the compiler changes with signature comment emission gated behind `--emit-metadata`. Enable it by default after a release or two once downstream tools have caught up.
* Encourage early adopters to run `tools/gazl-validate.js` explicitly in CI on the exact `.gazl` units that will be concatenated or loaded together.
* Monitor feedback from teams using large legacy codebases. If the comment stream causes unacceptable noise, offer targeted suppression (per-symbol opt-outs or an `@opaque` annotation similar to today’s `extern array` escape hatch).

## Risks and Mitigations

* **Schema drift between compiler versions.** Mitigate by embedding the compiler version (`IMPALA_VERSION`) or build stamp alongside the single `; signatures version=…` header and teaching the validator to report mismatched producer versions.【F:impala/jspeg/impala.jspeg†L132-L141】
* **Performance overhead during builds.** Comment emission is lightweight stringification. Validator runs scale with the number of imports/exports and can be optimised via hash maps. Provide a fast path that skips validation entirely when no signature comments are present.
* **False positives for advanced patterns.** Constructs that coerce types via casts might look incompatible in the signature. Allow per-declaration annotations (e.g., `@unchecked`) to suppress individual comparisons until richer metadata is available.

## Success Criteria

* Typical cross-unit mistakes—wrong argument counts, mismatched return categories, globals declared with incompatible primitives—are caught by the validator before the `.gazl` files reach the VM.
* Independent compilation and text concatenation remain the default path; developers who ignore the signature comments still obtain runnable output, albeit without cross-checking.
* Build time impact stays within 5% for metadata generation and under one second for validation on medium projects, matching today’s snappy iteration loop.
* Documentation and samples make it clear how the signature comments map to the compiler’s internal type codes, lowering the barrier for teams adopting the stricter checks.

## Open Questions

* Do we want signature comments to describe local-only helper functions marked `static` once that concept exists, or restrict the scope to exported symbols for now?
* Should `funcptr` metadata record both the pointer itself (`F`) and the signature it is expected to point at, enabling deeper validation for callback-heavy code?
* Should we additionally emit an optional aggregated index (for example, a JSON sidecar) for tooling that wants faster parsing while keeping the inline comments as the canonical source?
