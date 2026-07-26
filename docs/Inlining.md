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
- Imported `inline` functions work, because an import closure concatenates the defining unit first.

Rejected, each with its own diagnostic (section 6): recursion, taking the address, `export`, `extern`,
forward declaration, and - in v1 - declaring `locals`.


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
        if argOperand starts with '#':  return SUBSTITUTE   // literal or named constant
        if argOperand starts with '%':  return SUBSTITUTE   // fresh scratch, private to this call
        if argOperand starts with '&':  return MATERIALISE  // a global (see below)
        return opaque ? MATERIALISE : SUBSTITUTE            // a caller local

`MATERIALISE` emits exactly the instruction the call already emitted (`MOVi %t $x` or `PEEK %t &g`)
into a borrowed transient and uses that. It therefore never costs more than not inlining at all.

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


## 7. Diagnostics

| case | why |
|------|-----|
| `inline` function calls itself | would expand forever |
| `&f`, or assigning `f` to a funcptr / `functype` | no symbol exists to take the address of |
| `export inline function f` | nothing to export; a caller needs the source, which is what `import` is for |
| `extern inline` | meaningless |
| calling an `inline` function before its definition | the body is not captured yet; `extern function f` gives no body either |
| `inline` function declaring `locals` | v1 restriction, see below |

Mutual recursion needs no check: Impala is define-before-use, so `A` can only inline `B` if `B` was
already complete, and `B` cannot have inlined `A`.


## 8. v1 restrictions

**No `locals` in an inline function.** Frame declarations are emitted at the function head, before the
body separator; there is no way to add one mid-body at an expansion site. Allowing locals would mean
either re-declaring them in the caller's frame (wrong emission point) or mapping scalars onto borrowed
transients and frame objects onto appended slots with a growth bound.

This costs little: Impala already routes expression temporaries through transients, so
`r = a * 3 + b` needs no declared local. A helper that genuinely needs one is a bigger helper and a
weaker inlining candidate. Lifting this is the obvious v2 step.

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
