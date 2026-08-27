# Tail calls (design note)

Status: the SELF-RECURSION increment is IMPLEMENTED (2026-08-27, GAZL2 branch). `tail f(...);` is a
terminal statement under `--gazl2`: the arguments marshal exactly like a call's, the parameters are
rewritten from the window and control re-enters at the body's `.tail` entry label - no new instruction.
The enabler on the engine side is that `INP*` parameters are writable inside a `GAZL #2` region (W bit
minted at declaration; GAZL 1 acceptance is untouched). Diagnostics: E466 (`tail` needs `--gazl2`),
E467 (target must be the enclosing function), E468 (not in an `inline function`), plus the ordinary
call checks (E405/E406). Because `tail` is a statement that transfers control, "tail position" holds by
construction - anything after it on a path is simply unreachable, exactly as after `return`.

The general cross-function `TAIL` instruction below remains a DESIGN NOTE: it needs a GAZL opcode *and*
Impala syntax, so it is sized for its own branch and cannot be done from either side alone.

Related: [`GAZLAssemblerOptimizations.md`](GAZLAssemblerOptimizations.md) covers peepholes that need no
ISA change; this is the one that does.


## The gap

Recursion works. Tail recursion is not eliminated, so a textbook accumulator function overflows:

```impala
function count(int n, int acc) returns int r {
	if (n == 0) goto base;
	r = count(n - 1, acc + n);      // tail position
	goto done;
base: ;
	r = acc;
done: ;
}
```

`count(100, 0)` returns 5050. `count(100000, 0)` traps - `Exception: run returned status -6`, the
entry-time frame check in `FUNC` doing its job. It fails cleanly rather than corrupting, but it fails.
(This is now the self-recursion case `tail` closes; the CROSS-function version of the same trap is what
remains open below.)


## Why the assembler cannot do it alone

The call/branch forms are the whole vocabulary (`src/GAZL.cpp:337-342`, `:398`, `:497`):

| Form | Operands |
|---|---|
| `CALL_c__` | `FUNC\|FORWARD`, -, - |
| `CALL_cvs` | `FUNC\|FORWARD`, `TRANSIENT`, `CONST_INT_P` |
| `CALL_n__` / `CALL_nvs` | as above, `NATIVE\|FORWARD` target |
| `CALL_v__` / `CALL_vvs` | as above, `VAR_PTR_R` target (funcptr) |
| `GOTO_b__` | `FWD_BRANCH`, -, - |
| `RETU____` | -, -, - |

`CALL` always pushes a frame. `GOTO` takes a `FWD_BRANCH`, which is a **function-local** label - a label
is always local to its body, which is what `E445` relies on. So there is no way to spell "enter that
function reusing this frame", and `CALL f; RETU` cannot be peepholed into a jump. This is an ISA gap, not
a missing optimization.


## Proposed instruction

A `TAIL` family mirroring `CALL` exactly, which keeps the 3-operand budget (GAZL is a 3-operand ISA -
see [`Impala2Review.md`](../impala/Impala2Review.md) on pointer arithmetic for why that matters):

| Form | Operands |
|---|---|
| `TAIL_c__` | `FUNC\|FORWARD`, -, - |
| `TAIL_cvs` | `FUNC\|FORWARD`, `TRANSIENT`, `CONST_INT_P` |
| `TAIL_v__` / `TAIL_vvs` | `VAR_PTR_R` target |

Semantics: move the argument window down onto the current function's own incoming window, do **not**
push a return address, transfer control. The callee's eventual `RETU` returns directly to our caller.

**The legality rule is static and cheap.** The current function declares `PARA *n`; the tail target needs
a window of `*m`. The call is legal exactly when `m <= n`, and both numbers are assembly-time constants,
so the assembler can reject a bad one outright - no runtime check, no dynamic frame growth. Self-recursion
satisfies it by construction (`m == n`).

`LOCAL_BOUNDS` on the `_cvs` forms does frame-bounds bookkeeping; the `TAIL` forms want the analogous
check against the `PARA` region rather than against the frame top.

**Natives are deliberately excluded** from the first cut. A `TAIL_n__` would hand our frame to host C++
code whose return path is not a GAZL `RETU`; the semantics need settling separately and nothing needs it.


## Proposed Impala syntax

A terminal statement:

```impala
function count(int n, int acc) returns int r {
	if (n == 0) goto base;
	tail count(n - 1, acc + n);     // becomes a jump; runs in constant stack
base: ;
	r = acc;
}
```

`tail f(...)` transfers control; `f`'s return value becomes this function's return value, so the named
return variable is simply never assigned on that path.

Rules, each a real diagnostic rather than a silent decline to optimize:

| Rule | Why |
|---|---|
| Must be in tail position (nothing may follow it on that path) | otherwise it is not a tail call |
| Callee's return type must match this function's exactly | its `RETU` returns to *our* caller |
| Callee's window must fit our `PARA` | the static `m <= n` rule above |
| Not allowed in an `inline function` | an inline body is spliced into its caller, so the frame it would reuse is the wrong one - same reasoning that makes `E435` and the `return` design reject inline bodies |

Alternative spellings considered: Rust reserves `become` for precisely this, and `goto f(...)` reads well
given `goto` is already the jump keyword. `tail` is proposed because it names the concept and does not
overload an existing statement.


## Why explicit, and not an automatic optimization

This is the important design point, and it follows the language's own thesis.

Silent tail-call elimination is the canonical example of an optimization whose *presence* decides whether
a program works at all: with it, `count(100000, 0)` runs; without it, byte-identical source traps. That is
the exact opposite of "cost is predictable from the declared types"
([`Impala2.md`](../../docs/impala/Impala2.md), "The cost model: dots are free"), and it is why C never guaranteed it.

For a language whose users write firmware against a real stack budget, recursion depth belongs in the
source. An explicit keyword also converts every one of the rules above from "we quietly did not optimize
this" into a diagnostic the programmer can act on. Same argument as `inline`: opt in at the site, and the
cost is stated rather than inferred.


## A cheaper first increment: self-recursion only (IMPLEMENTED)

**Self**-tail-recursion needs no ISA change at all. If a function tail-calls itself, the rewrite is
entirely function-local: copy the new argument values into the existing parameter slots, then `GOTO` the
function's own entry label - a plain `GOTO_b__`, which already exists.

The one hazard is ordering: in `tail count(n - 1, acc + n)`, `acc + n` must be evaluated before `n` is
overwritten. The existing argument-marshalling window already materializes arguments before the call, so
copying back from it is natural - and the window (`%N`, above `dsp`) and the parameter slots (bottom of
the locals, below `dsp`) sit at opposite ends of the frame, so every new value is computed before any old
one is overwritten, with no per-argument ordering analysis.

As landed: every out-of-line body opens with a fixed `.tail` entry label (nulled when no `tail` statement
references it, so tail-free output is byte-identical - the golden corpus is that gate), and the FuncCall
close emits `MOV? $param %slot` per parameter plus `GOTO @.tail` instead of a `CALL`. The parameter
writes are why this is `--gazl2`-only: `INP*` is read-only in GAZL 1, and the engines deployed in the
field freeze that, so GAZL 2 regions mint the W bit instead.

It covers the accumulator idiom, which is the case that actually overflows today, and under the explicit
`tail` keyword it is a pure implementation detail - the same source keeps working unchanged when the
general instruction lands (the E467 diagnostic simply disappears).


## Interactions

- **`--dead-strip`**: a `tail` site is an ordinary reference edge; reachability is unaffected (and a
  self-edge changes no closure anyway).
- **Signature metadata**: a `tail` site emits the same `; expects f(...) -> T` row a call does, so any
  signature-row consumer keeps checking it across units.
- **Mutual recursion**: the general `TAIL` form covers it; the self-recursion increment does not. State
  machines written as mutually tail-calling handlers are the main thing that unlocks.
- **Debugging**: a tail call replaces the frame, so it does not appear in a stack trace. Standard for the
  technique, and another reason it should be visible in the source.
