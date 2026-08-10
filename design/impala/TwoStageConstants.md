# The two-stage build, and why a constant is not always a number

Status: NORMATIVE. This is the canonical statement of GAZL's two-stage constant model and of the rules
it imposes on Impala. Other documents point here rather than restating it.

**If you read one paragraph, read this one.** GAZL programs are distributed as assembly TEXT, and that
text is assembled on the end user's machine immediately before it runs. The host can inject named
constant values at that moment. So the same `.gazl` file compiles to different code depending on
conditions Impala never saw. It follows that **Impala routinely emits references to constants whose
values it does not know, and this is correct, expected, and the whole point.** Any change that makes
Impala demand a numeric value where a symbol would do is a bug, not a hardening.


## The vocabulary problem, first

The phrase "compile-time constant" is ambiguous in this repository, and that ambiguity is the single
most common source of mistakes. There are two compilations:

| Stage | Name used here | Input | Output | Runs on | When |
|---|---|---|---|---|---|
| 1 | **Impala compile time** | `.impala` | `.gazl` TEXT | the developer's machine | at build time |
| 2 | **GAZL assembly time** | `.gazl` TEXT | in-memory code | the END USER's machine | at load, just before running |

GAZL's own documentation says "compile-time constants" and "compile-time instructions" (`#NAME`, the
`!` directives) to mean **stage 2**, because from GAZL's point of view assembly *is* compilation. The
Impala documentation says "compile-time constant" to mean **stage 1**. Same words, different stages.

When precision matters, say **Impala compile time** or **GAZL assembly time**. Never bare "compile
time".

There is no third stage and no intermediate object format. `.gazl` text is the persistent,
distributable, version-controllable form of a GAZL program (`docs/Overview.md`, "Portable text
format"). This is deliberate: `GAZL.h`'s goals list includes *"Assembly source should be compiled to
internal representation immediately prior to execution"* and *"Compile-time calculations and
conditions should be supported"*, and its non-goals list includes *"No need for a compact persistent
code format."*


## Why the model exists

Because stage 2 happens on the user's machine, the host can specialize one shipped program per run.
`DEBUG` on or off, a struct layout that differs between hosts, a buffer size the host picks, a feature
switch: all of these can be decided at load with no recompilation of the Impala source and no
duplicate artifacts. Assembly-time folding means the specialization costs nothing at run time, because
`!` directives execute during assembly and emit no instructions.

Giving this up would remove the main thing that distinguishes GAZL from a conventional bytecode VM.


## How a stage-2 constant gets its value

Four mechanisms, all of which can produce a value Impala cannot see:

1. **The host injects it.** `Symbols::defineConstant(name, asFloat, value)` (`src/GAZL.h:186`) is
   called by the embedding application before or during assembly. This is how `DEBUG`,
   `GAZL_WORD_SIZE`, host-chosen sizes and host-owned struct layouts arrive.
2. **The GAZL text defines it.** `! DEFi #4`, `! DEFf #1.0`, `! DEFp &something`.
3. **Assembly-time arithmetic computes it.** The `<A>` through `<Z>` compile-time variables plus
   `! ADDi`, `! MULi`, `! SUBp`, `! SHLi`, `! iTOf` and friends. See `docs/Overview.md` for the full
   opcode list.
4. **Assembly-time conditionals select it.** `! IFDF` / `! IFND` (defined / not defined),
   `! EQUi` / `! LSSi` / `! GEQp` and the rest, all of which branch to a `@label`, plus `! GOTO`.
   Whole blocks of code can be skipped this way.

A symbol produced by any of these is a perfectly ordinary constant operand as far as the assembler is
concerned. It can appear anywhere a literal can: `#SIZE`, `&global:OFFSET`, `*COUNT`.


## What Impala actually does today (verified, with evidence)

This is not aspirational. The shipped compiler already works this way, and the golden files prove it.

**A `const` with a known value is still not inlined.** Impala emits it as an assembler define and
references it by name everywhere:

```impala
const int SOME_COUNT = 4
readonly array SOME_CONSTS[SOME_COUNT] = { 100, 200, 300, 400 }
```

becomes (`tests/impala/golden/ImpalaDemo.gazl:22,24-25`):

```
SOME_COUNT:         ! DEFi #4               ; signature const SOME_COUNT : int @ 87:1
.z.SOME_CONSTS:     ! DEFi #SOME_COUNT
SOME_CONSTS:        CNST *.z.SOME_CONSTS    ; signature array SOME_CONSTS[SOME_COUNT] : unknown @ 94:1
```

Note the array size reaches `CNST` as `*.z.SOME_CONSTS`, itself defined as `#SOME_COUNT` - two named
symbols deep and **not** `*4`, even though Impala knows it is 4. The symbols survive into the artifact,
so the size stays inspectable, greppable, and editable as a single line of the shipped text, and the
arithmetic around it stays visible.

**But be precise about what a valued `const` is NOT.** It emits `! DEFi #4`, so it is already defined
by the time the host sees the file and the host CANNOT override it:

    $ ./output/GAZLCmd program.gazl main SOME_COUNT 9
    Symbol already defined: SOME_COUNT

The two flavours of `const` are therefore genuinely different, and it matters:

| Form | Emits | Host can define it? | What you get |
|---|---|---|---|
| `const int N = 4` | `N: ! DEFi #4` | **No** - `Symbol already defined` | a name, not a knob |
| `const int N` | nothing (signature row only) | **Yes**, and must | a real specialization point |

So a valued `const` gives you readability and assembler-side folding; only a VALUELESS `const` is
host-overridable. When you want the host to decide, do not give the constant a default value.

**A `const` with no value emits no definition at all.** The host must supply it:

```impala
const int GAZL_WORD_SIZE;
```

produces only a signature comment (`tests/impala/golden/ImpalaDemo.gazl:23`):

```
; signature extern const GAZL_WORD_SIZE : int @ 91:1
```

Every use of `GAZL_WORD_SIZE` in the generated GAZL is a bare symbolic reference that resolves at
assembly time, or fails to assemble if the host never defined it:
`LEQi $minLength #GAZL_WORD_SIZE @.a2`, `ADDp %0 $buffer #GAZL_WORD_SIZE`
(`tests/impala/golden/FFTTest_code.gazl:138,141`).

**Arithmetic on an unknown constant is deferred, not refused.** This is the example to keep in mind.
`tests/impala/sources/FFTTest_code.impala:172` declares a local array whose size is an expression over
a constant the host supplies:

```impala
const int GAZL_WORD_SIZE
...
locals array buffer[GAZL_WORD_SIZE + 2]
```

Impala cannot evaluate `GAZL_WORD_SIZE + 2`. It does not need to, and it does not complain. It emits
the addition as an assembly-time computation into `<A>`, names the result, and sizes the local from that
name (`tests/impala/golden/FFTTest_code.gazl:173-175`):

```
                        ! ADDi <A> #GAZL_WORD_SIZE #2
.z.traceInts.buffer:    ! DEFi #<A>
$buffer:                LOCA *.z.traceInts.buffer
```

The stack frame layout is therefore decided at stage 2, by the host. A compiler that insisted on
knowing array sizes numerically could not compile this function at all.

**`assert` is compiled as an assembly-time conditional.** From
`tests/impala/golden/ImpalaDemo.gazl:198-202`:

```
        ! EQUi #DEBUG #0 @.a11          ; assert(0 == 1)
        EQUi #0 #1 @.a11
        MOVp %1 &.a_01_4d3
        CALL ^assertFail %0 *1
.a11:   POKE &aFuncPointer &showoff     ; global aFuncPointer = showoff
```

When the host defines `DEBUG` as 0 the assembler jumps straight to `.a11` - which carries the next real
instruction - and the check never enters the code image. The assert-message string constants are guarded
the same way, by a single `! EQUi #DEBUG #0 @.noAssertStrings` before the whole block
(`tests/impala/golden/ImpalaDemo.gazl:263`). Impala emits both the check and the strings
unconditionally and lets stage 2 decide. It has no idea what `DEBUG` is.

**Even a constant expression over a KNOWN constant is deferred, not folded.** `const int anInt = 4` is
a literal Impala fully evaluates, yet this expression:

```impala
printInt(ftoi(myFloat * (1.0 / itof(1 << anInt))))
```

emits the entire constant sub-expression as assembly-time directives into `<B>`, shift, int-to-float
conversion and float divide alike (`tests/impala/golden/ImpalaDemo.gazl:241-244`):

```
        ! SHLi <B> #1 #anInt
        ! iTOf <B> <B> #1.0
        ! DIVf <B> #1.0 <B>
        fTOi %1 $myFloat <B>
```

Impala could have emitted `#0.0625`. It does not. It transliterates the constant expression into
assembly-time instructions and lets the assembler evaluate it, so `anInt` remains a live
specialization point and the arithmetic stays visible in the artifact. This is the model working as
intended, and it is the shape to imitate.

**Struct layout is headed the same way.** `.z.Struct` (size) and `.o.Struct.field` (field offset) as
named GAZL constants, so an `extern struct` can have its layout supplied by the host at load. See
`design/impala/StructLayoutConstants.md`.


## The rules

Every check, fold, diagnostic and optimization in the Impala compiler must obey these.

1. **A constant expression is not necessarily a number Impala knows.** Treat "constant" as meaning
   "resolvable by the end of stage 2", not "a literal available to me now".

2. **Only reject on a value Impala genuinely knows.** If an operand folded to a numeric literal, check
   it and diagnose it properly. If it is a symbol, Impala has no grounds to reject it.

3. **Never silently pass a symbolic value off as checked.** Not knowing is not the same as being fine.
   If a check was skipped because the operand was symbolic, that fact must be visible somewhere, not
   quietly assumed away.

4. **Prefer deferring the check to stage 2** where the mechanism exists (see below). That is strictly
   better than giving up: the check still happens, it still fails the build, it just fails later.

5. **Prefer emitting the symbol over the folded number**, even when the value IS known. When Impala can
   emit either `*SOME_COUNT` or `*4`, emit the symbol; when it can emit `! SHLi <B> #1 #anInt` or the
   folded result, emit the directive. This is what the compiler already does (see the evidence above).
   Folding is the assembler's job (`design/gazl/GAZLAssemblerOptimizations.md`), and a folded literal has
   thrown away the name, the visible arithmetic, and the single-line edit point in the shipped text.

   **Corollary, and it has bitten this compiler: a check must never decide "do I know this value?" by
   inspecting the operand text it just emitted.** Because of this rule the emitted form of a value
   Impala knows perfectly well is usually `<A>`, `#KNOWN`, `#'a'` or `#0x08`, none of which look like
   a literal. A check that regex-matches for `#<digits>` therefore silently disables itself on the
   values it was written to catch. Decide from the parsed constant EXPRESSION, and treat it as unknown
   only when a host-supplied symbol genuinely enters it.

6. **Never be stricter than the machine you transliterate to.** If GAZL accepts a construct, Impala
   rejecting it needs a much stronger justification than "it looks wrong". See the `&a[7]` case in
   `design/impala/CompileTimeHardening.md`.


## The deferred assertion

When Impala wants to check something it cannot evaluate, it emits the check as GAZL directives and lets
the assembler decide. GAZL has a purpose-built directive for this: **`! FAIL <message>`** aborts
assembly with your text (`Assembler::feed`, the `FAIL_DIRECTIVE` throw). Pair it with an assembly-time
comparison:

```
SIZE:       ! DEFi #4
main:       FUNC
            PARA *1
            ! LSSi #9 #SIZE @inRange        ; index < size -> skip the failure
            ! FAIL index 9 is out of bounds for Frame (size is SIZE)
inRange:    !
```

which produces:

    FAIL directive encountered: index 9 is out of bounds for Frame (size is SIZE)
    Line 5:     ! FAIL index 9 is out of bounds for Frame (size is SIZE)

This is the canonical idiom, not a trick: `src/UnitTest.gazl:30-40` guards `GAZL_VERSION` and
`GAZL_WORD_SIZE` exactly this way, combining `! IFDF` (is it defined at all?) with `! EQUi` and
`! FAIL`. Note `inRange:  !` - a bare `!` is a no-op that exists to carry a label, since a label needs
an instruction on its line.

It works even when the operand is a host-supplied symbol Impala never knew, and costs nothing at run
time because every line is an `!` directive.

> **Do not use the undefined-label trick.** An earlier version of this section (and of
> `design/impala/CompileTimeHardening.md`) described branching to a deliberately undefined label so that
> `Compile time label not found: .INDEX_OUT_OF_BOUNDS` becomes the diagnostic. That works, but it
> abuses the label name as the error text, so the message cannot contain spaces or punctuation and
> reads like an internal error. `! FAIL` takes free text. Prefer it.


## Why this follows from "stay a transliterator"

This is not a separate principle bolted on. It is a direct consequence of Impala's first design
principle (`docs/impala/Impala2.md`, "Stay a transliterator"): the ~1:1 mapping between Impala constructs and
GAZL instructions is sacred, and a feature that cannot be expressed as thin sugar over the GAZL a
programmer would write by hand does not belong in the language.

GAZL's constant model is inherently two-stage. A hand-writing GAZL programmer freely references
symbols that the host will define later. Therefore a transliterator of GAZL must pass symbols through
to stage 2 untouched. The moment Impala insists on knowing the number, it has stopped transliterating
and started behaving like a conventional compiler, and the 1:1 mapping is broken in the one direction
that cannot be observed in the generated text.


## Anti-patterns

Concrete shapes that violate the model. If you are about to write one of these, stop.

- **"This must be a compile-time constant, so I will evaluate it."** Which compile? If the answer is
  "mine", you have just outlawed every host-supplied value.
- **Erroring out because a constant expression did not reduce to a literal.** Unknown is the normal
  case. Emit the symbol.
- **Folding a named constant into its literal value in the output.** The name is more useful than the
  number in the artifact, and folding removes a specialization point.
- **Bounds-checking, range-checking or overflow-checking a symbolic operand by assuming a value**
  (zero, the declared size, the last value seen). Either defer it to stage 2 or leave it unchecked and
  say so.
- **Requiring a `sizeof`, array extent or struct offset to be numerically known at stage 1.** For an
  `extern struct` it is not, by design.
- **Adding an optimization that depends on knowing a constant's value.** That optimization belongs in
  the assembler, where the value actually exists.
- **Treating "the host has not defined it" as an Impala error.** It is an assembly error, reported at
  stage 2, and that is the correct place for it.
- **Running `parseInt` / `parseFloat` / arithmetic on an OPERAND STRING.** An operand is `#4` today and
  `#HOST_N`, `<A>`, `#0x10` or `#'a'` tomorrow. `parseFloat("HOST_N")` is `NaN` and `#NaN` is a legal
  GAZL identifier; `parseFloat("0x10")` is `0`, so a plain hand-written hex offset silently becomes
  zero. If you must have a number, get it from the parsed constant expression and handle "I don't have
  one" explicitly. Guarding on `operand[0] === '#'` does not establish that the rest is a decimal.
  (FIXED 2026-08-04: `$parser.constInt` is that decoder — it returns `undefined` for everything it cannot
  read, which is the "I don't have one" branch, and it is now the only thing any site asks. The last two
  hand-rolled holdouts were `dereference`, which guarded on `'#'` and then ran `parseFloat` — the exact
  pair this bullet warns about, still shipping four days after the warning was written — and
  `subConstInt`, which spanned decimal digits. If you add a literal spelling, `constInt` is the one place
  that has to learn it.)
- **Comparing or iterating a declared extent as if it were a number** (`for (e = 0; e < field.size; ++e)`,
  `field.size * per`). When the extent is symbolic, `field.size` is the string `'N'`: the loop runs zero
  times and the multiply is `NaN`, so initializer words are silently dropped and every later field
  shifts. Test the extent for numeric-ness first and take a deliberate branch. (FIXED 2026-07-31:
  `fieldWords` returns `undefined` rather than `NaN`, and the two checks that read
  `structWords(...) === undefined` as "incomplete" now ask `structDefined` - otherwise a symbolically
  sized struct could not be nested by value or passed to `sizeof`. The field itself IS initializable -
  its words start at a position Impala knows - and the count check Impala cannot make is emitted for the
  assembler instead (`! LEQi` + `! FAIL` above the rows, rule 4 below); that is what now stops an over-filled
  array from spilling into the next field while still FITTING the region, which nothing could catch
  before. `E454` covers only the fields BEHIND it, whose positions are genuinely unknown, and zeros stay
  legal there, being what the region fills with regardless.)
- **Emitting positional `DATA` for a type whose layout the host owns.** If field offsets come from
  `.o.*`, an initializer written in declaration order is a guess. There is no seek/`.org` directive to
  fix it up, so the only sound options are to reject the initializer or to require a host-independent
  form. Half-deferring (symbolic reads, positional writes) is worse than either consistent choice.
  Confirmed 2026-08-01, and it WAS a live wrong-output defect - now closed by refusing it (`E459`),
  which is the "reject the initializer" branch above, narrowed so an all-zero one still compiles (zero
  is the same word under any layout). GAZL 1 cannot express the fix at all; the evidence, the GAZL 2
  requirements and the alternatives that do not work are in
  [`ParkedFeatures.md`](../ParkedFeatures.md) ("Placing static data at a symbolic offset").


## See also

- `docs/Overview.md` - the GAZL side: the `!` opcode list, `<A>`-`<Z>`, the portable text format.
- `docs/impala/Impala.md` - the `const` section of the language reference.
- `docs/impala/Impala2.md` - the design principles this follows from.
- `design/impala/CompileTimeHardening.md` - the diagnostics backlog, all of it constrained by these rules.
- `design/impala/StructLayoutConstants.md` - layout as named GAZL constants (`.o.*` / `.z.*`).
- `design/gazl/GAZLAssemblerOptimizations.md` - what the assembler folds, and why folding belongs there.
