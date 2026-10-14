# GAZL 1 Specification

**DRAFT.** This document is meant to let someone who has never seen `src/GAZL.cpp` build a GAZL 1 implementation
that runs every existing program with identical results. It defines the machine model, the source format, execution,
the host interface and the memory snapshot format. The per-instruction reference is
[InstructionSet.md](InstructionSet.md); the conformance suite is [`src/UnitTest.gazl`](../../src/UnitTest.gazl).
Where this draft and the reference implementation disagree, the disagreement is a bug in one of them and is listed
under [Open questions](#13-open-questions).

"Must" marks a requirement on every implementation. "Unspecified" means an implementation may produce any result,
but must stay inside the sandbox (section 10). Nothing in GAZL 1 is undefined in the C sense.

## 1. Compatibility promise

GAZL programs are distributed as source text and assembled when they are loaded, often years after they were
written. So the format only ever grows:

- A program that assembles under one GAZL 1 implementation must assemble under every later one, and run with
  identical results, statuses and step counts.
- New instructions, new operand forms and new assembly-time constants may be added, but only where the same text
  used to be an assembly error.
- The meaning of an accepted form never changes, and no accepted form is removed.
- One exception: a form whose execution could break the sandbox may start being refused at assembly. That has
  happened once (frames past `INT_MAX`, `FRAME_TOO_LARGE`), and must be recorded in this section when it happens.
- Programs can test the version at assembly with `#GAZL_VERSION` (1) and `#GAZL_WORD_SIZE` (32), which every
  implementation predefines.
- An incompatible GAZL would be GAZL 2, with a different `#GAZL_VERSION`.

## 2. Values

Every value is one 32-bit **word**. A word has no tag; the instruction that uses it decides its type.

| type    | meaning                                                                                     |
| ------- | ------------------------------------------------------------------------------------------- |
| int     | two's-complement signed 32-bit integer                                                      |
| float   | IEEE-754 binary32                                                                           |
| pointer | `0x12345678 + n`, the word at index `n` of data memory (section 3); `0` is the null pointer |
| funcptr | `0x56789ABC + k`, the function with ordinal `k`, in declaration order across all units      |

A word moved with `MOVE`, `PEEK`, `POKE`, `COPY`, `GETL` or `SETL` keeps its 32 bits exactly, whatever its type,
including the bits of a float NaN.

Byte order is never visible to a program. Memory is an array of words, not bytes, and no instruction reads part
of a word. That is what lets GAZL run unchanged on big- and little-endian hosts.

## 3. Data memory

Data memory is one array of words, sized by the host. Index 0 is at pointer `0x12345678`.

    index 0                                                                       memorySize
    | globals (GLOB, TEMP) | data stack ...                  | constants (CNST) | zero guard |
    |<---------------------- writable: rwMemorySize -------->|<----------- read-only ------>|

- **Globals** are placed from index 0 upward, in declaration order.
- **Constants** are placed from the top downward, below the zero guard. The assembler also places switch tables
  there.
- **The zero guard** is the last word of memory. It always holds 0, so a native scanning for a string's terminator
  stops there. No checked read reaches it.
- **The data stack** is everything between the globals and the constants.

A read must succeed exactly when its index is in `[0, memorySize - 1)`, and a write exactly when its index is in
`[0, rwMemorySize)`. An address is `base + offset` computed with 32-bit wrap-around, so an offset can never carry
an access around the end of memory and back in.

The absolute addresses of constants depend on the memory size the host chooses. A program must not rely on them,
and an implementation must not assume programs don't compare them (they are ordinary pointers).

## 4. Arithmetic

**Integers.** `ADDi`, `SUBi` and `MULi` wrap modulo 2^32. Shift counts are taken modulo 32. `SHRi` is arithmetic and
`SHRu` logical. `DIVi` truncates toward zero and `MODi` takes the sign of the dividend; `INT_MIN / -1` is `INT_MIN`
and `INT_MIN % -1` is 0. Division or modulo by zero stops the program with `DIVISION_BY_ZERO`, and is refused at
assembly when the divisor is a constant 0.

**Floats.** Arithmetic is IEEE-754 binary32 with round-to-nearest-even. `DIVf` by zero (either sign) stops the
program with `DIVISION_BY_ZERO`; it never produces an infinity. Infinities and NaNs can still arise from overflow
and from operations like `inf - inf`. Denormal handling follows the host thread's floating-point environment (see
open question 1).

**Conversions.** `fTOi` multiplies by its scale operand, then truncates toward zero; values at or beyond 2^31 in
magnitude, and infinities, saturate to `INT_MAX` / `INT_MIN`, and NaN converts to 0. `iTOf` converts to the nearest
float, then multiplies by its scale operand.

**Float comparisons** branch as follows when either operand is NaN:

| instruction    | branches on NaN |
| -------------- | --------------- |
| `EQUf`         | no              |
| `NEQf`         | yes             |
| `LSSf`, `GRTf` | no              |
| `LEQf`, `GEQf` | **yes**         |

`LEQf a b` is defined as "not `b < a`" and `GEQf a b` as "not `a < b`". This differs from C's `<=` and `>=`, and
an implementation must follow it.

**Pointers.** `ADDp` and `SUBp` add or subtract an int, wrapping modulo 2^32. `DIFp` is the difference in words.
Pointer comparisons compare the two words as **signed** ints. A function pointer supports only `EQUp`, `NEQp` and
calling; any other operation on it gives an unspecified, memory-safe result.

## 5. Source text

A program is ASCII text, one statement per line. Lines end with LF, CR or CRLF. Bytes above 0x7F may appear only in
comments and string data (see open question 4).

    [label:]  [!]  MNEM  [operand  [operand  [operand]]]  [; comment]

- **Mnemonics** are exactly four characters and case-sensitive.
- **Separators.** Tokens are separated by spaces or tabs. A `;` starts a comment that runs to the end of the line.
- **Quotes.** A token may contain a quoted part (`'...'` or `"..."`); spaces inside the quotes don't end it.
- **Identifiers** name everything a program declares: data sections, functions, natives, named constants, locals
  and labels. The rule is exact, and an assembler must refuse anything else with `INVALID_IDENTIFIER`:
  - **First character:** one of the 55 ASCII characters `A`-`Z`, `a`-`z`, `_`, `.` or `$`.
  - **Every following character:** one of those, or an ASCII digit `0`-`9`, so 65 characters in all.
  - **No other characters:** no other punctuation, no spaces, no bytes above 0x7F.
  - **Length:** at least 1 character, with no upper limit.
  - **Case:** case-sensitive, so `Gain` and `gain` are different names.

  As a regular expression: `[A-Za-z_.$][A-Za-z0-9_.$]*`. A switch case label (`@sw#'B'`, see `SWCH` in
  InstructionSet.md) is an identifier followed by `#` and a constant; the `#` part is not part of the identifier.

**Operands** are classified by their first character:

| prefix | operand                                                         | example                |
| ------ | --------------------------------------------------------------- | ---------------------- |
| (none) | local variable, optionally `name:offset` into a local array     | `x`, `buf:3`           |
| `%`    | transient `n`: the `n`-th word after the function's locals      | `%0`                   |
| `#`    | constant: a literal or a named assembly-time constant           | `#12`, `#0.5`, `#TRUE` |
| `<`    | assembly-time variable: any one identifier character in `<` `>` | `<A>`, `<x>`, `<0>`    |
| `&`    | address of a data section or function, optionally `:offset`     | `&table:4`, `&main`    |
| `^`    | native function                                                 | `^print`               |
| `@`    | branch label in the current function                            | `@loop`, `@sw.3`       |
| `*`    | non-negative size constant                                      | `*16`                  |

**Integer literals** are decimal, or hexadecimal after a lowercase `0x`, with an optional sign. A quoted literal
(`'AB'`) packs its characters into one int, the first character in the most significant byte used. A value outside
32 bits wraps. A literal that parses completely as an integer is an int, so a float operand needs a float literal:
`#1.0`, not `#1`.

**Float literals** have the form `[sign] digits [. digits] [e exponent]`. Their value must be exactly the result of
this algorithm, because a literal's bits are part of the program:

1. Accumulate the integer digits in IEEE binary64: `d = d * 10 + digit`.
2. For the fractional digits, keep a binary64 factor `f = 1`, and for each digit do `f = f * 0.1`, then
   `d = d + digit * f`.
3. If there is an exponent `x`, do `d = d * pow(10, x)`, in binary64.
4. Apply the sign, and round `d` once to binary32.

This is not correctly-rounded decimal conversion, and an implementation must not substitute one (see open question 2).

## 6. Program structure

A program is one or more **units** (source files) assembled in order into one program.
- **Shared across units:** global names, meaning data sections, functions and named constants.
- **Reset per unit:** assembly-time variables.

**Data sections.** `GLOB *n`, `TEMP *n` and `CNST *n` each declare a named section of `n` words, labelled by the
statement's label. The data statements that follow fill it in order:
- `DATi`, `DATf` and `DATp` write one value each; `DATA` writes several of mixed types.
- `DATs text` writes the rest of the line, one character per word, with trailing blanks removed.
- Words left unfilled are 0.

`TEMP` is `GLOB` that a memory snapshot skips (section 11). `CNST` sections are read-only.

**Functions** start with `FUNC`, labelled with the function's name.
- **Declarations first.** Parameters (`PARA`, `INPi/f/p`, `OUTi/f/p`) and locals (`LOCi/f/p`, `LOCA *n`) come
  before the first instruction.
- **Ending.** The last instruction must be `RETU` or `GOTO`.
- **Labels** are local to their function. A branch may not target the position where the next `FUNC` starts
  (`LABEL_ON_FUNCTION`).
- **`NOOP`** generates nothing; it exists so a line can carry a label.

**Frame size.** A function's frame is its parameters and locals, plus its transients: one word past the highest
`%n` the function uses. A frame that would reach `2^31` words is refused at assembly.

## 7. Assembly-time evaluation

A statement prefixed with `!` runs while the program is assembled and generates no code:
- `! DEFi`, `! DEFf` and `! DEFp` define a named constant: the statement's label.
- `! MOVi <A> #1`, `! ADDi <A> <A> #2` and so on compute into assembly-time variables.
- `! IFDF`, `! IFND`, the compare-and-branch forms and `! GOTO` skip forward to a label. Skipping is forward only;
  a skip that never reaches its label fails the unit.
- `! FAIL text` stops assembly with the text.

An ordinary instruction whose operands are all constants is evaluated at assembly instead. Its result must be
**exactly** what the same instruction would produce at run time. That includes NaN branches, wrap-around and
`fTOi` saturation. A run-time error, such as a constant division by zero, becomes an assembly error.

## 8. Calls

`CALL target %t *n` calls a function or native. Its arguments are the caller's transients `%t` to `%t + n - 1`, and
they become the callee's parameters in declaration order. Output parameters are written back there, so after the
call the caller reads its results from the same transients. The transients up to `%t + n - 1` count toward the
caller's frame, so the window is always inside the data stack.

- **`&function`** calls directly.
- **A pointer variable** calls through a function pointer. A value that is not a valid function pointer stops the
  program with `BAD_CALL`.
- **`^native`** calls a host function (section 9).

Call depth is limited by the host's call-stack size (`IP_STACK_OVERFLOW`). Frame space is limited by the data
stack: `FUNC` checks that the whole frame fits (`DATA_STACK_OVERFLOW`). `GETL` and `SETL` index a local array at
run time and are checked against the end of the data stack (`BAD_PEEK`, `BAD_POKE`).

## 9. Execution and the host

**Starting.** The host assembles the units, creates a processor over the code and memory, and calls a function with
`enterCall`. Then it calls `run` until `run` returns something other than `TIME_OUT`. `enterCall` may be called
again at any time, even while a program is suspended; it pushes a new call on top, which is how a host delivers
an interrupt.

**The step budget.** The host gives `run` a budget of steps (`resetTimeOut`).
- **Cost.** Every executed instruction costs one step, including `FUNC` on entry. `NOOP` and assembly-time
  statements cost nothing.
- **Exhaustion.** When the budget runs out, `run` returns `TIME_OUT` between two instructions, and the next `run`
  continues exactly there.
- **Consequence.** Step counts are part of observable behaviour: a program, a budget and a starting state must
  time out at the same instruction on every implementation.

**Natives** are host functions registered by name, so a program refers to them by name and the host maps them to
code. A native sees the call's argument window through `accessParams`, and data memory through
`accessConstMemory` / `accessMemory`, which grant exactly the ranges in section 3. What it returns decides what
happens next:
- **0** continues after the `CALL`.
- **Non-zero** makes `run` return that status with the `CALL` not yet completed, so the next `run` calls the
  native again. That is how a native blocks until something is ready.
- **`resetTimeOut(0)`** from inside a native makes `run` return `TIME_OUT` before the next instruction.
- **`pushCall`** from inside a native runs a GAZL function in place of returning from the native.

**Statuses** returned by `run`. Positive values are free for hosts' own natives:

| status                | value | meaning                                                         |
| --------------------- | ----- | --------------------------------------------------------------- |
| `OK`                  | 0     | the call started by `enterCall` returned                        |
| `TIME_OUT`            | -1    | the step budget ran out; resumable                              |
| `BAD_PEEK`            | -2    | read outside readable memory, or `GETL` outside the data stack  |
| `BAD_POKE`            | -3    | write outside writable memory, or `SETL` outside the data stack |
| `BAD_CALL`            | -4    | call through a value that is not a function pointer             |
| `DATA_STACK_OVERFLOW` | -5    | a frame does not fit in the data stack                          |
| `IP_STACK_OVERFLOW`   | -6    | call depth exceeds the call stack                               |
| `DIVISION_BY_ZERO`    | -7    | `DIVi`, `MODi` or `DIVf` by zero                                |
| `ACCESS_VIOLATION`    | -8    | `COPY` outside memory, or a native refused an access            |
| `ABORTED`             | -9    | a native reports unexpected termination                         |
| `TERMINATED`          | -10   | a native reports expected termination (e.g. `exit`)             |

After an error status the processor still points at the failing instruction, so running it again fails again. A
host recovers by starting a new call.

## 10. The sandbox

Whatever the program text, an implementation must guarantee all of the following:

- **Memory.** No read or write outside the memory the host provided, and no write to read-only memory. Every
  failing access either stops the program with a status from the table above or is refused at assembly.
- **Isolation.** No effect outside the processor except through natives. No process-wide state is touched:
  signal handlers, the floating-point environment, global variables.
- **Interruptibility.** Every program stops within its step budget.
- **Determinism.** The same program, inputs, natives and budget give the same results, statuses and step counts
  on every conforming implementation, subject to open question 1.

## 11. Memory snapshot format

A snapshot ("freeze") saves the values of a program's named global sections so a later run can restore them by name
("thaw"), even after the program has been changed and re-assembled. A snapshot is a payload that a host embeds in
its own storage (a plugin state chunk, a preset file); it is not a file format of its own.

**Encoding.** A snapshot is a sequence of bytes built from two kinds of field:
- A **word** is 4 bytes holding an unsigned 32-bit value, little-endian: bits 0-7 first, bits 24-31 last.
- A **name** is a section's identifier (section 5), one byte per character, followed by 0 to 3 zero bytes of
  padding, so that the name and its padding together take a multiple of 4 bytes. A name of `L` bytes has
  `(4 - L % 4) % 4` padding bytes.

Every field starts at a multiple of 4 bytes from the start of the snapshot. A reader must still decode each word
byte by byte, as above, and never by loading it through a pointer.

**Header,** 20 bytes:

| offset | field                                                   |
| ------ | ------------------------------------------------------- |
| 0      | magic, the bytes `G` `Z` `M` `M` (`47 5A 4D 4D`)        |
| 4      | word: snapshot format version, 1                        |
| 8      | word: the GAZL version of the writer, 1 for GAZL 1      |
| 12     | word: word size in bits, 32                             |
| 16     | word: `S`, the number of sections that follow, `S >= 1` |

**Sections.** `S` sections follow from offset 20, each directly after the previous one:

| field     | content                                                     |
| --------- | ----------------------------------------------------------- |
| word      | `L`, the length of the name in bytes, excluding its padding |
| name      | `L` bytes, then the padding                                 |
| word      | `N`, the size of the section in words                       |
| `N` words | the section's memory, first word first                      |

**The check section** is always the first section. It is recognised by its position, never restored, and exempt
from the name order below. Its 28 bytes are fixed:

    05 00 00 00   24 67 61 7A 6C 00 00 00   03 00 00 00   04 03 02 01   00 00 80 3F   01 00 80 7F
    L = 5         "$gazl" + 3 padding       N = 3         0x01020304    1.0f          signaling NaN

A writer must produce its words the way it produces any other section's: the first from an int, the second from
the float `1.0f`, the third from a word holding a signaling NaN. A reader must parse it like any other section and
then compare all 28 bytes. It proves the reader decodes lengths, names, padding and words exactly as the writer
encoded them, and that floats travel as raw bits, a NaN's payload included. Any mismatch is `MEMORY_BAD_CANARY`.

**Program sections** follow, in strictly ascending ASCII order of their names. There is one for each name that
labels writable data:
- **a `GLOB` statement,** covering the whole section;
- **a labelled data statement inside a `GLOB` section,** covering just the words that statement defines. Its words
  are therefore saved twice, once in each entry.

`CNST` and `TEMP` sections, and labels inside them, are left out. A section's words are its memory exactly, 32
bits each, whatever their type: an int as its two's-complement bits, a float as its binary32 bits, a pointer as
its raw value.

**Validation.** A reader validates the whole snapshot before restoring anything. It is rejected, with nothing
restored, when any of the following fails, checked in this order:

| check                                                                           | status                |
| ------------------------------------------------------------------------------- | --------------------- |
| at least 4 bytes, and the magic is right                                        | `MEMORY_BAD_MAGIC`    |
| the header is complete                                                          | `MEMORY_TRUNCATED`    |
| format version 1, and a GAZL version from 1 up to the reader's own              | `MEMORY_BAD_VERSION`  |
| word size 32                                                                    | `MEMORY_BAD_WORDSIZE` |
| every length stays inside the snapshot                                          | `MEMORY_TRUNCATED`    |
| the check section is exactly as above                                           | `MEMORY_BAD_CANARY`   |
| every name is a valid identifier, names ascend strictly, padding bytes are zero | `MEMORY_MALFORMED`    |
| the snapshot ends exactly after the last section                                | `MEMORY_MALFORMED`    |

`MEMORY_MALFORMED` is new; the other statuses exist today.

**Restore.** Only after validation succeeds, a program section is restored when the re-assembled program has a
writable section of the same name and size. Every other section is skipped without error.

**What survives a restore.** Ints and floats always do. Pointers are stored as raw values:
- **A data pointer** still points at the same thing only if the re-assembled program puts its target at the same
  index.
- **A function pointer** survives only if functions are declared in the same order.

**Layouts are not saved.** A section is identified by its name and size alone; the snapshot knows nothing of the
types or structs a compiler laid out in it. If a program changes what a section holds without changing its name,
for example by reordering two fields of a struct of the same size, the old words are restored into the new layout.
Keeping a name's layout stable, or renaming the section when it changes, is the program's and the compiler's
responsibility. A compiler that wants this checked can fold a layout hash into the names it gives sections.

**Versions.** A reader accepts a snapshot from any GAZL version up to its own, because a word in memory means the
same in GAZL 1 and GAZL 2. A GAZL version that changes what memory contents mean must say so here.

**Storing snapshots.** The format protects its structure, not its bits. A host should:
- store the snapshot's exact byte length with it, since validation requires the reader to be given exactly the
  bytes that were written;
- store a checksum with it (CRC-32 at least) and verify it before calling `thawMemory`. Strongly recommended:
  validation catches damaged lengths and names, but not a changed bit inside a section's words.

## 12. Conformance

An implementation conforms when it runs the following with the expected results:
- **`src/UnitTest.gazl`.** It must assemble and its `test` function must run to completion.
- **The replayed fuzz inputs** in `tests/fuzz/` must neither crash the implementation nor break the sandbox.
- **`tools/GAZLBoundsSweep.cpp`'s oracle** must agree with every memory access at every edge of memory.

## 13. Open questions

Each is a decision for the format's owner. Until one is decided, the current behaviour of `src/GAZL.cpp` is the
answer.

1. **Denormals and the FP environment.** Float results that involve denormals follow the host thread's settings
   (flush-to-zero, denormals-are-zero), which GAZL does not set. That is the one place two conforming
   implementations can differ. Should GAZL 1 require a fixed environment around `run`, or just record the
   exception?
2. **Float literals use `pow`.** Step 3 of the literal algorithm calls the C library's `pow`, which is not
   guaranteed to round the same way everywhere. Should the exponent be applied by an exactly specified method
   (for example, repeated multiplication by 10 in binary64)? That would only change literals with an exponent.
3. **`COPY` with overlap** is documented as "undefined". It is memory-safe either way. Should the result be
   specified (as if copied forward word by word), or stay unspecified?
4. **`DATs` with bytes above 0x7F** stores each byte as a C `char`. That is signed on x86 and Apple arm64 but
   unsigned on Linux arm64, so the stored word differs between hosts. Should `DATs` be ASCII-only, or define bytes
   as unsigned?
5. **`LEQf` / `GEQf` on NaN** branch, unlike C. Section 4 makes the current behaviour normative; confirm it.
6. **Constant addresses depend on memory size.** Should hosts that want reproducible pointer values be told to fix
   the memory size, or should this stay a host matter?
7. **Step counts.** Section 9 makes step counts observable, which binds every future engine, JIT included, to
   count exactly as the interpreter does. Confirm that, or relax it to "at least one step per backward branch and
   call".
