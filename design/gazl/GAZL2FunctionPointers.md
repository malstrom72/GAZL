# A distinct function-pointer type for GAZL 2 (`t`)

Status: PROPOSAL, researched against `src/GAZL.cpp` and GAZLCmd on 2026-08-02. Nothing here is
implemented. The conclusion is that GAZL 1 has a real silent-wrong shape that no amount of Impala-side
checking can close, because Impala has nowhere to encode the distinction.

## The problem in one line

GAZL has three storage types - `i`, `f`, `p` - and **`p` is doing double duty**: it is both "data
pointer" and "function pointer", which are not the same thing and are not interchangeable.

## The rule the ISA states, and one correction to it

`docs/gazl/InstructionSet.md`, under `CALL`, says (as corrected 2026-08-07):

> A function pointer (the value of `&function`) is an opaque handle: a stable ordinal assigned in function
> declaration order, not a code address. Equality (`EQUp` / `NEQp`), ordering (`LSSp`, `GEQp` etc.) and
> calling are defined operations on a function pointer. [...] Arithmetic (`ADDp`, `SUBp`, `DIFp`) applied
> to a function pointer yields an unspecified (but memory-safe) result.

(That paragraph originally put ordering on the undefined side with arithmetic. It was corrected on
2026-08-07, for the reason set out just below; the quote above is the corrected text.)

This document is asking the assembler to ENFORCE what is written down, which a shared `p` type makes
impossible. Note too that "unspecified (but memory-safe)" is accurate but undersells `ADDp`: the result is
not garbage, it is a *different function*, called silently.

**The rule, stated by RESULT TYPE - which is the only thing that decides safety here:**

> An operation on targets is safe if and only if `t` does not appear in its RESULT. A target may be
> NAMED (`&func`), COPIED (`MOVt`) or LOADED (`DATt` / `LOCt`) - never COMPUTED. Consuming targets is
> unrestricted.

That one line derives the whole set. `&one + 1` is the defect because it *produces* a `t` naming a
different function; `f < g`, `f == g` and `f - g` all consume targets and hand back a bool or an int, so
none of them can name anything at all. Classifying by the NAME of the operation - "arithmetic bad" - gets
`DIFt` wrong, because `t - t -> int` is arithmetic that cannot manufacture a target.

**GAZL 1 already encodes exactly this distinction in its mnemonics.** From `DIFp` in the instruction set:
"You cannot use `SUBp` to subtract a pointer from another. `SUBp` is only used for negatively offsetting a
pointer." `SUBp` is `ptr - int -> ptr`; `DIFp` is `ptr - ptr -> int`. They are separate instructions
*because the result types differ*. Keeping `DIFt` and dropping `SUBt` is not a new idea - it is applying a
split the ISA made long ago.

And ordering is *useful*, because a sorted container does not need a MEANINGFUL order - only a TOTAL and
RUN-STABLE one. Sort a funcptr table at init and binary-search it for membership: the search never depends
on which order declaration order happens to hand you, only that it stays consistent within the run. This
is precisely why C++ guarantees `std::less<T*>` is a strict total order where `<` on unrelated pointers is
not. "Meaningless by construction" is true of a declaration-order ordinal SEMANTICALLY, and irrelevant to
whether it sorts.

Impala already draws the line in the right place, and always has: `f + 1` and `f - g` are `E301`, while
`f < g` and `f == g` compile. The compiler is the evidence; this paragraph was the thing that was wrong.

So the defined operation set is **equality, ordering, and calling**, and `t` offers all three.

## They are already different things at run time

This is not a type-system nicety layered over one representation. The two live in different numeric
spaces, deliberately:

```c
// src/GAZL.h:77-78
const Pointer MEMORY_OFFSET = 0x12345678;   // data pointers: memory offset + this
const Pointer FUNCTION_OFFSET = 0x56789ABC; // function pointers: FUNCTION ORDINAL + this
```

```c
// src/GAZL.cpp:1130
v.p = (Int)(FUNCTION_OFFSET + functionCount);
// "A function pointer is its stable declaration-order ordinal (not a code offset),
//  resolved through `functionTable` at call time."
```

So a data pointer is an *address* and a function pointer is an *index*. The ISA already knows they are
different; it just has no type to say so.

## What is checked today, and what is not

Verified with GAZLCmd on 2026-08-02:

| shape | today |
|---|---|
| `CALL &gdata` (direct call to a data label) | **rejected** - `Incompatible types: gdata` |
| `MOVp $p &gdata` then `CALL $p` | accepted; traps at run time as `BAD_CALL` |
| `MOVp $p &target` then `PEEK` through it | accepted; traps on the memory region check |
| `DATp &target &gdata` (both kinds, one row) | accepted, unchecked |
| `ADDp $p $p #1` on a function pointer | **accepted - and does NOT trap** |
| `DIFp $i $funcPtr $dataPtr` | accepted, meaningless result - MIXING the two kinds is the defect here, not the difference itself |
| `LSSp` / `GRTp` ordering two function pointers | accepted - and CORRECTLY so, see the correction above |

The direct call is caught because `CALL_c__` demands the `FUNC` bit (`src/GAZL.cpp:337`). Everything else
accepts `ANY_FREE = NULL_PTR | FREE_ADDRESS | FUNC` (`:293`), the union - so the kind survives only while
the address is a *constant operand* and is lost the moment it is stored or computed.

## The one that does not trap

Most misuse is caught dynamically, because the two offsets are `0x44444444` apart: a data pointer used as
an ordinal underflows past `functionCount` and `CALL_VVC` rejects it (`if (ui >= functionCount) { err =
BAD_CALL; }`, `:1284-1285`). Reaching a *valid* ordinal that way would need ~4.5 GB of data.

**Arithmetic on a function pointer is the exception, and it is a silent wrong answer.** `&one + 1` is
`ordinal + 1`, which is a perfectly valid ordinal, so nothing traps - it just calls a different function.
Demonstrated by running it:

```
function one() { printInt(1); printLF(); }
function two() { printInt(2); printLF(); }
export function main() { one(); two(); }
```

with `$fp: LOCp / MOVp $fp &one / ADDp $fp $fp #1 / CALL $fp %0 *1` spliced into `main`, the program
prints:

    2 1 2

The injected call ran `two()`. No diagnostic at assembly, no trap at run time, wrong function executed.

Note this refutes the tempting summary that "misuse always traps". It does not, and the failing case is
the one a type would make unrepresentable rather than merely detectable.

## Why Impala cannot fix this alone

`impala.jspeg`'s `TYPE_SUFFIXES` maps `'F' -> 'p'`. Impala 2.0 *has* the type - named funcptr types,
`funcptr` declarations, element-type checks - and collapses it at emission because GAZL offers only `p`.
Every funcptr array and every data-pointer array emit identical `DATp` rows. The information exists and is
discarded at the boundary.

## Proposal: a fourth type, suffix `t`

`t` for "target" - the call target, which is what the ordinal actually names.

Needed:

| mnemonic | why |
|---|---|
| `LOCt` | a local holding a call target |
| `MOVt` (2 forms) | assignment |
| `DATt` | initializer rows for funcptr arrays and struct fields |
| `EQUt` / `NEQt` (8 forms) | equality is meaningful - "is this the same function?" |
| `LSSt` / `GRTt` / `LEQt` / `GEQt` | a TOTAL, run-stable order - sort a target table, binary-search it |
| `DIFt` | `t - t -> int`: consumes two targets, names none |
| `INPt` / `OUTt` | if call targets cross the host boundary |

plus `CALL_v__` accepting `t` rather than the generic `VAR_PTR_R`.

**The omissions are the feature, and by the result-type rule they are exactly the target-PRODUCING forms:**
no `ADDt`, no `SUBt`, no `FORt`. Each of those hands back a `t` computed from a `t`, so `&one + 1` stops
being a wrong answer and becomes a line that will not assemble. That is the whole return on the change.

Two that look like they belong on the wrong side:

- `DIFt` is KEPT though it is arithmetic - it yields an `int`. `SUBt` is dropped though it is the same
  minus sign, because `t - int` yields a `t`. This is the `SUBp`/`DIFp` split the ISA already makes.
- `FORt` is DROPPED though it looks like iteration rather than arithmetic - stepping a loop variable
  through targets is `ADDt` wearing a different hat, and its result is a `t`.

## Suffix letters ruled out, and why

The natural letter is `f`, and float has it. Everything else is a compromise, so record the rejects:

- **`F`** - `DATF` vs `DATf` would be two different instructions separated only by the case of the last
  letter, both legal in a data row. Mnemonics are case-sensitive (`DATI` and `dati` are both rejected),
  so it would parse - which is exactly what makes it a trap. Also breaks the convention that lowercase
  4th char = type and uppercase = word-form.
- **`a`** - collides the same way with `LOCA` / `DATA` / `PARA`.
- **`e`** - collides with `MOVE`.
- **`c`** - free and safe, but "code" has no obvious connection to a function pointer.

The lowercase suffix space is only `f i p`, plus `s` (`DATs`) and `u` (`SHRu`). `t` is free and collides
with no word-form on any stem that would take it (`MOV LOC DAT EQU NEQ INP OUT`).

## Where this is anchored, so GAZL 2 cannot miss it

A design note on its own gets missed. Every site that has to change carries a `GAZL 2:` comment pointing
back here, so the work is discovered by editing the code rather than by remembering this file:

| site | what it says |
|---|---|
| `src/GAZL.cpp`, `ANY_FREE` | SPLIT THIS - the union of `FUNC` and `FREE_ADDRESS` is the root cause |
| `src/GAZL.cpp`, `CALL_v__` / `CALL_vvs` | indirect call takes generic `VAR_PTR_R`; retype to `t` |
| `src/GAZL.cpp`, `DATp_c__` | one row can mix function and data addresses; needs a `DATt` sibling |
| `impala/impala.jspeg`, `TYPE_SUFFIXES` | `'F','p'` is where Impala discards the distinction; becomes `'F','t'` |
| `docs/gazl/InstructionSet.md`, `CALL` | the contract paragraph, plus a note that GAZL 1 cannot enforce it |
| `docs/gazl/InstructionSet.md`, `DATp` | records that `p` covers both, and why that is a problem |

**The Impala side is one map entry.** Impala already tracks funcptr as its own type `'F'` and collapses it
only at emission, so `'F','p'` -> `'F','t'` is the whole change there. Nothing else in the compiler needs
to learn a new concept - which is worth knowing before estimating the work, because the assembler side
looks like the expensive half and is.

## Open questions

- **Struct fields and `copy`.** A struct holding a call target is a mixed-type region; `DATA` rows stay
  the mixed form (see `consts.mixed` in `src/UnitTest.gazl`), so a funcptr field inside a struct
  initializer is still untyped. Whether that matters depends on how much of the win is in arrays.
- **`PEEK`/`POKE` through memory.** A word read out of memory into a `LOCt` slot is unchecked, exactly as
  a word read into `LOCi` is unchecked today. This is the existing boundary of the type system, not a new
  hole - but it means `t` is a declaration contract, not a guarantee about memory contents.
- **Host boundary.** Whether `INPt`/`OUTt` are needed at all depends on whether a host ever hands a call
  target across.
- **Migration.** `p` currently means "any pointer" in practice. Tightening it to "data pointer" is the
  breaking half; every existing funcptr use in emitted GAZL would have to move to `t`.

## See also

- [`docs/gazl/InstructionSet.md`](../../docs/gazl/InstructionSet.md) - the `DAT*` family and the operand forms.
- [`docs/impala/MemorySafetyModel.md`](../../docs/impala/MemorySafetyModel.md) - why dereferences are bounds-checked at run time.
- [`design/ParkedFeatures.md`](../ParkedFeatures.md) - the `GAZL2` branch and what else waits on it.
