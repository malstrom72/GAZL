# JSPEG TODO

JSPEG aims to port PikaScript's PPEG parser generator to JavaScript while keeping the familiar PEG syntax with embedded actions, tags, and captures.

See [PPEG.md](PPEG.md) for background on the original PikaScript-based implementation.

## Tasks
- [ ] Consolidate the PikaScript `.ppeg` grammar into `jspeg.jspeg` and drop the PPEG-based version once parity is reached.
        - [x] Diff `jspeg.ppeg` and `jspeg.jspeg` to catalog missing constructs.
                - Confirmed both grammars share rule structure; remaining gaps center on action/tag semantics and runtime helpers rather than missing PEG forms.
                - Documented that character class expansion and local-variable synthesis still mirror the slower PPEG behavior and require dedicated optimization work.
        - [ ] Port capture and tag semantics so both grammars produce equivalent JavaScript.
                - [x] Added `tagCaptureTest.jspeg` regression grammar to verify JSPEG handles nested tags, captures, and action-based aggregation.
	- [x] Regenerate `jspegCompiler.js` from `jspeg.jspeg` and keep it under test.
	- [ ] Add regression tests compiling both grammars and comparing parser behavior.
	- [ ] Remove `jspeg.ppeg` and update build scripts once parity is verified.
- [ ] Implement missing action and variable support in the JSPEG grammar; optimize character class handling and local variable generation.
        - [ ] Translate `{}` action blocks into inline JavaScript, rewriting `$$` helpers and `$name` references.
        - [ ] Support `name:expr` tagging and `name=expr` captures with proper scope handling.
        - [ ] Precompute character classes and avoid per-parse `indexOf` calls.
                - [ ] Fix escape handling so `[ \t\r\n]`-style classes compile without needing to expand to manual alternations.
        - [ ] Generate local variables at compile time rather than creating anonymous functions during parsing.
- [ ] Modernize generated compilers (`impalaCompiler.js`, `jspegCompiler.js`) and regenerate them from the current grammar sources.
        - [ ] Convert compiler outputs to ES modules or modern syntax while retaining Node compatibility.
        - [x] Provide a Node script that rebuilds the compilers from `jspeg.jspeg` (self-hosting step).
                - Added `updateJSPEG.js`, which regenerates both compilers or verifies they are current via `node updateJSPEG.js --check`.
        - [ ] Verify regenerated compilers pass `jspegCompilerTests.js` and any new tests.
- [x] Ensure `jspegCompilerTests.js` runs from the repository root.
- [x] Expand JSPEG test coverage with an arithmetic grammar test.
- [ ] Clean up legacy PPEG scripts (`ppeg.pika`, `initPPEG.pika`, `updatePPEG.pika`) and document the full JavaScript-based regeneration process.
	- [ ] Replace PikaScript regeneration with a Node-based workflow once JSPEG can compile itself.
	- [ ] Remove obsolete PikaScript files after confirming the JSPEG pipeline works end-to-end.
	- [ ] Update build scripts (`build.sh`, `build.cmd`) to call the new regeneration script.
- [ ] Document how JSPEG relates to the original PPEG and provide build and usage instructions once the port stabilizes.
	- [ ] Expand `PPEG.md` or create a dedicated `JSPEG.md` outlining differences and migration notes.
	- [ ] Write a quickstart example showing grammar definition, compiler invocation, and parser usage.
	- [ ] Link documentation from the repository's top-level README or `docs/` directory.
