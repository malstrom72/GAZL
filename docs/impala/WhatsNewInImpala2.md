# What's new in Impala 2.0

This page covers only what changed between Impala 1.0 and 2.0. It assumes you know 1.0 already; if you do
not, read [`Impala.md`](Impala.md) for the language and come back.

Impala 1.0 is a minimal "high-level assembler": four word-sized types (`int`, `float`, `pointer`,
`funcptr`), one composite (`array`), and a near 1:1 mapping to GAZL instructions. 2.0 keeps that mapping -
no hidden passes, no runtime machinery - and spends its additions on letting the type system describe what
a program is already doing.

**Nine new keywords:** `break` `continue` `export` `functype` `import` `inline` `return` `sizeof` `struct`.
Two of those are not features: `break` and `continue` are `E450`, reserved so the compiler can refuse them
with an explanation instead of a syntax error. `inline` IS a feature **on this branch** - an expansion
declares its locals in a GAZL 2 `SCOP`/`ENDS` scope, which is exactly why the Impala 2 line refuses it and
this line does not. See [`Inlining.md`](Inlining.md).

For *why* 2.0 is shaped this way, see [`Impala2.md`](Impala2.md). This page is only the what.

Every code sample here is compiled by `impala/docSamples.js` on each build, and every diagnostic code named
is one a sample actually provokes, so this page cannot drift away from the compiler.

## Structs

```impala
struct Point { int x; int y }
struct Body { Point pos; float mass; int array tags[4] }

global Body b

function move(Body pointer bp, int dx)
{
	bp->pos.x = bp->pos.x + dx;
}
```

Structs nest by value, arrays of structs and array fields both work, whole-struct assignment copies, and
`.`/`->` cost nothing - every offset folds into the addressing at GAZL assembly time, so `bp->pos.x` is a
single instruction rather than a chain of adds.

Field offsets and sizes are emitted as *symbolic* constants (`.o.Body.pos`, `.z.Body`), never baked
numbers. That is what makes the host-owned layout below possible.

Initializers name their fields. A positional list silently changes meaning the moment a field is inserted
or reordered, so it is `E455`:

```impala
global Point origin = { x: 1, y: 2 }
```

Using `.` through a pointer or `->` on a value is `E416`, in both directions.

A `const` is still a single assemble-time word - it becomes one `! DEFi` - so it cannot name a struct
*value* (`E447`) any more than it can name an array (`E001`). It can name a struct **pointer**, because a
pointer is one word: `const Point pointer ORIGIN = &global origin` is fine.

## One subscript, which strides by the element

`a[i]` strides by whatever `a`'s element is - one word for a scalar, `sizeof` the struct for a struct - so
the same spelling works everywhere:

```impala
struct Cell { int v; int w }
global Cell array grid[4]
global int array flat[4]

function read(int i) returns int r
{
	r = global grid[i].v + global flat[i];
}
```

The stride folds at GAZL assembly time whenever the index is constant, so `grid[2]` costs exactly what
`flat[2]` costs. A runtime index into a struct array pays one `MULi`, which you can see in the emitted
`.gazl`: an assemble-time line is prefixed `!` and a runtime one is not.

Arithmetic on a struct pointer is `E307` - move one with `&p[i]` instead. Comparison is untouched, so the
`while (p < end)` walk is the idiom.

## Types say what they point at

```impala
global float array gf[4]
global int pointer p = &global gf[0]
```

That is `E201`, at the declaration and at any later assignment. `int pointer`, `float pointer`,
`int pointer pointer` and `Struct pointer` are all new spellings - in 1.0 a `pointer` was just a word.

Direction matters: typed to untyped is silent, untyped to typed needs a cast. An untyped `pointer`,
`array` or `funcptr` still exists and still promises nothing, so 1.0 code that uses them is unaffected.

## Named function-pointer types

```impala
functype Step(int frame)

function tick(int frame) { }

global Step cb = tick
```

Assigning a function whose signature does not match is `E441`, at a declaration or an assignment. A bare
1.0 `funcptr` is still unchecked - declaring the type is what buys the check. `nullfunc` suits any of them,
and you assign the bare name (`cb = tick`), never `&tick`.

## `import` is linking

```impala
import "lib.impala"
```

The compiler walks the import closure, compiles every unit once, and emits the whole linked program. There
is no header file and no second artifact to drift. `export` marks what the host can see:

```
node impala/impala.node.js compile --dead-strip main.impala main.gazl
```

`--dead-strip` drops every function and data block unreachable from an `export`. **Careful:** with no
`export` anywhere it has no root and empties the module - you get `Code size: 0` and a loader that cannot
find `main`.

## `return`, `sizeof`, `extern struct`

`return;` is new - 1.0's only early exit was a `goto` to a label at the end of the body, which still works.
**It takes no value:** `return 5;` is `E448`. Assign to the named return variable, then `return;`.

`sizeof(T)` takes a bare type name - `sizeof(int pointer)` and `sizeof(someExpression)` are both `E001`.

```impala
extern struct HostFrame { int width; int height }
```

The host owns that layout. Impala emits `#.z.HostFrame` and `#.o.HostFrame.width` and the host supplies the
values at load, so the same `.gazl` runs against any layout with no recompile. An array field in an
`extern struct` must not state a size (`E430`).

`extern function` now takes a full prototype, and call sites are checked against it. The 1.0 name-only form
still parses and still asserts nothing. When a prototype and a real definition disagree in one build, that
is `E437`.

## Diagnostics became citable

1.0 already had a caret and a line and column. What it did not have: a code, a fix-it note, warnings, or
*correct* positions - a redeclaration on line 6 was reported at line 7, and carets landed on the token
after the one at fault.

```
old.impala:2:24: error[E201]: Pointer element type mismatch (expected int elements, got float elements)
global int pointer p = &global f[0]
                       ^
old.impala:2:24: note: use a cast: (int pointer)
```

71 codes, most carrying a note. Some notes are computed rather than canned - an undeclared name that is
defined further down names the definition and its file and line, and tells you to add a forward `extern`.

## Bounds checking, in three tiers

| When | What | Cost |
|---|---|---|
| Impala compile time | A constant index past a known extent is `E461`; a negative extent is `E462` | free |
| GAZL assembly time | A symbolic extent defers to a `! FAIL` the assembler evaluates once the host supplies it | free |
| Run time | `--range-checks` emits `DEBUG`-gated tests | off by default, free when `DEBUG` is 0 |

Tier 1 is the one that changed most: 1.0 compiled `a[9]` on a four-element array happily and let the *load*
fail with a GAZL symbol name. Tier 2 already existed for plain arrays; what is new there is symbolically
sized struct fields.

Pointers are not bounds-checked at any tier, deliberately - a pointer has no extent to check against, the
same as in C. See [`MemorySafetyModel.md`](MemorySafetyModel.md).

Typed arrays also emit typed data rows (`DATi`, `DATf`) where 1.0 emitted untyped `DATA`. That engages a
check the assembler already had and 1.0 never reached, so initializer data gets a second, independent
verification for free.

## Not available

Reserved so they can be refused clearly, not because they are coming soon in 2.0:

| | |
|---|---|
| A struct parameter by value | `E426` - use a pointer |
| Returning a struct by value | `E427` - use a pointer out-parameter |
| Multiple return values | `E428` |
| Destructuring assignment | `E429` |
| `break` / `continue` | `E450` - leave a loop with `goto`; a switch arm already does not fall through |

`inline function` is NOT in that table on this branch. The Impala 2 line refuses it, because it has to run
on a GAZL 1.0 engine; here the `SCOP`/`ENDS` an expansion needs exist, so the feature does too.

Multidimensional arrays are the exception to that. Every row above has a reserved word or a dedicated
diagnostic, so the compiler can say the feature was considered and parked - but nothing in the grammar
mentions multidimensional arrays at all, so `int array a[2][3]` is an ordinary syntax error (`E001`) with
no hint that it was ever designed.

It was, and the reason it is not here is worth knowing, because it is not "we ran out of time". An array's
dimensions want to be part of its **type**, so that calls can be checked - but a dimension may be any
expression, resolved by the assembler rather than by Impala. That leaves only bad options: identity by the
*form* of the expression (incomplete), or dimensions restricted to numeric literals. A milder revival that
avoids the question entirely - multidimensional only as a struct field, reached through a struct pointer -
was designed and rejected on cost: what it adds over writing `y * W + x` is subscript sugar plus shape
typing, the sugar already works today, and the shape typing *is* the unsolved question.
[`ParkedFeatures.md`](../../design/ParkedFeatures.md) has the full argument and the branch, for this and every row above.

## Upgrading a 1.0 program

Everything above is additive, so none of it can break existing code. What breaks is a short list of things
1.0 accepted that 2.0 refuses - but it is longer than it looks, and `--legacy` does not cover all of it.

**For calibration:** of the 67 programs in the 1.0-era test corpus, **14 fail** under 2.0. `--legacy`
rescues 13 of them; one does not build either way.

Start here, which turns everything in the first table into a warning:

```
node impala/impala.node.js compile --legacy old.impala old.gazl
```

### `--legacy` downgrades these to warnings

| 1.0 source | Code | Fix |
|---|---|---|
| A variable, label, function or global named `return`, `break` or `continue` | `E449` | Rename. **By far the most common** - 8 of the 14 corpus failures, because `goto break` was the standard 1.x early-exit idiom |
| `r = a & b \| c` - two *different* bitwise operators, no parens | `E101` | `(a & b) \| c`; the meaning is unchanged |
| `if (a & 1 == b)` - a bitwise expression as a comparison operand | `E102` | `(a & 1) == b` |
| `if (!a == 0)` - `!` on an unparenthesised operand | `E103` | `!(a == 0)`, which is what it already meant |
| `global K` where `K` is a `const`, or `global f()` | `E452` | Drop the `global` |

### `--legacy` does not help with these

| 1.0 source | Code | Fix |
|---|---|---|
| An identifier named `struct`, `export`, `import`, `inline`, `sizeof` or `functype` | `E001` | Rename. These are full keywords now, so there is no escape hatch and no tailored message - just a syntax error at the name |
| `r = --a` meaning `-(-a)` | `E001` | Write `- -a`, with the space |
| A `case` value outside the declared switch range | `E444` | Widen the range or drop the arm. The upper bound is exclusive |
| Two `case` arms with the same value | `E443` | Merge or renumber |
| The same label twice in one function | `E446` | Rename one |
| `goto` to a label that does not exist | `E445` | 1.0 emitted a dangling branch |
| A constant index past a known extent | `E461` | Fix the index |
| A negative array extent | `E462` | An array holds zero or more elements |
| A named return value assigned nowhere in the body | `E463` | Assign it somewhere |
| Writing through a `readonly` global, or into a string literal | `E404` | Use `global`; string data is now genuinely readonly |
| Two definitions of one function, or a global and a function sharing a name | `E401` | Top-level names are one flat namespace now |

The last seven are cases where 1.0 emitted something that failed later - at GAZL load, or not at all. They
are new diagnostics rather than new restrictions.

`E463` is the exception in that list: nothing failed later, it just returned the wrong number. A frame
local is not zeroed - `FUNC` only advances the stack pointer - so an unassigned return value is whatever
the previous call left at that depth. That is reproducible rather than random, and it changes when
UNRELATED code alters the call graph above it. Only the decidable case is diagnosed: assigned nowhere at
all. Assigned on some paths and not others needs definite-assignment analysis and is not attempted, since
guessing would reject correct programs. See [`MemorySafetyModel.md`](MemorySafetyModel.md).

### Things that did not change

`assert`, `copy`, `loop` / `while` / `do` / `for` / `goto` / labels, `switch` syntax including `case a, b:`
and `default:`, `abs` / `floor` / `itof` / `ftoi`, `null` / `nullfunc`, every literal form, `global` on
globals, untyped `array` / `pointer` / `funcptr`, plain `f = somefunc`, and the name-only
`extern native` / `extern function` / `extern array` forms.
