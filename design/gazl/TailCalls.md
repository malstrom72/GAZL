# Tail calls (design note)

Status: the `TAIL` instruction is IMPLEMENTED (2026-08-27, GAZL2 branch), and Impala's `tail f(...);`
terminal statement lowers to it under `--gazl2`. The Impala SURFACE is self-recursion only for now
(E467) - the instruction itself is general, but the cross-function window/return contract checks in the
compiler are future work. Diagnostics: E466 (`tail` needs `--gazl2` - the instruction exists only on
GAZL 2 engines), E467 (target must be the enclosing function), E468 (not in an `inline function`), plus
the ordinary call checks (E405/E406). Because `tail` is a statement that transfers control, "tail
position" holds by construction - anything after it on a path is simply unreachable, exactly as after
`return`.

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


## The instruction (as landed)

A `TAIL` pair mirroring `CALL`'s callee forms, within the 3-operand budget (GAZL is a 3-operand ISA -
see [`Impala2Review.md`](../impala/Impala2Review.md) on pointer arithmetic for why that matters):

| Form | Operands |
|---|---|
| `TAIL_cs_` | `FUNC\|TARGET\|FORWARD`, `CONST_INT_P` (*m), baked |
| `TAIL_vs_` | `VAR_PTR_R\|VAR_TGT_R` target (a `t` local inside a region), `CONST_INT_P` (*m), baked |

Semantics: the argument window is `%0 .. %m-1` **by definition** - fixing the window at the bottom of
the transient pool is what buys back the operand the 3-slot budget cannot spare, and a tail call sits in
statement position where nothing else is live, so a compiler gets `%0` for free. The engine slides those
`m` words down onto the current function's own frame base, rewinds `dsp` to it, and jumps to the target's
`FUNC`, which re-stacks its locals and re-runs its overflow check from OUR base. No return address is
pushed, so the target's eventual `RETU` returns directly to our caller, with its outputs in the slots
that caller already reads. The third operand slot carries the current function's own frame size, baked at
assembly the way `RETU` carries `localsSize` - that is how the runtime finds the frame base, since `dsp`
sits at the TOP of the locals and named locals resolve at negative offsets.

**The legality rule is static and cheap.** `*m` must not exceed the function's own incoming window (its
`INP*`/`OUT*`/`PARA` words, tracked per function as `windowSize`): a wider window is an assembly error
(`TAIL window exceeds the function's own parameter window`). Self-recursion satisfies it by construction
(`m == n`), and the source reads (`%0..m-1`) are folded into `paramsSize` so the frame check reserves
them even when nothing else references those slots.

What the rule actually protects is the COPY, not a calling contract: the destination is `base[0..m)`,
so the true bound is `m <= L` (frame size) - that keeps the write in-frame and, since the source starts
at `dsp = base + L`, overlap-free. `m <= window` is a tightening of that inherited from this note's
original `m <= n` rule, and it is NOT load-bearing: the callee's `FUNC` re-checks its own frame from the
base, and the original caller reads `base + 0` however wide the window was. When the cross-function
surface lands and a callee needs a window WIDER than the caller's own, the check should relax to
`m <= L` and the compiler simply pads the frame (a dead `LOCA`) until it fits - no fourth operand
needed. (A source operand `%w` would achieve the same via `w >= m - L`, but callee + w + m + the baked
frame size is four values in a three-slot cell, which is exactly why the window is fixed at `%0`.)

**Additive, not region-gated.** Like `SCOP`/`SEEK`, `TAIL` is a new mnemonic: dialect-1 text may use it
on a GAZL 2 engine, and an older engine rejects it as an unknown mnemonic - which is the whole version
story for additions.

**Natives are deliberately excluded.** A `TAIL_n__` would hand our frame to host C++ code whose return
path is not a GAZL `RETU`; the semantics need settling separately and nothing needs it.


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


## The no-ISA-change route: tried and REVERTED (2026-08-27)

This note originally proposed a cheaper first increment: for SELF-recursion, copy the new argument
values into the existing parameter slots and `GOTO` the function's own entry label - no new instruction.
It was built and then reverted the same day, and the reason is worth keeping.

The rewrite needs to WRITE the parameters, and `INP*` locals are read-only - a deliberate contract, not
an accident. Making them writable inside `GAZL #2` regions made the GOTO route work, but conceded the
key point: the relaxation was itself a GAZL 2 engine change, unusable on any deployed engine. Once tail
calls require a new engine anyway, the GOTO lowering has no compatibility advantage left over a real
instruction - it just spends `n` `MOV`s, an entry label and a weakened `INP` contract to emulate what
one opcode says outright. So `INP*` stays read-only everywhere, and `TAIL` is an instruction.


## Interactions

- **`--dead-strip`**: a `tail` site is an ordinary reference edge; reachability is unaffected (and a
  self-edge changes no closure anyway).
- **Signature metadata**: a `tail` site emits the same `; expects f(...) -> T` row a call does, so any
  signature-row consumer keeps checking it across units.
- **Mutual recursion**: the INSTRUCTION covers it already; Impala's surface does not yet (E467). The
  missing piece is compiler-side: checking the window and return contracts across two functions (and
  through funcptrs, where `TAIL_vs_` waits). State machines written as mutually tail-calling handlers
  are the main thing that unlocks.
- **Debugging**: a tail call replaces the frame, so it does not appear in a stack trace. Standard for the
  technique, and another reason it should be visible in the source.
