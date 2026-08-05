# Future optimizations

Status: CANDIDATES. Things we have measured but deliberately not built.

This is not a wishlist of ideas. Every entry here has been checked against the assembler source and
measured, so the numbers can be trusted and the analysis does not have to be redone. If an entry turns
out to be wrong, correct it here rather than rediscovering it in a third place.


## 1. Dead arms after a compile-time branch

**The assembler already erases the branch itself, for free.** An operator carrying `YIELDS_GOTO`
(`GAZL.cpp:1154`) has its condition evaluated at assemble time: if it is TRUE the assembler emits a
single `GOTO_B__`, and if it is FALSE it emits **nothing at all** - `ip` is not advanced. `NOOP` is the
same (`GAZL.cpp:1143`), and the value-producing equivalent is `YIELDS_CONST`, which computes the result
and emits a `MOVE_VC_` (`GAZL.cpp:1144`). Labels are declared at the current `ip` BEFORE emission
(`GAZL.cpp:1141`), so dropping an instruction never disturbs a target.

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
STRAIGHT LINE, precisely to avoid reasoning about branches (see `docs/Inlining.md` section 5). A
condition-folding pass would lift that restriction, and the two should be designed together rather than
bolted on separately - otherwise there will be two different notions of "this value is known".

Worth it when code size matters (the whole point of inlining is to trade size for speed, and a constant
`clamp` currently pays full size for a two-instruction result). Not worth it for run-time speed alone,
since the dead arms never execute.
