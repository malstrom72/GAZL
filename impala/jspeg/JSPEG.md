# JSPEG

JSPEG is a JavaScript port of the original [PPEG](PPEG.md) parser generator that powered the Impala toolchain.  The port keeps the familiar Parsing Expression Grammar (PEG) surface syntax—rules, captures, tags, and inline actions—but executes every action inside Node.js instead of the PikaScript runtime.  The long term objective is to retire the PPEG toolchain entirely and ship a 100% JavaScript workflow for Impala and any future grammars maintained in this repository.

## Repository layout

All JSPEG-related sources live in `impala/jspeg/`:

- `jspeg.jspeg` – JavaScript flavoured self-hosting grammar.  Compiling it yields `compileJSPEG`, the JSPEG compiler function.
- `jspegCompiler.js` – checked-in output of the grammar above; used by tests and update scripts.
- `impala.jspeg` – Impala language grammar rewritten with JavaScript actions.
- `impalaCompiler.js` – generated Impala compiler produced by JSPEG.
- `jspegTest.jspeg` / `tagCaptureTest.jspeg` – small grammars used by the regression suite.
- `updateJSPEG.js` – Node script that regenerates the compiler artifacts or checks that they are current.
- `PPEG.md` – reference documentation for the legacy PikaScript implementation.
- `TODO.md` – roadmap for finishing the port.

The folder also contains the historical `.ppeg` sources and helper scripts from the PikaScript implementation.  They are kept for reference while the JavaScript port is built out.

## Quickstart

The snippet below demonstrates how to compile a simple grammar and run its parser entirely inside Node.js.  It assumes you already built the repository once so `impala/jspeg/jspegCompiler.js` exists.  The generated compiler now exposes a CommonJS entry point, so a plain `require()` returns the `compileJSPEG` function directly.

```bash
node - <<'NODE'
const path = require('path');
const dir = path.join(__dirname, 'impala/jspeg');

// load the checked-in JSPEG compiler and obtain compileJSPEG
const compileJSPEG = require(path.join(dir, 'jspegCompiler.js'));

// compile a toy grammar that sums one digit plus another digit
const grammarSource = `
root <- a=[0-9] '+' b=[0-9] { $$ = (+$a) + (+$b); }
`;
const [, generated] = compileJSPEG(grammarSource);
let parse;
eval('parse=' + generated);

const input = '4+7';
const [ok, value, index] = parse(input);
console.log('success:', ok, 'value:', value, 'index:', index);
NODE
```

When everything is wired up correctly the script prints `success: true value: 11 index: 3`.

## Regenerating the compilers

Use `updateJSPEG.js` to keep the generated artifacts in sync with their grammars:

```bash
cd impala/jspeg
node updateJSPEG.js          # rebuild jspegCompiler.js and impalaCompiler.js
node updateJSPEG.js --check   # verify the checked in files are current
```

The `--check` mode is ideal for continuous integration because it avoids rewriting files and exits with a non-zero status when regeneration would change any output.

After rebuilding the compilers, run the regression suite from the repository root:

```bash
bash build.sh
```

The build regenerates PikaCmd, exercises the Impala tests, and runs all JSPEG checks, including the self-hosting guard inside `jspegCompilerTests.js`.

## Relationship to PPEG

JSPEG started as a line-for-line translation of the PPEG grammar into JavaScript.  The original `.ppeg` files remain in the folder so that it is easy to diff behaviours and migrate outstanding runtime helpers.  The [`TODO.md`](TODO.md) file tracks the remaining gaps—mostly around specialised actions, performance optimisations, and parity checks between the generated Impala compilers.

The [PPEG documentation](PPEG.md) captures how the PikaScript toolchain operates today.  Keep it handy when reasoning about the port: every helper or action mentioned there needs a JavaScript counterpart before the PPEG workflow can be retired.

