# The generated-symbol namespace (reference)

Status: REFERENCE, verified against `impala/impala.jspeg` on 2026-08-04. This is the inventory of every
symbol the Impala compiler mints for itself, and the rules for adding a new one. Check here before
choosing a prefix; a collision with a user identifier or with another generated name is silent until the
assembler rejects a symbol nobody wrote.

## The one invariant

**Every compiler-minted symbol starts with `.`, and no Impala identifier can.** An Impala identifier
starts with a letter, `_` or `$` (`docs/Impala.md`), so a leading dot is a namespace the user cannot
reach. Globals, constants and functions the user declares are emitted as BARE names; locals are
`$name`. So the leading dot is the whole separation, and it must be preserved by anything new.

## What is emitted today

### Compiler labels: `.<tag><counter>`

Minted by `newLabel(tag)` as `.<tag><n>`, with `labelCounter` reset per function. Eight tags are in use:

| tag | used for | example |
|---|---|---|
| `.a` | `assert` skip target | `.a9` |
| `.e` | exit / join point | `.e24` |
| `.f` | `if` false branch | `.f8` |
| `.l` | loop head | `.l3` |
| `.r` | `--range-checks` bounds test (skip + fail targets) | `.r2` |
| `.s` | `switch` table base | `.s0` |
| `.t` | temporary branch target | `.t0` |
| `.g` | deferred-assertion skip target | `.g0` |

`.a` and `.r` are minted INDIRECTLY as well as directly: `beginDebugGuard(tag)` calls `newLabel(tag)`
with a variable, so a grep for `newLabel('` alone misses `.a` entirely (`assert`'s only minter) and half
of `.r` (`emitRangeCheck` takes `beginDebugGuard('r')` for the skip and `newLabel('r')` for the fail).

`.g` has TWO independent minters, and they are the exception to the `labelCounter` rule above:

- `guardCounter`, at global scope, spelled `'.g' + guardCounter++` directly rather than through
  `newLabel`. `assembleAssert` (the deferred-assertion form behind `assertFitsExtent`) and
  `emitStructLayout`'s negative-extent guard both use it. `labelCounter` resets per function, so two
  globals either side of a function would both be handed `.g0`; `guardCounter` is reset once per
  compilation instead.
- `labelCounter`, inside a function, via the ordinary `newLabel('g')` - `checkIndexUse`'s deferred
  index assertion.

One program emits both and both land on `.g0` (a struct with a symbolic array extent, indexed by a
constant, does it). That assembles and runs anyway, because GAZL scopes function-local labels: the
global `.g0` and the one inside `main` are different symbols. So the two counters do not have to agree -
but neither may a THIRD minter assume `.g<N>` is unique across the file.

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

### Layout and size constants: `.o.` and `.z.`

Just two tags, and one sentence covers both: **`.o.<path>` is the offset of `<path>` in words, `.z.<path>`
is the size of `<path>` in words.** Emitted as `! DEFi` and referenced at every access and allocation
site. **These are implemented and shipping** - see
[`docs/StructLayoutConstants.md`](StructLayoutConstants.md). Unlike everything above they are STABLE and
deliberately predictable, because hand-written or host-supplied GAZL must be able to name them.

| symbol | `<path>` is | example |
|---|---|---|
| `.o.<Struct>.<field>` | a struct field | `.o.Voice.gain` |
| `.z.<Struct>` | a struct | `.z.Voice` |
| `.z.<Struct>.<field>` | a struct ARRAY field | `.z.S.v` |
| `.z.<global>` | a global array | `.z.grid` |
| `.z.<function>.<local>` | a local array | `.z.main.buf` |

An array VARIABLE's size is emitted immediately above its own allocation line, which then reads
`*.z.name` instead of a number or a scratch:

    					! MULi <A> #H #W
    .z.grid:			! DEFi #<A>
    grid:				GLOB *.z.grid

An array FIELD's is emitted inside the layout block, while the extent is still live, and the accumulator
then advances by the SYMBOL - so it outlives the `<X>` scratch it folded into:

    .o.S.a:				! DEFi #<a>
    .z.S.a:				! DEFi #<A>
    					! ADDi <a> #<a> #.z.S.a

**Always WORDS, never an element count.** It is the `*size` allocation operand, so a struct-element array
`Voice bank[3]` gives `.z.bank` = `3 * .z.Voice`, not `3`. A name like `.n.` or "length" would therefore
be actively misleading, and a genuine per-axis element count would be a NEW symbol, not a rename of this
one. Every array gets one - global, `readonly`, `temporary`, local and struct field alike; scalar and
by-value fields do not, since one word and `.z.Inner` already name themselves. Extern structs mint
nothing at all: the host owns their layout, sizes included.

**One tag can span types AND variables only because a top-level name has exactly one kind.** Otherwise
`.z.S` would be both `struct S`'s size and a global array `S`'s extent, and `.z.S.a` both a field of
`struct S` and a local of `function S`. Both shapes were legal until 2026-08-02, and the second produced
a real `Symbol already defined` from the assembler. `claimTopName` now rejects every top-level name clash
(`E401`), which is what let the separate `.x.` extent tag this file used to document collapse into `.z.`.
**If that rule is ever relaxed, `.z.` has to split again** - see "Adding a new one" below.

The point is that a size is usually NOT a number Impala knows. It can be a host-supplied
`! DEFi` count, or `count * .z.Elem` that only resolves once the host's struct layout is in. Folding it
into a `<X>` compile-time scratch made the allocation line the one and only place that could read it,
because the next `borrow('<')` recycles the register. A named constant is permanent, so the extent can
be quoted anywhere later - which is what lets a frame reservation, a bounds check or a second unit
refer to it. Like `.o.` / `.z.` these are STABLE and predictable, and carry no random-id suffix.

### Fixed labels

`.noAssertStrings` - guards the assert-message block so it vanishes when `DEBUG` is 0.

## PARKED - restored with `inline` on GAZL2

Nothing below is emitted by this branch's compiler. `inline` is rejected with `E439` here and the
expansion lives on the `GAZL2` branch, a separate line shipping AFTER Impala 2; `grep -c "_i"` over
`impala/impala.jspeg` returns 0. Kept because the tag rules above have to stay compatible with it, and
because the suffix comes back when `inline` does. Same caveat as [`docs/Inlining.md`](Inlining.md).

### Inline expansion suffixes

An expansion appends `_i<N>` to every label and local it replays, so repeated expansions of one body do
not collide: `.f0` becomes `.f0_i3`, and the callee's local `$tmp` becomes `$tmp_i3`. For a switch the
tag goes BEFORE the `#`: `.s0#0` becomes `.s0_i12#0`, never `.s0#0_i12`.

Note this is a SUFFIX namespace on names that already exist, so it does not consume a tag letter - but
it does mean a new tag must stay unambiguous when `_i<N>` is appended.

### Adding a tag while that is parked

**Check it survives the `_i<N>` suffix** if an inline body could ever contain it. This was step 5 of the
"Adding a new one" checklist below, and returns there when `inline` does.

## Why single letters are safe

Before minting a tag, check whether an EXISTING family can take the name instead - `.z.` absorbed every
array extent that way, retiring the whole `.x.` tag rather than keeping two for one idea.
Two things make that sound, and BOTH must hold: the concept has to genuinely be the same (an array's
extent and a struct's size are both "words occupied", so one tag is honest - whereas "length" would not
have been, since the value is words and not a count), and the OWNER names have to be unambiguous. The
second is not a property of the tag at all; it is `claimTopName`'s one-kind-per-top-level-name rule, so a
change THERE can retroactively break a symbol family here. That is the direction this file's inventory is
most likely to go stale in.


A layout constant is ALWAYS dot-followed (`.o.` / `.z.`) while a label is letter-then-digits with no dot
(`.s0`, `.e24`). So `.o.Voice` cannot be confused with a hypothetical `.oN` label even if `o` were later
reused as a label tag. The two shapes are distinguishable by the character after the tag.

`.` is the separator inside a layout constant because it is ILLEGAL in an Impala identifier but LEGAL in
a GAZL symbol. `_` cannot serve: it is a valid identifier character, so `.o_Voice_lo` is ambiguous
between struct `A_B` field `c` and struct `A` field `B_c`.

## Adding a new one

1. **Take a free tag letter.** In use: `a e f g l r s t` (labels), `a s` (data), `o z` (constants). Free
   today: `b c d h i j k m n p q u v w x y`. (`x` was the array-extent tag until 2026-08-02, when
   `.z.` absorbed it - do not resurrect it for a second size-like idea without re-reading the note above.)
2. **Decide which shape it is.** Dot-followed (`.k.Thing.part`) for a stable, nameable constant that
   host or hand-written GAZL may reference; letter+digits (`.kN`) for an internal, throwaway label.
3. **Stable names get NO random-id suffix**; internal content-derived ones get one.
4. **Re-verify the label inventory above** rather than trusting it - it is a snapshot. A label tag
   reaches `newLabel` through one of three doors, and all three have to be swept: directly, indirectly
   via `beginDebugGuard(tag)`, and the `'.g' + guardCounter` sites that bypass `newLabel` altogether.

        grep -oE "(newLabel|beginDebugGuard)[(]'[a-z]+'|'[.][a-z]+' [+] [(][$][$]parser[.]guardCounter" impala/impala.jspeg | sort -u

   Bracket classes rather than backslashes, and ERE rather than PCRE: `grep -oP` fails outright in this
   repo's Git-Bash (`-P supports only unibyte and UTF-8 locales`). Run as of 2026-08-04 this yields the
   eight tags in the table - `a e f g l r s t` - one line per minter. The data (`makeString`) and layout
   (`.o.` / `.z.`) tags are not covered; those are short enough lists to read off their emitters.

There used to be a step 5, the `_i<N>` survival check. It moved to "Adding a tag while that is parked"
above and comes back here with `inline`.

## See also

- [`docs/StructLayoutConstants.md`](StructLayoutConstants.md) - the `.o.` / `.z.` scheme and why layout
  is a named constant rather than a baked number.
- [`docs/TwoStageConstants.md`](TwoStageConstants.md) - why these are constants resolved at assembly
  time rather than values Impala folds away.
- [`docs/Inlining.md`](Inlining.md) - the `_i<N>` expansion suffix and the switch-label ordering trap.
