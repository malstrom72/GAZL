# Struct layout as GAZL constants (design note)

Status: **IMPLEMENTED** (verified 2026-07-31 against the shipped compiler). This file began as a design
note and the historical sections below are kept as the design record, but the scheme itself now ships:
normal structs, extern structs, `sizeof`, field access AND allocation sizes are all symbolic. See
[IMPLEMENTED (Phase 2a)](#implemented-phase-2a---normal-structs) and
[IMPLEMENTED (extern struct v1)](#implemented-extern-struct-v1) for the details, and treat any
"not implemented" / "still open" phrasing elsewhere in this file as historical.

Observed today from `struct Local { int a  float b }` with a `Local` local:

    ! MOVi <a> #0 ; layout of struct Local
    .o.Local.a:  ! DEFi #<a>
    .o.Local.b:  ! DEFi #<a>
    .z.Local:    ! DEFi #<a>
    $v:          LOCA *.z.Local
                 MOVi $v:.o.Local.a #1

Background: this note applies the two-stage constant model to struct layout. The model itself is
specified in [`docs/TwoStageConstants.md`](TwoStageConstants.md).

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
later reused.

`.z` was chosen over the `.w` ("words") alternative and both `.o.*` and `.z.*` now SHIP - the paragraph
here used to record them as unused and the choice as open, which is no longer true. The full inventory
of generated symbols, which letters remain free, and the rules for adding one live in
[`docs/SymbolNamespace.md`](SymbolNamespace.md); consult that rather than this section, which only
explains the two layout tags.

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

### Array extents in a signature row (DECIDED, IMPLEMENTED)

A signature row may state a FACT or state UNKNOWN. It must never state a token that merely looks like a
fact. So an array extent renders as:

- `[4]` / `[H]` - a literal or a single named const. A real, comparable claim.
- `[]` - the extent folded to a `<X>` compile-time scratch (any expression, e.g. `[H * N]`), or the
  array is an `extern` whose extent Impala cannot know. An explicit "unknown".

gazl-validate compares an extent only when BOTH sides state one; an unknown is skipped, never assumed
equal. Same wildcard model as a name-only extern prototype (`() -> unknown`).

The rule exists because rows used to print the scratch name itself (`int[<A>]`). Those names are
POOL-RECYCLED, so two unrelated extents both rendered `[<A>]` and compared EQUAL - a false clean. It
was inherited from Impala 1.0 and was live in shipped code (`LadderFilter.impala` emitted
`array audioBuffers[<A>]`).

Corollary for `extern struct`: an array field states NO extent at all (**E430** if it does), matching
1.0's rule that "`extern array` declaration should not include array size". The host owns that layout -
field positions come from host-supplied `.o.Name.field` and a 1-D element stride is 1 - so Impala never
reads the extent, and a number there would be an unverifiable claim. Every other array MUST state one
(**E431**).

Ordering trap worth knowing: an extent that needs folding is emitted through the queued `emit()`
channel, while the layout block is written with direct `output()`. `emitStructLayout` therefore has to
`flushMetaCode('')` first - exactly as `declare()` already does - or the `! ADDi` reads a `<X>` that is
defined later in the file and the (cleanly compiled) GAZL will not assemble. The struct also holds each
field's extent borrow until `endStruct`, so two expression extents cannot fold into the same scratch.

## IMPLEMENTED (Phase 2a - normal structs)

Every non-extern struct now emits its layout as GAZL compile-time constants: a rolling `<a>`
accumulator (`! MOVi <a> #0`; `.o.Struct.field: ! DEFi #<a>`; `! ADDi <a> #<a> #<size>`; `.z.Struct:
! DEFi #<a>`), emitted at struct-definition time in dependency order (inner-before-outer, guaranteed by
E412). Field access references `#.o.Struct.field`; `sizeof` references `#.z.Struct`. Nested struct and
array fields MATERIALIZE the sub-object address into a pointer place at offset 0 (via `ADDp`/`ADRL`), so
every access uses a SINGLE symbol - no runtime offset accumulation and no compile-time folding through
the lazy-meta model.

(Superseded detail: this section used to say allocation/copy SIZES stayed numeric. They no longer do.
Verified 2026-07-31: a struct local emits `LOCA *.z.Local`, an array of structs emits
`! MULi <A> #4 #.z.Voice` / `LOCA *<A>`, and whole-struct copy emits `COPY *.z.Name`. Allocation is
symbolic too, which is what makes host-owned layout actually work rather than only half-work.)

Verified: the playground demo (`Voice`/`Biquad`, nested access, whole-struct copy) renders the DEFi
accumulator and runs; the dedicated fixture tests/impala/sources/structLayout.impala prints `4 2 0.75
0.5`; all 10 struct fixtures + funcType + regTransientWindow produce byte-identical RUNTIME output to the
pre-change baseline (old committed goldens run and matched), and their compiled goldens were regenerated
to the symbolic form. Cost: **none**. `d.outer.mid.inner.q`, four levels deep, emits three `! ADDi`
assemble-time folds and one `MOVi` - the same single instruction a flat field costs. A dynamic index adds
one `MULi` for the stride, which is what `a[i]` always paid. Symbolic, host-adaptable offsets turned out
to be free rather than a trade. Extern structs (below) reference the same symbols but emit no accumulator
(host owns it).

## IMPLEMENTED (extern struct v1)

`extern struct Name { fields }` is implemented and tested (tests/impala/sources/externStruct.impala).
Impala emits symbolic `#.o.Name.field` offsets at field-access sites and `#.z.Name` for sizeof; it emits
NO layout - the host supplies those constants at load. Verified end to end on GAZLCmd: the same compiled
Impala GAZL ran correctly against two different host layouts (fields at different offsets) with no
recompile. Normal structs are unchanged (byte-identical goldens); only the extern path is symbolic.

Superseded scope note: this section used to list guards **E418** (fields must be scalar/pointer),
**E425** (no by-value extern instances) and **E424** (no nested extern field access), and to defer
by-value/nested/array extern fields to "v1.1". **None of those three codes exist in `impala.jspeg` any
more** (verified 2026-07-31; only stale comment references survive near lines 1951 and 2245), and all
three restrictions have been lifted: an extern struct with a nested array field compiles, and a
by-value extern local emits `LOCA *.z.E` / `COPY *.z.E`.

Still genuinely outstanding: the gazl-validator cross-check of host layout vs declared interface. And
one thing this expansion opened up that is NOT sound yet - brace-initializing an extern struct emits
positional `DATA` in declaration order while every read goes through `.o.*`, so the values land wrong
under any host layout that is not Impala's guess. See the audit note in
[`docs/TwoStageConstants.md`](TwoStageConstants.md) ("Emitting positional `DATA` for a type whose
layout the host owns"). **This is fixable only in GAZL 2** - verified 2026-08-01 that GAZL 1 has no fill
or origin directive and no backward compile-time branch, so `DATA` cannot skip a symbolic number of
words. Requirements, and the alternatives that do not work, are in
[`ParkedFeatures.md`](ParkedFeatures.md) ("Placing static data at a symbolic offset"). Note that naming
initializer fields (`E455`) did NOT fix this: it changed how the source reads, not where the words land.
(Superseded: this paragraph used to say Phase 2a was "still open". It landed - see the Phase 2a section
above, which this sentence predates.)

## Host-owned struct layout (late-bound ABI) - a motivating future use case

Because field offsets and struct size are symbols resolved at LOAD time, a HOST can define (and later
REDEFINE) a struct's layout and the SAME Impala-compiled GAZL runs against it WITHOUT recompiling any
Impala. This is late-bound ABI, and stronger than C's `offsetof` (which is compile-time): the host
supplies `.o.*` / `.z.*` at load, the distributed GAZL re-assembles against them, and runs. GAZL is
designed for this (per ImpalaDemo: "the size of an array can be a constant set just before executing").

The boundary (what is late-bound vs baked) is the whole subtlety:
- LATE-BOUND, host may change freely: offsets, struct size, field order, padding, and ADDING fields
  Impala does not know about. All of this lives only in the `.o.*` / `.z.*` constants.
- BAKED, host may NOT change without an Impala recompile: the field NAMES and their GAZL-level TYPES.
  Instruction selection is compiled in (`MOVf` vs `MOVi`, pointer load, `COPY` for a by-value nested
  field). Retyping a used field makes the baked instruction wrong; renaming/removing a used field makes
  `.o.X.field` fail to LINK. The link failure is the GOOD failure mode - a load-time build error, not
  silent corruption. (`.z.Inner`-driven `COPY` means resizing a nested struct is fine; retyping it is not.)

So the host's contract is: the NAMES and WORD-TYPES of the fields Impala touches stay stable; everything
else about the layout is the host's. Same discipline as C accessor headers, minus the recompile.

Mechanism this needs (= open question 3, now motivated): for the HOST to own the layout, Impala must
compile the struct in INTERFACE-ONLY mode - it knows field names+types (to select instructions and
type-check) and emits REFERENCES to `.o.*` / `.z.*`, but does NOT emit the accumulator preamble that
defines them; the host supplies the definitions at load. In the default mode Impala emits the layout and
owns the ABI (host can read, not override - redefining a defined constant conflicts). The gazl-validator
should cross-check the host's layout constants against Impala's expected interface (names+types) at
assemble time, so drift is a build error.

Caveat: clean only for GAZL-representable fields (word-sized int/float/ptr and nested word-structs). A
host C struct with packed sub-word fields (char/short/bitfields) does not map onto GAZL's uniform-word
model and cannot be exposed field-by-field this way, regardless of the offset scheme.

## Phase 1 spike - VERIFIED by running on GAZLCmd

Hand-authored GAZL (no compiler changes) confirmed every inference above actually assembles and runs:

- Took the real compiler output for a pointer-accessed struct (`Filter { int mode; float cutoff }`),
  prepended a rolling-accumulator preamble (`! MOVi <a> #0` / `.o.Filter.mode: ! DEFi #<a>` /
  `! ADDi <a> #<a> #1` / ... / `.z.Filter: ! DEFi #<a>`), and replaced the baked offsets with symbolic
  ones. Output was byte-for-byte identical behaviour (`3 0.75 3 99`).
- Global-scope compile-time arithmetic works: the accumulator preamble sits at top level (not inside a
  FUNC) and assembles fine.
- `! DEFi #<a>` snapshotting the accumulator into a named constant works - the whole trick is real.
- Symbolic offsets resolve in EVERY operand position tried, not just PEEK/POKE: pointer access
  (`POKE $f #.o.Filter.cutoff $c`, `PEEK %1 $p #.o.Filter.mode`), local-value access
  (`MOVi %1 $v:.o.Filter.mode`), and the ADRL size hint (`ADRL %0 $v *.z.Filter`). All ran identically.
- Conditional fields slide for free: a `Rec { int a; [int b if WITH_MID]; int c }` with `b` behind
  `! IFND #WITH_MID @noB` printed `offset(c)=2, sizeof=3` with the flag defined and `offset(c)=1,
  sizeof=2` without it - the later offset and the struct size recompute with no compiler involvement.

Conclusion: the target fully supports the scheme. Remaining work is purely on the Impala emitter side
(Phase 2): emit the per-struct accumulator preamble in topological order, and switch field-access
lowering from baked `#offset` / `:offset` / `*size` to the `.o.*` / `.z.*` symbols.

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
