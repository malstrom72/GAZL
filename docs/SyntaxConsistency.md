# Syntax consistency audit (2026-07-29)

Status: AUDIT. Every entry was reproduced against the compiler in this tree; nothing here is taken from
another document. Where an entry says "silent", it means the program compiles, assembles and runs, and
produces a different answer than the C it resembles.

Impala looks like C. That is the whole premise of the audit: the surface makes a promise, so every place
the language declines to honour it costs more than it would in a language that looked alien. The
question asked of each difference is therefore **not** "is it defensible?" - most are, individually - but
**"is it paying for something?"**

Three verdicts are used:

- **FORCED** - the transliteration model, single-pass codegen, or GAZL itself requires it.
- **ARBITRARY** - nothing would break if it matched C, or matched its own sibling construct.
- **BUG** - it is not a design difference at all.


## 1. Bugs

### 1.1 `for` over a pointer does not stride - SILENT WRONG DATA

```impala
struct S { int a; int b; int c }
global S array bank[3] = { { 10, 0, 0 }, { 20, 0, 0 }, { 30, 0, 0 } }

p = &global bank[0];
while (...) { printInt(p->a); p = p + 1; }          // 10 20 30
for (p = &global bank[0] to &global bank[3]) { printInt(p->a); }   // 10 0 0 20 0 0 30 0 0
```

`p + 1` and `p[i]` scale by element size; `for` steps one WORD. Nine iterations instead of three, reading
`.b` and `.c` as though they were `.a`. No diagnostic.

This is the exact invariant Phase 2a established ("pointer arithmetic is in elements") and
`tests/impala/sources/pointerStride.impala` pins - the fixture proves the rule while the loop that most
naturally expresses the same walk breaks it. `FORp` (`docs/InstructionSet.md:225`) has no stride operand,
so the fix is to stop emitting `FORp` for a typed pointer with a multi-word element and emit
`ADDp $p $p #.z.S` plus a compare-branch instead. One extra instruction per iteration, only for the
element sizes that are broken today. A one-word element (`int pointer`) and an untyped `pointer` are
already correct and should keep using `FORp`.

### 1.2 `!` inverts the C reading - SILENT WRONG BRANCH

`!` is not a unary operator. It lives at the *condition* level (`Comp <- '!' Comp`), BELOW comparison, so
`!x` alone is a syntax error but `!x == 2` parses as `!(x == 2)`. With `x = 1`, C evaluates `(!1) == 2` to
false; Impala takes the branch. Verified at runtime.

The fix shape already exists: this is what `E101`/`E102` do for mixed bitwise operators. Require
parentheses - `!(a == b)` or `(!a) == b` - and reuse the strict-expression machinery.

### 1.3 `--x` is a silent no-op

`--x` parses as `-(-x)` and compiles to two negations. A C programmer typing a decrement gets identity,
with no diagnostic. `x++`, `++x` and `x += 1` are all bare `E001`. Rejecting the unspaced `--` (accepting
`- -x`) costs nothing and catches a real mistake.

### 1.4 Duplicate label in one function

Accepted by the compiler; the assembler then fails at load with `Symbol already defined: x` and a GAZL
line number. Same class as the diagnostics closed in `docs/CompileTimeHardening.md`, and fully decidable -
the label map already exists in `processBranches`.

### 1.5 Element types unchecked in pointer difference and comparison

`ip - fp` (int-element minus float-element) and `ip == fp` are accepted with no check, while `ip = fp` is
correctly `E201`. The element-type rule is enforced on assignment and call arguments but not on these.

### 1.6 `&` defeats `readonly`

`global k = 1` on a `readonly int` is `E404`, but `p = &global k` is accepted and writes through `p` are
unchecked. Same for `&global ro[0]`.

### 1.7 A string literal is a writable lvalue

`"abc"[0] = 1;` compiles. It fails at GAZL *load* with `Incompatible types: .s_abc_...` - no source
location, no code.


## 2. Silent differences from C (accepted, different meaning)

Beyond 1.1-1.3 above:

| Source | C | Impala |
|---|---|---|
| `010` | octal 8 | **decimal 10** |
| `case 1, 2:` | comma operator - means `case 2` | both values |
| `switch` arm end | falls through | does not |
| falling off a `returns` function | undefined | returns the **previous call's leftover value** - there is no definite-assignment check |
| `do {…} while (c)` | `;` required | no `;`; a written one is a separate empty statement |
| `for (i = 3 to 0)` | counts down | runs **zero** times |
| nested `for` reusing one variable | shadows | silently shares |
| `goto` into a `for` body | - | legal; skips the init AND the entry guard |
| a `const` upper bound in a `switch` range | - | disables `E444`; an out-of-range case becomes silent dead code |

`010` is worth leaving alone - C's octal rule is itself a misfeature and adopting it would be worse - but
it belongs in the language reference.


## 3. Arbitrary inconsistencies

**Separators.** Struct fields take `;` (optional - juxtaposition is legal, and a trailing one is allowed);
`locals`, parameters, case lists and brace initializers take `,` (mandatory, trailing forbidden). Two
declaration lists over the same declarator grammar with two mutually exclusive conventions, each
rejecting the other's with a bare `E001`.

**Two initializer grammars.** `InitList` (flat, scalar arrays) and `Braced` (recursive, structs) have
identical punctuation and are mutually exclusive - flat-for-struct and nested-for-scalar are both `E422`.
One recursive rule could serve both; the split is an implementation artifact.

**Parameter and return names.** `functype` allows types-only; `function` and `extern function` require a
name on every parameter *and* on the return. Required-in-`function` is FORCED (the name is the return
slot you assign); required in an `extern` prototype is arbitrary - it has no body either.

**`extern const` has no keyword form.** Every other extern is spelled with `extern` (`extern function`,
`extern native`, `extern struct`, `extern int x`, `extern int array A`). An extern const is spelled by
*omitting the initializer* (`const int C;`), and `extern const int C;` is `E001`.

**`const` accepts a narrower type grammar than every other position** - no struct, no struct pointer, no
named functype. `ConstDecl` reads `BASE_TYPE` where its siblings moved to `TypeBase`.

**`sizeof` takes only a parenthesized bare type name.** Not an expression, not a modified type
(`sizeof(int pointer)` is `E001`), and not an array variable - `sizeof(a)` reports `Unknown type a`. There
is no way to get an array's word count, which is the main thing C programmers reach for `sizeof` to do.

**`assert(c);` and `goto l;` swallow their own `;`; `copy(n from p to q)` does not.** Three call-shaped
builtins, two terminator rules. `copy` also uses the keywords `from`/`to` where every other argument list
uses `,`.

**Keyword ordering.** Of ~45 rejected orderings tested, **none is forced by parsing** - PEG ordered choice
could accept either form in every case. Note that `global`/`readonly`/`temporary` are not an ordering
question at all: they name mutually exclusive GAZL sections, so no order can be legal.

**`global` is required, forbidden, or a silent no-op** depending on which table a name is in: required for
globals/readonly/temporary/extern data, `E403` for locals, and **silently accepted and ignored** for
functions, natives and consts. The third state is never diagnosed.

**`export` on a valueless `const` is silently dropped.** `export const int C;` and `const int C;` emit
byte-identical output. The two keywords are contradictory - `export` says "I provide this", a valueless
const says "someone else does" - so this should be an error, not a no-op.


## 4. Forced differences worth documenting

- **Conditions are a separate grammar**, not expressions. Every leaf must be `Expr COMP_OP Expr`. No
  truthiness, and a comparison is not a value - there is no bool. Forced by GAZL branching on comparisons.
- **No declarations in a body.** All locals are frame slots fixed at entry, declared in `locals`.
- **Casts never convert.** `(int) fl` is `E302`; use `itof`/`ftoi`. int<->pointer conversion does not exist.
- **Arrays exist in no signature position** - not as a parameter, functype parameter, return, or cast.
  `int pointer p` covers the use case, but an extent can never appear in a signature.
- **Prefix operators bind tighter than every binary operator**, so `abs x - 1` is `(abs x) - 1`. Since
  `abs(x)` looks like a call, `abs(x - 1)` and `abs x - 1` look interchangeable and are not.


## 5. The diagnostics story

This is the finding that cuts across everything above. Diagnostic quality is **bimodal**.

Precise, with an actionable note: `E101`, `E102`, `E201`, `E413` (forward reference), `E430`, `E442`,
`E443`, `E444`, `E445`, the readonly-array-element `E404`, and the `E403` "is a global" note.

Everything else is a bare `E001: syntax error`, or a bare `E404: Invalid lvalue` / `E302: Invalid type
(T)` / `E301: Invalid types (T and U)` whose wording names internal operand types rather than the user's
construct (`x[0]` on an int reports `Invalid types (int and int)`).

The three tokens a newcomer or a code generator is most likely to emit all land in the second group:

| Typed | Diagnostic |
|---|---|
| `return v;` | `E403 Undeclared identifier: return`, caret on `v` |
| `break;` in a loop | `E403 Undeclared identifier: break`, caret on the `;` |
| `break;` in a `case` arm | bare `E001` - **a different diagnostic for the same mistake** |
| `continue;` | `E403 Undeclared identifier: continue` |
| `int i;` in a body | bare `E001`, caret on the space after `int`, no mention of `locals` |
| `if (x)` | bare `E001`, caret on `)` |
| `x += 1` / `x++` | bare `E001` - the rejection is deliberate and reasoned in `docs/Impala2.md`, and none of that reaches the user |
| `1e6` | `E303 cannot assign int to float` - a *type* error for a *lexical* mistake |

Making these reserved words with dedicated messages is the single highest-value change available, and it
is independent of every other item here.

### Caret defects

| Code | Defect |
|---|---|
| `E445` | position recorded after `';'_` consumed trailing whitespace - lands on the next line |
| `E403` | lands on the token *after* the identifier |
| `E305` | lands after the loop variable, and the message never says parameters are non-modifiable |
| `E422`, `E428` | land on line 1 column 1 of the **next declaration** |
| `E001` before `else` | lands on the space after `else`, not on the offending `;` |

The `E422`/`E428` class is the one `$$parser.declOffset` was introduced to solve for the metadata path
(`impala/impala.jspeg:3464-3466`); it was never applied to these error paths.


## 6. Suggested order

1. **1.1 `for` stride** - silent wrong data, and it breaks an invariant the project has already ratified.
2. **`return` / `break` / `continue` as reserved words** with real messages (see `docs/ParkedFeatures.md`
   for the `return` design question - bare `return;` only, because 3.0 restores multi-return).
3. **1.2 `!` precedence** - reuse the `E101` paren requirement.
4. **1.4-1.7** - small, each decidable at compile time.
5. The caret defects in section 5, as one pass.
6. The arbitrary separator/ordering items in section 3 - cheap individually, but each is a grammar change
   with fixture churn, and none of them produces wrong output. Worth doing only alongside a diagnostics
   pass that makes the current rules explain themselves.
