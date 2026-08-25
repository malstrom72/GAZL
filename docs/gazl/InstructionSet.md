# Instruction Set

Descriptions extracted from [`src/UnitTest.gazl`](../../src/UnitTest.gazl).

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

> Nothing depends on `*size` for MEMORY SAFETY: at run time `ADRL` only computes an address, and the resulting pointer
> is bounds-checked on every `PEEK`/`POKE`. Supply it anyway. It is added to the function's frame requirement, so a
> declared span turns a stack exhaustion into a `DATA_STACK_OVERFLOW` at function entry - deterministic, and pointing at
> the culprit - instead of a late `BAD_POKE` on whichever path happens to run. It is also what tells a compiling backend
> that these slots are address-exposed and must live in real memory. See `docs/impala/MemorySafetyModel.md`.

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
- `tgt`
- `tgt             %temp           *size`

(The `ptr` forms are GAZL 1: inside a `GAZL #2` region an indirect call takes a `tgt` local or an untyped `%N`
slot, and a `ptr` callee is an assembly error - see `GAZL`.)

Function call. %temp should specify the "transient" variable for the first parameter (e.g. %0, %1, %2 etc). *size is the
number of parameters (counting both input and output parameters). In GAZL 1.0 there is no compile-time check on the
types and number of parameters passed to a function. The size operand is merely a hint that might be used to optimize
stack frame sizes or for bounds checking etc.

> Nothing depends on `*size` for MEMORY SAFETY here either: a GAZL callee re-checks its own frame in its `FUNC`
> prologue, and a native callee declares the word count it wants when it reads the parameters, which is where that
> bound is enforced. Supply it anyway, for the same reason as `ADRL`: it makes the caller's frame requirement cover the
> call window, so an exhausted stack is reported at entry rather than deeper in. See `docs/impala/MemorySafetyModel.md`.

A function pointer (the value of `&function`) is an opaque handle: a stable ordinal assigned in function declaration
order, not a code address. Equality (`EQUp` / `NEQp`), ordering (`LSSp`, `GEQp` etc.), difference (`DIFp`) and calling are defined
operations on a function pointer. Ordering is a TOTAL, run-stable order and nothing more - which ordinal a function
receives follows declaration order, so sort a table of function pointers and binary-search it, but never read meaning
into the order itself. `DIFp` between two function pointers is likewise defined - it is their ordinal distance.
Arithmetic that PRODUCES a function pointer (`ADDp`, `SUBp` offsetting one) yields an unspecified result: the ordinal
it lands on is a perfectly valid one, so it names a different function rather than failing.

> **GAZL 1 does not enforce that contract; GAZL 2 does, inside a `GAZL #2` region.** In GAZL 1 all of
> those assemble, because `p` covers both data pointers and function pointers - `ADDp` is the one that
> bites: `&one + 1` is a valid ordinal, so it does not trap, it silently calls a different function.
> Functions declared inside a `GAZL #2` region (see `GAZL`) have `t` (target) addresses instead: `t`
> offers no operation whose RESULT is a `t` - a target may be named, copied (`MOVt`) or loaded (`DATt`,
> a `PEEK` into a `LOCt`), never computed - so the undefined operations are unrepresentable rather than
> merely undefined. Ordering, equality and difference are all kept (`LSSt`, `EQUt`, `DIFt`): they consume
> targets and hand back a bool or an int, so none of them can name a function. `CALL` accepts both
> dialects' functions, which is what keeps concatenated GAZL 1 and GAZL 2 units calling each other. See
> [`design/gazl/GAZL2FunctionPointers.md`](../../design/gazl/GAZL2FunctionPointers.md).

## CNST
- `*size`

Declare a constant data section / array `*size` defines the size of the section (in number of words). Define the data
with DAT directives. You may not define more data items than `*size`, but you may define fewer (remaining items will be
zeroed). Data in a constant section is placed in a write-protected segment of the run-time memory. See consts
declarations in top of this file for examples.

## DATA
- `#const #const #const ...`

Define several constant values of MIXED types in one statement - `consts.mixed` in `src/UnitTest.gazl`
is the canonical example. `DATA` is not "the multi-value one": `DATf`, `DATi` and `DATp` take as many
operands as you like too. Mixing is the whole of what it adds, and it pays for that by giving up the
type check - `DATA` accepts any constant, so nothing here catches a float written where an int was meant.

## DATf
- `#float #float ...`

Float constant data items. Every operand on the line must be a float, `#NAME` included: a constant
declared `! DEFi` is rejected as `Incompatible types`.

## DATi
- `#int #int ...`

Integer constant data items. Every operand on the line must be an int (see `DATf`).

## DATp
- `&address &address ...`

Pointer constant data items. Every operand on the line must be an address (see `DATf`).

Note `p` covers BOTH data pointers and function pointers in GAZL 1, which are different things - a data
pointer is a memory address, a function pointer is a declaration-order ordinal. So `DATp &func &data`
assembles there. Inside a `GAZL #2` region `DATp` takes no function address at all - a GAZL 2 function's
address is a `t` value (`DATt`), and even a GAZL 1 function's address is refused from `p` positions
within the region. See `DATt` and [`design/gazl/GAZL2FunctionPointers.md`](../../design/gazl/GAZL2FunctionPointers.md).

## DATs
- `string`

String constant data item, one word per character. Unlike the four above `DATs` takes NO operands: the
rest of the line is the literal, spaces and all, with trailing blanks stripped. It appends no terminating
zero - follow it with `DATi #0` if you need one.

## DATt
- `&function &function ...`

Call-target constant data items (GAZL 2 and later) - a funcptr table. Every operand must be the address
of a function declared inside a `GAZL #2` region (or `&NULL`); the assembler re-checks each one, which is
the check `DATp` could never make because `p` accepts any address.

## DEFf
- `#float`

Define a compile-time constant float

## DEFi
- `#int`

Define a compile-time constant integer

## DEFp
- `&address`

Define a compile-time constant pointer
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

## DIFt
- `int(d)          &function       &function`
- `int(d)          &function       tgt`
- `int(d)          tgt             &function`
- `int(d)          tgt             tgt`

Difference of two call targets (GAZL 2 and later): their ordinal distance, an int. This is the `SUBp`/`DIFp` split
applied to targets - `t - t -> int` consumes two targets and names none, while `SUBt`/`ADDt`/`FORt` would each
PRODUCE a target and so do not exist. The two-constant variant needs both functions declared first, as `DIFp` does.

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

## ENDS

Closes the innermost `SCOP` (GAZL 2 and later). See [SCOP](#scop).

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

## EQUt
- `&function       &function       @label`
- `&function       tgt             @label`
- `tgt             &function       @label`
- `tgt             tgt             @label`

Branch on equal call targets (GAZL 2 and later) - "is this the same function?". `NEQt`, `LSSt`, `GRTt`, `LEQt` and
`GEQt` take the same forms; ordering is a TOTAL, run-stable order (sort a target table, binary-search it), nothing
more. Both operands must be targets - a target and a data pointer cannot meet in one comparison, which `EQUp`'s
`ANY_FREE` forms never enforced.

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

The assembler attaches two computed constants to `FUNC`: the size of the declared frame (which advances the stack
pointer) and the highest fixed offset the body reaches (which allocates nothing). Together they form a single entry-time
stack check, which is why accesses at fixed offsets need no check of their own. See `docs/impala/MemorySafetyModel.md`.

A function declared inside a `GAZL #2` region has a `t` (target) address instead of a `p`-compatible one - see `GAZL`.

## GAZL
- `#version`

Set the GAZL dialect for what follows (GAZL 2 and later; an engine too old to know the mnemonic rejects it as
unknown, which is as close to a version diagnostic as it can give). `GAZL #2` opens a GAZL 2 region and `GAZL #1`
closes it; outside any region the dialect is 1, so every pre-existing file means what it always did. A file must be
back in dialect 1 at the end (`GAZL 2 region not closed with GAZL #1`), and a declared version above the engine's is
`File requires a newer GAZL engine`.

Inside a `GAZL #2` region:

- `FUNC` declares functions whose addresses are `t` (target) values, accepted by the `t` instruction family and by
  `CALL` - but by no `p` position, in this region or any other unit.
- No `p` position accepts ANY function address, GAZL 1 functions included: within the region, a function is never a
  data pointer.
- An indirect `CALL` goes through a `t` local (or an untyped `%N` window slot), never a `p` local: `CALL p0` is an
  assembly error inside the region, where in GAZL 1 it was legal and guarded only by the run-time `BAD_CALL` check.

The protection travels with the SYMBOL, so it survives the region: a GAZL 2 function's address cannot enter a `p`
slot anywhere in the program, and the region's own rows cannot smuggle one in through `DATp`/`MOVp`/`POKE`.

**Emitting GAZL for concatenation.** GAZL programs are linked by plain concatenation, and the bracket is what makes
that safe: emit `GAZL #2` as the first mnemonic line of a unit and `GAZL #1` as its last, so the unit carries its
dialect with it and hands the stream back to the default when it ends. Bracketed and unbracketed units then
concatenate in ANY order and mix - GAZL 1 units are never re-interpreted, `CALL`s cross freely in both directions,
and a truncated unit is caught by the end-of-file check instead of silently re-typing whatever follows. This is
exactly what Impala's `--gazl2` output does.

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

## GEQt
- see `EQUt`

Branch on greater or equal call target (GAZL 2 and later).

## GETL
- `var(d)          var             int`

Get local variable `var` (any type) with offset `int`

The offset is dynamic, so it is bounds-checked on every access, against the end of the data stack rather than the extent
of `var`. See `SETL` and `docs/impala/MemorySafetyModel.md`.

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

## GRTt
- see `EQUt`

Branch on greater call target (GAZL 2 and later).

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

## INPt

Declare a local read-only call-target parameter (GAZL 2 and later)
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

## LEQt
- see `EQUt`

Branch on less or equal call target (GAZL 2 and later).

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

## LOCt

Declare a local call-target variable (GAZL 2 and later). A word `PEEK`ed into it is unchecked, exactly as one
`PEEK`ed into a `LOCi` is - memory stays typeless; `t` is a contract on named slots and constants.

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

## LSSt
- see `EQUt`

Branch on less call target (GAZL 2 and later).

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

## MOVt
- `tgt(d)          &function`
- `tgt(d)          tgt`

Move a call target (GAZL 2 and later). The constant form takes only a `GAZL #2` function's address or `&NULL`.

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

## NEQt
- see `EQUt`

Branch on unequal call targets (GAZL 2 and later).

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

## OUTt

Declare a local call-target output parameter (GAZL 2 and later)

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

## SCOP

Opens a local scope (GAZL 2 and later). Like `LOC` / `PARA` declarations, `SCOP` and its matching `ENDS` must appear
after a `FUNC` declaration but before any real instruction.

Declarations inside a scope are released by `ENDS`, so the NEXT sibling scope reuses the same frame offsets while a
NESTED scope stacks on top. A function's frame is therefore the DEEPEST nesting chain, not the sum of every
declaration. Use it for groups whose lifetimes do not overlap - an expanded inline body being the motivating case,
including inlines within inlines.

    $g:     LOCi            ; outlives everything - declared before any scope
            SCOP
    $a:     LOCA *SIZE_A    ; offset 1
            SCOP
    $b:     LOCi            ; offset 1 + SIZE_A  (nested: stacks)
            ENDS
            ENDS
            SCOP
    $c:     LOCA *SIZE_C    ; offset 1 again     (sibling: overlays $a and $b)
            ENDS

Sizes are resolved before offsets are assigned, so `LOCA *SOME_CONSTANT` overlays correctly even when only the host
knows the value - which is what makes this usable for a host-owned struct size. Anything that must outlive a scope has
to be declared BEFORE the first `SCOP`; a declaration placed after `ENDS` reuses the released space.

Unbalanced `SCOP` / `ENDS` is an error, as is nesting deeper than 32.

Code that wants to run on either engine can guard its use with `! GEQi #GAZL_VERSION #2 @label`, since a skipped
conditional region is not parsed for mnemonics - the scoped variant is ignored entirely on a GAZL 1 engine. (Use
`! EQUi` instead when you deliberately mean one exact version, as `src/UnitTest.gazl` does.)

## SEEK
- `:offset`
- `:offset *extent`

Set the data cursor to an assemble-time `:offset` within the current data section and open a bounded region there
(GAZL 2 and later). The `DAT*` rows that follow fill the region sequentially, exactly as they fill a section. Both
operands take any assemble-time constant - `.o.` / `.z.` symbols and `<X>` variables included - so data lands at
offsets a host-supplied layout decides, without the emitter knowing the numbers.

With `*extent` the region claims `offset .. offset + extent` whole: filling past the extent is an error, and claimed
words that are never written stay zero. Without `*extent` the region claims exactly the words it writes and is fenced
by the section end. Unwritten words are zero either way, and regions may be opened in any offset order. Prefer
stating the extent even where it seems redundant - Impala emits `*1` on scalar fields - because it turns a
miscounted row into an error at that row rather than a silent write into whatever the layout puts next.

    voice:  GLOB *.z.Voice
            SEEK :.o.Voice.note
            DATi #60
            SEEK :.o.Voice.state *.z.Voice.state
            DATf #1.0 #2.0                       ; 2 of 4 words written - the tail stays zero
            SEEK :.o.Voice.gain
            DATf #0.5

A section containing no `SEEK` is a single implicit region at offset 0 - GAZL 1 semantics, unchanged. An offset or
extent reaching past the section is `Offset out of bounds` at the `SEEK` row; two regions of one section overlapping
is `Data regions overlap`, checked when a region closes (at the next `SEEK`, the next section, or the end of
assembly). The `! GEQi #GAZL_VERSION #2` guard idiom above applies to `SEEK` equally. The runnable proof that placed
data survives a layout re-pack is `design/proofs/seekRegions.gazl` and its twin; the design rationale is
`design/gazl/GAZL2DataRegions.md`.

## SETL
- `var(d)          int             #const`
- `var(d)          int             var`

Set local variable `var` (any type) with offset `int`

The offset is dynamic, so it is bounds-checked on every access - but against the end of the data stack, not against the
extent of `var`. An overrun stays inside the sandbox and typically corrupts the writing function's own frame first. See
`docs/impala/MemorySafetyModel.md`.

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

