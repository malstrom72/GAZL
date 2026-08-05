# Impala Language Reference

Impala is a small, imperative, C-like language that compiles to the GAZL virtual
machine. It is deliberately close to the underlying assembly: there is little
optimization, types are minimal, and most constructs map almost one-to-one to GAZL
instructions. Think of it as a high-level assembler.

Because GAZL programs are distributed as assembly *text* and assembled on the end user's
machine at load, Impala compiles into a two-stage build: values the host supplies at load
are legal constants that Impala never sees a number for. This shapes the whole language
and is specified in [`design/impala/TwoStageConstants.md`](../../design/impala/TwoStageConstants.md).

This document is the language reference. For a feature-rich program with extensive
inline commentary, see `impala/ImpalaDemo.impala`.

The authoritative grammar is `impala/impala.jspeg`; this reference follows it.

## Lexical elements

### Comments

```impala
// Line comment, C++ style.
/* Block comment. Does not nest. */
```

### Identifiers and keywords

Identifiers start with a letter, `_`, or `$` and continue with letters, digits, `_`,
or `$`. The following words are reserved and cannot be used as identifiers:

```
abs array assert break case const continue copy default do else export extern
float floor for from ftoi funcptr function functype global goto if import inline
int itof locals loop native null nullfunc pointer readonly return returns sizeof
struct switch temporary to while
```

Using any of them as an identifier is `E449 … is a reserved word, not a variable name`.
Three of them are reserved without being usable everywhere the word suggests: `return;`
is a bare early exit only (`return expr;` is `E448` — assign to the return variable
first), and `break;` / `continue;` are `E450 not supported`, with the note pointing at
`goto` and a label.

### Integer literals

Three forms are accepted:

```text
123          // decimal
0x1F         // hexadecimal
'A'          // character literal - the word value of the character(s)
'abcd'       // multi-character literal, packed into one word
```

### Float literals

A float **must** have at least one digit on both sides of the decimal point, with an
optional exponent. There is no float suffix; the decimal point is what distinguishes a
float from an integer.

```text
1.0          // valid
3.14159
6.022e23
1.           // INVALID - needs a digit after the point
.5           // INVALID - needs a digit before the point
```

### String literals

A string literal yields a pointer to a zero-terminated array of word-sized characters
(words are typically 32-bit). Its type is `int pointer` — assigning one to a
`float pointer` is `E201`.

```impala
const pointer WELCOME = "Welcome to Impala!\n"
```

The supported escape sequences are `\"`, `\\`, `\b`, `\f`, `\n`, `\r`, `\t`, and
`\uXXXX` (exactly four hex digits).

## Types

There are four primitive types, all one VM word wide (standard configuration 32-bit):

| Type | Meaning |
|---|---|
| `int` | signed integer |
| `float` | floating point |
| `pointer` | generic pointer (untyped target) |
| `funcptr` | function pointer |

### Typed pointers and arrays (Impala 2)

Since Impala 2, pointers and arrays may carry a **compile-time element type**, written
type-first with stackable trailing keyword modifiers:

```text
int pointer p                    // pointer to int
int array a[10]                  // array of 10 ints
float array buf[64]              // array of 64 floats
int pointer array table[8]       // array of 8 (pointers to int)
int pointer pointer pp           // pointer to pointer to int
```

The element type is a zero-cost overlay: at runtime a typed pointer is still one word and
a typed array is still N words, with stride 1 word - the generated GAZL is identical to
the hand-cast 1.0 equivalent. What changes is that element accesses are typed, so the
casts disappear:

```impala
function findSmallest(int n, int pointer vector)
returns int j
locals int i
{
        j = 0;
        for (i = 1 to n)
                if (vector[i] < vector[j])      // no (int) casts needed
                        j = i;
}
```

Subscripts, dereferences (`*p`), pointer arithmetic (`p + i` keeps the element type), and
`&` (address-of an `int` yields an `int pointer`; `&a[i]` of an `int array` likewise) all
propagate element types. String literals are `int pointer` (their characters are word-sized
ints), so `("0123456789abcdef")[v & 0xf]` needs no cast. The boundary rules are asymmetric - *erasing is silent, assuming
is loud*:

- typed → untyped (`pointer raw = p`, passing a typed buffer to a native): **implicit**;
- untyped → typed: **requires a cast** (`p = (int pointer) raw`);
- differing element types: **requires a cast** (`(float pointer) intPtr` reinterprets);
- `null` assigns into any pointer type.

Bare `pointer` and `array` remain valid and mean untyped raw-word storage - the escape
hatch for host boundaries, with exactly the 1.0 semantics.

Function arguments and return values are untyped unless declared with typed forms.
External functions need no prototypes, and multiple Impala sources can still be linked
simply by concatenating their assembled `.gazl` output; casts are only needed where code
genuinely crosses between the typed and untyped worlds (see [Casting](#casting)).

### `struct` and `functype`

`struct` types (with `.` on a value and `->` through a pointer), named function-pointer
types declared with `functype`, and `sizeof` are shipped and working. They are owned by
[`docs/impala/Impala2.md`](Impala2.md) rather than repeated here — see
[Step 2: Structs](Impala2.md#step-2-structs-implemented) and
[Step 3: Typed function pointers](Impala2.md#step-3-typed-function-pointers-implemented).

## Declarations

### Globals

Global variables live in slow global memory. The `global` keyword must prefix a global
both at its declaration and at **every** access. The prefix is a constant reminder that
global access is more expensive than local access, so audio-rate code should minimize it.

```impala
global int uninited
global int inited = 23
global float aFloat
global pointer aPointer = &global aFloat
global funcptr aFuncPointer
global array defaultArray[100]
```

Initializers for globals must be compile-time constants.

### `const`

`const` introduces a named constant (a *define*). It occupies no memory and can be used
for array sizes and other constant values. The value must be a constant expression.

```impala
const int SOME_COUNT = 4
```

**"Constant" here does not mean "a number the Impala compiler knows."** A constant is
anything that will have a value by the time the generated GAZL finishes assembling, and
assembly happens on the end user's machine at load. So constants may be supplied by the
VM or host at that point, and are then declared without a value:

```impala
const int GAZL_WORD_SIZE
const int DEBUG
```

Such a constant is legal everywhere a literal is: as an array size, a `switch` range, a
`sizeof` result, an operand. Impala emits a symbolic reference and the assembler resolves
it. Even a constant Impala *does* know is passed through by name rather than folded, so
`const int SOME_COUNT = 4` emits `SOME_COUNT: ! DEFi #4`, and an array declared
`[SOME_COUNT]` reaches that name through the array's own size symbol rather than a folded
number:

```gazl
.z.SOME_CONSTS:	! DEFi #SOME_COUNT
SOME_CONSTS:	CNST *.z.SOME_CONSTS
```

Nowhere does a `4` appear, which is what keeps the value overridable at load.

This two-stage model is the reason GAZL ships as text, and it shapes what the compiler is
allowed to check and fold. It is specified in
[`design/impala/TwoStageConstants.md`](../../design/impala/TwoStageConstants.md); read that before relying on, or
changing, any constant handling.

### `readonly`

`readonly` data is stored in global memory but cannot be modified - any attempt to write
it is a runtime error. Unlike `const`, it is real storage and is accessed with the
`global` keyword.

```impala
readonly int IMMUTABLE = 42
readonly array SOME_CONSTS[SOME_COUNT] = { 100, 200, 300, 400 }
```

Read a readonly array with the `global` prefix even though the declaration begins with
`readonly`:

```impala
x = (int) global SOME_CONSTS[i];
```

### `temporary`

Marks a global that the host does not need to serialize when saving VM state:

```impala
temporary int forgetMe
```

### `extern`

Introduces a symbol defined elsewhere. Native functions supplied by the host use
`extern native`:

```impala
extern int defineMeLaterPlease
extern array futureArray[]
extern function thisFunctionInAnotherSource
extern native abort
```

`import` and `export` — the source-level way to pull another unit in rather than declare
its symbols one at a time — are shipped and documented in
[`docs/impala/Impala2.md`](Impala2.md#step-5-import-implemented-except-cycles).

### Arrays

Only one-dimensional arrays are supported. Elements may hold any mix of values, and the
size must be a compile-time constant. Array initializers must be compile-time constants.

```impala
global array initedArray[10] = {
        1, 2.0, &global defaultArray[0], 4
}
```

### Function pointers

Function pointers use the `funcptr` type, are assigned and called like any other
pointer, and can be tested against `nullfunc`:

```impala
global aFuncPointer = showoff;
if (global aFuncPointer != nullfunc)
        global aFuncPointer();
```

`null` is the corresponding null value for ordinary `pointer`s.

## Functions

A function declares arguments, optional return value, and optional locals. Every slot is
one VM word, but the declared type is checked at Impala compile time: passing a `float`
where the definition says `int` is `E406`.

```impala
function fetchSomeConst(int index)
returns int fetched
{
        fetched = (int) global SOME_CONSTS[index];
}
```

Locals are declared in a single `locals` clause and may include arrays:

```impala
function test()
locals int i, array mydata[TEST_SIZE]
{
        for (i = 0 to TEST_SIZE)
                mydata[i] = myrand();
}
```

External functions require no prototypes and multiple Impala sources can be linked just
by concatenating their assembled `.gazl` output. Signature metadata records the contract
shapes in comments without changing the generated instructions, so legacy assemblers
continue to accept the files. Casting does not convert between ints and floats; use
`itof()` or `ftoi()` for that.

Functions cannot be nested. **A name must be declared before it is used** — the compiler is
single-pass, so calling a function defined later in the same source is `E403 Undeclared
identifier` (the diagnostic finds the later definition and says so). To call across sources,
or to break a cycle within one, declare a forward `extern function` above the use.

## Statements

Every simple statement ends with a mandatory `;` — an assignment, a call, `goto`,
`copy`, `assert`, and the empty statement. Omitting it is `E001 syntax error`, usually
reported on the *next* line, since the parser only gives up once it sees something that
cannot continue the expression. The compound statements — `if`, `for`, `while`,
`do…while`, `loop`, `switch`, and a `{ … }` block — carry no terminator of their own;
the statements inside them do. Declarations (`global`, `const`, `readonly`, `extern`)
accept a trailing `;` but do not require one.

### Assignment

Assignment uses `=` and is itself an expression that yields the assigned value, so it may
appear inside a larger expression:

```impala
a = b = 0;
while ((c = nextValue()) != 0) { /* ... */ }
```

There are no compound assignment operators (`+=`, `&=`, …) and no `++`/`--`.

The assignable forms (lvalues) are a variable, a pointer dereference `*p`, a subscript
`a[i]`, and a struct field reached either directly (`s.a`) or through a pointer
(`p->a`) — see [`docs/impala/Impala2.md`](Impala2.md#step-2-structs-implemented).

### Conditionals

```impala
if (x > 0)
        positive();
else if (x < 0)
        negative();
else
        zero();
```

The condition is a parenthesized boolean expression (see
[Conditions](#conditions-and-boolean-expressions)).

### Loops

`for` increments its variable by one and stops **before** the upper bound. The
initializer is optional, and the loop variable must be a local `int` or `pointer`:

```impala
for (i = 0 to TEST_SIZE)        // i runs 0,1,...,TEST_SIZE-1
        mydata[i] = myrand();

for (i to TEST_SIZE)            // reuses i's current value as the start
        consume(i);
```

Other loops:

```impala
while (cond != 0) { /* ... */ }
do { /* ... */ } while (cond != 0)
loop { /* runs forever; exit with goto or return */ }
```

A condition must be a comparison; a bare value is not one, so `while (cond)` is
`E001 syntax error`. Write the `!= 0` out.

Use `goto` and labels to break out of loops manually:

```impala
loop {
        if (done != 0) goto finished;
        step();
}
finished: ;
```

A label must be followed by a statement, so a label at the end of a block needs the empty
statement `;`. Writing a bare `finished:` there reports `E001: syntax error` on the closing
brace.

### `switch`

`switch` tests an integer expression against a range written as `== low to high`. The upper
bound is **exclusive**, exactly as in `for (i = 0 to N)` — `switch (i == 0 to 10)` covers 0
through 9, and `case 10:` is `E444`. If the value falls outside the range, the `default` case
runs. Cases do **not** fall through, so there is no `break`. A case can list several values.

A case value outside the range is rejected only when the compiler can see both numbers. If
`high` is a named `const` or a host-supplied constant, the range is symbolic and an
out-of-range case compiles into silently unreachable code.

```impala
switch (i == 0 to 10) {
        case 0,1,2: {
                j = i;
        }
        case 5: x();
        default: j = -1;
}
```

### `copy`

Copies a fixed number of words from one pointer to another. The count must be a
compile-time constant.

```impala
copy(3 from &global initedArray[1] to &global futureArray[0]);
```

### `assert`

Performs a runtime check, but only when the `DEBUG` constant is non-zero. When `DEBUG`
is zero the check (and its message) are compiled out.

The emitted guard is `! EQUi #DEBUG #0`, so `DEBUG` must exist by GAZL assembly time even
in a release build. A program that uses `assert` without one compiles happily and then
fails to load with `Symbol not previously defined (in expected scope): DEBUG`. Declare it
in the source, or leave it valueless (`const int DEBUG`) and have the host supply it at
load.

```impala
const int DEBUG = 0

function check(int i)
{
        assert(i != 0);
}
```

## Expressions

### Operator precedence

From loosest to tightest binding:

| Group | Operators | Associativity |
|---|---|---|
| Assignment | `=` | right |
| Bitwise / shift | `\|` `&` `^` `<<` `>>` `>>>` | left |
| Additive | `+` `-` | left |
| Multiplicative | `*` `/` `%` | left |
| Prefix / postfix | prefix `-` `~` `&` `*`, casts `(type)`, `abs` `floor` `itof` `ftoi`; postfix `()` `[]` | - |

> **Important:** all bitwise and shift operators share **one** precedence level that binds
> *looser* than `+` and `-`. (Arithmetic-vs-bitwise actually agrees with C: `x & 0xFF + 1` is
> `x & (0xFF + 1)` in both. The divergence from C is *within* the bitwise family, which C ladders
> internally.) Since Impala 2, **mixing different bitwise/shift operators at the same
> parenthesization level is a compile error** - write `(a | b) & c`, never `a | b & c`. The same
> applies to an unparenthesized bitwise operator directly against a comparison in a condition:
> write `if ((a & 3) == 0)`. Same-operator chains need no parentheses (`a | b | c`). The
> `--legacy` compiler argument downgrades these errors to warnings for old code, which should
> simply be updated - the parenthesized form compiles to identical GAZL.

Details that are easy to miss:

- `>>` is an **arithmetic** right shift and preserves the sign bit. `>>>` is a **logical**
  right shift that fills with zeroes - usually what you want for bit masks, hashing, and
  random-number generators.
- `~` is bitwise NOT; `^`, `&`, `|` are the other bitwise operators.
- `%` is integer modulo only. There is no float modulo.

### Conditions and boolean expressions

The comparison operators `<`, `<=`, `>`, `>=`, `==`, `!=`, the logical operators `&&`
and `||`, and the logical NOT `!` exist **only** inside the parenthesized condition of
`if`, `while`, `do…while`, and `assert`. They do not produce values you can store or pass
around:

```impala
if (0 <= x && x < limit) { /* ok */ }
```

...but not as a value:

```text
flag = (a < b);       // INVALID - comparisons are not values
```

`&&` and `||` short-circuit. To capture a comparison result, branch on it and assign
inside the branches.

### Casting

A cast `(type) expr` reinterprets a value's type; it does **not** convert the
representation. To convert between integers and floats use the built-ins `itof()` and
`ftoi()`.

```impala
f = itof(n);                  // int -> float (value conversion)
n = ftoi(f);                  // float -> int (value conversion)
p = (pointer) alloc(16);      // retype only, no conversion
```

A cast only applies to a value the compiler has **not** already typed. Casting a typed
operand to an incompatible type is `E302 Invalid type` — `(pointer) x` where `x` is a
declared `int` is rejected, not reinterpreted. The values worth casting are the untyped
ones: a call result from a name-only `extern`, an element of a bare `array`, and a bare
`pointer` being narrowed to a typed one.

Because those return values and untyped elements carry no type, they usually need a cast
before use in a typed context:

```impala
y = (int) lfoVal(1) + 1;      // a bare untyped value + int needs the cast
z = lfoVal(1);                // a plain assignment is fine without it
p2 = (int pointer) raw;       // untyped -> typed: assuming is loud
```

### Built-in operators

Impala has exactly four built-ins; everything else is either a statement keyword (`copy`, `assert`)
or a host-supplied `extern native` function.

They are **prefix operators, not functions** — `abs x` is the primitive form, and they join `-`, `~`,
`&` and `*` in the prefix table. `abs(x)` also works, but only because the parentheses are an ordinary
parenthesized expression, so it is worth knowing what you are actually writing: both `abs()` and
`abs(x, 2)` are `E001 syntax error` — with the caret on the `,`, because there is no argument list
here to be malformed (`E442` is reserved for a real call's). Precedence is unary-tight, so
`abs x - 1` means `(abs x) - 1`.

| Built-in | Description |
|---|---|
| `abs x` | absolute value; works on `int` and `float` |
| `floor x` | floor; works on `float` |
| `itof n` | convert `int` to `float` |
| `ftoi f` | convert `float` to `int` |

### Pointers and arrays

`&` takes an address; `*` dereferences. Pointer arithmetic is supported: `pointer + int`
and `pointer - int` yield a pointer, and `pointer - pointer` yields the `int` element
distance.

Given the declaration `global pointer p = &global defaultArray[0]`, the `global` prefix is
required at every use of `p` too — `*(p + 3)` on its own is `E403`:

```impala
x = *(global p + 3);
```

The subscript `[]` works on any pointer, not just declared arrays: `p[i]` is equivalent to
`*(p + i)`. The index may be negative, and the pointer may even be a string literal:

```impala
last = (int) global p[-1];
hexDigit = ("0123456789abcdef")[value & 0xf];
```

## The Impala tools

The active compiler is generated by JSPEG and shipped as JavaScript under
`impala/`:

- `impalaCompiler.js` - generated compiler used by the build and command-line workflow
- `impala.nuxjs.js` - NuXJS command-line wrapper around the generated compiler
- `impalaImportClosure.js` - `import` closure walk and `--dead-strip`, shared by both front ends
- `impala.jspeg` - grammar source used when regenerating the compiler
- `runJspegTests.js` - parity test runner for the JSPEG compiler fixtures

The build system compiles the NuXJS command-line runtime from `externals/NuXJS`
and places it in `output/`. The `BuildImpala` scripts then copy
`impala.nuxjs.js`, `impalaImportClosure.js` and `impalaCompiler.js` to `output/`
so Impala sources can be compiled without Node.js.

### Signature metadata and validation

> **gazl-validate is not the assembler.** It is a `; signature` metadata linter: it compares
> declared contracts across units, and it is built to run on modules whose externs are
> deliberately unresolved — which is exactly what the assembler refuses to load. A file it
> passes may still fail to assemble.
>
> The assembler (and the VM) is `output/GAZLCmd`. It has no assemble-only mode; it enters `main`
> by default, so checking "does this assemble" by running it can launch a whole program. Name an
> entry point that cannot exist instead — `./output/GAZLCmd f.gazl .no-entry-point` — and read the
> `Code size:` banner, which is the proof it assembled. It then exits 1 on the missing entry
> point, so do not read the exit code. See the README's "Which tool does what".

The compiler emits human-readable signature comments alongside the
`.gazl` instructions it produces. Each definition, global, and call site
is annotated with its expected `{int, float, ptr, funcptr, void}`
categories so mismatches can be caught after compiling one or more units.
This matters both for separately compiled modules and for calls made
before a later definition has supplied its real signature. When the
compiler knows the original source location it appends
`@ path:line:column` to the end of each comment, allowing the validator
to cite precise spans in diagnostics. Functions that omit an explicit
`returns` clause map the compiler's implicit `?` type to `void` in the
comment stream, keeping the metadata aligned with the language's
behaviour.

The validator merges those comments into a single contract per symbol. An
`extern function add;` declaration contains no argument or return
information, so the metadata line emitted for it becomes `; signature
extern func add() -> unknown`. Rather than locking that in as a concrete
signature, `gazl-validate` treats the zero-argument/`unknown` pair as a
placeholder and waits for a definition to provide the real types. When
another unit defines `function add(int x, int y) returns int z`, its
comment advertises `; signature func add(int arg0, int arg1) -> int`, and
the validator reconciles the two entries before comparing them with the
call sites. This keeps the Impala surface unchanged - bare extern
declarations remain valid while still enabling cross-unit type checking
through the assembler comments.

```gazl
; signatures version=1
FUNC showoff         ; signature func showoff(ptr text) -> void @ ImpalaDemo.impala:42:1
LOC demoFloat        ; signature global demoFloat : float @ ImpalaDemo.impala:9:1
CALL showoff         ; expects showoff(ptr) -> void @ ImpalaDemo.impala:49:9
; signature extern func print(ptr message) -> void @ ImpalaDemo.impala:3:1
```

The signature comment grammar is intentionally small:

- Each metadata-capable unit starts with `; signatures version=<n>`.
- Function definitions use `; signature func name(<params>) -> <return>`.
- Extern declarations use `; signature extern func name(<params>) -> <return>` or
  `; signature extern native name(<params>) -> <return>`.
- Call sites use `; expects name(<arg-types>) -> <return>`.
- Value and array metadata rows use
  `; signature <role> name : <type>` or `; signature array name[size] : <type>`.

Function parameter entries may include names (`int value`) or only types
(`int`). Call-site expectations always list the argument types seen by the
caller. Any metadata line may end with `@ <origin>`, where the origin is either
`path:line:column` or just `line:column` when no source filename was supplied.

Native callbacks are checked against the manifest in
`design/proofs/nativeCallbackSignatures.gazl`. Keep that manifest in sync with the host
registration tables in `tools/GAZLCmd.cpp` and `src/GAZL.cpp` whenever adding or
changing a native callback. Bare native extern placeholders such as
`; signature extern native printInt() -> unknown` do not override the manifest;
native calls must still match the manifest's arity, argument types, return type,
and native calling convention.

For non-function symbols, the validator checks the metadata contract rather than
the runtime contents. Extern globals and constants must agree with their
definitions on primitive category, duplicate definitions of the same value name
must agree on primitive category, and arrays must agree on category and on size
when both sides provide a size. `unknown` remains a wildcard. The validator does
not compare constant values, distinguish `readonly` from writable storage during
matching, or assign per-slot types to arrays. An untyped array (`global array nums[4]`)
has no element type, so its row is `: unknown`; a typed one (`global int array nums[4]`)
now carries it, as `; signature array nums[4] : int`. Either way the row states the type
of the whole array's elements, not a per-slot promise.

Run the validator on every `.gazl` unit that will be concatenated or
loaded together. It compares the expectations recorded by callers with
the definitions supplied by the same file or by other units. After
building the toolchain (`bash build.sh`), compile two sample sources and
validate them from the repository root:

```bash
./output/NuXJS output/impala.nuxjs.js tests/impala/sources/calc.impala output/calc.gazl 0x4d2 calc.impala
./output/NuXJS output/impala.nuxjs.js tests/impala/sources/multitap_code.impala output/multitap.gazl 0x4d2 multitap_code.impala
bash tools/gazl-validate.sh output/calc.gazl output/multitap.gazl
```

`gazl-validate.sh` starts with `cd "$(dirname "$0")"/..`, so it always resolves its
arguments against the repository root. Running it from inside `output/` with bare file
names therefore fails with `Could not open input file` — pass root-relative paths, or run
it from the root as above.

The validator reports mismatched signatures as errors by default. Pass
`--warn-only` to downgrade them while you migrate existing modules, or
`--force` to turn missing-definition warnings into errors. Missing metadata and
missing definitions remain warnings by default and do not make the validator exit
with failure unless `--force` is used. The normal `build.sh` path runs the
validator's regression tests and validates the generated JSPEG fixture metadata
with explicit file sets. For other programs, run `tools/gazl-validate.sh` or
`tools\gazl-validate.cmd` directly on the exact `.gazl` units that will be
linked together.

See [Impala JSPEG](../../design/jspeg/ImpalaJS.md) for the CLI, regeneration flow, and
parity test commands.

## Compiling and running

After running `bash build.sh` the `output/` folder contains `NuXJS`, `GAZLCmd`,
`impala.nuxjs.js`, and `impalaCompiler.js`.
Compile an Impala source file like so:

```bash
cd output
./NuXJS impala.nuxjs.js ../impala/ImpalaDemo.impala demo.gazl 0x4d2 ImpalaDemo.impala
```

Execute the resulting program with the VM:

```bash
./GAZLCmd demo.gazl main
```

From the repository root, the same flow is:

```bash
./output/NuXJS output/impala.nuxjs.js impala/ImpalaDemo.impala output/demo.gazl 0x4d2 impala/ImpalaDemo.impala
./output/GAZLCmd output/demo.gazl main
```
