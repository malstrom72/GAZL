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

Generated parsers return `[ success, value, endIndex ]`. Parsing starts at the rule named
`root`, so every grammar needs one.

## `$$`, Tags, And Captures

`$$` is the value a rule produces. Inside an action it is an ordinary variable: read it, assign
it, mutate its fields. A rule matches or it does not — that is the boolean the parser tracks —
and `$$` is what it leaves behind when it does.

- **Sub-rules share your `$$`.** Reference a rule without a tag and it works on the same value
  you do, so whatever it leaves is what you see next. A rule with no action of its own therefore
  passes its sub-rule's value straight up.

- **`name: expr` (tag)** runs `expr` against a *fresh* value, then puts the result in `$name` and
  leaves your `$$` untouched. Use it when you need a sub-rule's value as well as your own.

- **`name = expr` (capture)** puts the *matched text* in `$name`. `$$ = expr` captures the text
  into `$$` itself.

- **`$name` is the value.** `$name.field` is a field on it. Both mean exactly what they look like.

Also available inside an action: `$$s` (the whole source string), `$$i` (the current index), and
`$$parser` (the helper/rule table).

### Examples

Build a record from key/value pairs — `$$` is the record, `$key`/`$val` are the captured text:

```
pair <- key=ident ':' _ val=number  { $$[$key] = $val }
```

Accumulate — `$$` carries the running total across the loop. Parsing `"1 2 3"` gives `6`:

```
root <- { $$ = 0 } ( _ n=[0-9]+ { $$ += +$n } )* _ !.
```

Take a sub-rule's value while keeping your own — `$t` gets `TypeBase`'s value, `$$` stays yours:

```
VarDecl <- { $$ = {} } t:TypeBase id:Identifier  { $$.type = $t; $$.name = $id }
```

### How it is implemented

Enough to read the generated file or debug a miscompile:

- A rule compiles to `function Rule()` — no parameters — returning `true`/`false` for match.
- `$$` compiles to `_val`, one module-level variable; `$$.field` to `_val.field`. That is why an
  untagged sub-rule shares your value: it is literally the same variable.
- A tag saves `_val`, gives the sub-rule a fresh slot, then restores — on failure as well as
  success, since backtracking rewinds the input but not `_val`.
- `$$s`/`$$i` compile to `_s`/`_i`.

## Authoring Hazards

The rewriter is textual and its rules are positional, so several mistakes produce *silently wrong
JavaScript* rather than a grammar error. These are the ones that have actually cost time; each is
verifiable from `impala/jspeg.jspeg` and from the generated `impalaCompiler.js`.

- **Comment syntax depends on where you are.** At grammar level (between rules, between terms)
  comments are `#`-to-end-of-line only — `Comment <- '#' (!EndOfLine .)* …` (`impala/jspeg.jspeg:241`).
  A `/* … */` there is a syntax error, and `updateJSPEG.js` reports it as a byte *index* into the
  grammar with no line number. Inside an action block, `/* */` and `//` are both fine (`PikaComment`,
  `impala/jspeg.jspeg:283`).
- **The action rewriter walks into `/* */` comments.** It special-cases `//` (copied verbatim to the
  newline, `impala/jspeg.jspeg:122-129`) but never `/*`, so `$` tokens inside a block comment are
  rewritten and its line breaks are collapsed. A real example from `impala.jspeg`'s `VarDecl`: the
  source comment says “an end-of-rule `$$i`” and the generated file says “an end-of-rule `_i`”. Prose
  about `$$`/`$$i`/`$name` is safest in a `#` comment above the rule, or in a `//` comment.
- **Never declare `_val`, `_s` or `_i` in an action.** An action body is wrapped in
  `(function(){ … })()`, so a `var` of one of those names shadows the parser variable that `$$`,
  `$$s` and `$$i` compile to. The failure is a *silent miscompile*: the action writes its own local,
  the register keeps a stale value, and nothing errors. The byte-compare gate is what catches it.
- **A rule that builds a record must initialize it.** `$$.count = 0` needs `$$` to already be an
  object. Value rules that act as containers start with `$$ = {}` (see `TypeDeclr`, `VarDecl`,
  `ArrayDecl`, `ExternDecl` in `impala.jspeg`).
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
