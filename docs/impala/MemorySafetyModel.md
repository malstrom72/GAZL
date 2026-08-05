# GAZL memory safety and frame layout

Status: REFERENCE. Written 2026-07-27 after re-deriving all of it from `src/GAZL.cpp` and `src/GAZL.h`,
because the `*size` operands read as if containment depended on them and it does not. Every claim below
cites the FUNCTION or `case` that implements it - by name, not by line number, because line numbers here
went stale within a week. If you are here because `*size` looks load-bearing for SAFETY: it is
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
- A callee's frame begins wherever the CALLER's `CALL` window base says (`dsp += C1.i`, at the `call:`
  target the `CALL_*` cases share in `Processor::run`).
  There is no separate allocation step for it.
- **`dsp` only ever moves UP on the way in.** Both `CALL` and `FUNC` add to it, so your caller sits at
  LOWER addresses than you and your callees at higher ones. This matters in section 7.

So the only thing that ever advances the stack pointer is a function's own declared locals.


## 2. The two numbers on FUNC, and what they are NOT

`FUNC` carries two constants, computed by the assembler and used in exactly one instruction:

    case FUNC_CC_:  if ((dsp += (UInt)(C0.i)) + C1.i > dataStackEnd) { err = DATA_STACK_OVERFLOW; }
                                       ^^^^     ^^^^
                                       |        C1 = paramsSize (allocates nothing)
                                       C0 = localsSize (advances dsp)

**`localsSize`** is the allocation. It accumulates only from declared frame lines - `OUTi`, `INPi`,
`PARA`, `LOCi`, `LOCA` - in `Assembler::feed`'s `case LOCA____:`, which ends `localsSize += size`. That is
what advances `dsp`.

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

    // Assembler::parseOperand
    case '%':  paramsSize = maximum(paramsSize, (UInt)(v->i + 1));

Write `MOVi %20 #1` anywhere and the frame is proven to reach slot 20. This is why transients need no
declaration. Second, the `*size` operand, carried by the four `OPERATORS[]` rows flagged `LOCAL_BOUNDS` -
`ADRL_vvs`, `CALL_cvs`, `CALL_nvs`, `CALL_vvs`:

    // Assembler::feed, its `(op->otherFlags & LOCAL_BOUNDS)` branch
    paramsSize = maximum((Int)(paramsSize), p1->i + p2->i);

That second one covers slots reached WITHOUT being named: a pointer's span, or a call window.


## 4. What is checked, and when

Sites below are in `GAZL.cpp` unless marked `GAZL.h`; the `case` names are `Processor::run`'s.

| access                          | bound                         | enforced                                   |
| ------------------------------- | ----------------------------- | ------------------------------------------ |
| `%N`, `$x` (fixed offset)       | `localsSize + paramsSize`     | once, at `FUNC` entry (`case FUNC_CC_:`)   |
| `$obj:CONST` (constant offset)  | that object's extent          | ASSEMBLY: `Symbols::resolve`               |
| `SETL` / `GETL` (dynamic index) | `dataStackEnd`                | every access (`GETL_VVV:`, `SETL_VVV:`)    |
| `PEEK`                          | `memorySize`                  | every access (`PEEK_VVV:`, `PEEK_VCV:`)    |
| `POKE`                          | `rwMemorySize`                | every access (`POKE_VVV:` and siblings)    |
| GAZL -> GAZL call window        | the callee's own frame        | the callee's `FUNC`, from the new `dsp`    |
| GAZL -> native call window      | the count the NATIVE asks for | `Processor::accessParams` (`GAZL.h`)       |

`POKE` is bounded by `rwMemorySize` rather than `memorySize`, so read-only memory really is read-only.

Note the `SETL`/`GETL` row: they are bounded by the END OF THE STACK, not by the object they index. The
index is compared unsigned, so it cannot run backwards below its base either - the reach is base up to
`dataStackEnd` and no further. A dynamic index cannot be validated statically, so the only guarantee is
containment, not correctness.


## 5. Why `*size` is not needed for containment

Every row of that table is self-sufficient without it:

- **GAZL -> GAZL call.** `CALL_CVC` moves the view (`dsp += C1.i` at the shared `call:` target) and the
  callee's own `FUNC` performs its own entry check from there. The caller's coverage is belt-and-braces.
- **GAZL -> native call.** `case CALL_NVC:` only sets `this->dsp = dsp + C1.i` and runs host code. It
  never reads the size. The native declares how many words it wants, and that is where the check happens:

      // GAZL.h
      inline Value* Processor::accessParams(UInt count) const {
          return (dsp + count <= dataStackEnd ? dsp : 0);
      }

  A null return means the native should report `DATA_STACK_OVERFLOW`.
- **`ADRL`.** At run time it only computes an address - `case ADRL_VV_:` is the single line
  `V0.p = Pointer(&dsp[C1.i] - mb)`. The size operand is never read there, and the resulting pointer is
  checked on every use by `PEEK`/`POKE`.
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


## 7. What "safe" means here, and what it does not

The guarantee is CONTAINMENT: a GAZL program cannot touch anything outside its own memory block and data
stack, whatever it does. Every way out is checked (section 4), so the worst case is a clean trap -
`BAD_PEEK`, `BAD_POKE`, or `DATA_STACK_OVERFLOW` - never a wild host pointer.

The guarantee is NOT that an index stays inside the object it indexes. A frame is an allocation, not a
fence, and a modest overrun quietly lands on whatever the frame layout happened to put next. Two
different reaches are worth keeping apart:

**A dynamic index (`SETL`/`GETL`) walks forward only.** The index goes into an UNSIGNED compare
(`ui = V1.i` in `case SETL_VVV:`), so a negative index wraps huge and traps rather than stepping backwards.
The base is an assembler-resolved offset in your own frame. So the reach is base .. `dataStackEnd`:
the rest of your own locals, your transients, and then the unused space above `dsp` where your CALLEES'
frames will be built. It cannot reach your caller - callers are at LOWER addresses (locals are negative
offsets - `v->i -= localsSize` in `Assembler::parseOperand` - and both `FUNC` and `CALL` only ever
advance `dsp`).

**A computed pointer reaches the whole RW region.** `ADRL` hands out a memory-block-relative pointer,
`ADDp`/`SUBp` are unchecked, and `PEEK`/`POKE` test only against `memorySize`/`rwMemorySize`. The data
stack lives INSIDE that RW region (see the memory map in `GAZL.h`), so a pointer walked far enough can
read or write any frame, including the caller's, and every global. That is deliberate - it is how
arrays and structs are passed - but it means pointer arithmetic is the wide door, not the array index.

Either way the sandbox holds. A bad index or a stray pointer is a correctness bug with a blast radius,
not an escape. Anything that must be caught tighter than that is on whatever generates the GAZL, in three
tiers: at **Impala compile time** when index and extent are both numbers Impala knows (`E461`); at **GAZL
assembly time** when either end is a symbol, as an emitted `! FAIL` or the assembler's own
`Offset out of bounds`; and at **run time**, for an index no static tier can see, as the `--range-checks`
guards Impala can emit into the GAZL - two compares per subscript, `DEBUG`-gated, off by default. A bare
pointer has no extent and is checked by none of them. See `docs/CompileTimeHardening.md` and
`docs/Impala2.md`, "Array bounds".


## 8. The short version

1. `localsSize` allocates; `paramsSize` does not - it feeds one entry-time comparison.
2. Fixed offsets are proven safe once, at entry, so they cost nothing afterwards.
3. Anything genuinely dynamic is checked at the point of access, against the sandbox edge.
4. `*size` is a declaration channel for frame regions the text does not name. Nothing depends on it for
   containment, but it is what turns a stack overflow into an entry-time, path-independent error rather
   than a late `BAD_POKE`. Omitting it is safe; supplying it is better.
