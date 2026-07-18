# JSPEG Future

> Status: analysis and direction, written alongside the Impala 2.0 design (`docs/Impala2.md`).
> JSPEG is good enough to build Impala 2.0 on as-is; this document records its three structural
> problems, what fixing each would take, and what each fix changes in `impala/impala.jspeg`.
> Everything here is verifiable against the existing parity harness — that harness is what makes
> any of these changes safe to attempt.

## Problem 1: Actions run during backtracking — side effects don't undo

### Mechanics

`Sequence` backtracks by restoring the input position only:

```js
p1 && p2 && ... || (_im = (_i > _im ? _i : _im), _i = _b, false)
```

Any action that already ran keeps its effects. Worse, `&`/`!` predicates *evaluate actions too* —
the `Prefix` codegen runs the captured expression (actions included) and restores only `_i`
(`jspeg.jspeg:144–147`). Since the Impala compiler emits code and mutates symbol tables *from
actions*, a speculative parse would emit phantom code.

`impala.jspeg` works around this with a hand-rolled transaction flag: **one** lookahead site sets
`$$parser.dry` (the `Comp` rule's `!Group` probe, used to decide whether a parenthesized thing is
a boolean group or an expression), and **~16 `if (!$$parser.dry)` guards spread across the grammar
exist solely to serve that one site. Two costs: every new action must remember its guard (a
silent-corruption bug when forgotten), and every parenthesized condition is parsed twice (once
dry, once wet).

### What it would take

- **Near term — automate `dry` in JSPEG itself.** Same mechanics, centralized: the code generator
  wraps every action in an implicit `if (!_dry)` and makes `&`/`!` predicates set/restore `_dry`
  automatically. Predicates become side-effect-free by construction.
  *Impact on `impala.jspeg`: pure deletion — the two `$$parser.dry` toggles and all ~16 guards go
  away; no rule changes. Impact on JSPEG semantics: actions inside predicates no longer run, which
  must be audited across grammars (for `impala.jspeg` that is exactly the intent).*
- **Long term — two-phase compilation (parse → AST → emit).** Actions become pure node
  constructors; a separate walk emits GAZL. Backtracking discards half-built nodes (garbage), so
  the side-effect problem ceases to exist rather than being managed. This also unlocks: multi-error
  diagnostics, free lookahead for new syntax (destructuring `x, y = f()` vs expression statement),
  and Impala 2.0's `import` interface mode (parse, take declarations, emit nothing) as a trivial
  variant instead of a special mode.
  *Impact on `impala.jspeg`: rule structure unchanged; every action rewritten from emit-now to
  build-node. This is the "JSPEG 2" moment and should be done once, deliberately — not piecemeal.*

## Problem 2: The `._` holder duality

### Mechanics

JavaScript lacks by-reference variables, so JSPEG models `$$` and tagged names as holder objects
whose `._` field is the value. The action rewriter then applies heuristics: bare `$name` →
`$name._` *unless* followed by `.`, `$$` → `$._`, `$$.` → the holder itself. Whether `._` gets
appended depends on how the name was introduced (tag vs capture) — i.e. **the meaning of action
text depends on distant grammar context**.

The cost is documented by the repository itself: `JSPEG.md` needs a dedicated semantics section,
`docs/jspeg-dollar-report.md` is an architecture review of one sigil, and `impala/RefactorPlan.md`
exists to migrate helpers because "holder/value mistakes" happen in practice. `impala.jspeg`
currently has ~50 `$$.` holder-escape sites.

### What it would take

- **Near term — finish `RefactorPlan.md`.** Return-style helpers (`$$ = binaryRet(...)`) shrink
  the number of places that touch holder semantics at all. Behavior-preserving, fixture-gated,
  already planned milestone by milestone.
- **Long term — value-returning rules.** Change the codegen so a rule is a function returning its
  value; tags become plain local variables; `$$` becomes an ordinary variable; the rewriter's
  heuristics and the `._` convention are deleted outright. Object-valued `$$` still supports field
  mutation (`$$.count = 0` works on a plain object), so the container-style rules (`FuncCall`)
  migrate by initializing `$$ = {...}` instead of relying on a pre-existing holder.
  *Impact on `impala.jspeg`: mechanical migration of the ~50 `$$.` sites plus an audit of tag
  rebinding; retire the dollar-report and most of the `$$` documentation. Pairs naturally with the
  AST move in Problem 1 — in two-phase style, `$$` is just the node under construction and the
  holder question evaporates. Do both in the same breaking step.*

## Problem 3: Performance

### Mechanics

`jspeg.jspeg`'s own header lists the sins: char classes compile to `indexOf` over expanded strings
(`[a-z]` becomes a 26-character string scanned linearly per test), locals are threaded through
closures, and the codegen wraps every expression, prefix, repetition, and action in an IIFE. One
generated rule as exhibit — 4 IIFEs and 2 holder allocations per invocation:

```js
function Bitwise($){var $op=createParserContext(),$r=createParserContext();
  return (function(){var _b=_i;return AddSub($)&&((function(){while((function(){...})());})(),true)
  || (_im=(_i>_im?_i:_im),_i=_b,false)})()};
```

**Measured reality check:** under Node (V8 JIT), this is already fast — `calc.impala` (676 lines)
compiles in ~100 ms and `chess.impala` (1442 lines) in ~155 ms *including* interpreter startup.
The pain is specifically the **NuXJS interpreter path** (the zero-dependency toolchain the build
ships), where closure-per-step codegen is paid at full price — that path is the reason the demo
calls the compiler "slow, a prototype". The double-parse of every condition (Problem 1) also
taxes both paths.

### What it would take

- **Codegen-only, parity-gated, zero grammar changes:**
  - Statement-style output instead of IIFEs — success flags plus labeled breaks; no closure
    allocation per parse step. Biggest single win on NuXJS.
  - Char classes as range comparisons (`c >= 'a' && c <= 'z'`) or per-class lookup tables.
  - Allocate holder objects only for names actually used as containers (shrinks further as
    Problem 2 progresses; disappears with value-returning rules).
- **Pragmatic immediately:** bless `impala/impala.node.js` as the development-loop compiler (it
  already exists and is fast); keep NuXJS as the dependency-free distribution path.
- Packrat memoization is *not* recommended — Impala's grammar is nearly deterministic and the
  measured costs are constant-factor, not asymptotic.

## Adjacent gap: syntax-error quality

Not one of the three, but the Impala 2.0 diagnostics contract (`docs/Impala2.md`, "Diagnostics")
depends on it. Today a failed parse returns only the farthest-failure offset (`_im`) — no expected
tokens, no rule context. Semantic errors via `$$parser.fail` are fine; *parse* errors are not, and
agents writing new 2.0 syntax will hit parse errors constantly.

**What it would take:** collect an expected-set at the failure frontier — terminals that fail while
`_i === _im` push a short description; the runner formats `path:line:col: error[E###]: expected
X, Y, or Z` (offset→line:col mapping is runner-side and trivial). Token rules get display names.
Modest codegen change, no grammar changes, and it can land before any Impala 2.0 work.

## Sequencing

**Nothing here gates Impala 2.0 Step 1.** Step 1 (typed declarations) and the strict-expression
rules need zero JSPEG changes — they are additive grammar work in the existing style. The groups
below express *pairing and deadlines*, not prerequisites; every item is gated only by the parity
fixtures.

| When | Work | `impala.jspeg` impact | Ordering constraint |
|---|---|---|---|
| Any time, independent | Expected-set error reporting; finish `RefactorPlan.md` return-style helpers | none / mechanical helper migration | error reporting should exist by the time 2.0 *ships* (Diagnostics contract); neither blocks Step 1 |
| Before Steps 4/5, *if adopted* | Automatic `dry` for predicates in JSPEG; de-IIFE + char-class codegen | delete the dry toggles and ~16 guards; otherwise none | destructuring lookahead and import interface mode are the two features that lean on the side-effect weakness — the only real ordering edge in this document |
| After 2.0 stabilizes, if ever ("JSPEG 2") | Two-phase AST + value-returning rules, in one deliberate breaking step | rules unchanged; all actions rewritten as node constructors; holders, `dry`, and the `$$` special-casing retired | none — optional end-state |

The closing point from the Impala 2.0 review bears repeating: JSPEG's parity discipline is its best
feature, because it makes every one of these changes — up to and including a full replacement of
the code generator — cheap to verify. The fixtures are the spec.
