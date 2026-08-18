# JSPEG

JSPEG is the JavaScript PEG generator that powers the Impala compiler. It
preserves the familiar PEG syntax inherited from the old PPEG toolchain: rules,
captures, tags, and inline JavaScript actions.

## Current Status

- Self-hosting: `jspeg.jspeg` compiles to `jspegCompiler.js`, and the self-hosted compiler reproduces identical output.
- Impala compiler: `impala.jspeg` compiles to `impalaCompiler.js`; parity checks pass against recorded output.
- Regression suite: arithmetic and tag/capture grammars match across baseline and self-hosted compilers; Impala parity fixtures match expected `.gazl` outputs; compiler state recovers after aborted compiles.

## Repository Layout

Core JSPEG files live in `impala/`:

- `jspeg.jspeg` - self-hosting grammar that yields the JSPEG compiler function `compileJSPEG`.
- `jspegCompiler.js` - checked-in compiler output for `jspeg.jspeg` (CommonJS export).
- `jspegCompilerTests.js` - self-hosting and grammar equivalence tests for JSPEG.
- `updateJSPEG.js` - regenerates or verifies compiler outputs; runs the JSPEG regression suite.

Impala-on-JS (the compiler generated from `impala.jspeg`) has its own usage and tooling. See `design/jspeg/ImpalaJS.md` for those details (CLI, programmatic API, and parity tests).

## Quickstart (JSPEG)

Compile a small grammar and run the resulting parser inside Node.js using the checked-in JSPEG compiler:

```bash
node - <<'NODE'
const path = require('path');
const dir = path.join(__dirname, 'impala');

// Load the JSPEG compiler (CommonJS function export)
const compileJSPEG = require(path.join(dir, 'jspegCompiler.js'));

// Toy grammar: one digit '+' one digit
const grammar = 'root <- a=[0-9] \'+\' b=[0-9] { $$ = (+$a) + (+$b); }\n';
const [ok, generated, index] = compileJSPEG(grammar);
if (!ok || index !== grammar.length) throw new Error('compile failed');

let parse; eval('parse=' + generated);
console.log(parse('4+7'));
NODE
```

If wired correctly, it prints `[ true, 11, 3 ]`.

## Parser Interface

- Return shape: generated parsers return `[ success, value, endIndex ]`.
- Value holder: internally the root rule creates a container and returns its `._` field as the `value` element. This is an implementation detail to carry the `$$` value between actions. You normally do not need to interact with it directly.
  - See `jspeg.jspeg:25`-`26` for the `var _o={_:void 0}` initializer and the return of `_o._`.
- Actions and `$$`: within a grammar, references to `$$` are compiled to read/write that holder; tags and captures populate local temporaries as usual.

## `$$`, Tags, Captures, And `._`

- **Concept:** In PPEG/JSPEG, `$$` is the semantic value threaded through rules. It is both input to a rule and output from that rule after actions run. Tags (`name:expr`) and captures (`name=expr`) introduce named temporaries that actions can read.
- **Why `._`:** JavaScript does not have by-reference variables. JSPEG models each tagged/captured name as a small “holder” object whose `._` property contains its current value. This lets actions either treat a name as a container (`$name.field`) or as the value itself (`$name`), with the latter desugared to `$name._` by the code generator.
- **Codegen rules:**
- Bare `$$` inside actions is rewritten to `$._`, and writing `$$.` (optionally followed by a property) yields the holder `$`, so grammars can opt into container semantics without losing the default value rewrite. See `jspeg.jspeg:80`-`110`.
  - The root parser initializes `var _o={_:void 0}` and returns `_o._` as the second element of the parser tuple. See `jspeg.jspeg:25` and `jspeg.jspeg:26`.
  - Bare `$name` in actions refers to the name’s value and is rewritten to `$name._` unless immediately followed by a `.` (meaning field access on the container). This keeps container vs. value usage unambiguous without extra syntax. See the action rewriter heuristics in `jspeg.jspeg:73`-`126`.
- The tokeniser special-cases both `$$.` (rewritten to holder-qualified names) and bare `$$` (rewritten to `'$._'`). See `jspeg.jspeg:204`-`207`.
- **Tags:** `name: expr` temporarily binds `$$` to `$name` while `expr` runs; on return, `$name` holds the produced value and remains visible to subsequent actions in the rule. This mirrors the original PPEG semantics.
- **Captures:** `name=expr` stores the consumed substring into `$name` before any attached actions run. Actions can then use `$name` (value form) or `$name.…` (container form) within the same rule.

### Examples

- Build a record from pairs (classic PPEG style):
  - Grammar: `pair <- key=ident ':' _ val=number { $$[$key] = $val }`
  - Action intent: read the captured key/value and assign into the current container `$$`.
  - Codegen: `$key` and `$val` are holders; bare uses rewrite to `$key._`/`$val._` so property access and arithmetic “just work”.

- Summation with `$$` as accumulator:
  - Grammar: `sum <- $$:0 ( _ n=[0-9]+ { $$ += +$n } )* !.`
  - Action intent: start at 0, add each number; `$$` is both the incoming accumulator and the outgoing value. Codegen maps `$$` to `$._` consistently.

## Why Keep `._`

- **PPEG parity:** The holder model preserves PPEG’s by-reference flavour for `$$` and tagged names while staying idiomatic in JavaScript.
- **Clarity in actions:** Authors can write `$name` to mean “the value” and `$name.something` to mean “a field on the container,” without extra syntax or helper calls.
- **Deterministic codegen:** The rewriter applies a simple, local rule to add `._` where needed, producing predictable JavaScript and avoiding accidental creation of globals or getters.

JSPEG keeps the old grammar semantics for actions, tags, captures, and `$$` as a
threaded value, adapting the implementation to JavaScript’s object model.

### Legacy Semantics

- Actions: PPEG lets you attach a code block `{ … }` to any expression. Before the block runs, `$$` already contains the value produced by that expression. The action may inspect and modify `$$` directly; helpers available inside an action are `$$` (value), `$$s` (source), `$$i` (index), and `$$parser` (rule table).
- Tags: `name: expr` temporarily rebinds `$$` to `$name` while `expr` runs; afterward `$name` holds the result and stays available until the rule returns.
- Captures: `name=expr` (or `$$=expr`) stores the consumed substring in `$name` before the action runs.
- Threaded value: Every rule receives `$$` as both input and output. Without a tag, sub-rules operate on the same value; tags and captures introduce additional temporaries that actions can read.
- JS adaptation: JSPEG keeps these rules. Because JavaScript lacks by‑reference variables, JSPEG implements names as holders with `._` for their value, and rewrites bare uses of a name to `name._` unless you explicitly access a field on the holder.

## Authoring Hazards

The rewriter is textual and its rules are positional, so several mistakes produce *silently wrong
JavaScript* rather than a grammar error. These are the ones that have actually cost time; each is
verifiable from `impala/jspeg.jspeg` and from the generated `impalaCompiler.js`.

- **Comment syntax depends on where you are.** At grammar level (between rules, between terms)
  comments are `#`-to-end-of-line only — `Comment <- '#' (!EndOfLine .)* …` (`impala/jspeg.jspeg:243`).
  A `/* … */` there is a syntax error, and `updateJSPEG.js` reports it as a byte *index* into the
  grammar with no line number. Inside an action block, `/* */` and `//` are both fine (`PikaComment`,
  `impala/jspeg.jspeg:285`).
- **The action rewriter walks into `/* */` comments.** It special-cases `//` (copied verbatim to the
  newline, `impala/jspeg.jspeg:124-131`) but never `/*`, so `$` tokens inside a block comment are
  rewritten and its line breaks are collapsed. A real example from `impala.jspeg`'s `VarDecl`: the
  source comment says “an end-of-rule `$$i`” and the generated file says “an end-of-rule `_i`”. Prose
  about `$$`/`$$i`/`$name` is safest in a `#` comment above the rule, or in a `//` comment.
- **To hand the holder to a helper, write `$$.` — not `$$`.** A bare `$$` compiles to `$._`, the
  *value slot*, which is usually still unset when an action starts; `helper($$, …)` therefore passes
  `undefined`-ish and every field the helper writes is silently lost. The trailing dot is the escape
  hatch: `helper($$., …)` compiles to `helper($, …)` and hands over the holder itself, so shared
  field-assignment *can* be factored out. Getting this wrong is quiet — the symptom is a downstream
  consumer reading `undefined` out of a rule that looked like it populated fine. (`$$.field` compiles
  to `$.field`, a property on that same holder.)
- **Whether `$name` gains `._` depends on how the name was introduced, earlier in the file.** A name
  registered as a tag gets `._`; a capture does not (`impala/jspeg.jspeg:118-122`). The meaning of
  action text therefore depends on distant grammar context — see `JSPEGFuture.md:93-96`.
- **Before reaching for a new rule, check whether one exists** — and before merging two that look
  alike, measure. `TypeDeclr` (optionally-named) and `VarDecl` (required-name) already cover both
  declarator shapes, so a third rule written for an extern prototype's return was deleted once
  `TypeDeclr` was found. But they are *not* redundant: their `words` differ for a funcptr type
  (`TypeDeclr` sets 1, `VarDecl` leaves it undefined), and making them agree reds four goldens. Reuse
  the rule whose fields the call site actually reads — the extern-return site reads only
  `type`/`elem`/`struct`, which is why `TypeDeclr` is safe there.

## Regenerating Compilers

Use `updateJSPEG.js` to keep generated artifacts in sync with their grammars. It also runs the JSPEG regression suite.

```bash
# From anywhere
node impala/updateJSPEG.js          # rebuild jspegCompiler.js and impalaCompiler.js
node impala/updateJSPEG.js --check  # verify checked-in files are current
```

`--check` is CI-friendly: it does not rewrite files and exits non‑zero if regeneration is needed. After regeneration (or `--check`), tests run automatically inside the script.

You can also run the full repository build and regression tests:

```bash
timeout 180 ./build.sh
```

## Using the Impala Compiler (JS)

For using the JavaScript Impala compiler (CLI, API, and tests), see `design/jspeg/ImpalaJS.md`.

## Running the JSPEG Tests

- `node jspegCompilerTests.js` - verifies `jspegCompiler.js` matches `jspeg.jspeg`, and that a self-hosted compile reproduces identical output.

Impala parity tests and the Impala CLI are documented in `ImpalaJS.md`.

## Programmatic Notes

JSPEG began as a direct translation of the old PPEG grammar into JavaScript. The
JavaScript action library embedded in `impala.jspeg` mirrors the legacy helper
set so grammar actions keep the established semantics without keeping the old
implementation in the active tree.
