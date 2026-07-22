# Extern prototypes / the extern linking model (design note)

Status: DESIGN NOTE, not decided, not implemented. A place to collect the thinking on whether (and how)
extern declarations should carry full signatures. Add to it as more cases surface.

## The trigger

There is no way today to call an extern that returns multiple values. Externs are declared name-only:

    extern native foo                              // OK
    extern native foo(int n) returns int q, int r  // error[E001]: syntax error

So the compiler never learns an extern's arity. `a, b = foo(x)` fails with E432 ("the right side is
not a multi-value function call") because a name-only extern is assumed single-word (or void).
Multi-return destructuring only works for Impala-defined functions, whose `returns int q, int r`
signature tells the compiler how many output words to set up. There is also no argument type-checking
for extern calls - an extern accepts anything.

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
