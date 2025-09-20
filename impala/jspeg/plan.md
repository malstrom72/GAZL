# Modernise `impala.jspeg` helpers for ES3 JavaScript

## Milestone 0 — Refresh context and baseline
- [x] Re-read the JSPEG documentation, `jspeg.jspeg`, and `jspegCompiler.js` to confirm how helper blocks are spliced into the generated parser.
- [x] Document how the compiler strips the `$$parser.` prefix while leaving local declarations intact, and why we avoid emitting a runtime container.
- [x] Capture the current helper inventory (constants, tables, state buckets, utility functions) so follow-up cleanups have a checklist.
- [x] ✅ `node impala/jspeg/updateJSPEG.js --check`
- [x] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [x] ✅ `timeout 180 ./build.sh`

## Milestone 1 — Replace pseudo declarations with locals plus aliases
- [x] Convert every `var $$parser.name = …` pseudo declaration into a real local (`var name = …;`) followed by `$$parser.name = name;` so the generated JavaScript is valid ES3 while future backends can attach a container.
- [x] Lift helper functions to named locals (or `var` bindings) and mirror them through `$$parser` after their bodies so they stay private once the prefix is stripped.
- [x] Ensure any stateful helpers read and write through the new locals instead of relying on implicit globals.
- [x] Regenerate `impalaCompiler.js` via `node impala/jspeg/updateJSPEG.js` and confirm the emitted header now uses standard declarations.
- [x] ✅ `node impala/jspeg/updateJSPEG.js`
- [x] ✅ `node impala/jspeg/updateJSPEG.js --check`
- [x] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [x] ✅ `timeout 180 ./build.sh`

## Milestone 2 — Prefer literals over helper-driven tables
- [x] Replace the `map()` helper invocations with explicit object or array literals for lookup tables like `META_TO_GAZL`, `SUPPORTED_OPS`, and `UNARY_OPS`.
- [x] Remove the obsolete `map()` helper once all data structures are initialised directly.
- [x] Update any documentation/comments to reflect the literal layout and note required ordering.
- [x] ✅ `node impala/jspeg/updateJSPEG.js`
- [x] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [x] ✅ `timeout 180 ./build.sh`

## Milestone 3 — Centralise shared mutable state
- [x] Gather mutable buckets (metacode, string pools, counters, switch stack) behind a `parserState` helper object with accessor functions to avoid direct structure pokes.
- [x] Update helper routines and grammar actions to use the new accessors while keeping exported names stable.
- [x] Document invariants and reset flows so future refactors know which helpers mutate shared state.
- [x] ✅ `node impala/jspeg/updateJSPEG.js`
- [x] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [x] ✅ `timeout 180 ./build.sh`

## Milestone 4 — Evaluate module layout options
- [x] Experiment with removing the outer block or adopting an ES3-friendly IIFE to keep `'use strict';` effective without leaking helpers.
- [x] Ensure the compiler still strips `$$parser.` correctly under any new layout and record the findings.
- [x] Leave the door open for packaging modes that reinterpret `$$parser` as a real container once helpers are idiomatic.
- [x] ✅ `node impala/jspeg/updateJSPEG.js`
- [x] ✅ `node impala/jspeg/jspegCompilerTests.js`
- [x] ✅ `timeout 180 ./build.sh`

## Additional insights
- Keep `$$parser` references in the grammar so current generators strip the prefix; the alias assignments become no-ops today but preserve a hook for future object-based exports.
- Note any helper whose side effects depend on evaluation order, especially when switching to literals or shared state accessors.
- Track optional follow-ups (documentation updates, sample packaging modes) separately so they do not block the milestones above.
