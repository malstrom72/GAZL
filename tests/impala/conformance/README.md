# Impala conformance suite

One small program per section of the language reference, [`docs/impala/Impala.md`](../../../docs/impala/Impala.md),
each with its golden GAZL beside it (`name.impala` and `name.gazl`).

The goldens protect the 1:1 mapping from Impala to GAZL, one construct at a time: a change to how a construct
compiles shows up as a diff in the one small file that exercises it, rather than somewhere inside a firmware.
`tests/impala/sources` holds the regression tests and the real programs; this folder holds the language.

Each program:

- names its section in its header, `Conformance: Impala.md#anchor`;
- is run by `impala/runJspegTests.js` with the arguments of its `Expected (GAZLCmd ...)` line, and must print exactly
  what that line says and end with status 0. The expected values were written from the reference, not copied from
  a run;
- starts from the same four lines (`DEBUG` and three printing natives), so the goldens differ only where the
  constructs do.

`runJspegTests.js` also checks coverage: every section of the reference from "Lexical elements" to "Expressions"
must be claimed by a program here or listed below, and every claim must name a real section. A new section in the
reference therefore needs a program, or a line here saying why not.

Regenerate the goldens with `node impala/runJspegTests.js makegold`, and review the diff: a golden records what the
compiler emits, right or wrong.

## Exempt sections

- `Impala.md#import-and-export`: needs several source files; `impala/importBuildTests.js` covers imports, export and
  `--dead-strip`.
- `Impala.md#cycles-resolve-in-one-direction`: needs an import cycle; covered by `impala/importBuildTests.js` and
  `tests/impala/sources/importcycle`.
