# Extern prototypes / the extern linking model (design note)

Status: **"allow + validate" IMPLEMENTED** (the recommendation below). `extern native f(int a) returns int q`
is accepted; calls against a prototyped extern are argument-count and type-checked and get a real result
type; the emitted row carries real types (`extern native printInt(int n) -> void`). Name-only externs are
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
(see `docs/ParkedFeatures.md`), so a prototype declares at most one return (`E428` otherwise) and extra
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
  signatures (see `docs/nativeCallbackSignatures.gazl`).

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
- Interaction with the deferred implicit-decay change (`--legacy`): extern prototypes with array/pointer
  params inherit the same "value vs pointer" rules; confirm they compose.
