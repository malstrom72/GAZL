# Struct layout as GAZL constants (design note)

Status: DESIGN NOTE, not implemented. Idea + naming decision + open questions.

## The idea (GAZL as a macro-assembler)

Emit each struct's field offsets and its size as NAMED GAZL constants, and reference those symbols at
every field-access site instead of baking in immediate numbers (`#0`, `#2`, `#8`). Example:

    struct Voice { int note; Biquad lo; float gain }

would emit:

    .o.Voice.note   ! DEFi #0
    .o.Voice.lo     ! DEFi #2
    .o.Voice.gain   ! DEFi #6
    .z.Voice        ! DEFi #8

and then a field read `v->gain` lowers to `PEEK $x $v .o.Voice.gain` rather than `PEEK $x $v #6`.

Why: it turns the struct layout into first-class assembler data with one source of truth. You can then
conditionally add / remove / reorder fields directly in GAZL (driven by an assembly-time constant) and
every access site adapts, because they all resolve through the same `.o.*` / `.z.*` symbols.
It also makes the layout inspectable and greppable, and it composes with imports (below).

## Naming (DECIDED): reserved leading tag, `.`-separated, hex-free

Scheme: `.o.<Struct>.<field>` for a field offset, `.z.<Struct>` for a struct size.

Rationale (this is the whole subtlety):
- The leading tag is MANDATORY and the struct name can NEVER be the first segment. Struct names are
  arbitrary, and a struct named `s0` / `e24` / `l3` written as `.s0.note` would collide with the
  compiler's own label namespaces (switch `.s0`, exit `.e24`, loop `.l3`). Putting a reserved tag first
  (`.o.s0.note`) keeps it clear. This mirrors `.s` for string literals.
- `.` is the separator because it is ILLEGAL in an Impala identifier but LEGAL in a GAZL symbol
  (the compiler already emits `.s_Domain_4d2`, `.t0`, `.s0`). `_` cannot be used as the separator: it is
  a valid identifier char, so `.o_Voice_lo` is ambiguous (struct `A_B` field `c` vs struct `A`
  field `B_c`, etc.). Only a non-identifier separator guarantees uniqueness.
- NO `_<hex>` (randomId) suffix, unlike the string symbols `.s_<derived>_<hex>`. String symbols are
  internal, content-derived, and never referenced by a predictable name, so they lean on the randomId
  for uniqueness. Layout constants are the opposite: they must be STABLE and referenced by a
  deterministic name (hand-written or imported GAZL must be able to name `.o.Voice.lo`). Structs
  are defined once (one source of truth), so no duplicate-definition problem.

Tags are SHORT single letters (`.o` offset, `.z` size) on purpose: they appear at every field-access
site (`PEEK $x $v #.o.Voice.gain`), so brevity keeps generated GAZL compact and readable. Single letters
are safe here despite the crowded label namespace because a layout tag is ALWAYS dot-followed (`.o.` /
`.z.`) while the compiler's own labels are letter+digits with no dot (`.s0`, `.e24`, `.l3`,
`.s_...`). So `.o.Voice` cannot be confused with any `.oN` / `.o_...` label even if that letter were
later reused. Verified free: the compiler currently emits only `.f .s .e .l .a .t` label tags; `.o` and
`.z` are unused. (`.z` = size; if you prefer the "words" mnemonic, `.w` is equally free - pick when
implementing.)

Compiler reserves the `.o.*` and `.z.*` prefixes; nothing in Impala emits a leading-dot symbol
(globals/consts are bare names).

## Consequence: interface vs layout decoupling

Emitting layout as GAZL constants splits two things that are currently fused:
- INTERFACE - what fields a struct has and their types. Needed at Impala COMPILE time (to type-check
  `v->note` as int / `v->lo` as Biquad, and to know `note` exists and pick `.o.Voice.note`).
- LAYOUT - where each field sits and the struct size. With the constants, this moves to GAZL ASSEMBLE
  time.

So a unit that USES a struct needs only the interface (field names + types), not the concrete
offsets/size - those resolve from the single definition. Define the layout once; every other unit
references `.o.Voice.lo`. This is the same "one source of truth" property as imports (import the
interface; link the layout), and it enables the macro-assembler adaptivity.

Nuance: it is "interface without layout", not "nothing". Drop the interface too and you are in untyped
territory (like a name-only extern) - you can reference `.o.Voice.note` raw, but Impala no longer
knows the field's type or that it is valid.

## What GAZL supports (VERIFIED against src/GAZL.cpp)

The feasibility question is answered: GAZL has a full COMPILE-TIME (assemble-time) instruction set - the
`!`-prefixed lines. There is no infix; every calculation is a prefix three-address `!` instruction that
writes a compile-time value. Confirmed from the assembler and src/UnitTest.gazl:

- Named compile-time constants: `NAME: ! DEFi #int` (also `! DEFf`, `! DEFp &address`). A label before
  the `!` binds a persistent, referenceable symbol.
- Scratch compile-time variables (registers): `<A>`, `<a>`, `<off>`, ... . `! MOVi <a> #0` writes one;
  `#<a>` reads its value as a CONST_INT. `parseOperand` treats `#`, `<`, `&` all as constant-class, and
  `#<a>` strips the `#` then parses `<a>`, so a slot value is a legal CONST_INT source ANYWHERE a
  constant is accepted - including as the operand of `! DEFi`. So `NAME: ! DEFi #<a>` SNAPSHOTS the
  accumulator into a named constant. This is the whole trick.
- Compile-time arithmetic: `! ADDi <a> #<a> #n`, `! SUBi`, `! MULi`, `! DIVi`, `! MODi`, bitwise,
  shifts, `! ADDp`/`! SUBp`/`! DIFp` for addresses. Operands may be literals, named constants
  (`#.z.Biquad`), or slots (`#<a>`) - all CONST_INT.
- Conditional assembly: `! IFDF <sym> @label` / `! IFND <sym> @label` (defined / not-defined) plus
  `! EQUi #a #b @label`, `! GOTO @label`, and compile-time labels `skip: !`. This is what makes fields
  conditional (below).
- Access side: `PEEK var(d) ptr #int` takes a `#int` offset, so `PEEK $x $v #.o.Voice.gain`
  resolves a symbolic constant offset directly (note the `#` - it is a constant, not an `&` address).

## Rolling offset accumulator (how offsets are actually computed)

The compiler does NOT bake `#0 / #2 / #6`; it emits ONE scratch accumulator per struct and walks the
fields, snapshotting the running offset into each `.o.*` and advancing by each field's size:

    ; struct Voice { int note; Biquad lo; float gain }
    ! MOVi <a> #0                              ; accumulator = 0
    .o.Voice.note: ! DEFi #<a>            ; note @ 0
    ! ADDi <a> #<a> #1                         ; advance by sizeof(int) = 1 word
    .o.Voice.lo:   ! DEFi #<a>            ; lo @ 1
    ! ADDi <a> #<a> #.z.Biquad            ; advance by a SYMBOLIC struct size
    .o.Voice.gain: ! DEFi #<a>            ; gain @ 1 + sizeof(Biquad)
    ! ADDi <a> #<a> #1                         ; advance by sizeof(float)
    .z.Voice:      ! DEFi #<a>            ; total size = final accumulator

`<a>` is scratch and reused - each struct's block re-runs `! MOVi <a> #0` first (UnitTest.gazl reuses
`<A>` across sections the same way). Scalar advances can be literals (`#1`) or symbolic (`#.z.int`);
nested-struct and array advances MUST be symbolic so they track the referenced definition.

Field kinds and their advance line:
- scalar (int/float/ptr): `! ADDi <a> #<a> #1` (the VM word-count of the scalar).
- nested struct by value: `! ADDi <a> #<a> #.z.Inner`.
- scalar array `int d[128]`: `! ADDi <a> #<a> #128` (count * 1).
- struct array `Voice bank[8]`: `! MULi <t> #8 #.z.Voice` then `! ADDi <a> #<a> #<t>`.

### The payoff: conditional fields adapt for free

Wrap a field's TWO lines (snapshot + advance) in a conditional. When the flag is absent, the field's
offset is never defined AND the accumulator never advances, so every later offset and the struct size
shrink automatically - zero compiler involvement:

    ! MOVi <a> #0
    .o.Voice.note: ! DEFi #<a>
    ! ADDi <a> #<a> #1
    ! IFND #WITH_FILTER @noLo                  ; if WITH_FILTER undefined, skip the whole field
    .o.Voice.lo:   ! DEFi #<a>
    ! ADDi <a> #<a> #.z.Biquad
    noLo: !
    .o.Voice.gain: ! DEFi #<a>            ; slides down when lo is absent
    ! ADDi <a> #<a> #1
    .z.Voice:      ! DEFi #<a>

Referencing `.o.Voice.lo` in a build where it was compiled out fails to link - which is correct:
you cannot access a field that is not there. (Value-based toggles work too: `! EQUi #WITH_FILTER #0 @noLo`.)

### Ordering constraint (the one real rule)

`!` instructions execute during assembly IN SOURCE ORDER, so `#.z.Biquad` must be defined before
Voice's accumulator reaches the `lo` field. Therefore struct layout blocks must be emitted in
TOPOLOGICAL order: a struct's `.z` before any struct that embeds it BY VALUE. By-value nesting is
acyclic (a cycle would be infinite size), so an order always exists. POINTER fields impose no ordering
(a pointer is one word, no `.z` dependency).

### Nested-field access (`v.lo.b0`)

Offset = `.o.Voice.lo + .o.Biquad.b0`, a sum of two compile-time constants. Two clean options:
fold at the site into a scratch slot (`! ADDi <t> #.o.Voice.lo #.o.Biquad.b0` then
`PEEK $x $v #<t>`) for a single load, or emit a runtime `ADDp $t $v #.o.Voice.lo` then
`PEEK $x $t #.o.Biquad.b0`. No per-pair named constant needed (that would combinatorially explode).

## Drift / validation

If a using unit's interface (field names + types) lives separately from the definition, they can
disagree. The gazl-validator can cross-check the interface's field types against the definition's
emitted layout, so a mismatch is a build error, not a silent lie - the same "verifiable contract" theme
as extern prototypes (see [[docs/ExternPrototypes.md]]).

## Open questions

1. `.z` vs `.words` for the size tag.
2. (ANSWERED - symbolic constants and compile-time arithmetic exist; see "What GAZL supports" and the
   rolling-accumulator section. Symbolic const as operand: yes. Constant-expression: yes, via prefix
   `!` instructions into scratch slots, not infix.)
3. How does an Impala unit declare a struct INTERFACE without a full definition (so it can use the
   struct but defer layout)? A forward/opaque struct decl carrying field names + types? Or is this only
   via imports?
4. Interaction with by-value struct semantics (copy `*sizeof`) - the COPY count becomes `.z.Voice`
   symbolic; confirm COPY accepts a symbolic word count.
5. Debug output / retabulation: symbolic offsets change the golden byte output for every struct access -
   a large, one-time golden regeneration. Plan for it.
