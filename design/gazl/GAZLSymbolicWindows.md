# Symbolic call windows in GAZL (finding)

Status: FINDING, not implemented. Parked for Impala 3.0. See `design/ParkedFeatures.md`.

Short version: **GAZL already supports fully symbolic by-value call windows. Nothing is missing from the
VM or the assembler.** The only thing standing in the way is that Impala's transient allocator keys its
slots by integer index. This note records what was verified, the runnable proof, the algebra a symbolic
allocator would need, and why the current numeric by-value ABI is nevertheless the correct choice today.


## The question

Impala emits struct layouts as compile-time constants (`.o.Name.field`, `.z.Name`; see
`design/impala/StructLayoutConstants.md`), so a struct in MEMORY adapts if the layout is re-packed: `LOCA *.z.V`,
`$v:.o.V.a`, strides `k*.z.Elem` and whole-struct `COPY *.z.V` all read the same symbols and scale
together.

A struct passed or returned BY VALUE does not adapt. Its size is baked as a number (`PARA *2`,
`COPY ... *2`) because it lives in a fixed block of `%` transient slots that the register allocator lays
out at compile time. That makes the emitted GAZL LOOK re-packable while by-value quietly is not. The
question was what it would take to close that gap.


## What GAZL already supports (verified, not assumed)

Every one of these was checked against `src/GAZL.cpp` and by running GAZLCmd:

- **Compile-time variables are a single identifier character.** `<AA>` and `<o0>` are rejected
  (`Invalid compile-time variable`); `parseOperand` requires `b + 3 == e` with one valid identifier char.
  Any identifier char works, so the usable set is much wider than `A-Z` (digits, lowercase, `_`, `$` all
  assemble). Impala deliberately uses only `A-Z`.

- **A transient can be named by a SYMBOLIC index: `%<A>`.** This is the key enabler and it works,
  including with a computed value:

        ! MOVi <A> #1
        MOVi %<A> #42           ; lands in %1
        ! MOVi <B> #2
        ! ADDi <B> #<B> #-1
        MOVi %<B> #99           ; lands in %1 as well

  So a call-window position at a symbolic offset needs NO runtime address arithmetic. (An earlier
  assumption that this would cost extra `ADDp` instructions was simply wrong.)

- **Window sizes and frame extents accept compile-time constants.** `CALL`, `PARA`, `LOCA` all type their
  size operand as `CONST_INT_P`, so `*.z.V` and `*<A>` are legal. `LOCA *.z.V` and `GLOB *<A>` are already
  emitted today. At run time `FUNC`/`CALL` just do `dsp += C` with the already-resolved integer, plus a
  data-stack overflow check.

- **The callee side is ALREADY fully symbolic.** A `PARA` chain is summed by the assembler, so this needs
  no Impala work at all:

        someFunction:   FUNC
                        $res: OUTi
                        $i:   INPi
                        $s:   PARA *.z.V
                        $t:   PARA *.z.R      ; sits at 1 + 1 + .z.V, resolved at assemble time
                        $j:   INPi

- **Frame addressing takes a symbolic offset plus a runtime index.** `GETL`/`SETL` compute
  `(dsp + C)[V]`, and `C` may be `.z.`-derived. That is what the local struct-array optimisation uses.

- **One real constraint:** a `CALL`'s window base must be a `TRANSIENT`. A named local is rejected
  (`CALL &f $w *1` -> `Incompatible types: $w`). This does not matter, because only the offsets WITHIN the
  window need to be symbolic, not the base.


## Runnable proof

`output/symwin.gazl` is a complete hand-written program in the fully symbolic style. It models

    R funcReturningStruct(W w)
    int someFunction(int i, V s, R t, int j)
    someFunction(someInt, structOne, funcReturningStruct(structTwo), anotherInt)

The caller lays its whole window out at assemble time:

    ;   %0     out (int)
    ;   %1     arg i
    ;   %2     arg s (V)   .z.V words
    ;   %<A>   arg t (R)   .z.R words   <- nested call writes its result straight here
    ;   %<B>   arg j
                    ! ADDi <A> #2 #.z.V
                    ! ADDi <B> #<A> #.z.R
                    ! ADDi <C> #<B> #1          ; outer frame extent (the CALL size)
                    ! ADDi <F> #<B> #.z.W       ; top of the nested window
                    ! ADDi <G> #<F> #1          ; scratch, clear of BOTH windows

                    MOVi %1 $someInt            ; arg i

                    ADRL %<F> %2 *.z.V          ; arg s: window region
                    ADRL %<G> $structOne *.z.V  ;        source
                    COPY %<F> %<G> *.z.V        ;        symbolic copy

                    ! ADDi <E> #.z.R #.z.W      ; nested frame extent
                    ADRL %<F> %<B> *.z.W        ; nested arg w: window region
                    ADRL %<G> $structTwo *.z.W
                    COPY %<F> %<G> *.z.W
                    CALL &fRS %<A> *<E>         ; result lands ON arg t - no copy-out

                    MOVi %<B> $anotherInt       ; arg j - AFTER the nested window died
                    CALL &someFunction %0 *<C>
                    MOVi $result %0

It assembles to 32 instructions and prints `56`. Three things to note:

1. **Zero runtime cost.** Every `%<X>` and `*<X>` folds at assemble time; the `! ADDi` lines are
   directives, not instructions. No extra `ADDp` versus the numeric ABI.
2. **The nested call is free.** Its output window is placed exactly on `arg t`, so the inner return is
   written directly into the outer argument - no copy-out. That is today's window-sliding trick, expressed
   symbolically.
3. **`<B>` is both `arg j` and the nested call's input slot**, which is why `arg j` must be written after
   the inner `CALL`. Left-to-right argument evaluation already guarantees that.

**Re-pack test.** Patching only the layout header - a leading pad field added to `V` (`.z.V` 2 -> 3), `W`
grown (1 -> 2), and `R`'s two fields swapped - with every instruction left byte-identical, the program
still prints `56`. `diff` shows only `! DEFi` header lines changed. Argument `t` moved from `%4` to `%5`
and `j` from `%6` to `%7`, all re-derived by the assembler.


## What actually blocks it: Impala's allocator

A call window is not requested as a range. It is built one slot at a time, each claim asserting that slot
is free. In `impala/impala.jspeg`:

- `counters['%']` is a high-water mark (slots `0..counters-1` minted); `stock['%']` is a free list of
  tokens like `'%3'`. A slot is live if minted and not in the free list.
- `borrowForCall` (:764) picks the base. Empty free list -> mint fresh. Otherwise, if the topmost minted
  slot is LIVE (`maxFree < counters-1`) it mints above everything rather than seat a window in a hole
  below a live temp. Else it sorts ascending and takes the first slot of the last consecutive free run.
- `claimSlot` (:823) takes a specific slot: either `n === counters` -> `counters++`, or splice `'%'+n` out
  of the free list, **asserting it is there**. That assert is the overlap check.
- Call sites: base at :3670, extra output slots at :3672, `winSlot = base + retSlots + words` at :3847,
  by-value struct args at :3850, the `CALL` emit at :3790, argument slots freed at :3796.

Contiguity is emergent: the base sits at or above the top contiguous free block, so upward growth either
consumes that run or extends `counters`. The same property is what makes nested-call adoption fall out for
free - an inner call's `borrowForCall` naturally picks the outer call's next argument slot, so
`copyStructArg` sees `winBase === winSlot` and adopts the window instead of copying.

Every step of that needs an **integer identity**: `claimSlot` compares against `counters` or looks up
`'%'+n`; `borrowForCall` compares `maxFree` against `counters-1`; freeing reconstructs concrete `%N`
names. With `winSlot = base + retSlots + .z.V` there is no integer to look up or compare.


## The algebra a symbolic allocator needs

A slot position becomes an expression

    pos = c + sum over i of k_i * .z.X_i          (c, k_i integers)

canonicalised as a constant plus a multiset of `.z.` terms. Then:

- **Equality is decidable** - structural comparison of the canonical form. This is all the free list needs
  as a key.
- **Ordering is decidable only when the difference is sign-definite.** `A > B` whenever `A - B` is a sum of
  non-negative terms with something positive. Since every struct is at least one word, **monotone growth is
  always provable**, so "the new top is above everything live" is always checkable.
- **General fitting is NOT decidable.** "Does a value of size S fit in the hole `[lo, hi)`?" needs
  `S <= hi - lo` with both sides symbolic. And after partially filling a hole you cannot tell whether
  anything remains - the leftover `.z.V - 1` might be 0.

So reuse is not lost, it is restricted to **exact extent matches**, which is decidable and covers the
cases that matter:

| kind                              | reuse                | why                        |
| --------------------------------- | -------------------- | -------------------------- |
| scalar temps (extent 1)           | full, as today       | all extents identical      |
| a freed region of the same struct | yes                  | canonical forms match      |
| cross-size or partial holes       | no - push on top     | fit undecidable            |

Because all scalars have extent 1, **today's scalar temp reuse survives completely** - and that is where
nearly all register pressure lives. Only cross-size hole reuse is given up.

Two further consequences. The call path is **already** push-on-top (`borrowForCall` refuses to seat a
window in a hole), so its semantics barely move. And out-of-order frees stop being a correctness problem
and become a waste problem: an unmeasurable hole simply is not reused, and the space returns when the
region unwinds at statement or function end. Frames are small, so that is cheap.

The work, concretely: a canonical position type with add/equal/render; `borrowForCall`, `claimSlot` and
`returnBack` keyed by it instead of an integer; and the argument/`CALL` emitters rendering `%<X>` / `*<X>`
through the existing `<`-scratch folding. Losing cross-size hole reuse is a minor efficiency cost.

**The risk is location, not size.** This is the module holding the R2 invariant and a fuzzer-found
window-sliding bug. Today `claimSlot`'s assert catches overlap loudly per slot; a symbolic allocator would
replace that with structural discipline (always push above the provable top). That is arguably stronger -
overlap becomes impossible by construction - but it is a different safety model, and getting the
discipline wrong fails silently. That happened while building `symwin.gazl`: the first version placed
scratch at the outer frame extent, which the re-packed `.z.W = 2` nested window then overlapped.


## Why the numeric by-value ABI is correct today

Not a compromise, and not a bug to be fixed:

- GAZL's `CALL` window is a fixed, contiguous, compile-time-sized slot block. Mapping a struct straight
  onto it - struct words ARE frame slots, reached frame-relative as `$x:.o.V.a` - is the **most 1:1
  representation of GAZL's calling convention available**. A hidden-pointer ABI would insert an
  indirection GAZL's call does not have, and would be LESS faithful.
- GAZL itself has exactly this asymmetry: a struct in memory takes a symbolic size, a struct in a call
  window takes a fixed slot count. Impala mirrors both honestly.
- Writing the by-value size as the baked number means it can never diverge from the window it must match.
  A symbolic hint over a still-numeric slot count is the genuinely dangerous option: if `.z.V` later
  resolved larger, the `COPY` would overrun a window that is still only N slots wide.

The two coherent designs are all-numeric (today) and all-symbolic (this note). The middle - symbolic size
hints over numeric slot positioning - is the bug. It was briefly committed (`211131c`) and reverted
(`ed3c3a8`), with the rationale left in a comment in `copyStructArg`. Do not re-litigate it into `*.z.V`
without also doing the allocator.


## What implementing it would buy

- `E425` lifts: an extern struct (host-owned size) could be passed and returned by value, because nothing
  would need its size at Impala-compile time.
- A host re-packing a struct would only need to RE-ASSEMBLE, not recompile Impala - by-value calls
  included. The modifiable-layout promise would hold end to end instead of stopping at the call boundary.
- Runtime predictability is unaffected: the assembler still resolves every size to a fixed number, so
  frames stay fixed and bounded. What Impala loses is compile-time knowledge of exact stack usage.

Suggested trigger for revisiting: either a real program wanting extern-struct-by-value, or the transient
allocator being hardened for its own sake, at which point this rides along cheaply.
