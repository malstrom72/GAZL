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
