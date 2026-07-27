# GAZL memory safety and frame layout

Status: REFERENCE. Written 2026-07-27 after re-deriving all of it from `src/GAZL.cpp` and `src/GAZL.h`,
because the `*size` operands read as if containment depended on them and it does not. Every claim below
cites the line that implements it. If you are here because `*size` looks load-bearing for SAFETY: it is
not, and section 5 says why. It is still worth emitting, and section 6 says why.


## 1. The stack model

The data stack is one flat array. `dsp` points at the boundary between what a function has DECLARED and
what is simply free space above it.

    dataStackBase                                 dsp                        dataStackEnd
    v                                             v                                     v
    [ ... outer frames ... ][ my declared locals ][ my scratch and everything above ... ]

- `$name` (a declared local) is a NEGATIVE offset from `dsp`.
- `%N` (a transient) is `dsp[N]`, a positive offset.
- **Nothing above `dsp` is allocated or reserved.** The space up to `dataStackEnd` is yours to use.
- A callee's frame begins wherever the CALLER's `CALL` window base says (`dsp += C1.i`, GAZL.cpp:1287).
  There is no separate allocation step for it.

So the only thing that ever advances the stack pointer is a function's own declared locals.


## 2. The two numbers on FUNC, and what they are NOT

`FUNC` carries two constants, computed by the assembler and used in exactly one instruction:

    case FUNC_CC_:  if ((dsp += (UInt)(C0.i)) + C1.i > dataStackEnd) { err = DATA_STACK_OVERFLOW; }
                                       ^^^^     ^^^^
                                       |        C1 = paramsSize (allocates nothing)
                                       C0 = localsSize (advances dsp)

**`localsSize`** is the allocation. It accumulates only from declared frame lines - `OUTi`, `INPi`,
`PARA`, `LOCi`, `LOCA` (GAZL.cpp:1137) - and it is what advances `dsp`.

**`paramsSize` allocates nothing.** It is the high-water mark of the fixed offsets the body reaches, and
it appears in that `>` comparison and nowhere else in the file. Its only job is to prove, ONCE at entry,
that those offsets are inside the stack.

That single check is what makes every later fixed-offset access free:

    MOVi %7 #1      ->   dsp[7] = 1        no runtime bounds test, and none is needed
    MOVi $q %0      ->   likewise

This is the central trade in the design: one comparison per call replaces a comparison per access.


## 3. How paramsSize is computed

Two contributors. First, the operand parser, which counts **every `%N` that literally appears in the
text**:

    // GAZL.cpp:837
    case '%':  paramsSize = maximum(paramsSize, (UInt)(v->i + 1));

Write `MOVi %20 #1` anywhere and the frame is proven to reach slot 20. This is why transients need no
declaration. Second, the `*size` operand, carried by the ops flagged `LOCAL_BOUNDS` - `ADRL` and the
three `CALL` forms (GAZL.cpp:331, 337, 339, 341):

    // GAZL.cpp:1199
    paramsSize = maximum((Int)(paramsSize), p1->i + p2->i);

That second one covers slots reached WITHOUT being named: a pointer's span, or a call window.


## 4. What is checked, and when

Line numbers below are `GAZL.cpp` unless marked `GAZL.h`.

| access                          | bound                         | enforced                                   |
| ------------------------------- | ----------------------------- | ------------------------------------------ |
| `%N`, `$x` (fixed offset)       | `localsSize + paramsSize`     | once, at `FUNC` entry (cpp:1273)           |
| `$obj:CONST` (constant offset)  | that object's extent          | ASSEMBLY: `Offset out of bounds` (cpp:619) |
| `SETL` / `GETL` (dynamic index) | `dataStackEnd`                | every access (cpp:1312-1313)               |
| `PEEK`                          | `memorySize`                  | every access (cpp:1306-1307)               |
| `POKE`                          | `rwMemorySize`                | every access (cpp:1308-1311)               |
| GAZL -> GAZL call window        | the callee's own frame        | the callee's `FUNC`, from the new `dsp`    |
| GAZL -> native call window      | the count the NATIVE asks for | `accessParams(count)` (h:385)              |

`POKE` is bounded by `rwMemorySize` rather than `memorySize`, so read-only memory really is read-only.

Note the `SETL`/`GETL` row: they are bounded by the END OF THE STACK, not by the object they index. A
dynamic index cannot be validated statically, so the only guarantee is containment, not correctness.


## 5. Why `*size` is not needed for containment

Every row of that table is self-sufficient without it:

- **GAZL -> GAZL call.** `CALL_CVC` moves the view (`dsp += C1.i`) and the callee's own `FUNC` performs
  its own entry check from there (GAZL.cpp:1287). The caller's coverage is belt-and-braces.
- **GAZL -> native call.** `CALL_NVC` only sets `this->dsp = dsp + C1.i` and runs host code
  (GAZL.cpp:1290-1294). It never reads the size. The native declares how many words it wants, and that
  is where the check happens:

      // GAZL.h:385
      inline Value* Processor::accessParams(UInt count) const {
          return (dsp + count <= dataStackEnd ? dsp : 0);
      }

  A null return means the native should report `DATA_STACK_OVERFLOW`.
- **`ADRL`.** At run time it only computes an address (`V0.p = Pointer(&dsp[C1.i] - mb)`,
  GAZL.cpp:1315). The size operand is never read there, and the resulting pointer is checked on every
  use by `PEEK`/`POKE`.
- **Fixed offsets.** Already covered by name-counting (section 3), which is stronger than it sounds: to
  touch a slot at a fixed offset you must write it, and writing it counts it.

Hence `InstructionSet.md`: *"It is always legal to specify a size of zero if you do not know."* A
generator that emits `*0` throughout produces correct, contained programs. Understating a size can
corrupt your own frame; it cannot escape the sandbox.


## 6. What `*size` IS good for

Two things. The first is real today.

**Fail fast, at entry, deterministically.** Sizes on `ADRL` and `CALL` feed `paramsSize` (section 3),
so declaring them moves a stack exhaustion from wherever it would eventually surface - a `BAD_POKE`
from some pointer write, or a callee's own `FUNC` several frames down - up to the entry of the function
that is actually too big for the remaining stack. That is a categorically better failure:

| with `*0`                                      | with a real `*size`                              |
| ---------------------------------------------- | ------------------------------------------------ |
| trips only if the path that overruns is taken  | trips on entry, whether or not that path runs    |
| data-dependent, so it can hide in testing      | deterministic for a given call depth             |
| reported as `BAD_POKE` at some unrelated write | reported as `DATA_STACK_OVERFLOW` at the culprit |

For QA that is the difference between a crash you can reproduce and one you cannot. A firmware target
that wants to prove "this call graph fits in N words" needs the sizes; without them the proof has holes
exactly where the frame is not named.

**Frame layout for a compiling backend.** `ADRL %p %N *k` is the only way to learn that slots
`N..N+k-1` are address-exposed and so must live in real, contiguous memory rather than in machine
registers. No such consumer exists in `GAZL.cpp` yet, and the exact form is deliberately left open.

So the rule is: `*0` is always SAFE, and a correct `*size` is always BETTER. Emit real sizes where you
know them.


## 7. Consequence: the blast radius of a bad index

A frame is an allocation, not a fence. An out-of-range dynamic index writes into the stack above, which
in practice means the program corrupts ITSELF first. Take a 2-word local array followed immediately by
a loop counter, and a loop that writes 60 elements of it:

    slots 0..11 afterwards:  12345 12345 12346 3 0 0 0 0 0 0 0 0
                             ^^^^^^^^^^^ ^^^^^
                             the array   the loop counter

The loop destroyed its own counter at index 2 and terminated after three iterations. It never reached
another frame. Pushed far enough (index ~1e8) it trips `SETL`'s per-access check and returns `BAD_POKE`
(status -3); deep recursion trips `DATA_STACK_OVERFLOW` (status -6). Both are clean traps.

What is NOT diagnosed is the small overrun: index 7 into a 5-word array silently hits the neighbouring
slot. That is the documented design - no per-access runtime bounds checks - and it is why a generator
targeting GAZL should catch what it can at compile time. See `docs/CompileTimeHardening.md`.


## 8. The short version

1. `localsSize` allocates; `paramsSize` does not - it feeds one entry-time comparison.
2. Fixed offsets are proven safe once, at entry, so they cost nothing afterwards.
3. Anything genuinely dynamic is checked at the point of access, against the sandbox edge.
4. `*size` is a declaration channel for frame regions the text does not name. Nothing depends on it for
   containment, but it is what turns a stack overflow into an entry-time, path-independent error rather
   than a late `BAD_POKE`. Omitting it is safe; supplying it is better.
