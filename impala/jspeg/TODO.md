# JSPEG TODO

JSPEG aims to port PikaScript's PPEG parser generator to JavaScript while keeping the familiar PEG syntax with embedded actions, tags, and captures.

See [PPEG.md](PPEG.md) for background on the original PikaScript-based implementation.

## Tasks
- [x] Catalog the differences between the existing PikaScript `.ppeg` grammar and the reference `jspeg.jspeg` file without modifying either source.
        - [x] Diff `jspeg.ppeg` and `jspeg.jspeg` to catalog missing constructs.
                - Confirmed both grammars share rule structure; remaining gaps center on action/tag semantics and runtime helpers rather than missing PEG forms.
                - Documented that character class expansion and local-variable synthesis still mirror the slower PPEG behavior and require dedicated optimization work.
        - [x] Summarize how captures, tags, and actions currently diverge so future updates can be scoped precisely.
                - JSPEG now mirrors the PPEG variable bookkeeping by guarding lookups with a shared `hasOwn` helper, keeping the generated locals aligned with the original dictionary semantics.
                - The action rewriter preserves `$` substitutions, normalizes whitespace, and short-circuits on empty segments, matching the control flow of the PPEG grammar while staying in JavaScript.
        - [x] Added `tagCaptureTest.jspeg` regression grammar to verify JSPEG handles nested tags, captures, and action-based aggregation.
- [x] Strengthen regression coverage around the current toolchain.
        - [x] Ensure `jspegCompilerTests.js` runs from the repository root.
        - [x] Expand JSPEG test coverage with an arithmetic grammar test.
        - [x] Add regression tests compiling both grammars and comparing parser behavior.
                - `jspegCompilerTests.js` now compiles the regression grammars with both the recorded compiler and the self-hosted build, comparing generated code and parse results.
                - The Impala grammar is rebuilt under both compilers to verify the generated JavaScript stays byte-for-byte identical.
- [ ] Maintain and modernize the generated compilers (`impalaCompiler.js`, `jspegCompiler.js`) once grammar updates are defined.
        - [x] Convert compiler outputs to ES modules or modern syntax while retaining Node compatibility.
                - Compilers now wrap the generated function in a CommonJS export shim so `require('jspegCompiler.js')` yields the compiler directly while still supporting the sandboxed VM loader used during regeneration.
        - [x] Provide a Node script that rebuilds the compilers from `jspeg.jspeg` (self-hosting step).
                - Added `updateJSPEG.js`, which regenerates both compilers or verifies they are current via `node updateJSPEG.js --check`.
        - [x] Verify regenerated compilers pass `jspegCompilerTests.js` and any new tests.
                - `updateJSPEG.js` now runs the regression suite after both regeneration and `--check` validation so stale or broken outputs are caught immediately.
- [x] Document how JSPEG relates to the original PPEG and provide build and usage instructions once the port stabilizes.
        - [x] Expand `PPEG.md` or create a dedicated `JSPEG.md` outlining differences and migration notes.
                - Added `JSPEG.md` covering the port's goals, directory layout, and how the JavaScript compiler fits alongside the legacy PPEG sources.
        - [x] Write a quickstart example showing grammar definition, compiler invocation, and parser usage.
                - Documented a Node.js REPL-style snippet that compiles a toy grammar and evaluates it against sample input.
        - [x] Link documentation from the repository's top-level README or `docs/` directory.
                - Referenced `impala/jspeg/JSPEG.md` from the root `README.md` so the new guide is easy to discover.
- [ ] Translate `impala.ppeg` semantics into `impala.jspeg` while preserving runtime behavior.
        - [x] Inventory PikaScript helpers referenced from the PPEG grammar and sketch JavaScript equivalents.
                - Catalogued the support routines that PPEG actions call (`map`, `clone`, `args`, string/queue helpers, math utilities) and confirmed their JavaScript mirrors live at the top of `impala.jspeg` with matching semantics.
                - Verified the control-flow helpers (`processBranches`, stock borrowing/return, meta emitters, unary/binary operator dispatch, type errors) are ported one-for-one so future grammar work can reference the same names.
                - Noted that PikaScript macros like `vargs`/`defaults` translate cleanly to optional parameters in the JavaScript helpers, and added a `$$parser["return"]` alias so future grammar work can reference the original helper name without tripping the JavaScript `return` keyword.
        - [x] Ensure the JSPEG action library covers queue/state helpers so the generated `impalaCompiler.js` matches the PPEG output on sample programs.
                - `$$parser.start` now resets the symbol tables, transient pools, and deferred string queues before each compilation so repeated runs mirror the PPEG lifecycle.
        - [ ] Establish regression comparisons between the PPEG-driven and JSPEG-driven Impala compilers.
