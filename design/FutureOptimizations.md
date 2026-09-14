# Future optimizations

Status: CANDIDATES. Things we have measured but deliberately not built.

This is not a wishlist of ideas. Every entry here has been checked against the assembler source and
measured, so the numbers can be trusted and the analysis does not have to be redone. If an entry turns
out to be wrong, correct it here rather than rediscovering it in a third place.


## 1. Dead arms after a compile-time branch

**The assembler already erases the branch itself, for free.** An operator carrying `YIELDS_GOTO` (its arm
of the emit switch in `GAZL.cpp`) has its condition evaluated at assemble time: if it is TRUE the assembler
emits a single `GOTO_B__`, and if it is FALSE it emits **nothing at all** - `ip` is not advanced. `NOOP` is the
same (the `NOOP____` arm beside it), and the value-producing equivalent is `YIELDS_CONST`, which computes
the result and emits a `MOVE_VC_`. Labels are declared at the current `ip` BEFORE emission (the
`declare(locals, ...)` above that switch), so dropping an instruction never disturbs a target.

Measured with minimal probes, where a bare `FUNC` + `RETU` is 2 words:

    GEQi #0 #1 @lbl        (false)          2 words - vanishes
    GEQi #1 #0 @lbl        (true)           3 words - one GOTO
    NOOP                                    2 words - vanishes
    three stacked false compares            2 words - all vanish

**What it does NOT do is remove the code the branch made unreachable.** That is the whole of this item.
A fully-constant `clamp(-4, 0, 6)` expansion:

    GEQi #-4 #0 @.f0_i0        ->  (nothing, -4 >= 0 is false)
    MOVi %1 #0                 ->  MOVE_VC_        runs
    GOTO @.e1_i0               ->  GOTO_B__        runs
    .f0_i0: LEQi #-4 #6 ...    ->  GOTO_B__        (-4 <= 6 is true)
    MOVi %1 #6                 ->  MOVE_VC_        unreachable
    GOTO @.e3_i0               ->  GOTO_B__        unreachable
    .f2_i0: MOVi %1 #-4        ->  MOVE_VC_        unreachable
    .e3_i0: NOOP               ->  (nothing)

Verified by assembling exactly those lines: 8 words total, 6 for the block. Both comparisons disappear,
but only 2 of the 6 instructions ever execute. The run-time cost is already near optimal; the CODE SIZE
is not.

The same shape appears at every `for` loop, whose bottom-tested form needs a pre-check for the empty
range:

| source             | emitted for the pre-check | cost                                           |
|--------------------|---------------------------|------------------------------------------------|
| `for (i = 0 to 3)` | `GEQi #0 #3 @.e0`         | 0 words - false, dropped                       |
| `for (i = 0 to n)` | `GEQi #0 $n @.e2`         | 1 word - a real run-time check, needed         |
| `for (i = 2 to 2)` | `GEQi #2 #2 @.e4`         | 1 word - true, becomes a GOTO over a dead body |

So the pre-check itself is already free wherever it is decidable. The cost is the third row: five words
are emitted for a loop that provably runs zero times, of which four are unreachable and the fifth is a
GOTO that would not be needed if the arm were dropped.

**Why it is not done, and where it would go.** Impala never evaluates these conditions itself - it emits
`GEQi #-4 #0` and lets `YIELDS_GOTO` sort it out. Eliminating the arms means the COMPILER has to know
the outcome, which is a constant-folding pass over conditions plus a reachability walk to decide which
labels and instructions survive. That is a real pass, not a peephole.

Note the interaction with inline constant folding: `expandInline` only folds bodies that are
STRAIGHT LINE, precisely to avoid reasoning about branches (see `design/impala/Inlining.md` section 5). A
condition-folding pass would lift that restriction, and the two should be designed together rather than
bolted on separately - otherwise there will be two different notions of "this value is known".

Worth it when code size matters (the whole point of inlining is to trade size for speed, and a constant
`clamp` currently pays full size for a two-instruction result).

### MEASURED 2026-09-14: the arms are free, the GOTO over them is not

The line that used to end this section - "not worth it for run-time speed alone, since the dead arms never
execute" - is right about the ARMS and wrong about the item. Removing an arm leaves the `GOTO` that hops it,
and that hop is taken every time the site is reached. The table above already notices this ("the fifth is a
GOTO that would not be needed if the arm were dropped") without pricing it.

Measured on `IIRDecimate.220.gazl`, an AudioClay bench artifact whose 107 array guards all fold, assembled
three ways and run on the x64 JIT and the interpreter:

| assembler does | GAZL instrs | JIT bytes | JIT ms | interp ms |
|---|---|---|---|---|
| nothing (today) | 508 | 37,360 | 80.0 | 365 |
| drops the dead arms | 405 | 19,232 | 79.9 | - |
| drops the arms AND the hop GOTOs | 302 | 12,640 | **39.9** | **187** |

**Halving the generated code bought 0.1 ms of 80. Dropping 103 `GOTO`s bought 2.01x** (and 1.95x on the
interpreter). Every gain is in the second step.

The mechanism is not the jump, and not the fuel check on its own - it is that **a `GOTO` makes its target a
basic-block leader**. Each folded guard mints two leaders: one at `j+1` (the dead arm) and one at the GOTO's
target (the live path). Native `js` counts across the three builds go 221 -> 118 -> 17: each step removes
about 103 leaders, but only the second removes leaders that are ENTERED. A leader with no residency map does
`cache.barrier()` - spill every dirty line and drop it - so 103 of them inside a hot loop flush the register
cache 103 times per iteration. That is the `mov` count falling 3582 -> 1625 -> 1026, and it is why the JIT
gains more than the interpreter, which has no register cache to lose.

**An assembler-side pass was built and then REJECTED on cost/benefit, not on correctness.** A reachability
sweep in `finalizeFunction` (mark from the FUNC entry, compact, re-target displacements, SWCH tables and the
raw `Value*` pointers `globals.forwardRefs` holds into the code array) reproduced the whole 2.01x and passed
every gate. It was dropped because across 74 corpus programs - the Impala goldens, the shipped firmwares and
the benchmark suite - it removed **0.96%**, touching 7 of them, and the one dramatic case was a defect in
another compiler's guard emission that its own maintainers fixed at source the same week. The pass moves
addresses, which is exactly what `threadBranches` is careful never to do, so it cannot be a peephole; it
needs a reachability walk plus three kinds of re-patching, inside the trusted gatekeeper. Do not rebuild it
without a corpus that justifies it.

What DID land is the JIT half: `jitFuelSafepoints` no longer mints a leader at `j+1` after a `GOTO` or
`SWCH`, neither of which falls through. 15.5% less native code on guard-heavy input, nothing on the suite,
and no speed change either way - because, again, those are the leaders nothing can enter.

**Benchmarking hazard, recorded because it cost real time.** `Processor::run()` is a large switch in the
same translation unit as the assembler, under `/O2 /GL`. Adding a function to `GAZL.cpp` moved interpreter
timings by 10-25% on a program every build assembled IDENTICALLY. An assembler change cannot be judged by
interpreter wall-clock in this build setup; compare generated instructions, or measure on the JIT.
