# Extern prototypes / the extern linking model (design note)

Status: **"allow + validate" IMPLEMENTED** (the recommendation below). `extern native f(int a) returns int`
is accepted - the return name is optional, and was briefly required for no reason: only its TYPE is ever
read, so `returns int q` made the author mint an identifier that reaches neither the row nor any check,
while the sibling `functype f() returns V` had never asked for one. Parameter names stay required; they
are printed in the row. Calls against a prototyped extern are argument-count and type-checked and get a
real result type; the emitted row carries real types (`extern native printInt(int n) -> void`). Name-only externs are
unchanged and still assert nothing (`() -> unknown`, a wildcard the validator skips), so prototypes are
ALLOWED, never demanded. Fixture: `tests/impala/sources/externPrototype.impala`.

A prototype and a definition of the same name are two claims about one function, so where the compiler
holds BOTH it now checks them against each other (**E437**) instead of letting whichever parsed last
overwrite the other - which used to compile clean and emit contradictory `; signature` rows for
gazl-validate to catch. This is not a linkage rule: a prototype for a name the closure never defines is
still a promise only gazl-validate can settle, and a name-only extern still asserts nothing. It matters
most under `import`, where the builder compiles the whole closure as one unit, so a stale hand-written
prototype and the real definition routinely land in the same compilation.

Not done: the two nudge WARNINGS (name-only-but-verifiable, prototyped-but-unverifiable), and making the
native manifest authoritative and complete (step 2 of the sequencing below) - so a prototype for an opaque
host native is still a trusted claim rather than a checked one.

Superseded: the multi-return trigger below. Multi-value returns are PARKED for Impala 3.0
(see `design/ParkedFeatures.md`), so a prototype declares at most one return (`E428` otherwise) and extra
results come back through pointer out-parameters. The remaining motivation - argument type-checking for
extern calls - is what got implemented.

Related: an `extern struct` now emits `; signature extern struct Name { field : type, ... }` and
gazl-validate checks it against the layout constants a host supplies (`.o.Name.field` / `.z.Name`).

Be precise about what that check is, because the name invites over-trust. The scanner records **field
names only** - it reads the `.o.Name.field:` labels and checks that `.z.Name` exists. It never reads the
`! DEFi` values and stores no types. So it fails the build on a **renamed or dropped field**, a **missing
`.z.` size**, two `extern struct` declarations that **disagree with each other**, or a declaration that
disagrees with a real `struct` definition in the scanned set. It does **not** catch a host layout that
reorders fields, moves them to overlapping offsets, retypes them, or adds fields the interface never
declared - all of those pass clean. And in this repo's actual host workflow the constants arrive as
GAZLCmd command-line arguments at load, which gazl-validate never sees at all.

Layout drift is caught by the fact that offsets are symbolic, not by this linter. The full cross-check of
host layout against declared interface is still deferred - see
[`StructLayoutConstants.md`](StructLayoutConstants.md).

## One rule for every kind of extern

> **At most one DEFINITION of a name; any number of `extern` declarations, provided every claim agrees
> - with the definition where the closure has one, and otherwise with each other. A declaration that
> asserts nothing never collides with anything.** Order never matters.

That is the whole model, and it now holds uniformly:

| Kind | Opaque form (asserts nothing) | Claim form | Mismatch | Two definitions |
|---|---|---|---|---|
| function | `extern function f;` | `extern function f(int a) returns int r` | **E437** | E401 |
| struct | `extern struct S` (bodyless) | `extern struct S { int a }` | **E438** | E410 |
| global | *(none - a type is always stated)* | `extern int g` | E402 | E401 |
| array | `extern array a` (untyped) | `extern int array a` | E203 | E401 |
| functype | the built-in `funcptr` type | the `functype` declaration itself | **E440** | *(re-declaring is legal if it matches)* |

Notes on the corners:

- The mismatch checks fire **declaration-against-declaration too**, not only against a definition -
  with nothing to arbitrate, two disagreeing claims are both suspect, and the compiler generates calls
  and field offsets from whichever it happened to keep. Message says so: *"extern declarations of f
  disagree"* rather than blaming a definition that does not exist.
- Where a definition IS present it is authoritative: it wins, it keeps ownership of the emitted struct
  layout, and a re-declaration publishes no second `; signature` row.
- An extern array field states no extent by design, so extents are never compared - the same wildcard
  model as a name-only prototype. An untyped `array` element type is likewise opaque. (`E430` is
  specifically the `extern struct` array *field* case; a standalone `extern int array aa[4]` is a plain
  `E001` syntax error, not E430.)
- Globals have no opaque form because `extern g` cannot be written without a type; that is a gap only
  in the sense that there is nothing to be opaque *about*.
- `functype` has no extern form, and needs none. A functype **emits nothing** - no symbol, no layout,
  not even a `; signature` row - so there is no artifact for a second declaration to collide with, and
  the declaration simply repeats as long as the shapes match. That is the difference from a `struct`,
  whose definition owns real `.o.`/`.z.` constants and so must be unique with any second mention
  spelled `extern`. Emitting nothing also means gazl-validate never sees a functype, so the compiler
  is the *only* place a disagreement can be caught.
- Nothing *forces* a functype on you: the untyped `funcptr` is its opaque form and is accepted
  everywhere a named one is - parameter, struct field, `extern struct` field, `extern` prototype. But
  it is opaque in the same direction a bare `pointer` is: a named type widens to `funcptr` freely,
  and going the other way needs an explicit `(Cb)` cast (**E441**, for assignments and arguments
  alike), because the named type exists to guarantee the shape of what gets called and an untyped
  source guarantees nothing. A functype takes no `pointer` modifier in a cast, being a pointer
  already; `(Cb pointer)` casts to a pointer *to* one. Consequence to keep in mind for `.gazl` blob
  imports: a blob can never carry a functype, so a source importing one and wanting the typed form has
  to declare it locally - which is exactly what a repeatable declaration allows.

This matters under `import` for the reason E437 and E438 both exist: the builder compiles the whole
closure as one unit, so a hand-copied `extern` and the real definition land in the same compilation.

The original note follows, as the design record.

## The trigger

There is no way today to call an extern that returns multiple values - but not for the reason this
section used to give. Re-verified 2026-08-01:

    extern native foo                              // OK - name-only, asserts nothing
    extern native foo(int n) returns int q         // OK - a prototype, and it IS checked
    extern native foo(int n) returns int q, int r  // error[E428]: multiple return values

So a prototype is accepted (that is what the rest of this document is about); it is the SECOND return
value that is refused, by the same `E428` an Impala-defined function gets. And on the call side,
`a, b = foo(x)` fails with **`E429`** ("Destructuring assignment is not supported in Impala 2.0") for
every callee, extern or not - multi-return and destructuring are both parked for 3.0.

(Corrected: this paragraph claimed a prototype was `E001`, that destructuring worked for
Impala-defined functions, and that the call failed with `E432` "the right side is not a multi-value
function call". `E432` was retired with `inline function`, so that message no longer exists; the CODE was
re-allocated on 2026-08-05 to the host-owned-array rank rule.)

A **name-only** extern still asserts nothing, so it gets no argument type-checking - that is the
wildcard case the validator skips, and it is why a prototype is worth writing.

The working pattern today is out-parameters (pointers the native writes through), which needs no
special support:

    extern native divmod                           // native writes through *q and *r
    divmod(7, 3, &q, &r);

## The 1.0 rationale (why externs were kept signature-less)

A hand-written prototype is an UNCHECKED ASSERTION: the compiler trusts it and generates code from it,
so if it drifts from the real native it becomes a silent lie the toolchain cannot catch. Name-only
externs assert nothing, so they cannot be wrong about the extern's shape - at the cost of no
type-checking and no multi-return. This was a deliberate choice to avoid "drift and lies".

## What changed: prototypes can now be CHECKED, not just trusted

Two mechanisms added in Impala 2.0 change the calculus:

- Imports (Step 5): source-as-interface, one source of truth. Importing a module yields its REAL
  signature - nothing to re-declare, nothing to drift. The cross-unit signature check already fires on
  disagreement (E203 "element type mismatch with previous declaration").
- The gazl-validator pass over `.gazl` signature metadata, plus an authoritative place for native
  signatures (see `design/proofs/nativeCallbackSignatures.gazl`).

A prototype stops being a lie exactly when the toolchain has an authoritative signature to check it
against. If the validator cross-checks a declared extern prototype against the linked GAZL definition
or the native-signature manifest, a drift becomes a BUILD FAILURE, not a runtime surprise.

## Two linking cases (they differ)

- Impala <-> Impala: imports already solve it. Import the source, get the true signature. You do NOT
  want an extern prototype here - you want an import. No prototype, no drift.
- Host natives (C / JS / host): still need `extern`. This is the only place the prototype question
  really lives, and also where verification is hardest, because there is no Impala source to import.
  It is verifiable ONLY if native signatures live in an authoritative manifest the validator checks.

## Recommendation (current thinking)

- ALLOW full extern prototypes: `extern native divmod(int a, int b) returns int q, int r`. Name-only
  stays valid. Immediately enables multi-return externs and extern-argument type-checking. Wire the
  validator to cross-check a declared prototype against the definition / manifest where one exists, so
  the prototype is verified rather than trusted.
- DO NOT demand universally yet. A prototype for a native with no authoritative signature anywhere is
  still an unverifiable claim; mandating it re-introduces the exact drift 1.0 avoided, now compulsory.
  Keep name-only as the "I will not assert a shape I cannot verify" mode.
- Safe sequencing: (1) allow + validate, (2) make the native-signature manifest authoritative and
  complete, (3) THEN consider demanding - at that point every prototype is checkable, so requiring them
  adds safety without adding lies.
- Middle ground the validator can enforce meanwhile: WARN on a name-only extern that DOES have a
  verifiable counterpart (type-safety left on the table), and WARN on a prototyped extern with nothing
  to check against (an unverifiable assertion). This nudges toward prototypes exactly where they are
  safe.

## Implementation sketch (for if/when we do "allow + validate")

- Grammar: extend `ExternDecl` with an optional parameter list and `returns` clause, reusing the same
  `ArgsDecl` / return-list machinery a normal `function` declaration uses, and storing it as the
  extern's `signature` (params, returnList, returnWords).
- Calls: with a signature present, extern calls type-check arguments (like within-unit calls) and set
  up the multi-word return window, so `q, r = divmod(7, 3)` works through the existing multi-return path.
- Validation: teach the gazl-validator to compare a declared extern signature against the linked
  definition's signature metadata (and/or the native manifest); mismatch is an error. Emit the two
  warnings above for the unverifiable / left-on-the-table cases.
- Backward compat: name-only externs keep working unchanged (untyped, single-word/void return).

## Open questions / things to find

- Where is the single authoritative source for HOST-native signatures? Is `nativeCallbackSignatures.gazl`
  it, and should Impala IMPORT it rather than re-declare (to honor "one source of truth")?
- Should a prototyped extern that IS importable just be an import instead - i.e. do we even want extern
  prototypes for anything the toolchain can see, or only for opaque host natives?
- What does the validator do when there is genuinely nothing to check against (pure runtime-registered
  native)? Warn only, or provide a way to mark "trust me, unverifiable" explicitly?
- Does GAZL's multi-word return ABI for natives match the Impala multi-return window exactly, or is
  there a calling-convention gap to close before multi-return externs can work?
  **Answered 2026-09-16: there is a gap** - see "Arbitrary call windows" at the end of this document.
- Interaction with the deferred implicit-decay change (`--legacy`): extern prototypes with array/pointer
  params inherit the same "value vs pointer" rules; confirm they compose.

## Arbitrary call windows (answers the multi-word-ABI open question above)

Status: **ANALYSIS AND PROPOSAL - NOTHING DECIDED.** Not whether to do it, not the spelling, and not
which Impala version it would target. Nothing is implemented or scheduled. The analysis below finds it
COULD land additively in Impala 2.0 - no engine change, no host change, no version bump - which bears on
`design/ParkedFeatures.md`'s decision to defer the related item to 3.0. **That deferral stands until
someone changes it**; see "What the 3.0 deferral rests on".

### The framing: the reserved slot was never load-bearing

Impala reserves window slot 0 for a return even when there is none (`CALL ^abort %0 *1`). It is tempting
to read that as a safety margin - a word a callee may scribble without touching an argument. It is not.
The actual guarantees are elsewhere, and none of them involve it:

- a GAZL callee re-checks its own frame in its `FUNC` prologue;
- a native declares the word count it wants to `accessParams`, which is where that bound is enforced;
- `GETL` / `SETL` bounds-check a dynamic offset against the end of the data stack;
- **neither engine reads `*size` at run time** (below).

`ParkedFeatures.md` says what it actually is: "a leftover from Impala 1.0 not requiring function
prototypes - the caller could not know a callee's output arity, so it always reserved one slot."

And the feeling of security it gives is already scheduled for demolition. The 3.0 wishlist commits to "0
to n outputs", and a by-value struct return "**is** a multiple return whose OUT slots carry names and
offsets - the same window layout". Once the leading region can be K words, "slot 0 is the return" is not
an invariant but a coincidence of K=1, and K=0 is not an exception to a rule - it is the smallest K. With
by-value aggregates the leading region is not even countable without knowing the types.

**The cost that is real, and should be accepted deliberately rather than discovered:** once windows vary,
reading a `.gazl` by eye - or with a tool - no longer tells you where the arguments begin without
consulting the prototype. That is lost to by-value returns whatever is decided here.

### The finding

GAZL imposes NO order on a window. `src/GAZL.cpp`'s `case LOCA____` handles LOCA, PARA, LOCi/f/p,
INPi/f/p and OUTi/f/p in one branch, bumping `localsSize` in declaration order; the rows differ only in
their type flags (`INPi` is `VAR_INT_R & ~TRANSIENT`, `OUTi` is `(VAR_INT_R | VAR_INT_W) & ~TRANSIENT`).
Read-only versus read-write, never position. `INP` declared before `OUT` assembles and runs. So a GAZL
window is an arbitrary sequence of slots with write permission per slot, and **outs-first is Impala's own
convention, not the machine's rule** - a misattribution `docs/impala/Impala2.md` carried until 2026-09-16
("The calling convention", now corrected).

Those rows sit OUTSIDE every `#if GAZL_2` block in the opcode table (blocks at 362-364, 388-390, 530-534,
562-565; rows at 426-429, 450, 509-513). An arbitrary window is plain **GAZL 1**: Permut8's shipped
engine executes one today. Unlike `inline`, nothing here needs a GAZL 2 engine.

### Full parity: four gaps, not one

This section closes the first. The others are recorded so the scope is not misread.

| what a GAZL window can do | Impala today | mechanism | cost |
|---|---|---|---|
| **any order** of read/write slots | outs-first only | this section | cheap |
| **caller-chosen arity** (native callee) | fixed by prototype | a prototype tail | small |
| **caller-chosen arity** (Impala callee) | - | `GETL`/`SETL` + a typing rule | small, one open question |
| **multi-word entries** (`PARA *N` as one parameter) | one word per parameter | the parked transient allocator | real - stays 3.0 |

### The rule

> **A prototype describes its window. `=T name` is an output slot. `returns T` is sugar for a leading
> `=T`. A prototype that does not say where its first parameter sits keeps the 1.0 default.**

The open question is how a prototype says that. Five spellings were considered; **`%N` is the
recommendation**, not yet a decision:

```impala
extern native MonoOutputN(%0 int i, int n, pointer s)     // [i, n, s]
extern native printInt(%1 int value)                      // [unused, value] - the 1.0 window, stated
extern native printInt(int value)                         // same, by omission
extern function addone(%0 int x, =int y)                  // [x, y] - interleaved
extern native sqrt(float x) returns float r               // [r, x] - identical either way
extern native trace                                       // name-only: 1.0 default, forever
```

Why `%N`:

- It states the thing directly - *the first parameter lives at window slot N* - rather than negating a
  default.
- Prefix `%` is unambiguous: `%` is infix MOD in Impala and needs a left operand, and a parameter must
  otherwise begin with a type.
- It is not borrowed from GAZL; `%N` is already the COMPILER's own internal name for a transient slot
  (`impala/impala.jspeg` mints them with `'%' + counters['%']++`).
- One notation covers both polarities, so "the exception" stops being a category: `%0` and `%1` are
  equally explicit and neither is default-by-silence.
- **It mitigates the main danger** (below): the migration hazard is a prototype and its C native
  disagreeing about slot indices, and `%N` puts the index in the prototype next to the first parameter,
  so reviewing `natives.impala` against `GAZLCmd.cpp` is a literal side-by-side - `%1` against
  `params[1]`.

Against it: it leaks the machine into the language, and a newcomer meets a slot number before a type.

Call sites mark written slots with the same sigil used in the prototype:

```impala
addone(41, =result)
addone(41, =arr[i])
addone(41, =_)                // discard
printInt(42)                  // declared (%1 int value) -> CALL ^printInt %0 *2, 42 at %1
```

`returns T` remains the only form usable in **expression position** (`z = sqrt(x) * 2`); an interleaved
window has no single value, so a prototype carrying any `=` is statement-only at the call site. `returns`
and `=` may not be combined.

### Spellings considered

| spelling | says | note |
|---|---|---|
| `(%0 int i, ...)` | where the first parameter sits | **recommended**; see above |
| `... returns nothing` | there is no leading output | reads as English; `nothing` is 48 corpus hits, all prose; a negative clause |
| `... returns ()` | the empty return list | reserves no word at all, but implies a list whose non-empty members are parked multi-return |
| `f=(int i, ...)` | this list IS the window | no new word, but `name=(` reads like assignment |
| `(=, int value)` | a reserved slot nobody names | inverts the polarity: mark the legacy, not the exception; needs the name-only rule to protect shipped firmware |

Rejected outright:

- **A `window` selector keyword**, or **marking every parameter `input`/`output`**: verbose, and the words
  collide at the call site (`output` has 620 corpus hits including an `extern native output` in
  `tests/impala/sources/`).
- **A distinct bracket** (`f{a, b}`): `Block` is already a `Statement` alternative
  (`impala/impala.jspeg`, `Statement <- ... / Block`), so `addone{41, =result}` collides with "statement,
  then block"; and the `Statement` rule's metadata scanner does `find($$s, "{;\r\n", $$i)`, treating `{`
  as the end of a statement's text.
- **A dual-ABI accessor in the engine**, or an ABI marker in the `.gazl` text keyed off the `GAZL #2`
  bracket. Both put the convention in the machine rather than the prototype, and a text marker would be
  irreversible once user-authored `.gazl` files exist.

`=` is chosen for output slots because it **cannot start an expression** - prefix `=` is a parse error in
1.0 and 2.0, the same "previously rejected syntactic space" argument the parked Step 4 used for
`returns a, b`.

### What the 3.0 deferral rests on

`ParkedFeatures.md` bundles "Remove the mandatory reserved return transient" with by-value structs,
multi-return and destructuring because "they are all changes to the same calling convention" and "it is
kept deliberately for now rather than churn the ABI twice." A prototype-described window would not churn
the HOST ABI at all - an old host keeps reading `params[1]` because its prototype says `%1` - so that
particular argument would not apply to this item. Whether the item still belongs in the 3.0 pass for
other reasons is an open decision, not one this section makes.

| declarations | count | effect |
|---|---|---|
| name-only externs (1.0 style) | 356 | none - keep the 1.0 default |
| prototyped externs WITH `returns` | 21 | none - same window either way |
| prototyped VOID externs | 188 | state `%0` or `%1`; all in-repo (`natives.impala`, `ImpalaDemo`, `output/audit*`, fixtures) |

Nothing has shipped with Impala 2, so no `.impala` source outside this repo carries a prototyped void
extern. Every shipped Permut8 firmware uses the 1.0 name-only form. The other ABI surface - the host
calling INTO firmware - is `process()`, `reset()` and `update()`, all no-argument and void, with zero
entry points taking arguments or returning values. So the change is one commit in this repo.

### Dangers

- **The serious one: a prototype and its native silently disagreeing about slot indices.** Today a void
  native's first argument is at `params[1]` and there is no way to get it wrong. Under this proposal the
  prototype decides, and nothing checks it - `; signature` rows carry names and types, and
  `gazl-validate` checks field names, not slot maps. A wrong index shifts every argument with no trap.
  Across 188 declarations migrated at once, this is the risk. **Mitigation: a gate, not a review** -
  compile the corpus both ways and diff behaviour, and make the migration mechanical. The `%N` spelling
  exists partly to make the mismatch reviewable.
- **A hazard that on inspection is not one.** A native writing an output it did not declare was expected
  to clobber a live value instead of the dead reserved slot. It does not: the caller picks a window base
  it knows is free (which is why `fib` slides to `%1` when `%0` still holds a result), so slot 0 of the
  window is a free transient under both conventions. Such a native corrupts its own first argument - a
  local, visible bug, not caller corruption.
- **Frame-requirement diagnostics weaken marginally.** `*size` makes the caller's frame requirement cover
  the window so exhaustion is reported at entry rather than deeper; one word less per void call means
  fractionally later detection. Not correctness - the callee re-checks its frame.
- **`accessParams`' bounds check cannot fail on an empty window** (`dsp + 0 <= dataStackEnd`). Only
  reachable if a native's declared count disagrees with how it indexes - the first hazard again, not an
  independent one.
- **Not at risk: memory safety.** Every hazard here is "wrong data, silently", never out of bounds.

### Arity (varargs)

GAZL has no varargs feature - the window is untyped words, `*size` is a caller-side literal the engine
never reads, and a native gets whatever it asks `accessParams` for. So variadic is a CONVENTION, and the
key consequence is that **a variadic call site still has a statically known arity**: `trace(fmt, a, b)`
emits a 3-word window, `trace(fmt, a)` a 2-word one. Nothing dynamic happens at run time.

- **Native callee:** nothing is needed beyond a prototype tail (`extern native trace(%0 pointer fmt, ...)`)
  so the call site emits the right number of words. The C side already indexes via `accessParams`.
- **Impala callee:** `GETL` / `SETL` is the primitive and fits exactly - "Get local variable `var` (any
  type) with offset `int`", where the offset is a RUNTIME value, bounds-checked against the end of the
  data stack. So `varargs[i]` lowers to `GETL $t $varargs $i`, with no pointer and no static maximum.
  `ADRL` would NOT do: its size operand is `CONST_INT_P`, so a pointer-based `va_list` would force a
  declared maximum tail.
- **The count must be explicit** (an `int n` parameter, or a format string). The compiler could inject an
  arity word, but that changes the window shape, and for a native the host owns the layout.
- **Open question:** `GETL` is `ANY_VAR_W` - the tail is untyped words, and Impala 2 is typed, so
  `varargs[i]` has no static type. Typed accessors, an explicit cast, or a rule that the tail is
  `int`-shaped. This is the one part that does not fall out of existing machinery.
- Reading past what the caller passed yields the callee's own uninitialised locals - wrong data, never
  out of bounds.

### Width: by-value aggregates (stays parked)

`case LOCA____` records a SIZE per declaration, and `PARA *N` names N words as one entry - which is
exactly what the parked work used ("struct-sized `PARA *N`"). So a by-value struct or array parameter is
a GAZL window entry Impala cannot name. Unlike ordering, this genuinely needs the transient allocator
(`copyStructArg`, `freeStructWindow`, `winBase`/`winWords`, the return-window `ADRL`), so the parking
rationale holds precisely here and this proposal does not touch it.

They **compose rather than compete**: if by-value aggregates return, `=Struct name` is the natural
spelling for a by-value struct output at an arbitrary position, which neither feature gives alone.

Related: **`=` delivers the multi-return CAPABILITY without the machinery that got Step 4 parked.**
Multiple outputs via `=` are just more `OUT` slots in an ordinary window - the caller names lvalues, the
compiler emits `MOV`s after the call, exactly as for a single return. So restoring multi-return SYNTAX
(`returns a, b` plus `lo, hi = split(n)`) would be a parser form and a desugaring onto `=`, not a revival
of the allocator. Not proposed here; recorded so the parking rationale is not misread as covering it.

### No run-time stake in `*size`

**Neither engine reads the size operand at run time.** The interpreter does `dsp + C1.i` (`GAZL.cpp`,
`case CALL_NVC`) and the JIT does `if (window != 0) addImmQ(DSP, window * 4u)` on `p1`
(`GAZLJitX64.cpp`, `OP_CALL_NVC`); `p2` is consumed once at assembly time for the `LOCAL_BOUNDS` frame
check, where over-reserving is conservative. So `CALL ^abort %0 *1` and a bare `CALL ^abort` produce
identical native code and identical interpretation, and an empty window needs no special case. Both JIT
backends already guard `window != 0`, so a zero-base window needs no JIT change either.

### Unknowns before implementation

- **`impala/impala.jspeg` has been read only around `Statement`, `Block`, the transient-slot minting and
  the rule index.** Whether `%N` and `=T` in a parameter list, and `=` at argument position, are clean PEG
  additions or a restructure of the argument/parameter productions is unverified - the main cost driver.
- **`functype` and indirect calls.** If hand-written GAZL hands Impala a function pointer, the slot map
  has to ride the TYPE, not just the extern declaration, or the gap reopens at every indirect call site.
- **In-out slots.** GAZL's `OUT` is read AND write, so a slot can legitimately be in-out. One sigil
  cannot say whether the caller must initialise. Decide deliberately.
- **`; signature` rows** must carry the slot map, not just a return type, or cross-unit checking silently
  degrades to assuming outs-first - and that is what would catch the serious danger above.
- **`main` and `PARA *1`.** The wishlist notes `main` still declares `PARA *1`; confirm what a
  prototype-described `main` should emit.
- **The typing rule for a variadic tail** (above).

### Provenance

Raised by the AudioClay agent (`audioclay-4a`) on 2026-09-16 while scoping an ACL -> Impala emitter.
From its `aclc` native table: of 31 host natives, **14 have zero scalar outputs**, 14 have several, and
only 3 are expressible in Impala 2.0 today - and those 3 fit by accident. 779 of 2091 corpus modules
reach at least one host native. Its first message claimed GAZL mandates outs-first; its second retracted
that with the source reading confirmed above.

**Corrected by that agent 2026-09-16, and it narrows the motivation** - recorded so this section is not
read as answering a need it does not: AudioClay'''s own native window is [scalar outs..., instance index,
buffers..., inputs...], which IS outs-first. So its 28 awkward natives are covered by outs-first plus the
zero-output case the 3.0 wishlist entry already specifies, and its interleaved-window argument was about
GAZL'''s expressiveness, not about any requirement of its own. It also confirmed it never needs a multi-word
window entry: every ACL port is one word, arrays and strings cross as pointers with a separately declared
count, and module instances are reached through a state pointer rather than passed. So the by-value
aggregate row gates nothing for that host either.

The live motivation for the interleaved case is therefore GAZL parity itself - calling a hand-written
GAZL `FUNC` whose window is not outs-first - and no shipped product is currently blocked on it.
