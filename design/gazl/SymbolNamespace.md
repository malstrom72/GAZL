# The generated-symbol namespace (reference)

Status: REFERENCE, verified against `impala/impala.jspeg` on 2026-08-01. This is the inventory of every
symbol the Impala compiler mints for itself, and the rules for adding a new one. Check here before
choosing a prefix; a collision with a user identifier or with another generated name is silent until the
assembler rejects a symbol nobody wrote.

## The one invariant

**Every compiler-minted symbol starts with `.`, and no Impala identifier can.** An Impala identifier
starts with a letter, `_` or `$` (`docs/impala/Impala.md`), so a leading dot is a namespace the user cannot
reach. Globals, constants and functions the user declares are emitted as BARE names; locals are
`$name`. So the leading dot is the whole separation, and it must be preserved by anything new.

## What is emitted today

### Compiler labels: `.<tag><counter>`

Minted by `newLabel(tag)` as `.<tag><n>`, with `labelCounter` reset per function. Six tags are in use:

| tag | used for | example |
|---|---|---|
| `.a` | `assert` skip target | `.a9` |
| `.e` | exit / join point | `.e24` |
| `.f` | `if` false branch | `.f8` |
| `.l` | loop head | `.l3` |
| `.s` | `switch` table base | `.s0` |
| `.t` | temporary branch target | `.t0` |

A `switch` arm appends `#<k>` to the table base (`.s0#0`), because `SWCH` finds its arms by appending
`#k` to its own target. That `#` is part of the label, not a separator the compiler is free to move.

### Content-derived data symbols: `.<tag>_<derived>_<hex>`

Minted by `makeString`, using a slug of the content plus the unit's random id, so two units cannot
collide:

| tag | used for | example |
|---|---|---|
| `.s_` | string literal | `.s_Welcom_4d2` |
| `.a_` | `assert` message text | `.a_pointe_4d2` |

These are internal and never referenced by a predictable name, which is why they lean on the random id.

### Struct layout constants: `.o.` and `.z.`

`.o.<Struct>.<field>` (field offset) and `.z.<Struct>` (struct size), emitted as `! DEFi` and referenced
at every access site. **These are implemented and shipping** - see
[`design/impala/StructLayoutConstants.md`](../impala/StructLayoutConstants.md). Unlike everything above they are STABLE and
deliberately predictable, because hand-written or host-supplied GAZL must be able to name them.

### Array extent constants: `.x.`

`.x.<global>` and `.x.<function>.<local>`, emitted as `! DEFi` immediately above the array's own
allocation line, which then reads `*.x.name` instead of a number or a scratch:

    					! MULi <A> #H #W
    .x.grid:			! DEFi #<A>
    grid:				GLOB *.x.grid

The value is the allocation size **in words** - the `*size` operand, not the element count (for a
struct-element array those differ by `.z.Elem`). Every array gets one: global, `readonly`, `temporary`
and non-inline local alike.

The point is that an extent is usually NOT a number Impala knows. It can be a host-supplied
`! DEFi` count, or `count * .z.Elem` that only resolves once the host's struct layout is in. Folding it
into a `<X>` compile-time scratch made the allocation line the one and only place that could read it,
because the next `borrow('<')` recycles the register. A named constant is permanent, so the extent can
be quoted anywhere later - which is what lets a frame reservation, a bounds check or a second unit
refer to it. Like `.o.` / `.z.` these are STABLE and predictable, and carry no random-id suffix.

### Fixed labels

`.noAssertStrings` - guards the assert-message block so it vanishes when `DEBUG` is 0.

### Inline expansion suffixes

An expansion appends `_i<N>` to every label and local it replays, so repeated expansions of one body do
not collide: `.f0` becomes `.f0_i3`, and the callee's local `$tmp` becomes `$tmp_i3`. For a switch the
tag goes BEFORE the `#`: `.s0#0` becomes `.s0_i12#0`, never `.s0#0_i12`.

Note this is a SUFFIX namespace on names that already exist, so it does not consume a tag letter - but
it does mean a new tag must stay unambiguous when `_i<N>` is appended.

## Why single letters are safe

A layout constant is ALWAYS dot-followed (`.o.` / `.z.`) while a label is letter-then-digits with no dot
(`.s0`, `.e24`). So `.o.Voice` cannot be confused with a hypothetical `.oN` label even if `o` were later
reused as a label tag. The two shapes are distinguishable by the character after the tag.

`.` is the separator inside a layout constant because it is ILLEGAL in an Impala identifier but LEGAL in
a GAZL symbol. `_` cannot serve: it is a valid identifier character, so `.o_Voice_lo` is ambiguous
between struct `A_B` field `c` and struct `A` field `B_c`.

## Adding a new one

1. **Take a free tag letter.** In use: `a e f l s t` (labels), `a s` (data), `o z x` (constants). Free
   today: `b c d g h i j k m n p q r u v w y`.
2. **Decide which shape it is.** Dot-followed (`.k.Thing.part`) for a stable, nameable constant that
   host or hand-written GAZL may reference; letter+digits (`.kN`) for an internal, throwaway label.
3. **Stable names get NO random-id suffix**; internal content-derived ones get one.
4. **Re-verify the inventory above** rather than trusting it - it is a snapshot. The tags are all minted
   through `newLabel` / `makeString` and the layout emitters, so:

        grep -oP "newLabel\('\K[a-zA-Z_]+" impala/impala.jspeg | sort -u

5. **Check it survives the `_i<N>` suffix** if an inline body can contain it.

## See also

- [`design/impala/StructLayoutConstants.md`](../impala/StructLayoutConstants.md) - the `.o.` / `.z.` scheme and why layout
  is a named constant rather than a baked number.
- [`design/impala/TwoStageConstants.md`](../impala/TwoStageConstants.md) - why these are constants resolved at assembly
  time rather than values Impala folds away.
- [`design/impala/Inlining.md`](../impala/Inlining.md) - the `_i<N>` expansion suffix and the switch-label ordering trap.
