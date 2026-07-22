# Struct layout as GAZL constants (design note)

Status: DESIGN NOTE, not implemented. Idea + naming decision + open questions.

## The idea (GAZL as a macro-assembler)

Emit each struct's field offsets and its size as NAMED GAZL constants, and reference those symbols at
every field-access site instead of baking in immediate numbers (`#0`, `#2`, `#8`). Example:

    struct Voice { int note; Biquad lo; float gain }

would emit:

    .offset.Voice.note   ! DEFi #0
    .offset.Voice.lo     ! DEFi #2
    .offset.Voice.gain   ! DEFi #6
    .sizeof.Voice        ! DEFi #8

and then a field read `v->gain` lowers to `PEEK $x $v .offset.Voice.gain` rather than `PEEK $x $v #6`.

Why: it turns the struct layout into first-class assembler data with one source of truth. You can then
conditionally add / remove / reorder fields directly in GAZL (driven by an assembly-time constant) and
every access site adapts, because they all resolve through the same `.offset.*` / `.sizeof.*` symbols.
It also makes the layout inspectable and greppable, and it composes with imports (below).

## Naming (DECIDED): reserved leading tag, `.`-separated, hex-free

Scheme: `.offset.<Struct>.<field>` for a field offset, `.sizeof.<Struct>` for a struct size.

Rationale (this is the whole subtlety):
- The leading tag is MANDATORY and the struct name can NEVER be the first segment. Struct names are
  arbitrary, and a struct named `s0` / `e24` / `l3` written as `.s0.note` would collide with the
  compiler's own label namespaces (switch `.s0`, exit `.e24`, loop `.l3`). Putting a reserved tag first
  (`.offset.s0.note`) keeps it clear. This mirrors `.s` for string literals.
- `.` is the separator because it is ILLEGAL in an Impala identifier but LEGAL in a GAZL symbol
  (the compiler already emits `.s_Domain_4d2`, `.t0`, `.s0`). `_` cannot be used as the separator: it is
  a valid identifier char, so `.offset_Voice_lo` is ambiguous (struct `A_B` field `c` vs struct `A`
  field `B_c`, etc.). Only a non-identifier separator guarantees uniqueness.
- NO `_<hex>` (randomId) suffix, unlike the string symbols `.s_<derived>_<hex>`. String symbols are
  internal, content-derived, and never referenced by a predictable name, so they lean on the randomId
  for uniqueness. Layout constants are the opposite: they must be STABLE and referenced by a
  deterministic name (hand-written or imported GAZL must be able to name `.offset.Voice.lo`). Structs
  are defined once (one source of truth), so no duplicate-definition problem.

Open sub-choice: `.sizeof.<Struct>` reads clearest but visually shares `.s` with strings/switch labels
(no ACTUAL collision - different full symbols). If zero visual overlap is wanted, use `.words.<Struct>`
(GAZL counts words). Pick one when implementing.

Compiler reserves the `.offset.*` and `.sizeof.*` (or `.words.*`) prefixes; nothing in Impala emits a
leading-dot symbol (globals/consts are bare names).

## Consequence: interface vs layout decoupling

Emitting layout as GAZL constants splits two things that are currently fused:
- INTERFACE - what fields a struct has and their types. Needed at Impala COMPILE time (to type-check
  `v->note` as int / `v->lo` as Biquad, and to know `note` exists and pick `.offset.Voice.note`).
- LAYOUT - where each field sits and the struct size. With the constants, this moves to GAZL ASSEMBLE
  time.

So a unit that USES a struct needs only the interface (field names + types), not the concrete
offsets/size - those resolve from the single definition. Define the layout once; every other unit
references `.offset.Voice.lo`. This is the same "one source of truth" property as imports (import the
interface; link the layout), and it enables the macro-assembler adaptivity.

Nuance: it is "interface without layout", not "nothing". Drop the interface too and you are in untyped
territory (like a name-only extern) - you can reference `.offset.Voice.note` raw, but Impala no longer
knows the field's type or that it is valid.

## What GAZL must support (verify before building)

- A symbolic constant as an OFFSET operand: `PEEK $x $v .offset.Voice.gain`. Almost certainly fine
  (the assembler already resolves symbolic data addresses like `&.s_...`).
- Nested fields `v.lo.b0` = `.offset.Voice.lo + .offset.Biquad.b0` - a sum of two assembly-time symbols.
  Either GAZL supports a constant-expression operand (fold the sum), OR the compiler emits two `ADDp`s
  (one symbolic offset each). Both work; the first is tighter. Today this is a single baked `#total`.
- Struct arrays `a[i]` = `base + i * .sizeof.Voice` - a runtime `MULi $i .sizeof.Voice` where the
  constant is symbolic. Fine as long as a symbolic constant is a legal `MULi` operand.

So feasibility hinges on: can a GAZL operand be a symbolic constant (and ideally a small
constant-expression over symbolic constants)? Check against the assembler.

## Drift / validation

If a using unit's interface (field names + types) lives separately from the definition, they can
disagree. The gazl-validator can cross-check the interface's field types against the definition's
emitted layout, so a mismatch is a build error, not a silent lie - the same "verifiable contract" theme
as extern prototypes (see [[docs/ExternPrototypes.md]]).

## Type identity when a dimension/size is assembler-resolved (the hard part)

> RESOLVED - see docs/ArrayLengthIdentity.md. The decision below (value-fold vs single-symbol as a
> GLOBAL per-constant choice) was the WRONG framing: it either broke 1.0 (`int array a[c1 * c2]` was
> legal) or was unsound (folding an assembler-variable const). The actual rule splits by ROLE, not by
> constant: a length used AS A VALUE (allocate, index, copy-count) allows any expression (1.0-compatible);
> a length used AS A TYPE (shape comparison: copy, by-value pass/return, shaped-pointer params) requires a
> single named constant or a folded frozen value. Since 1.0 had no array-typed parameters, only pointers,
> no 1.0 code sits in a length-as-a-type position, so the requirement is non-breaking. The text below is
> kept for the reasoning about nominal vs value identity, which the resolution reuses; read it through the
> value-vs-type lens.


If a dimension or struct size is only resolved by the GAZL assembler (the macro-assembler goal), the
compiler cannot use its VALUE for type identity. Type identity must be comparable at compile time
(E201/E202 compare descriptor strings), so there are two models, per constant:

- BY VALUE - `[6]`. The compiler evaluates the dimension to an integer; `SIX`, `3*2`, `6` all
  canonicalize to `[6]` and unify. Sound ONLY if that constant is FIXED at compile time. If the
  assembler can change it, the compiler type-checked against a value the assembler overrides -> unsound.
- BY SYMBOL (nominal) - `[SIX]`. The compiler compares the dimension SYMBOL, never its value. Sound
  under assembler changes (if `SIX` changes, every `[SIX]` type moves together and still matches).
  Stricter: `[SIX]` and `[6]` are different types even when equal; the signature carries the symbol.

Structs already prove the nominal model: `Voice` is identified by NAME, and its size `.sizeof.Voice` is
derived from the name and resolvable by the assembler. Two `Voice`s always match because the name is the
identity; the size never enters the comparison, so the assembler can change it freely. Extend this to
dimensions: a dimension that is an assembler-resolved constant makes the array a nominal type keyed by
that symbol (`int array[SIX]` has identity `[SIX]`, value is the assembler's business).

Consequence: Impala must DISTINGUISH two kinds of constant, because you can only fold-and-unify the
frozen ones:
- compile-time-fixed const (`const int SIX = 6`, frozen) -> fold to a value (constant evaluator, #20),
  value identity `[6]`, unifies with literals.
- link/assembler-variable const (macro-assembler, conditional layout) -> stays symbolic, nominal
  identity `[SIX]`, does NOT unify with `[6]`; the signature carries the symbol (e.g. `int-array-SIX-ptr`).

This is a real language decision (a qualifier/keyword to mark link-time constants, or the rule "anything
defined in linked GAZL is symbolic"). It also means #20 (evaluate dims) applies only to the frozen kind;
the assembler-variable kind is never evaluated by the compiler at all - it flows through as a symbol.

IMPORTANT - symbolic dimensions must be a SINGLE named constant, never an expression. A symbolic
identity that carried an expression would be FORM-dependent: `[constant*125]` and `[125*constant]` are
different strings and thus different types even though equal, and canonicalizing arbitrary integer
expressions (commutativity, associativity, distributivity, `a*100 + a*25`, ...) is a bottomless pit. So:
- frozen -> evaluate to a value; `constant*125` and `125*constant` both become `1000`; order irrelevant.
- assembler-variable -> a single named symbol only. Arithmetic must be pre-named into another constant
  (`const int ROW_STRIDE = constant * 125`, then `int array[ROW_STRIDE] pointer`), so the identity is the
  single symbol `ROW_STRIDE` and matching is trivial. This is exactly the struct precedent: a struct's
  size in a type is `.sizeof.Voice` - one named symbol, never an expression.

So NEITHER mode needs expression normalization: the frozen mode reduces to a number, the symbolic mode
reduces to a single symbol.

### The fundamental limit (no free lunch)

If a dimension's value is NOT known at compile time, its array type identity CANNOT be value-based -
full stop. Folding only works for genuinely compile-time-fixed constants. For an assembler-variable
constant (or `c1 * c2` of them), the compiler does not have the number, so `[6]`-style identity is
impossible; identity becomes SYNTACTIC (compare the dimension expressions). Syntactic identity is
inherently INCOMPLETE: you cannot decide equality of arbitrary integer expressions over unknown symbols
(`[c1*c2]` vs `[c2*c1]` needs commutativity; `[a*125]` vs `[a*100 + a*25]` needs distributivity; in
general it is the polynomial-identity problem). There is no way to have BOTH "write any calculation
directly" AND "sound, complete type equivalence" when the values are unknown.

The three-way choice for assembler-variable dimensions:
1. Single named symbol (name the arithmetic): `const N = c1 * c2` then `int array[N] pointer`. Identity
   is the lone symbol `N` - trivially decidable and sound; costs one `const` line. Matches structs
   (`.sizeof.Voice` is one symbol, never an expression). RECOMMENDED.
2. Allow inline expressions + a canonicalizer (polynomial/linear normal form over symbolic constants):
   permits `[c1*c2]` and makes `[c1*c2]==[c2*c1]`, even `[a*100+a*25]==[a*125]` if implemented - but it is
   real machinery and STILL incomplete once division or a non-polynomial op appears.
3. Raw syntactic (any textual difference is a different type): the `c1*c2` vs `c2*c1` footgun. No.

Also distinguish the two senses of "not compile-time known":
- Assembler-resolved (fixed per build, unknown to the COMPILER) -> symbolic identity works (above).
- Genuinely run-time variable (differs while the program runs) -> NO static shape exists; that is a
  variable-length array (a non-goal). Drop to a raw `int pointer` + explicit stride; no type identity
  can exist for a runtime-varying dimension.

## Open questions

1. `.sizeof` vs `.words` for the size tag.
2. Does GAZL allow a symbolic constant as an operand, and a constant-expression (sym + sym, i * sym)?
   If not, nested-field and struct-array access lower to multiple instructions instead of one folded
   offset - acceptable, but confirm.
3. How does an Impala unit declare a struct INTERFACE without a full definition (so it can use the
   struct but defer layout)? A forward/opaque struct decl carrying field names + types? Or is this only
   via imports?
4. Interaction with by-value struct semantics (copy `*sizeof`) - the COPY count becomes `.sizeof.Voice`
   symbolic; confirm COPY accepts a symbolic word count.
5. Debug output / retabulation: symbolic offsets change the golden byte output for every struct access -
   a large, one-time golden regeneration. Plan for it.
