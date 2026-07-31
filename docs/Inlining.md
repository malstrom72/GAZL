# Function inlining in Impala (spec)

Status: SPEC. Explicit `inline` keyword, no heuristics. Targets FUTURE firmware - see the coverage note at
the end for what this deliberately does not reach.

Background measurements and the Impala-vs-assembler placement argument live in `docs/InliningInvestigation.md`
(brought onto this branch, with a status header recording what this work corrected).


## 1. Why the compiler and not the assembler

Measured on this machine, one program, three lowerings, all verified to produce the same result
(2-argument leaf helper `mix(int a, int b) { r = a * 3 + b; }` called 10M times in a loop):

| lowering | min ms | ns/call saved | share of the prize |
|----------|--------|---------------|--------------------|
| out-of-line CALL                              | 84.66 | -    | -    |
| assembler-level inline (body copied literally) | 46.89 | 3.78 | 73%  |
| Impala-level, one argument materialised        | 36.76 | 4.79 | 94%  |
| Impala-level, arguments substituted            | 32.61 | 5.18 | 100% |

The assembler ceiling is 73% because it works on raw window slots and has no provenance to substitute
from - it must keep the marshalling moves. Impala knows what each argument IS, so the moves disappear
rather than merely getting cheaper. That last 27% is the whole reason to do this in the compiler.

Note the practical consequence for the rules below: materialising ONE argument costs about 6% of the
prize. Materialising is cheap, so the rules should substitute only where it is unarguably safe and
materialise everywhere else, rather than reach for a clever analysis.


## 2. Semantics of `inline`

    inline function mix(int a, int b) returns int r { r = a * 3 + b; }

- **No out-of-line copy is emitted.** No `FUNC`, no body, no signature row. The name exists only as
  something to expand at a call site.
- Every direct call is expanded. There is no budget, no size heuristic, and no silent fallback: if a
  call cannot be expanded, that is a diagnostic, not a de-optimisation.
- Nested inlining works for free: an `inline` function that calls another `inline` function has the
  inner body already expanded into its own captured body.
- Imported `inline` functions work, because an import closure concatenates the defining unit first, so
  the body is captured before any importer can call it. Verified in the `import/` fixture: `clampi`
  lives only in `mathlib.impala`, is called from `main.impala`, and emits no symbol in the linked GAZL.
  `impala compile` resolves the closure, so this needs no separate step.

Rejected, each with its own diagnostic (section 7): recursion, taking the address, `export`, `extern`,
forward declaration, and declaring an array or struct local.


## 3. What gets captured

Impala already keeps a per-function linear IR: `$$parser.metacode`, a flat list of
`{ operator, type, operands[3] }`. `flushMetaCode` renders it and then clears it with
`metacode.length = 0`, and `FuncDecl` follows with `prune(symbols.locals)`. So the body already exists
in exactly the right form and is simply thrown away.

Capture therefore means: snapshot the list before the flush, and record

- `body` - a copy of each record, minus the trailing `RETU` and minus the `;` source-comment records
- `params` - the parameter names in order (`$a`, `$b`)
- `ret` - the return name (`$r`), or none for a void helper
- `opaque` - the single classification flag from section 4

There is exactly ONE `RETU`, at the end, because Impala has no `return` statement. Multi-exit callees -
the complication an assembler-level pass has to solve - do not exist here.


## 4. Transparent vs opaque

An **opaque** routine is one whose writes the compiler cannot attribute to a named location.

Writes come in two kinds:

    global gs = v      ->  POKE &gs $v            target is spelled out: the global "gs"
    global ga[i] = v   ->  POKE &ga $i $v         target is spelled out: the global "ga"
    la[i] = v          ->  SETL $la $i $v         target is spelled out: our own local "la"
    ls.f = v           ->  MOVi $ls:.o.S.f $v     a frame move, not a memory write at all

    *p = v             ->  POKE $p $v             target is WHEREVER p POINTS - unknown
    sp->f = v          ->  POKE $sp #.o.S.f $v    likewise, through the pointer in sp

A routine is transparent if all of its writes are of the first kind, and opaque if any write is of the
second kind - or if it calls anything, since the callee's writes are invisible.

    isOpaque(body):
        for instr in body:
            if instr.operator not in KNOWN:        return true   // unclassified: assume the worst
            if instr.operator in CALLS:            return true   // cannot see the callee's writes
            if instr.operator in BLOCK_COPY:       return true   // COPY: computed destination
            if instr.operator in POINTER_STORE:
                if not instr.operands[0] starts with '&':
                    return true                                  // destination is a pointer VALUE
        return false

    CALLS         = { '()'   }        // CALL
    BLOCK_COPY    = { 'copy' }        // COPY
    POINTER_STORE = { '[]=', '*=' }   // both lower to POKE
    FRAME_STORE   = { '[]$=' }        // SETL - writes INSIDE a named local, never opaque
    KNOWN         = PURE + POINTER_STORE + FRAME_STORE + CALLS + BLOCK_COPY

Two things that are easy to get wrong:

- **The `&` test must be per-operator.** For `POKE`, an operand starting with `$` is a pointer whose
  VALUE is the target (`*p = v`). For `SETL`, an operand starting with `$` is the frame object being
  written into (`la[i] = v`). Same sigil, opposite meaning, so "starts with `$`" is not a usable test.
- **`KNOWN` is a positive list.** An operator the classifier does not recognise must default to opaque.
  With a blacklist of writing operators, a newly added write form would default to SAFE and silently
  break soundness with no diagnostic. Same code, opposite failure direction.

Why transparency is enough: if every write names its destination, then every location the body can
write is either a global or a frame object the callee itself declared. A callee has no way to NAME a
caller's local, so a transparent body provably cannot disturb one - regardless of what pointers exist
elsewhere in the program. This is a statement about naming, not a points-to analysis, which is why
unbounded pointer reachability (a callee can load a pointer out of a global) does not weaken it.


## 5. Per-argument decision

    argumentPlan(argOperand, opaque):
        if argOperand is a caller local ('$') and not opaque:  return SUBSTITUTE
        otherwise:                                             return MATERIALISE

`MATERIALISE` emits exactly the instruction the call already emitted (`MOVi %t $x` or `PEEK %t &g`)
into a borrowed transient and uses that. It therefore never costs more than not inlining at all.

**Finding the move to delete: the backward scan stops at anything OPAQUE.** Substitution is implemented
as a deletion - walk back from the end of the metacode to the call's `mark` for the instruction that
filled window slot `%N`, and if it is a plain move, drop it and use its source. Deleting the move DEFERS
the read to wherever the body uses it, so it is sound only if nothing emitted in between can write that
source. Two different things break a naive `operands[0] === slot` scan:

- **A CALL writes its own window but names the base in operand 1**, so the test walks straight past it.
  Slot numbers are recycled by borrow/return, so what it finds next can be an unrelated earlier call's
  marshalling:

        CALL &fn2 %1 *2         ; its argument MOV into %2 gets deleted, far below
        ...
        CALL &fn1 %2 *4         ; %2 is THIS call's RESULT - the scan must stop here
        <expansion>             ; instead: fTOi %<A> #1 #1.0   <- fn2's int argument, in a float operand

  Found by the fuzzer. It only surfaced because the operand class happened to catch it - the assembler
  refused with *"Did not expect constant int: 1"*. A same-typed constant would have been silent.

- **A sibling argument that is itself an inline call expands in place, emitting no CALL record at all.**
  So testing for a call is not enough:

        add(x, bump(&x))    with both inline, and bump doing `*p = 99`
        ->  ADRL %4 $x *0 / POKE %4 #99 / ADDi %1 $x #0      ; reads x AFTER the poke: 99, not 1

  Spelling `bump` without `inline` gave 1. One keyword apart, two answers.

Both are the same question - "can this record write a location it does not NAME?" - which is exactly
what section 4's transparency predicate already answers. `recordIsOpaque` is that predicate for a single
record; `bodyIsOpaque` is a loop over it, and the scan breaks on it. One definition, so the two cannot
drift apart, and an unclassified operator fails safe in both.

**A literal may be substituted where the reading position accepts an immediate.** GAZL operand classes
distinguish slots from immediates, and a literal dropped into the wrong position is rejected outright
(`SWCH #0 *3`, and "Did not expect constant int: -33" from a fuzzed program). The first implementation
banned literals outright for that reason, on the grounds that tracking an operand class per instruction
would be endless.

It is not endless, because the question is much narrower than a full operand-class model: for each meta
operator, WHICH SOURCE POSITIONS take a `#const`? That is the `INLINE_IMM_OK` table - a positive list in
the same spirit as `INLINE_PURE`, so an operator that is not listed simply materialises, which is always
correct.

An entry also carries a leading `d` when position 0 is the DESTINATION, which every operator has except
the COMPARISONS: a comparison branches rather than producing a value, so its operands 0 and 1 are both
SOURCES (the label is operand 2) and both take a `#const` - the instruction set lists all four
combinations for `LSSi` / `GEQi` / `EQUi` and their float and pointer forms.

`inlineFoldWidth` reads that marker off the same table, and one derivation then serves both the constant
fold and the straight-line test, so the two can never disagree about what "produces a value" means. The
marker is written out rather than inferred from an absent `0` on purpose: the failure direction matters.
Forgetting `d` on a new operator makes it read as "calculates nothing", which is conservative; inferring
writer-ness from absence would make a mistyped branch read as straight-line and let constant propagation
run across it, silently and with no diagnostic.

Comparisons are worth more than they look, because bounds and guards are the classic inline arguments. In
`clamp(v, lo, hi)` both bounds are only ever compared and then moved, so both substitute; the same goes
for the loop bound in a `sumTo(n)` and the condition of an `assert`. When BOTH operands end up constant,
GAZL resolves the branch at assemble time into a `GOTO` or a `NOOP` (the `YIELDS_GOTO` flag in
`GAZL.cpp`), so it costs nothing at run time either - a three-call `clamp` went from 38 to 28 code words.

The decision is per PARAMETER and is made once at capture (`takesImmediate`): a parameter may carry
a literal only if EVERY place the body reads it accepts one. One disqualifying use - a `SWCH` value, an
array index, a POKE address - spoils the whole parameter, because materialising it for that one use
would emit the MOV anyway.

Opacity does not gate this. The transparency argument exists to stop the body observing a write to the
argument's STORAGE; a literal has no storage, and nothing in a body writes the caller's argument window
(the body's own temporaries are a separately borrowed run). So a literal is substituted even into an
opaque body.

Measured on the fixtures: `inlineEquivalence` 154 -> 133 instructions (-14%), `inlineFunctions` 33 -> 29
(-12%). The earlier estimate that literals were worth only ~6% understated it.

**Substituted literals then STACK, and the intermediate must not cost a MOV.** Once both arguments of
`z = x * y + x` are literals the multiply is compile-time, but writing it as `MULf %4 #15.0 #3.0` still
costs a run-time instruction: the assembler computes the value and emits a `MOVE` to park it in the
transient (`GAZL.cpp`, "Const calc -> MOVE"). The result is known, so it belongs in a `<X>` compile-time
scratch instead, exactly like a struct offset fold:

        MULf %4 #15.0 #3.0        ->    ! MULf <A> #15.0 #3.0     (assemble time, no run-time cost)
        ADDf %1 %4 #15.0                ADDf %1 <A> #15.0

Two run-time instructions become one. `expandInline` therefore propagates constants through the body:
an all-constant calculation is emitted as `<> op` into a borrowed `<X>` and the destination is recorded
in `constMap`, so later reads take the scratch; a move of a constant propagates with no instruction at
all. Three conditions keep it sound:

- **The body must be straight-line** (`straightLine`, computed at capture): every instruction a move or
  a calculation. Propagation assumes the previous write happened, which a branch can skip and a label
  can join around. Rather than model control flow, bodies containing any branch, label, call or memory
  op simply do not fold - and pure arithmetic helpers are the ones worth folding anyway.
- **Every READ of the folded value must accept an immediate**, by the same `INLINE_IMM_OK` table. The
  destination position is skipped: that is the write being replaced.
- **The return slot is never folded.** The caller reads `%base`, so the last instruction stays run-time.

**Globals always materialise, and this is not a safety compromise - the instruction set forces it.**
GAZL arithmetic has no memory operands: `MULi %0 &r #3` is rejected with *"Did not expect address:
&r"*. A global's value must pass through a `PEEK` into a slot, so there is no operand to substitute -
only a choice of where the `PEEK` lives. Leaving it at the call site costs one `PEEK` and matches the
call's ordering. Moving it into the body costs one `PEEK` if the parameter is used once (no gain), N
`PEEK`s if used N times (a loss), and is precisely the move that introduces the aliasing bug. So the
safe choice and the optimal choice coincide.

The **return value** always goes to a borrowed transient, never straight to the destination local.
That avoids the `q = f(q)` clobber, and it is free: the call already emitted `MOVi $q %0`.


## 6. Expansion

For each expansion, with a fresh sequence number N:

| callee operand | becomes |
|----------------|---------|
| `$param`       | the substituted operand, or the materialised transient |
| `$r`           | a borrowed transient |
| `%k`           | a freshly borrowed transient (per distinct k) |
| `@.label`      | `@.label_N` - **mandatory**; `labelCounter` resets per function so `.f0`/`.e1` collide |
| `&g`, `#imm`   | unchanged |
| trailing `RETU`| dropped |

Records are pushed through the existing `emit`/`emitMeta`, so `flushMetaCode` renders them and
`processBranches` sees the labels. No new emission path.


**An expanded body is already SETTLED.** `processBranches` merges a comparison with its branch by
shifting operands left, destructively. The callee ran it before capture, so the caller must not run it
again - records an expansion contributes are flagged and skipped. Only a non-inverted comparison is
exposed (an inverted one becomes `!<` and no longer matches), which is why `if` survived and `assert`
came out with its operands shifted off the end.

**Callee transients move as one contiguous BLOCK.** The callee numbers its temporaries from 0; the
expansion shifts the whole range to a fresh base. Remapping them individually breaks any window inside
the body - `CALL &helper %0 *2` needs `%0` and `%1` adjacent and in order, and per-slot borrowing
scattered them to `%4` and `%3`, so the callee read garbage.

**A switch case label is `<base>#<k>`**, and SWCH finds its arms by appending `#k` to its own target.
The per-expansion tag therefore goes BEFORE the `#`: `.s0#0` becomes `.s0_i0#0`, not `.s0#0_i0`.

**Some operand positions reject an immediate.** `SWCH` needs a value operand, so substituting a literal
argument would emit `SWCH #0 *3`, which the assembler rejects. This needs no per-position bookkeeping:
the blanket "only a slot may be substituted" rule above already keeps every literal in its window slot.

## 7. Diagnostics

| case | why |
|------|-----|
| `inline` function calls itself | would expand forever |
| `&f`, or assigning `f` to a funcptr / `functype` | no symbol exists to take the address of |
| `export inline function f` | nothing to export; a caller needs the source, which is what `import` is for |
| `extern inline` | meaningless; the grammar does not accept the pair at all, so this is a plain syntax error |
| calling an `inline` function before its definition | the body is not captured yet; `extern function f` gives no body either |
| forward-declaring an `inline` function (`extern function f`) | promises a symbol it never emits, and a call before the definition would lower to `CALL &f` |

Mutual recursion needs no check: Impala is define-before-use, so `A` can only inline `B` if `B` was
already complete, and `B` cannot have inlined `A`.


## 8. Locals are real frame locals (GAZL 2)

A callee's declared locals become REAL named locals of the caller, bracketed by `SCOP` / `ENDS`. The
assembler owns their placement, exactly as it does for an ordinary local - which is the point: it
resolves `*.z.Struct` and `*.x.f.name` before assigning offsets, so a host-owned size lands correctly,
where a compile-time slot count could only ever have been Impala's guess at it.

Sibling expansions overlay, so a function's frame cost is its LARGEST expansion, not the sum. That is
sound because an expansion's locals are dead once its body ends: the result leaves through the call
window, never through a local.

Two things had to exist first. **`SCOP` / `ENDS`** (GAZL 2, `docs/InstructionSet.md`) so the frame is a
max over nesting chains rather than a sum. And a **buffered function head**, because GAZL wants every
declaration before the first instruction while an expansion happens mid-body - so head lines accumulate
in `headSink` and are replayed once the body, and therefore every expansion in it, is known.

The callee's own temporaries still move as transients; only DECLARED locals become frame locals. A
CALL window's base must be a transient, so that block cannot be anything else.

### What an expansion emits for a local

Nothing but a declaration line repeating the size operand verbatim:

    SCOP
    $buf_i0:	LOCA *.x.sum3.buf
    $i_i0:	LOCi
    ENDS

Both size forms are SYMBOLS the assembler resolves - `*.z.Struct` for a struct local, `*.x.f.name` for
an array (see `docs/SymbolNamespace.md`). That is what makes the expansion trivial. An extent computed
by folding (`t[H * N]`, or `count * .z.Ext` for an extern struct) lives in a recycled `<X>` scratch that
belongs to wherever it was folded, so it can NOT be repeated at an expansion site - naming it once, at
the callee's declaration, is what lets every site refer to it.

This is why there is no longer an E433. The extent never has to be a number Impala knows.

**Array element access.** GAZL has no `%N:offset` - `MOVi %10:0 #5` is rejected with *"Invalid number:
:0"*. So a constant element index is FOLDED into the slot number (`$buf:2` with base `%5` becomes `%7`),
while a dynamic index uses `SETL`/`GETL` with a transient base, which the assembler accepts (`SETL %10
$i #99` and `GETL %1 %10 $i` both assemble).

**Struct fields** are reached by a SYMBOLIC offset (`.o.S.f`) that only the assembler resolves, so it
cannot fold into a slot number. It does not have to: **`%<X>` is legal GAZL**, so the base and the field
offset are folded together at ASSEMBLE time and the transient is indexed symbolically:

    ! ADDi <A> #6 #.o.P.x        ; base slot 6 plus the field offset, resolved at assembly
    MOVi %<A> #1                 ; a symbolically indexed transient

Every fold is a `!` directive, so this costs nothing at run time. Struct locals and arrays of structs
both work this way.

**A `returnBack` ordering trap.** `%<A>` starts with `%` but is NOT a slot number. `returnBack` used to
test the bare-token case first, so it pushed the whole compound string into the transient stock - losing
the `<A>` scratch and poisoning the pool with a token no allocator can hand out. The compound
trailing-`<X>` case must be tested BEFORE the bare-token case.

**No `--inline` flag.** Nothing is inlined unless the source says `inline`, and no existing source
does, so the 84-file byte-diff gate is unaffected and there is nothing to gate.


## 9. Coverage - what this does not reach

Only code that goes through Impala benefits. `vortex` (100 call sites), `reciter` (46) and `js80rmx`
(22) have no Impala source on this branch, and the firmware host wrapper is generated GAZL text
(`permut8Host.js`), so its per-frame `driver->process()` calls are invisible here. Those need the
assembler-level pass described in `docs/InliningInvestigation.md`; the two compose rather than compete.


## 10. Testing

- A fixture using `inline` with an `Expected (GAZLCmd ...)` header line, so the assemble-and-run gate
  covers it rather than only byte-comparing.
- An **equivalence pair**: the same program written with and without `inline`, asserted to print
  identical output. This is the real behavioural oracle, and it must include the aliasing case (a
  helper that writes the same global it is passed).
- The rejection cases from section 7, table-driven through `expectCompileOutcome`.
- `fuzzImpala.js --vm` already compiles and runs generated programs; generating the with/without pair
  form gives a differential oracle equivalent to the `--inline` toggle an assembler pass would use.
  **That oracle only works if the generated program PRINTS something.** `renderFunc` ends `main` with a
  `printInt` of every reachable int local, array element and global for exactly this reason. Without it
  both builds emit nothing, every comparison is `"" === ""`, and the oracle silently passes everything -
  which is how it shipped at first, and it caught nothing until the dump was added.
