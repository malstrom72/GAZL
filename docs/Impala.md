# Impala Language Reference

Impala is a small, imperative, C-like language that compiles to the GAZL virtual
machine. It is deliberately close to the underlying assembly: there is little
optimization, types are minimal, and most constructs map almost one-to-one to GAZL
instructions. Think of it as a high-level assembler.

This document is the language reference. For a feature-rich program with extensive
inline commentary, see `impala/ImpalaDemo.impala`. For the host API exposed to
Permut8 firmware (entry points, native functions, globals), see
[Permut8 Firmware API](../../docs/Permut8%20Firmware%20API.md).

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
abs array assert case const copy default do else extern float floor for from
ftoi funcptr function global goto if int itof locals loop native null nullfunc
pointer readonly returns switch temporary to while
```

### Integer literals

Three forms are accepted:

```impala
123          // decimal
0x1F         // hexadecimal
'A'          // character literal — the word value of the character(s)
'abcd'       // multi-character literal, packed into one word
```

### Float literals

A float **must** have at least one digit on both sides of the decimal point, with an
optional exponent. There is no float suffix; the decimal point is what distinguishes a
float from an integer.

```impala
1.0          // valid
3.14159
6.022e23
1.           // INVALID — needs a digit after the point
.5           // INVALID — needs a digit before the point
```

### String literals

A string literal yields a pointer to a zero-terminated array of word-sized characters
(words are typically 32-bit). Its type is the generic `pointer`.

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

```impala
int pointer p                    // pointer to int
int array a[10]                  // array of 10 ints
float array buf[64]              // array of 64 floats
int pointer array table[8]       // array of 8 (pointers to int)
int pointer pointer pp           // pointer to pointer to int
```

The element type is a zero-cost overlay: at runtime a typed pointer is still one word and
a typed array is still N words, with stride 1 word — the generated GAZL is identical to
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
ints), so `("0123456789abcdef")[v & 0xf]` needs no cast. The boundary rules are asymmetric — *erasing is silent, assuming
is loud*:

- typed → untyped (`pointer raw = p`, passing a typed buffer to a native): **implicit**;
- untyped → typed: **requires a cast** (`p = (int pointer) raw`);
- differing element types: **requires a cast** (`(float pointer) intPtr` reinterprets);
- `null` assigns into any pointer type.

Bare `pointer` and `array` remain valid and mean untyped raw-word storage — the escape
hatch for host boundaries, with exactly the 1.0 semantics.

Function arguments and return values are untyped unless declared with typed forms.
External functions need no prototypes, and multiple Impala sources can still be linked
simply by concatenating their assembled `.gazl` output; casts are only needed where code
genuinely crosses between the typed and untyped worlds (see [Casting](#casting)).

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

`const` introduces a compile-time constant (a *define*). It occupies no memory and can
be used for array sizes and other compile-time values. The value must be a compile-time
constant expression.

```impala
const int SOME_COUNT = 4
```

Some constants are supplied by the VM or host at load time and are declared without a
value:

```impala
const int GAZL_WORD_SIZE
const int DEBUG
```

### `readonly`

`readonly` data is stored in global memory but cannot be modified — any attempt to write
it is a runtime error. Unlike `const`, it is real storage and is accessed with the
`global` keyword.

```impala
readonly int IMMUTABLE = 42
readonly array SOME_CONSTS[SOME_COUNT] = { 100, 200, 300, 400 }
```

Read a readonly array with the `global` prefix even though the declaration begins with
`readonly`:

```impala
x = (int) global SOME_CONSTS[i]
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
extern array futureArray
extern function thisFunctionInAnotherSource
extern native abort
```

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
global aFuncPointer = showoff
if (global aFuncPointer != nullfunc)
        global aFuncPointer()
```

`null` is the corresponding null value for ordinary `pointer`s.

## Functions

A function declares arguments, optional return value, and optional locals. Arguments and
the return value are untyped storage; their `int`/`float`/etc. declarations only name the
slot.

```impala
function fetchSomeConst(int index)
returns int fetched
{
        fetched = (int) global SOME_CONSTS[index]
}
```

Locals are declared in a single `locals` clause and may include arrays:

```impala
function test()
locals int i, array mydata[TEST_SIZE]
{
        for (i = 0 to TEST_SIZE)
                mydata[i] = myrand()
}
```
Function arguments and return values are untyped. Pointers and arrays carry no
compile-time type either, so casts are common. A side effect of this simplicity is
that external functions require no prototypes and multiple Impala sources can be linked
just by concatenating their assembled `.gazl` output. Signature metadata records those
contract shapes in comments without changing the generated instructions, so legacy
assemblers continue to accept the files. Casting does not convert between ints and
floats; use `itof()` or `ftoi()` for that.

Functions cannot be nested. There are no prototypes; a function may call another defined
later in the same source or in another linked source.

## Statements

### Assignment

Assignment uses `=` and is itself an expression that yields the assigned value, so it may
appear inside a larger expression:

```impala
a = b = 0
while ((c = nextValue()) != 0) { /* ... */ }
```

There are no compound assignment operators (`+=`, `&=`, …) and no `++`/`--`.

The assignable forms (lvalues) are a variable, a pointer dereference `*p`, and a
subscript `a[i]`.

### Conditionals

```impala
if (x > 0)
        positive()
else if (x < 0)
        negative()
else
        zero()
```

The condition is a parenthesized boolean expression (see
[Conditions](#conditions-and-boolean-expressions)).

### Loops

`for` increments its variable by one and stops **before** the upper bound. The
initializer is optional, and the loop variable must be a local `int` or `pointer`:

```impala
for (i = 0 to TEST_SIZE)        // i runs 0,1,...,TEST_SIZE-1
        mydata[i] = myrand()

for (i to TEST_SIZE)            // reuses i's current value as the start
        consume(i)
```

Other loops:

```impala
while (cond) { /* ... */ }
do { /* ... */ } while (cond)
loop { /* runs forever; exit with goto or return */ }
```

Use `goto` and labels to break out of loops manually:

```impala
loop {
        if (done) goto finished;
        step();
}
finished:
```

### `switch`

`switch` tests an integer expression against an inclusive range written as
`== low to high`. If the value falls outside the range, the `default` case runs. Cases do
**not** fall through, so there is no `break`. A case can list several values.

```impala
switch (i == 0 to 10) {
        case 0,1,2: {
                j = i
        }
        case 5: x()
        default: j = -1
}
```

### `copy`

Copies a fixed number of words from one pointer to another. The count must be a
compile-time constant.

```impala
copy(3 from &global initedArray[1] to &global futureArray[0])
```

### `assert`

Performs a runtime check, but only when the `DEBUG` constant is non-zero. When `DEBUG`
is zero the check (and its message) are compiled out.

```impala
assert(i != 0);
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
| Prefix / postfix | prefix `-` `~` `&` `*`, casts `(type)`, `abs` `floor` `itof` `ftoi`; postfix `()` `[]` | — |

> **Important:** all bitwise and shift operators share **one** precedence level that binds
> *looser* than `+` and `-`. (Arithmetic-vs-bitwise actually agrees with C: `x & 0xFF + 1` is
> `x & (0xFF + 1)` in both. The divergence from C is *within* the bitwise family, which C ladders
> internally.) Since Impala 2, **mixing different bitwise/shift operators at the same
> parenthesization level is a compile error** — write `(a | b) & c`, never `a | b & c`. The same
> applies to an unparenthesized bitwise operator directly against a comparison in a condition:
> write `if ((a & 3) == 0)`. Same-operator chains need no parentheses (`a | b | c`). The
> `--legacy` compiler argument downgrades these errors to warnings for old code, which should
> simply be updated — the parenthesized form compiles to identical GAZL.

Details that are easy to miss:

- `>>` is an **arithmetic** right shift and preserves the sign bit. `>>>` is a **logical**
  right shift that fills with zeroes — usually what you want for bit masks, hashing, and
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

flag = (a < b)        // INVALID — comparisons are not values
```

`&&` and `||` short-circuit. To capture a comparison result, branch on it and assign
inside the branches.

### Casting

A cast `(type) expr` reinterprets a value's type; it does **not** convert the
representation. To convert between integers and floats use the built-ins `itof()` and
`ftoi()`.

```impala
f = itof(n)           // int -> float (value conversion)
n = ftoi(f)           // float -> int (value conversion)
p = (pointer) x       // retype only, no conversion
```

Because function return values and pointer/array elements are untyped, they usually need
a cast before use in a typed context:

```impala
y = (int) lfoVal(...) + 1     // a bare untyped value + int needs the cast
z = lfoVal(...)               // a plain assignment is fine without it
```

### Built-in functions

Impala has exactly four built-in functions; everything else is either a statement
keyword (`copy`, `assert`) or a host-supplied `extern native` function.

| Built-in | Description |
|---|---|
| `abs(x)` | absolute value; works on `int` and `float` |
| `floor(x)` | floor; works on `float` |
| `itof(n)` | convert `int` to `float` |
| `ftoi(f)` | convert `float` to `int` |

Trigonometric and exponential functions are **not** built in. Either precompute them into
tables at load time (`ringmod_code.impala` builds a cosine table with a Taylor series in
`init()`), or copy a ready-made implementation from the snippets library described below.

### Standard snippets library

Impala has no `#include` and no separate compilation units, so there is no linked
standard library. Instead, [`examples/Firmwares/Impala Snippets.txt`](../../examples/Firmwares/Impala%20Snippets.txt)
collects reusable, copy-paste functions and data. Copy only what a firmware needs. It
includes:

- **Math**: `exp`, `log`, `log2`, `log10`, `pow`, `sqrt`, `sin`, `cos`, `tan`, `trunc`,
  `round`, `ceil`, `fmod`, `minInt`/`maxInt`, `minFloat`/`maxFloat`, the `xorShiftRandom`
  generator, and constants such as `PI`, `E`, and `HALF_PI`.
- **Strings**: `strlen`, `strcpy`, `strcat`, `strcmp`, `strncmp`, `stpcpy`.
- **Conversion**: `intToString`, `floatToString`, `stringToFloat`.
- **Tracing**: `traceInt`, `traceInts`, `traceFloat`, `traceFloats`, and `error`.
- **Permut8 tables**: `EIGHT_BIT_EXP_TABLE` and `SEVEN_BIT_EXP_TABLE`, the exact
  exponential tables used by the built-in operators (see
  [Operand Scaling Conventions](../../docs/Operand%20Scaling%20Conventions.md)).

(The other way to share code is to assemble each source separately and concatenate the
resulting `.gazl` files, since linking is just concatenation.)

### Pointers and arrays

`&` takes an address; `*` dereferences. Pointer arithmetic is supported: `pointer + int`
and `pointer - int` yield a pointer, and `pointer - pointer` yields the `int` element
distance.

```impala
global pointer p = &global defaultArray[0]
x = *(p + 3)
```

The subscript `[]` works on any pointer, not just declared arrays: `p[i]` is equivalent to
`*(p + i)`. The index may be negative, and the pointer may even be a string literal:

```impala
last = (int) p[-1]
hexDigit = ("0123456789abcdef")[value & 0xf]
```

## The Impala tools

The active compiler is generated by JSPEG and shipped as JavaScript under
`impala/`:

- `impalaCompiler.js` – generated compiler used by the build and command-line workflow
- `impala.nuxjs.js` – NuXJS command-line wrapper around the generated compiler
- `impala.jspeg` – grammar source used when regenerating the compiler
- `runJspegTests.js` – parity test runner for the JSPEG compiler fixtures

The build system compiles the NuXJS command-line runtime from `externals/NuXJS`
and places it in `output/`. The `BuildImpala` scripts then copy
`impala.nuxjs.js` and `impalaCompiler.js` to `output/` so Impala sources can be
compiled without Node.js.

### Signature metadata and validation

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
call sites. This keeps the Impala surface unchanged—bare extern
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
`docs/nativeCallbackSignatures.gazl`. Keep that manifest in sync with the host
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
matching, or assign per-slot types to arrays. Impala arrays do not currently
have element types, so array rows normally use `: unknown`; that field is
reserved for future metadata rather than a promise about the array contents.

Run the validator on every `.gazl` unit that will be concatenated or
loaded together. It compares the expectations recorded by callers with
the definitions supplied by the same file or by other units. After
building the toolchain (`bash build.sh`) you can compile two sample
sources and validate them without leaving the `output/` directory:

```bash
cd output
./NuXJS impala.nuxjs.js ../tests/impala/sources/calc.impala calc.gazl 0x4d2 calc.impala
./NuXJS impala.nuxjs.js ../tests/impala/sources/multitap_code.impala multitap.gazl 0x4d2 multitap_code.impala
bash ../tools/gazl-validate.sh calc.gazl multitap.gazl
```

From the repository root, run the validator directly on any already
compiled files:

```bash
bash tools/gazl-validate.sh output/calc.gazl output/multitap.gazl
```

The validator reports mismatched signatures as errors by default. Pass
`--warn-only` to downgrade them while you migrate existing modules, or
`--force` to turn missing-definition warnings into errors. Missing metadata and
missing definitions remain warnings by default and do not make the validator exit
with failure unless `--force` is used. The normal `build.sh` path runs the
validator's regression tests and validates the generated JSPEG fixture metadata
with explicit file sets. For other programs, run `tools/gazl-validate.sh` or
`tools\gazl-validate.cmd` directly on the exact `.gazl` units that will be
linked together.

See [Impala JSPEG](../impala/ImpalaJS.md) for the CLI, regeneration flow, and
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
