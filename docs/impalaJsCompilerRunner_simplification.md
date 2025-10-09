# impalaJsCompilerRunner.js simplification ideas

## Potential refactors

- **Share newline scanning logic** – both `findLineBounds` and `renderErrorContext` walk the source string character by character to find line starts, ends, and printable fragments. Replacing the manual scans with a shared helper that slices once or leverages `source.slice(0, index).split(/\r\n|\n|\r/)` would reduce duplicated control flow and make the intent clearer.【F:impala/jspeg/impalaJsCompilerRunner.js†L57-L152】
- **Lean on `String.prototype.replace` for tab handling** – `retabulate` currently maintains explicit `tabIndex`, `outPosition`, and a `while` loop to emit tabs and spaces. Converting tabs via `line.replace(/\t/g, callback)` (where the callback computes the next stop) would eliminate the manual loop state and clarify that the goal is just to expand tabs to aligned indentation.【F:impala/jspeg/impalaJsCompilerRunner.js†L10-L41】
- **Collapse `withGlobalBindings` bookkeeping** – the helper first builds a `Set`, then populates a `Map`, and later iterates it twice. Capturing the previous values in a plain object and iterating a single list of keys would achieve the same restore semantics with less scaffolding.【F:impala/jspeg/impalaJsCompilerRunner.js†L183-L211】
- **Normalize compiler option plumbing** – `compileWithJsImpala` contains several ternaries that handle `compilerPath`, `compilerSource`, and `sourceName`. Destructuring `options` up front (e.g., `const { compilerPath, compilerSource, sourceName, retabulate = true, trailingNewline } = options;`) would remove repetitive property checks and make the defaulting behavior more declarative.【F:impala/jspeg/impalaJsCompilerRunner.js†L228-L280】

These cleanups avoid altering behavior while making the runner easier to follow and maintain.
