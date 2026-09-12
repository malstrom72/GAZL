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

## Arrays can have a shape

An array states its axes in one bracket clause, separated by commas, and is subscripted the same way:

```impala
struct Grid { int array cells[3, 4] }

global int array board[3, 4]
global Grid grid

function at(int y, int x) returns int v
{
	v = global board[y, x] + global grid.cells[y, x];
}
```

Globals, locals and struct fields all take a shape. The axes become `.d.` constants in the emitted `.gazl`
(`.d.board.0`, `.d.Grid.cells.0`), and an axis Impala cannot evaluate is deferred to the assembler, so
`global int array sym[H, W * 2]` is as legal as a literal shape.

Subscripts are rank-checked: naming a number of axes that is not the array's rank is `E206`, whatever the
indices are. Each axis is bounds-checked on its own, so a constant index past one axis is caught even when
the flat offset would have landed inside the array. Initializers nest one brace group per axis - a flat
list for a shaped array is `E422`.

The C spelling `a[y][x]` is not the syntax. `[3][4]` on a declaration is `E001`.

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

Between two named types, assignment is nominal and the cast is shape-checked: `(Other)cb` converts freely
when the shapes agree and is `E465` when they differ - as is casting a non-funcptr (an int, a data
pointer) to a named type at all. Untyped `funcptr` is the escape hatch - `(Other)(funcptr)cb` spells the
deliberate conversion.

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
values at load, so the same `.gazl` runs against any layout with no recompile. A host-owned array states its
rank and not its size: `int array cells[]` for one axis, `[,]` for two. A size is `E430`, and omitting the
brackets is `E432` - the size is the host's to decide, the rank is not.

`extern function` now takes a full prototype, and call sites are checked against it. The 1.0 name-only form
still parses and still asserts nothing. When a prototype and a real definition disagree in one build, that
is `E437`.

The return may be named or bare - `returns int` and `returns int n` compile to the same thing, because a
prototype records only the type. Parameter names are not optional: they are printed in the signature row.

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

73 codes, most carrying a note. Some notes are computed rather than canned - an undeclared name that is
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

## `tail` - self-recursion in constant stack (`--gazl2` only)

A tail-recursive accumulator no longer has to grow the stack until the frame check traps it:

```impala
function count(int n, int acc) returns int r
{
	if (n == 0) { r = acc; return; }
	tail count(n - 1, acc + 1);
}
```

`tail f(...);` is a terminal statement, like `return;`, that re-enters the current function with new
arguments in the SAME frame - the arguments marshal like any call's, the parameters are rewritten, and
control jumps back to the top of the body. `count(100000, 0)` runs where the plain recursive spelling
traps with `status -6`.

It is explicit on purpose: silent tail-call elimination decides whether byte-identical source works at
all, which is the opposite of a predictable cost model. And it is deliberately narrow in 2.0: the target
must be the enclosing function (`E467` - the GAZL `TAIL` instruction is general, but cross-function
contract checking is future compiler work), it needs `--gazl2` (`E466` - the instruction exists only on
GAZL 2 engines), and an `inline function` cannot use it (`E468`). `tail` itself stays a legal name for
anything else.

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

[`ParkedFeatures.md`](../../design/ParkedFeatures.md) has the full argument and the branch for every row above.

One shape-related limit is worth naming: a shape belongs to an array or a struct field, never to a pointer.
Axes reach a callee when they are keyed to a struct type - `g->cells[y, x]` is checked inside a function
that received nothing but a `Grid pointer` - but a standalone array passed as a plain pointer arrives as an
address with no axes and no guard. Shape-carrying pointer types are deferred to 3.0.

## What is worth rewriting

The table above is what 2.0 REFUSES. This is the other half: what it lets you delete. None of it is
required - every 1.0 spelling below still compiles, so a rewrite is a choice - and none of it changes the
emitted code, which is the reason to treat it as tidying rather than as risk. The measurements are from
converting three real programs in `tests/impala/sources` (`fileList2`, `disasm2`, `calc2`), each of which
sits beside its 1.0 original so the diff is readable.

### A record built from index constants becomes a struct

1.0 has no record type, so a table of them is a flat array plus a set of offsets the author keeps in step
by hand:

```impala
const int FILE_FIELD_NAME = 0
const int FILE_FIELD_SIZE = 1
const int FILE_FIELD_COUNT = 2
const int MAX_FILES = 8

global array FILES[MAX_FILES * FILE_FIELD_COUNT]
global array NAMES[MAX_FILES * 32]
```

The offsets, the stride and the row length are now the assembler's job:

```impala
struct File { int pointer name; int size }

global File array files[MAX_FILES]
global int array names[MAX_FILES, 32]
```

`files[i].size` lowers to the same three instructions `FILES[i * FILE_FIELD_COUNT + FILE_FIELD_SIZE]` did -
`.z.File` and `.o.File.size` replace the hand-written constants, so the layout cannot drift from the code
that reads it. Converting `fileList.impala` removed all **19** uses of `FILE_FIELD_*` outside their own
declarations, and left the
emitted data words byte-identical. A rectangular `[MAX_FILES, 32]` does the same for a row walk: `&names[i, 0]`
instead of a pointer the author has to remember to advance by the row length.

### A signature in a comment becomes a checked prototype

An `extern` may state its parameters and a single return, which turns a comment into an assertion:

```impala
extern native loadText(int pointer name, int offset, int pointer dest) returns int
```

Calls are then argument-checked and the result has a real type - which is what retires the `(int)` casts
around them. The return name is optional (`returns int` and `returns int n` mean the same). Name-only
externs stay valid and stay unchecked; declare a prototype only where you actually know the shape.

### A dispatch table becomes a struct of typed function pointers

A 1.0 table of alternating names and functions pairs them only in the `* 2 + 0` / `* 2 + 1` at each use,
and the call needs a `(funcptr)` cast because a flat array has no element type:

```impala
functype MathFn(float x) returns float

function half(float x) returns float y { y = x * 0.5; }

struct MathOp { int pointer name; MathFn fn }

readonly MathOp array OPS[1] = { { name: "half", fn: half } }
```

The pairing is now the struct and the call is checked against `MathFn`. This one is not only tidier: in
1.0 a two-argument `pow`, or a `(pointer, pointer) -> int` `strcmp`, could be stored in that table and
**compiled clean**, to be called as `float(float)` at run time. Both are `E441` in 2.0.

### An untyped pointer becomes a typed one, and the casts go

`pointer` and `array` are still accepted and still mean "untyped". Because they have no element type,
every dereference has to say what it read:

```impala
function strlen10(pointer s)
returns int n
locals pointer p
{
	p = s;
	while ((int) *p != 0) p = p + 1;
	n = p - s;
}
```

```impala
function strlen(int pointer s)
returns int n
locals int pointer p
{
	p = s;
	while (*p != 0) p = p + 1;
	n = p - s;
}
```

Those two lower to *identical* instructions. The cast was never doing work - it existed because `*p` had
no type to begin with. Typing the declarations in `calc.impala` deleted **88** casts and changed not one
emitted instruction.

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
`extern native` / `extern function` forms.

`extern array` is the one on that list that moved: it now states a rank, `extern array g[]`, which is the
spelling the 1.0 reference and the corpus already used. A bare `extern array g` is `E432`, and `--legacy`
does not downgrade it.
