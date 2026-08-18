# JSPEG Future

> Status: analysis and direction, written alongside the Impala 2.0 design (`docs/impala/Impala2.md`).
> JSPEG is good enough to build Impala 2.0 on as-is; this document records its three structural
> problems, what fixing each would take, and what each fix changes in `impala/impala.jspeg`.
> Everything here is verifiable against the existing parity harness - that harness is what makes
> any of these changes safe to attempt.

## Problem 1: Actions run during backtracking - side effects don't undo

### Mechanics

`Sequence` backtracks by restoring the input position only:

```js
p1 && p2 && ... || (_im = (_i > _im ? _i : _im), _i = _b, false)
```

Any action that already ran keeps its effects. Worse, `&`/`!` predicates *evaluate actions too* -
the `Prefix` codegen runs the captured expression (actions included) and restores only `_i`
(`jspeg.jspeg:144-147`). Since the Impala compiler emits code and mutates symbol tables *from
actions*, a speculative parse would emit phantom code.

`impala.jspeg` avoids hitting this at every backtrack point only because its actions are hand-placed
at **sequence ends** - they fire after an alternative has committed, so ordered-choice / `?` / `*`
backtracking unwinds *before* any action runs. That discipline breaks in exactly two spots, patched
two different ways:

1. **The `Comp` `!Group` probe** (deciding whether a parenthesized thing is a boolean group or an
   expression) - a predicate *forces* its sub-parse's actions to run and then discards them. Handled
   by a hand-rolled transaction flag: **one** site sets `$$parser.dry` and **~16 `if (!$$parser.dry)`
   guards** spread across the grammar serve it.
2. **The `FuncCall` prologue**, which borrows the call window *before* parsing the arguments - a
   mid-sequence effect that cannot sit at the end, so it cannot use `dry` (the borrow must really
   happen for the wet parse). Patched instead by **hard-failing** (E442) rather than allowing a
   backtrack out of a half-borrowed call.

So the grammar already carries **two** speculation mechanisms plus a positional convention holding the
rest together. Costs of the `dry` half: every new action must remember its guard (silent corruption
when forgotten), and every parenthesized condition is parsed twice (once dry, once wet).

### What it would take

The framing that matters: `dry`, and every variant of it, **manages** speculation inside an emit-now
model - and PEG speculates pervasively, so that is whack-a-mole. `FuncCall` was already a second mole,
patched differently from the first. Two of the options below only manage; **only the last removes the
problem.**

- **Does not generalize - automate `dry`, or swap the receiver.** Two tempting shortcuts, both dead
  ends, for the same reason. *(a) Automate `dry` in the codegen:* the generator wraps every action in
  an implicit `if (!_dry)` and predicates set/restore `_dry` - deletes the ~16 guards, but teaches the
  PEG *code generator* Impala's dry/wet semantics (the wrong layer), and still only covers the "don't
  run it" case. *(b) Swap the receiving object to a no-op twin* while a probe runs (route all effects
  through one `sb`, point it at a do-nothing board during speculation) - keeps the generator
  semantics-free, but covers even less. Both only answer **"don't perform it"** (the predicate case).
  Neither answers **"already performed it, now undo it"** - which is what ordered-choice / `?` / `*`
  backtracking needs, and which is the *majority* of PEG speculation. By the time such an alternative
  fails, the effect already happened, so a no-op board is unreachable. This is `FuncCall` restated:
  there the borrow must really happen, so no-op is useless and the grammar hard-fails instead of
  undoing.
- **Generalizes, but IS two-phase - a recording board.** The seam in (b) is right; the *twin* is
  wrong. Swap to a board that does not emit but **buffers** each effect into the current rule,
  discarding the buffer on backtrack and flushing it to the parent on commit - that covers all
  speculation uniformly. But an action that appends to a per-rule buffer instead of emitting, thrown
  away if the rule fails, is two-phase in miniature. So this is not a separate option; it is the
  incremental on-ramp to the one below. *Precondition either way, verified 2026-08-11: `$$parser.x`
  FLATTENS to bare closure locals (`var $$parser = {}` is a near-empty shell; `dry`/`binaryOp`/
  `symbols`/`metacode` all generate as plain locals inside the compiler function), so there is no
  object to swap or buffer on today - step 0 is moving parser state onto a real object, the same "thin
  the fat inline actions into `$$parser`" surface `RefactorPlan.md` and collect mode already want.*
- **Removes the problem - two-phase compilation (parse → AST → emit).** The only option where
  **nothing observable happens until a rule commits.** Actions become pure node
  constructors; a separate walk emits GAZL. Backtracking discards half-built nodes (garbage), so
  the side-effect problem ceases to exist rather than being managed - both `dry` and the `FuncCall`
  hard-fail dissolve, because the borrow and the emit move to the walk, after the parse succeeded.
  This also unlocks: multi-error
  diagnostics, free lookahead for new syntax (destructuring `x, y = f()` vs expression statement),
  and Impala 2.0's `import` interface mode (parse, take declarations, emit nothing) as a trivial
  variant instead of a special mode. *That last one is a convenience, **not** a dependency: import
  cycles need only declaration-level two-phase, which `design/impala/Impala2Slices.md:155-163` scopes as a
  mode on `$$parser` and explicitly separates from this rework. Do not wait for it to fix cycles.*
  *Impact on `impala.jspeg`: rule structure unchanged; every action rewritten from emit-now to
  build-node. Note this is **not** a change to JSPEG - nothing in the code generator stops a grammar
  from building nodes in its actions today, so it is a rewrite of `impala.jspeg` and sits with the
  grammar author alone. It pairs well with JSPEG 2 (Problem 2), which makes a node simply the value
  a rule returns, but neither waits for the other.*

## Problem 2: The `._` holder duality

### Mechanics

JavaScript lacks by-reference variables, so JSPEG models `$$` and tagged names as holder objects
whose `._` field is the value. The action rewriter then applies heuristics: bare `$name` →
`$name._` *unless* followed by `.`, `$$` → `$._`, `$$.` → the holder itself. Whether `._` gets
appended depends on how the name was introduced (tag vs capture) - i.e. **the meaning of action
text depends on distant grammar context**.

The cost is documented by the repository itself: `JSPEG.md` needs a dedicated semantics section,
one sigil earned a whole architecture review of its own (`docs/jspeg-dollar-report.md`, retired
2026-08-05 once this section chose a direction none of its six options proposed), and
`design/jspeg/RefactorPlan.md` exists to migrate helpers because "holder/value mistakes" happen in practice. `impala.jspeg`
currently has ~126 `$$.` holder-escape sites.

### What it would take

- **Near term - finish `RefactorPlan.md`.** Return-style helpers (`$$ = binaryRet(...)`) shrink
  the number of places that touch holder semantics at all. Behavior-preserving, fixture-gated,
  already planned milestone by milestone.
- **Long term - value-returning rules. This is "JSPEG 2"** - the one breaking change to JSPEG
  itself, and it is about `$$`, nothing else. Change the codegen so a rule is a function returning
  its value; tags become plain local variables; `$$` becomes an ordinary variable; the rewriter's
  heuristics and the `._` convention are deleted outright. Object-valued `$$` still supports field
  mutation (`$$.count = 0` works on a plain object), so the container-style rules (`FuncCall`)
  migrate by initializing `$$ = {...}` instead of relying on a pre-existing holder.
  *Impact on `impala.jspeg`: mechanical migration of the ~126 `$$.` sites plus an audit of tag
  rebinding; retire most of the `$$` documentation. Pairs naturally with the
  AST move in Problem 1 - in two-phase style, `$$` is just the node under construction and the
  holder question evaporates - so doing both in one breaking step is convenient, not required.*

## Problem 3: Performance

### Mechanics

`jspeg.jspeg`'s own header lists the sins: char classes compile to `indexOf` over expanded strings
(`[a-z]` becomes a 26-character string scanned linearly per test), locals are threaded through
closures, and the codegen wraps every expression, prefix, repetition, and action in an IIFE. One
generated rule as exhibit - 4 IIFEs and 2 holder allocations per invocation:

```js
function Bitwise($){var $op=createParserContext(),$r=createParserContext();
  return (function(){var _b=_i;return AddSub($)&&((function(){while((function(){...})());})(),true)
  || (_im=(_i>_im?_i:_im),_i=_b,false)})()};
```

**Measured reality check:** under Node (V8 JIT), this is already fast - `calc.impala` (676 lines)
compiles in ~100 ms and `chess.impala` (1442 lines) in ~155 ms *including* interpreter startup.
The pain is specifically the **NuXJS interpreter path** (the zero-dependency toolchain the build
ships), where closure-per-step codegen is paid at full price - that path is the reason the demo
calls the compiler "slow, a prototype". The double-parse of every condition (Problem 1) also
taxes both paths.

### What it would take

- **Codegen-only, parity-gated, zero grammar changes:**
  - Statement-style output instead of IIFEs - success flags plus labeled breaks; no closure
    allocation per parse step. Biggest single win on NuXJS.
  - Char classes as range comparisons (`c >= 'a' && c <= 'z'`) or per-class lookup tables.
  - Allocate holder objects only for names actually used as containers (shrinks further as
    Problem 2 progresses; disappears with value-returning rules).
- **Pragmatic immediately:** bless `impala/impala.node.js` as the development-loop compiler (it
  already exists and is fast); keep NuXJS as the dependency-free distribution path.
- Packrat memoization is *not* recommended - Impala's grammar is nearly deterministic and the
  measured costs are constant-factor, not asymptotic.

## Adjacent gap: syntax-error quality

Not one of the three, but the Impala 2.0 diagnostics contract (`docs/impala/Impala2.md`, "Diagnostics")
depends on it. Today a failed parse returns only the farthest-failure offset (`_im`) - no expected
tokens, no rule context. Semantic errors via `$$parser.fail` are fine; *parse* errors are not, and
agents writing new 2.0 syntax will hit parse errors constantly.

**What it would take:** collect an expected-set at the failure frontier - terminals that fail while
`_i === _im` push a short description; the runner formats `path:line:col: error[E###]: expected
X, Y, or Z` (offset→line:col mapping is runner-side and trivial). Token rules get display names.
Modest codegen change, no grammar changes, and it can land before any Impala 2.0 work.

## Adjacent gap: the PikaScript emulation layer

`impala.jspeg`'s prelude still carries a shim of hand-ported PikaScript builtins - `bake`, `evaluate`,
`replace`, `char`, `ordinal`, `args`, and the `resetQueue`/`queueSize`/`pushBack`/`popBack`/`pushFront`/
`popFront` wrappers over plain Arrays. They were the cheapest possible port from the PikaScript original,
not a design. Most are now removable, but the set splits three ways (call-site counts exclude each
definition itself, verified 2026-08-11):

- **Already dead - `args`, `queueSize`, `pushBack`, `popBack`, `pushFront`, `popFront`.** Zero call
  sites. Pure deletion.
- **Live but a pure alias - `evaluate` (1 site) is `JSON.parse`; `resetQueue` (2 sites) is `q.length = 0`.**
  Inline and delete.
- **Keep - `replace` (11 sites), `char`/`ordinal` (3/2), `find`/`span`/`rspan`.** These are NOT
  builtin-aliases. `replace(s, a, b)` is a GLOBAL replace - `String.prototype.replace` with a string
  argument hits only the first match - so each call site would grow to `s.split(a).join(b)`; the helper
  spells them shorter, not longer (the reverse of what this note used to claim). `char`/`ordinal` carry
  a `& 0xFF` mask over `fromCharCode`/`charCodeAt`. Inlining any of these ADDS lines and loses meaning.

**`bake` is the one that bites - and it turns out to be functionally dead.** Every `$$parser.fail`
message is passed through it, and it **`eval`s whatever sits between braces**. This note used to say that
is how `{$type1}` interpolation in `typeError` works - it is NOT: the `typeError` helper does its own
substitution with `replace(desc, '{$type1}', verboseType(...))` BEFORE it calls `fail`, so by the time
`bake` sees the message the braces are already gone. `bake`'s eval therefore never fires for
interpolation; its only live effect is the HAZARD that a stray `{…}` in a diagnostic is executed as
JavaScript. A struct row (`struct S { a : int }`) in an error message throws a `SyntaxError` from inside
`eval` with nothing in the stack naming the real cause, so `E438` mangles its rows `{`->`(` to dodge it,
and one other site (the E407 "Expected constant" message) redundantly double-bakes a brace-free string.
Both are workarounds for a routine that was never needed.

**What it would take:** delete `bake` outright - `fail` uses its `error` argument directly, the E438
row-mangling and the E407 double-bake both drop out (E438's message regains its true `{ }` rendering),
and NO `format(template, values)` replacement is required, because the interpolation the shim was
credited with is already `replace`'s job. Then inline the two pure aliases and delete the six dead
wrappers. Mechanical, fixture-gated, behaviour-preserving, and a net line reduction. The messages are
covered by `impala/jspegCompilerTests.js` with substring assertions, so the `{ }` restoration surfaces
as a golden check rather than a test break. NuXJS-safe by construction: every builtin the inlines lean
on (`JSON.parse`, `arr.length =`, `split`/`join`, `fromCharCode`) is already exercised by the shim code
being removed.

## Sequencing

**Nothing here gates Impala 2.0 Step 1.** Step 1 (typed declarations) and the strict-expression
rules need zero JSPEG changes - they are additive grammar work in the existing style. The groups
below express *pairing and deadlines*, not prerequisites; every item is gated only by the parity
fixtures.

| When | Work | `impala.jspeg` impact | Ordering constraint |
|---|---|---|---|
| Any time, independent | Expected-set error reporting; finish `RefactorPlan.md` return-style helpers; retire the PikaScript emulation shim (delete `bake` - a vestigial eval hazard - plus six dead wrappers; inline `evaluate`/`resetQueue`) | none / mechanical; net line reduction | error reporting should exist by the time 2.0 *ships* (Diagnostics contract); none blocks Step 1 |
| Before Steps 4/5, *if adopted* | Retire the speculation patches (`dry` + the `FuncCall` hard-fail) - flag/receiver tricks only *manage* it and don't generalize (see Problem 1); the general fix is two-phase, whose on-ramp is a recording board (needs state on a real object first, a superset of `RefactorPlan`); de-IIFE + char-class codegen | rule structure unchanged; actions move from emit-now to build-node | destructuring lookahead and import interface mode are the two features that lean on the side-effect weakness - the only real ordering edge in this document |
| After 2.0 stabilizes, if ever ("JSPEG 2") | Value-returning rules - the `$$`/holder change, and the only breaking change to JSPEG itself | mechanical migration of the ~126 `$$.` sites; holders and the `._` convention retired | none - optional end-state |
| Independent of JSPEG entirely | Two-phase AST in `impala.jspeg` (actions build nodes, a walk emits) | rules unchanged; every action rewritten as a node constructor | not a JSPEG change at all - possible today; convenient to do alongside JSPEG 2, not gated on it |

The closing point from the Impala 2.0 review bears repeating: JSPEG's parity discipline is its best
feature, because it makes every one of these changes - up to and including a full replacement of
the code generator - cheap to verify. The fixtures are the spec.
