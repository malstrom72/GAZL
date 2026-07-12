# Instruction Set

Descriptions extracted from [`src/UnitTest.gazl`](../src/UnitTest.gazl).

**Floating-point environment.** Integer and `FTOi` results are identical on every target (two's-complement wrap,
count-mod-32 shifts, saturating `FTOi`; see the individual opcodes). Float arithmetic (`ADDf`, `SUBf`, `MULf`, `DIVf`,
`FLOf`, and so on) is IEEE-754 single precision, but its rounding mode and denormal handling (FTZ/DAZ) follow the host's
current floating-point environment. GAZL does not set or restore it. Results that involve denormals are therefore
host-defined and can differ between callers (for example an audio thread with flush-to-zero enabled versus a default
thread) or between platforms. For bit-reproducible float results, run GAZL under a fixed FP environment (round-to-nearest
plus a deliberate FTZ/DAZ choice) around `run()`.

## ABSf
- `float(d)        #float`
- `float(d)        float`

Absolute of float

## ABSi
- `int(d)          #int`
- `int(d)          int`

Absolute of int

## ADDf
- `float(d)        #float          #float`
- `float(d)        #float          float`
- `float(d)        float           #float`
- `float(d)        float           float`

Add floats

## ADDi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Add ints

## ADDp
- `ptr(d)          &address        #int`
- `ptr(d)          &address        int`
- `ptr(d)          ptr             #int`
- `ptr(d)          ptr             int`

Add to pointer

## ADRL
- `ptr(d)          var             *size`

Load effective address of local `var` into `ptr(d)` `*size` should hint how many words is expected to be accessed from
`ptr(d)`, i.e. typically one for a single variable or the array size for arrays. It may be used to optimize stack frame
sizes, for bounds checking etc. It is always legal to specify a size of zero if you do not know.

## ANDi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Bitwise AND ints

## CALL
- `&function`
- `&function       %temp           *size`
- `^native`
- `^native         %temp           *size`
- `ptr`
- `ptr             %temp           *size`

Function call. %temp should specify the "transient" variable for the first parameter (e.g. %0, %1, %2 etc). *size is the
number of parameters (counting both input and output parameters). In GAZL 1.0 there is no compile-time check on the
types and number of parameters passed to a function. The size operand is merely a hint that might be used to optimize
stack frame sizes or for bounds checking etc.

A function pointer (the value of `&function`) is an opaque handle: a stable ordinal assigned in function declaration
order, not a code address. Only equality (`EQUp` / `NEQp`) and calling are defined operations on a function pointer;
ordering (`LSSp`, `GEQp` etc.) and arithmetic (`ADDp`, `SUBp`, `DIFp`) applied to a function pointer yield an
unspecified (but memory-safe) result.

## CNST
- `*size`

Declare a constant data section / array `*size` defines the size of the section (in number of words). Define the data
with DAT directives. You may not define more data items than `*size`, but you may define fewer (remaining items will be
zeroed). Data in a constant section is placed in a write-protected segment of the run-time memory. See consts
declarations in top of this file for examples.

## DATA
- `#const #const #const ...`

Define several constant values of mixed types in one statement.

## DATf
- `#float`

Float constant data item

## DATi
- `#int`

Integer constant data item

## DATp
- `&address`

Pointer constant data item

## DATs
- `string`

String constant data item

## DEFf
- `#float`

Define a compile‑time constant float

## DEFi
- `#int`

Define a compile‑time constant integer

## DEFp
- `&address`

Define a compile‑time constant pointer
## COPY
- `&address(w)     &address(r)     *size`
- `&address(w)     ptr             *size`
- `ptr             &address(r)     *size`
- `ptr             ptr             *size`

Copies memory. Behaviour is undefined if target and source overlaps. This instruction can be used to accelerate memory
access since global and constant data must otherwise be accessed with individual PEEK and POKE instructions. With COPY
you can copy a range of global / constant data to a local array which can then be used directly with arbitrary
instructions. (Just remember that you need to use ADRL to obtain the address to the local array.)

## DIFp
- `int(d)          &address        &address`
- `int(d)          &address        ptr`
- `int(d)          ptr             &address`
- `int(d)          ptr             ptr`

Difference of two pointers You cannot use SUBp to subtract a pointer from another. SUBp is only used for negatively
offsetting a pointer. Notice that for the first variant with two constant addresses, both addresses need to be declared
before the instruction. (The other variants accepts forward declarations.) The motivation behind this exception is that
the difference of two constant addresses becomes another constant, and this may be utilized for local optimizations etc.

## DIVf
- `float(d)        #float          #float`
- `float(d)        #float          float`
- `float(d)        float           #float`
- `float(d)        float           float`

Divide floats Division by 0 is considered illegal and should generate a run-time error (or compile-time error if 0 is a
constant).

## DIVi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Divide ints. Division truncates towards zero (also for negative numbers); this is guaranteed by C++11, which GAZL
requires. `INT_MIN / -1` is defined to yield `INT_MIN` (no trap). Division by 0 is considered illegal and generates a
run-time error (or compile-time error if 0 is a constant).

## EQUf
- `#float          #float          @label`
- `#float          float           @label`
- `float           #float          @label`
- `float           float           @label`

Branch on equal floats

## EQUi
- `#int            #int            @label`
- `#int            int             @label`
- `int             #int            @label`
- `int             int             @label`

Branch on equal ints

## EQUp
- `&address        &address        @label`
- `&address        ptr             @label`
- `ptr             &address        @label`
- `ptr             ptr             @label`

Branch on equal pointers

## FLOf
- `float(d)        #float`
- `float(d)        float`

Floor of float. Notice that there is no ceil instruction, but ceil is equivalent to -floor(-x).

## FORi
- `int(d)          #int            @label`
- `int(d)          int             @label`

Increment `int(d)` and branch to `@label` if it is less than `#int` / `int`

## fTOi
- `int(d)          #float          #float`
- `int(d)          float           #float`

Convert float to int. The float is first scaled with the third operand before it is converted to int. The conversion
truncates toward zero; out-of-range values saturate to INT_MAX / INT_MIN and NaN converts to 0 (see the Floating-point
environment note near the top).

## FORp
- `ptr(d)          &address        @label`
- `ptr(d)          ptr             @label`

Increment `ptr(d)` and branch to `@label` if it is less than `&address` / `ptr`

## FUNC

Declares the beginning of a new function. Any previous function must have ended with either RETU or GOTO.

## GEQf
- `#float          #float          @label`
- `#float          float           @label`
- `float           #float          @label`
- `float           float           @label`

Branch on greater or equal float

## GEQi
- `#int            #int            @label`
- `#int            int             @label`
- `int             #int            @label`
- `int             int             @label`

Branch on greater or equal int

## GEQp
- `&address        &address        @label`
- `&address        ptr             @label`
- `ptr             &address        @label`
- `ptr             ptr             @label`

Branch on greater or equal pointer

## GETL
- `var(d)          var             int`

Get local variable `var` (any type) with offset `int`

## GLOB
- `*size`

Declare a global (writable) data section / array `*size` defines the size of the section (in number of words). You may
optionally want to define the initial data with

## GOTO
- `@label`

Unconditionally branch to `@label`

## GRTf
- `#float          #float          @label`
- `#float          float           @label`
- `float           #float          @label`
- `float           float           @label`

Branch on greater float

## GRTi
- `#int            #int            @label`
- `#int            int             @label`
- `int             #int            @label`
- `int             int             @label`

Branch on greater int

## GRTp
- `&address        &address        @label`
- `&address        ptr             @label`
- `ptr             &address        @label`
- `ptr             ptr             @label`

Branch on greater pointer

## IFDF
- `&address        @label`
- `#const          @label`
- `^native         @label`

Branch to `@label` if the compile-time symbol or address is defined

## IFND
- `&address        @label`
- `#const          @label`
- `^native         @label`

Branch to `@label` if the compile-time symbol or address is not defined

## INPf

Declare a local read-only float parameter

## INPi

Declare a local read-only int parameter

## INPp

Declare a local read-only pointer parameter
## IORi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Bitwise (inclusive) OR ints

## iTOf
- `float(d)        #int            #float`
- `float(d)        int             #float`

Convert int to float. The float is scaled with the third operand after it has been converted from int.

## LEQf
- `#float          #float          @label`
- `#float          float           @label`
- `float           #float          @label`
- `float           float           @label`

Branch on less or equal float

## LEQi
- `#int            #int            @label`
- `#int            int             @label`
- `int             #int            @label`
- `int             int             @label`

Branch on less or equal int

## LEQp
- `&address        &address        @label`
- `&address        ptr             @label`
- `ptr             &address        @label`
- `ptr             ptr             @label`

Branch on less or equal pointer

## LOCA
- `*size`

Declare a local data section / array (any type) `*size` defines the size of the section (in number of words). LOCA
declarations must appear after a FUNC declaration but before any real instruction. You would not normally place LOCA
before INP, OUT and PARA declarations either.

## LOCf
Declare a local float variable

## LOCi

Declare a local int variable

## LOCp

Declare a local pointer variable

## LSSf
- `#float          #float          @label`
- `#float          float           @label`
- `float           #float          @label`
- `float           float           @label`

Branch on less float

## LSSi
- `#int            #int            @label`
- `#int            int             @label`
- `int             #int            @label`
- `int             int             @label`

Branch on less int

## LSSp
- `&address        &address        @label`
- `&address        ptr             @label`
- `ptr             &address        @label`
- `ptr             ptr             @label`

Branch on less pointer

## MODi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Modulus two ints. The remainder takes the sign of the dividend (truncation towards zero); this is guaranteed by C++11,
which GAZL requires. `INT_MIN % -1` is defined to yield `0` (no trap). Modulus by 0 is considered illegal and generates a
run-time error (or compile-time error if 0 is a constant).

## MOVE
- `var(d)          var`
Move any variable to another variable

## MOVf
- `float(d)        #float`
- `float(d)        float`

Move a float value

## MOVi
- `int(d)          #int`
- `int(d)          int`

Move an int value

## MOVp
- `ptr(d)          &address`
- `ptr(d)          ptr`

Move a pointer value

## MULf
- `float(d)        #float          #float`
- `float(d)        #float          float`
- `float(d)        float           #float`
- `float(d)        float           float`

Multiply two floats

## MULi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Multiply two ints

## NEQf
- `#float          #float          @label`
- `#float          float           @label`
- `float           #float          @label`
- `float           float           @label`

Branch on unequal floats

## NEQi
- `#int            #int            @label`
- `#int            int             @label`
- `int             #int            @label`
- `int             int             @label`

Branch on unequal ints

## NEQp
- `&address        &address        @label`
- `&address        ptr             @label`
- `ptr             &address        @label`
- `ptr             ptr             @label`

Branch on unequal pointers

## NOOP

No operation. The NOOP instruction does nothing and will not consume any CPU cycles (it is effectively removed during
assembly). It can be used to define a branching label without associating it with a specific instruction since every
line in GAZL needs an opcode.

## OUTf

Declare a local float output parameter

## OUTi

Declare a local int output parameter

## OUTp

Declare a local pointer output parameter

## PARA
- `*size`

Declare a local parameter section / array (any type, input or output) `*size` defines the size of the section (in number
of words). PARA declarations must appear after a FUNC declaration but before any real instruction. You would also
normally place PARA before LOC declarations.

## PEEK
- `var(d)          &address(r)`
- `var(d)          &address(r)     int`
- `var(d)          ptr`
- `var(d)          ptr             #int`
- `var(d)          ptr             int`

Read a value from memory with an optional integer offset

## POKE
- `&address(w)     #const`
- `&address(w)     var`
- `&address(w)     int             #const`
- `&address(w)     int             var`
- `ptr             #const`
- `ptr             #int            #const`
- `ptr             #int            var`
- `ptr             var`
- `ptr             int             #const`
- `ptr             int             var`

Write a value to random access memory with an optional integer offset.

## RETU

Returns from function call.

## SETL
- `var(d)          int             #const`
- `var(d)          int             var`

Set local variable `var` (any type) with offset `int`

## SHLi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Shift bits left

## SHRi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int;`

Shift bits right (arithmetic, i.e. replicate most significant bit and keep sign)

## SHRu
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Shift bits right (logical, i.e. shift in zeroes and lose sign)

## SUBf
- `float(d)        #float          #float`
- `float(d)        #float          float`
- `float(d)        float           #float`
- `float(d)        float           float`

Subtract floats

## SUBi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Subtract ints

## SUBp
- `ptr(d)          &address        #int`
- `ptr(d)          &address        int`
- `ptr(d)          ptr             #int`
- `ptr(d)          ptr             int`

Subtract from pointer

## SWCH
- `int             *size           @label`

Switch (multi-way branch) The switch instruction creates an internal switch table of a chosen size (`*size`) for quick
multi-way branching on `int`. Case labels can be defined for integer values between 0 and `*size` - 1. Case labels
should be formed by appending integer constants to `@label` with either a period `.` or `#` in between. Use `#` to allow
any constant literal or compile-time variable. E.g. `@myLabel#'B'` and `@myLabel#<A>` are valid case labels. If `int` is
out of range (< 0 or >= `*size`) or a case label is not defined, the default case label (`@label`) is used. This label
is the only one that must be declared. The greater the `*size` the more memory will be required for the jump table, so
avoid huge switch ranges.

## TEMP
- `*size`

Like `GLOB` but hints that this section contains temporary data that can be ignored in case the GAZL host supports
memory serialization.

## XORi
- `int(d)          #int            #int`
- `int(d)          #int            int`
- `int(d)          int             #int`
- `int(d)          int             int`

Bitwise XOR ints

