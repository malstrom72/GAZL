# Impala Quick Start

Impala is a small, imperative language that compiles to the GAZL virtual machine.
This guide highlights the syntax and toolchain using the demo program as a reference.

## Syntax basics

### Comments

```impala
// Comments use C++ style slashes.
/* Block comments are also allowed. */
```

### Globals and constants

```impala
global int uninited
global int inited = 23
global float aFloat
```

The `global` keyword must prefix variables both in declarations and whenever they are accessed. Global loads and stores are more costly than local variables, so the prefix reminds you to minimize their use. Arrays and pointers are untyped:

```impala
global pointer aPointer = &global aFloat
global funcptr aFuncPointer
global array defaultArray[100]
```
Function pointers are assigned and called like any other pointer:

```impala
global aFuncPointer = showoff
if (global aFuncPointer != nullfunc)
        global aFuncPointer()
```

Constants are declared with `const` and may be used for array sizes or
compile-time values:

```impala
const int SOME_COUNT = 4
readonly array SOME_CONSTS[SOME_COUNT] = { 100, 200, 300, 400 }
```

### Arrays

Arrays hold any combination of values and only one dimensional arrays
are supported:

```impala
global array initedArray[10] = {
        1, 2.0, &global defaultArray[0], 4
}
```
### Strings

String literals yield pointers to zero-terminated arrays of word characters (typically 32-bit). The value uses the generic pointer type:

```impala
const pointer WELCOME = "Welcome to Impala!\n"
print(WELCOME)
```


### Functions

Functions declare arguments, locals and an optional return value:

```impala
function fetchSomeConst(int index)
returns int fetched
{
        fetched = (int) global SOME_CONSTS[index]
}
```

Locals are declared in one statement:

```impala
function test()
locals int i, array mydata[TEST_SIZE]
{
        for (i = 0 to TEST_SIZE)
                mydata[i] = myrand()
}
```
Function arguments and return values are untyped. Pointers and arrays carry no compile-time type either, so casts are common. A side effect of this simplicity is that external functions require no prototypes and multiple Impala sources can be linked just by concatenating their assembled .gazl output.
Casting does not convert between ints and floats; use itof() or ftoi() for that.

The demo ends with a simple `main`:

```impala
extern native print
function main()
locals float myFloat
{
        print(WELCOME)
        myFloat = 1000.0
        showoff()
        print("Bye!\n")
}
```
### Loops and switches

The `for` statement increments by one and stops before the upper bound. Other loops include `while`, `do ... while` and the unconditional `loop`. Use `goto` to jump to labels and manually exit loops.

```impala
for (i = 0 to TEST_SIZE)
        mydata[i] = myrand()
```
The `switch` statement tests a value within a range. If the expression is outside the given bounds the whole statement is skipped. Cases do not fall through, so no `break` is needed:

```impala
switch (i == 0 to 10) {
        case 0,1,2: {
                j = i
        }
        case 5: x()
        default: j = -1
}
```
The `copy` statement copies a fixed number of words from one pointer to another and `assert` performs a runtime check when the `DEBUG` constant is non-zero:

```impala
copy(3 from &global initedArray[1] to &global futureArray[0])
assert(i != 0);
```


### Expressions

Assignments are statements and cannot appear inside other expressions. Only the plain `=` operator exists. Bitwise operators share the same precedence and `>>>` performs an unsigned right shift. Relational expressions yield a boolean result that can only control `if`, `while` and similar statements.


## The Impala tools

The compiler and test suite are written in [PikaScript](https://github.com/mjs/pikascript)
and shipped as `.pika` files under `impala/`:

- `impala.pika` – command line driver able to `rebuild`, `compile` or `run` programs
- `impalaCompiler.pika` – generated compiler produced by the `rebuild` step
- `initPPEG.pika` – initializes the parsing library
- `systools.pika` – small helper routines used by the scripts
- `runTests.pika` – compiles all sources in the test suite

`PikaCmd` executes these scripts. The build system compiles PikaCmd from
`externals/PikaCmd` and places the executable in `output/`. The
`BuildImpala` scripts then call `PikaCmd impala.pika rebuild` to create
`impalaCompiler.pika` before copying all required files to `output/` for
convenient use.

## Compiling and running

After running `bash build.sh` the `output/` folder contains `PikaCmd` and
`GAZLCmd`. Compile an Impala source file like so:

```bash
cd output
./PikaCmd impala.pika compile ../impala/ImpalaDemo.impala demo.gazl
```

Execute the resulting program with the VM:

```bash
./GAZLCmd demo.gazl main
```

For convenience you can compile and run in one step:

```bash
./PikaCmd impala.pika run ../impala/ImpalaDemo.impala
```

