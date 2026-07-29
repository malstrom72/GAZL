# `impala/`

The Impala compiler: a JSPEG grammar with embedded JavaScript actions, plus the front ends, test
harnesses and fixtures around it. Start here to find out which file you want.

Language reference: [`../docs/Impala.md`](../docs/Impala.md) for 1.0, [`../docs/Impala2.md`](../docs/Impala2.md)
for what 2.0 adds. CLI and programmatic API detail: [`ImpalaJS.md`](ImpalaJS.md). JSPEG itself:
[`JSPEG.md`](JSPEG.md).


## What is in here

**The compiler source.** Hand-written, and the only place to make a compiler change.

| File | |
|---|---|
| `impala.jspeg` | The whole Impala -> GAZL compiler: grammar plus JS actions. **This is the source of truth.** |
| `jspeg.jspeg` | The grammar for JSPEG itself. Self-hosting - it compiles to `jspegCompiler.js`, which reproduces itself byte-identically. |
| `jspegTest.jspeg`, `tagCaptureTest.jspeg` | Small regression grammars for the JSPEG test suite. |

**Generated. Never hand-edit these** - line 1 of each says so, and `updateJSPEG.js --check` fails the
build if they drift from their grammar.

| File | |
|---|---|
| `impalaCompiler.js` | Generated from `impala.jspeg`. Checked in, because `playground.html` and the staged NuXJS build load it directly. |
| `jspegCompiler.js` | Generated from `jspeg.jspeg`. |

**Front ends.**

| File | |
|---|---|
| `impala.node.js` | Node CLI. `compile` / `run` subcommands, `--legacy`, `--dead-strip`. What you use during development. |
| `impala.nuxjs.js` | The same compiler driven from the NuXJS ES5 runtime, positional args only. This is the production driver; `build.sh` stages it into `output/`. |
| `playground.html` | Standalone browser playground, no server and no build. Open it from this directory. |
| `updateJSPEG.js` | Regenerates the two `*Compiler.js` files, or `--check`-verifies them. |

**Shared libraries.**

| File | |
|---|---|
| `impalaImportClosure.js` | The `import` closure walk, concatenation and `--dead-strip`. Written in the ES5 subset NuXJS accepts, because both front ends share it. |
| `impalaJsCompilerRunner.js` | Node adapter that loads `impalaCompiler.js` and normalizes its output. Every Node harness goes through its `compileWithJsImpala`. |
| `gazlAssembleCheck.js` | Feeds a `.gazl` to the real assembler. Used by both golden gates; not something you run directly. |

**Test harnesses.**

| File | |
|---|---|
| `jspegCompilerTests.js` | The main suite: self-host equivalence, grammar regressions, diagnostics, both golden systems, and the `gazl-validate` fixtures. |
| `runJspegTests.js` | The golden gate over `../tests/impala/sources` -> `../tests/impala/golden`. Owns `--makegold`. |
| `importBuildTests.js` | Import-as-linking, dead-strip and cycle cases. Owns its own two goldens. |
| `fuzzImpala.js` | Seeded generative fuzzer. A coded `error[Exxx]` is a pass; a raw JS throw is a compiler bug. |
| `fuzzCampaign.js` | Long-running driver that respawns `fuzzImpala.js` in chunks. |

**Everything else.** `ImpalaDemo.impala` is the demo and language tour that `build.sh` compiles and
runs as its end-to-end smoke test. `testdata/` is the second golden system (see below), and
`testdata/validator/` holds hand-written `.gazl` inputs for the metadata linter — those are inputs, never
regenerated. `Impala2Slices.md` and `RefactorPlan.md` are design notes.


## Commands

Compile one source, and compile-and-run one source. `run` needs `output/GAZLCmd` from a full build.

```
node impala/impala.node.js compile impala/testdata/smoke.impala out.gazl 42
node impala/impala.node.js run impala/testdata/smoke.impala
```

The fast gate. Every JS-only check, about 15 seconds, no C++ toolchain. Run this before committing a
compiler change; both `build.sh` and `build.cmd` call it, so it cannot drift from them.

```
tools\test-js.cmd
```
```
bash tools/test-js.sh
```

Check that the generated compilers match their grammars, and regenerate them after editing a `.jspeg`.

```
node impala/updateJSPEG.js --check
node impala/updateJSPEG.js
```

Regenerate goldens after an intentional change to compiler output. Three separate owners — all three,
or you get a half-updated corpus.

```
node impala/runJspegTests.js --makegold
node impala/importBuildTests.js makegold
tools\regen-jspeg-fixtures.cmd
```
```
bash tools/regen-jspeg-fixtures.sh
```

Always read the resulting diff before committing it. `--makegold` records whatever the compiler
currently emits, so it turns a bug into an expected value just as happily as a fix.


## The two golden systems

Their names are similar and putting a fixture in the wrong one silently buys you no coverage.

- **`../tests/impala/sources/*.impala` + `../tests/impala/golden/*.gazl`** — the main corpus, 94 files.
  **This is where a new fixture goes** unless you have a specific reason otherwise. Owned by
  `runJspegTests.js`, which also assembles each golden, and *runs* it when the source header carries an
  `Expected (GAZLCmd <name>.gazl <entry>): <output>` line.
- **`testdata/*.impala` + `*.expected.gazl`** — ten files, for **cross-unit `; signature` metadata**:
  return contracts, extern assignment, caller/provider link sets. Owned by
  `../tools/regen-jspeg-fixtures.*`. Use it only when the point of the fixture is multi-unit linking.

The two also compile with different settings — random id `0x4d2` without retabulation for the goldens,
`42` with retabulation for `testdata` — so compiling a fixture by hand with the wrong pair produces a
diff that means nothing.


## Traps

- **`impalaCompiler.js` is generated but checked in.** It looks like ordinary editable source. A hand
  edit survives until `updateJSPEG.js --check` runs, which is the first thing `test-js` does.
- **`output/` holds staged copies.** `BuildImpala` copies `impala.nuxjs.js`, `impalaImportClosure.js`
  and `impalaCompiler.js` there. Edit any of them and `output/` keeps the old behaviour until you
  rebuild — and all three must be staged together.
- **`--help` is not implemented on the harnesses**, and each fails differently. `runJspegTests.js
  --help` silently runs the whole gate, `importBuildTests.js --help` runs the whole suite, and
  `fuzzImpala.js --help` parses `--help` as the iteration count, prints `NaN programs` and exits 0.
  Only `impala.node.js` and `updateJSPEG.js` print usage.
- **All I/O is latin1**, not UTF-8, to match the fixtures. A new tool that reads a `.impala` or `.gazl`
  as UTF-8 will corrupt high bytes.
- **A failed `compile` overwrites the output file** with `Error: <message>` rather than leaving the
  previous good `.gazl` in place.
- **`OK (compile-only)` in the gate output means not link-checked**, not passed — 45 of the 94 goldens
  need a host to link. And `GAZLCmd` stops at the first unresolved symbol, so a host name near the top
  of a module waves the rest of it through.
- A failing `runJspegTests.js` writes what it actually got to `../tests/impala/erroneous/<name>.gazl`.
  That is the diff target, not a bug.
