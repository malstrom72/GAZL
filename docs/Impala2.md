# Impala 2.0 Design

> **Status: implemented, with Step 4 parked.** Steps 1, 2, 3 and 5 (typed pointers/arrays, structs,
> typed function pointers, import) plus the strict-expression rules and coded diagnostics are
> implemented in the JSPEG compiler: VM-verified against fixtures in `tests/impala/sources/`, held to a
> byte-identical golden gate, and fuzzed (`impala/fuzzImpala.js`). `--legacy` gates the strictness rules;
> `impala compile` drives import-as-linking with `export` and `--dead-strip`.
>
> **Step 4 (multiple return values), destructuring, and passing/returning STRUCTS BY VALUE were
> implemented and then deliberately parked for Impala 3.0** - the work is preserved on the
> `Impala3-byvalue-multireturn` branch. Impala 2.0 rejects them (`E426`-`E429`); results come back
> through pointer out-parameters instead. Struct locals/globals, struct pointers, field access,
> `sizeof`, whole-struct assignment and single named returns are all unaffected. See
> [`docs/ParkedFeatures.md`](ParkedFeatures.md) for what is parked, where, and why.
>
> The per-step sections below are the design records. **Import cycles resolve in one direction only**: a
> backwards cross-cycle reference needs a forward `extern`, because the declaration pre-pass ("collect
> mode") was designed and then DEFERRED TO IMPALA 3.0 (2026-07-29) rather than held up 2.0 - `extern`
> covers the function and global cases and adding the pre-pass later only removes the need for it, so
> this is a relaxation waiting to happen, not a compatibility question. See [Cycles](#cycles) for the
> rule and [Deferred to 3.0: collect mode](#deferred-to-30-collect-mode-the-declaration-pre-pass) for the
> design. Smaller items left: `.gazl` blob imports, richer parse errors.

Impala 1.0 is a deliberately minimal "high-level assembler" for the GAZL virtual machine: four
word-sized primitive types (`int`, `float`, `pointer`, `funcptr`), one composite type (`array`),
and a near 1:1 mapping from language constructs to GAZL instructions. See
[`docs/Impala.md`](Impala.md) for the 1.0 reference.

2.0 grows the language beyond that minimum **without giving up what makes Impala Impala.**

## Design principles

These are the constraints every 2.0 feature is measured against.

1. **Stay a transliterator.** The ~1:1 mapping between Impala constructs and GAZL instructions is
   sacred. 2.0 adds no hidden optimization passes and no runtime machinery the programmer can't
   predict from the source. If a feature can't be expressed as a thin sugar over the GAZL a
   programmer would write by hand, it doesn't belong in 2.0.

   **Corollary: the build has two stages, and the second one is not ours.** Impala compiles to GAZL
   TEXT; that text is assembled on the END USER's machine at load, and the host can inject named
   constant values at that moment. A hand-writing GAZL programmer freely references symbols the host
   will define later, so a transliterator must pass them through. Therefore **a constant is not always
   a number Impala knows**, and one `.gazl` artifact is *expected* to compile to different code under
   different host conditions. This is the constraint most often violated by well-intentioned
   hardening: 2.0 never demands a numeric value where a symbol would do, never folds a named constant
   away, and never treats "unknown at Impala compile time" as an error. Checks that cannot be decided
   at compile time get DEFERRED to assembly time, not abandoned and not guessed at. Specified in
   [`docs/TwoStageConstants.md`](TwoStageConstants.md), which is normative for every feature below.

2. **Types are a zero-cost compile-time overlay.** This is the spine of the whole release. Every
   type in 2.0 **erases to exactly the GAZL that 1.0 would emit.** Types exist so the *compiler*
   stops throwing away information it already computes at parse time - not to change what runs.
   1.0's original sin is discarding known types (its per-slot argument types are recorded and then
   dropped), which is why a whole external `; signature` metadata channel and `gazl-validate` pass
   had to be bolted on *after* codegen. 2.0 keeps the types instead.

3. **Audience: strangers and AI agents.** 2.0 is designed to be written by people who have never
   seen Impala and by AI agents generating code. That drives concrete choices:
   - **Regularity over cleverness** - one obvious way, minimal special cases.
   - **Match convention or hard-error, never silently differ.** Where Impala resembles C, it must
     either behave like C or reject the construct - never quietly mean something else. (The 1.0
     bitwise-precedence inversion, where `x & 0xFF + 1` parses as `x & (0xFF + 1)`, is exactly the
     silent-difference trap to avoid.)
   - **Types as guardrails** - an agent produces type errors constantly; the compiler should turn
     them into crisp, machine-readable diagnostics instead of silent runtime garbage.
   - **No tribal knowledge** - features must be discoverable from the source, not from a snippet
     `.txt` file the reader has to already know exists.

4. **Backward compatibility: never silent, and additive where possible.** Silently changing the
   meaning of any 1.0 construct is absolutely forbidden. New *syntax* is purely additive - every
   existing `.impala` source parses as before and compiles to byte-identical `.gazl` (a gate, not
   an aspiration; see [Backward compatibility](#backward-compatibility)), with exactly one
   sanctioned deviation: type refinement may upgrade an untyped `MOVE` to the typed variant of the
   same operation (`MOVi`/`MOVf`/`MOVp`) - semantically identical instructions, verified
   variant-only before regolding. New *strictness rules* may reject old code, but only **loudly**,
   only with a **mechanical, meaning-preserving fix** (byte-identical output after the edit), and
   always with a compile-time argument (`--legacy`) that downgrades the errors to warnings.
   Behavior never varies per file: no version markers, no pragmas, no dialect inference - one
   language, one set of rules, one escape flag at the invocation.

## Roadmap

The features are ordered by dependency, not ambition:

1. **Typed pointers and arrays** - *this document covers this step in depth.* A typed array is the
   degenerate case of a struct (one repeated field type), so the "slots carry a type, and the
   compiler picks the typed instruction from the slot type" machinery must exist first.
2. **Structs** - named heterogeneous layouts over words. Introduces the first multi-word elements,
   which is what changes pointer stride. Implemented; design in
   [Step 2: Structs](#step-2-structs-implemented).
3. **Typed function pointers** - named signature types for `funcptr`, riding the existing
   `; signature` metadata channel. Implemented; design in
   [Step 3: Typed function pointers](#step-3-typed-function-pointers-implemented).
4. **Multiple return values** - closing a known 1:1 gap: GAZL has supported multiple `OUT` words
   per function since 1.0, and Impala never exposed it. Implemented, then PARKED for Impala 3.0
   (see [`docs/ParkedFeatures.md`](ParkedFeatures.md)); design record in
   [Step 4: Multiple return values](#step-4-multiple-return-values-parked).
5. **Import** - sharing typed interfaces between units without textual copying. Implemented; full cycle
   resolution deferred to 3.0. Design in [Step 5: Import](#step-5-import-implemented-except-cycles), rule
   and design in [Deferred to 3.0: collect mode](#deferred-to-30-collect-mode-the-declaration-pre-pass).

Cross-cutting decisions - strict expressions, the rejection of
[compound assignment](#compound-assignment---rejected), and the [diagnostic format](#diagnostics) -
have their own sections below.
Other long-standing gaps (richer `for`) are out of scope for this document and tracked separately.

---

## Step 1: Typed pointers and arrays

### The idea

A pointer or array gains a **compile-time element type**. At runtime it is still exactly what it was
in 1.0 - a word (pointer) or N words (array). The element type is information the compiler carries so
it can (a) select the correct typed GAZL instruction automatically and (b) type-check the surrounding
expression. It costs nothing at runtime.

### Declaration grammar

Declarations become **type-first with stackable trailing keyword modifiers**:

```
Declaration := (BASE_TYPE)? (POINTER | ARRAY)* Identifier ('[' ConstExpr ']')?
BASE_TYPE   := int | float | pointer | funcptr | <struct name, in a later release>
```

A scalar is simply the zero-modifier case. The modifiers read left-to-right as an English noun
phrase:

| Declaration | Meaning |
|---|---|
| `int i` | int |
| `int pointer p` | pointer to int |
| `int array a[10]` | array of 10 ints |
| `float array buf[64]` | array of 64 floats |
| `int pointer array a[10]` | array of 10 (pointers to int) |
| `int pointer pointer p` | pointer to pointer to int |
| `funcptr fp` | function pointer |
| `funcptr array handlers[8]` | array of 8 function pointers |

Each trailing `pointer` wraps the whole type-so-far in another pointer; `array` (with its `[size]`)
marks array storage of the element type to its left. The reading is strictly left-to-right with no
backtracking - deliberately unlike C's inside-out declarators (`int **p`, `int (*fp)(int)`), which
are the least agent-friendly corner of C.

Scope and storage prefixes (`global`, `readonly`, `temporary`, `extern`) precede the type exactly as
in 1.0:

```impala
global int pointer head
readonly float array WINDOW[512] = { /* ... */ }
extern int array futureArray            // extern arrays still omit the size
function findSmallest(int n, int pointer vector) returns int j locals int i { /* ... */ }
```

### Design choices, and why

- **`funcptr` stays a keyword.** C's function-pointer declarator is the single most
  agent-hostile piece of C syntax. Impala's `funcptr` is strictly better and composes cleanly
  (`funcptr array`, `funcptr pointer`). 2.0 never adopts `int (*fp)(int)`.
- **The `array` keyword is kept** (`int array a[10]`, not `int a[10]`). Beyond consistency, it
  future-proofs structs: `Filter f` will be one struct value and `Filter array f[4]` an array of
  four, a distinction that would otherwise rest entirely on the presence of `[4]`.
- **Pointer-to-pointer is spelled out** (`int pointer pointer p`). Rare, unambiguous, and clearer
  than a `**` shorthand; no shorthand is introduced.

### Semantics

- **One word per element still holds.** Every primitive (and every pointer) is one word, so a
  `pointer int`, `pointer float`, and `pointer pointer` all have **stride 1 word**. Typed pointers
  therefore change pointer arithmetic **not at all** - `p + 3` is still three words. (Stride only
  stops being 1 when elements become multi-word, i.e. structs - which is precisely why structs come
  later. A struct pointer does not do arithmetic at all: it moves by scaled subscript, `&p[[3]]`.
  See the cost model below.)
- **The compiler selects the typed instruction from the element type.** Reading `a[i]` where `a` is
  `int array` yields an `int` and emits the int-typed peek; where `a` is `float array` it yields a
  `float` and emits the float-typed peek. No cast required, and the result type flows into
  surrounding type-checking.
- **Array-to-pointer decay is unchanged.** Passing an `int array` where an `int pointer` is expected
  works as in 1.0; the element types must agree (or the target is untyped - see below).

### The untyped escape hatch

Omitting the base type gives the 1.0 behaviour: **untyped, raw-word storage.**

```impala
pointer raw            // untyped pointer - the "word*/void*" of Impala
array scratch[256]     // untyped array
```

Untyped pointers/arrays are where reinterpretation legitimately lives - host/native boundaries such
as `read(offset, count, values)` fill raw words. Because typing is opt-in per declaration, an
untyped value requires a cast to enter the typed world, which makes every cast a **loud, greppable
signal that you are leaving the type system** - the desired inversion of 1.0, where casts are
everywhere and meaningless.

### What it buys: the casting churn disappears

This is the payoff, and it's a direct hit on the biggest 1.0 pain. From the demo's `findSmallest`:

```impala
// 1.0 - every untyped element access needs a cast
function findSmallest(int n, pointer vector) returns int j locals int i {
    j = 0
    for (i = 1 to n)
        if ((int) vector[i] < (int) vector[j])   // casts required
            j = i
}

// 2.0 - the element type is known
function findSmallest(int n, int pointer vector) returns int j locals int i {
    j = 0
    for (i = 1 to n)
        if (vector[i] < vector[j])               // no casts
            j = i
}
```

Real firmwares benefit the same way: `global array params[PARAM_COUNT]` is all ints, so today every
`global params[X]` read is cast. Type the array once (`global int array params[PARAM_COUNT]`) and the
casts vanish at every use site - while the emitted GAZL is identical.

### Casts generalize with the type grammar

A cast names a type using the same grammar, so it extends naturally:

```impala
p = (int pointer) raw          // reinterpret raw words as a pointer to int
q = (Filter pointer) raw       // a struct type is a type name like any other
```

`(pointer)` and `(int)` continue to mean exactly what they mean in 1.0 (the zero-modifier cases).

**`array` is not a cast modifier.** `pointer` is the only one, so `(funcptr array) table` is `E001` -
a cast never produces an array type. Cast to a pointer and index that instead.

### The typed-untyped boundary

If typed code needed a cast every time it touched untyped code (natives, 1.0 units, raw buffers),
casts would stop being signals and become 1.0-style noise again. If everything flowed implicitly,
the types would be decorative. The resolving principle is an **asymmetry**:

> **Erasing type information is silent. Assuming type information is loud.**

Dropping to untyped cannot create a wrong belief - the consumer treats words as words on purpose.
Climbing to typed *mints a claim* the compiler cannot verify, and unverifiable claims are exactly
what should be greppable. The rules:

| Flow | Rule | Example |
|---|---|---|
| `T pointer` → `pointer` | **implicit** (erase) | `read(off, n, samples)` with `samples : float pointer` - no cast |
| `T array` → `T pointer` | **implicit** (decay - 1.0's existing decay, now typed) | `findSmallest(n, mydata)` |
| `T array` → `pointer` | **implicit** (decay + erase) | passing a typed buffer to a native |
| `pointer` → `T pointer` | **explicit cast** | `fp = (Filter pointer) alloc(...)` |
| `T pointer` → `U pointer` | **explicit cast** (reinterpret) | `(int pointer) floatBuf` - legal, loud |
| `T pointer`/`T array` element, field read | no cast - the whole point | `vector[i]`, `fp->mode` |
| untyped word → typed scalar | **1.0's rules, unchanged** (frozen by the byte-diff gate) | `z = lfoVal(...)` stays legal; arithmetic still needs `(int)` |
| int ↔ float | **never by cast** - `itof`/`ftoi` only; `(float pointer)` on an `int pointer` reinterprets words, converts nothing | |
| named funcptr type → `funcptr` | implicit (erase) | |
| `funcptr` → named funcptr type | **explicit cast** (mints a signature claim) | |
| data pointer ↔ `funcptr` | **never** (1.0 wall - different address spaces in GAZL) | |
| `null` / `nullfunc` | implicit into any pointer / funcptr type | |

**Comparisons never element-check** (revised during implementation - an earlier draft made
`T pointer` vs `U pointer` an error). Two reasons: a comparison mints no lasting assumption, so
there is nothing to state loudly; and since `&` is type-producing, 1.0 sources that legitimately
compare addresses of differently-typed globals (`&global aFloat == &global marker`) would
otherwise become errors, violating compatibility. Pointer comparisons erase, always.

Sideways reinterpretation (`T pointer` → `U pointer`) takes a **single cast** - no forced
round-trip through untyped. Casts are already defined as reinterprets in this language; ceremony
beyond one cast would be punishment, not information.

Initializers for typed arrays are element-checked: `global int array a[4] = { 1, 2.0 }` is a
compile error on the float literal (new syntax, so no compatibility concern).

Two free upgrades enabled by the boundary rules:

- **`&` becomes type-producing.** `&global aFloat` yields `float pointer` (previously: generic
  `pointer`); `&x` on an `int` local yields `int pointer`; `&a[i]` of an `int array` likewise.
  All existing uses still compile because erase is implicit.
- **String literals become `int pointer`** (GAZL string data is int words via `DATs`).
  `hexDigit = HEX_CHARS[v & 0xf]` needs no cast; passing a literal to `print(pointer)` is an
  implicit erase.

Both upgrades share one compatibility consequence, **explicitly sanctioned** *(decided
2026-07-20)*: type refinement can upgrade a formerly-untyped result inside an expression, so a
line that emitted the untyped `MOVE` in 1.0 may now emit the typed variant of the same operation
(`MOVi`/`MOVf`/`MOVp` - e.g. `x = ("0123456789abcdef")[v & 0xf]`). These are **semantically
identical instruction variants**, and they are the *only* permitted deviation from byte-identity;
every such golden change is verified to differ in MOV-variant mnemonics alone before regolding.

### Cross-unit checking

An `extern int array futureArray` now advertises an element type, so the `; signature` metadata that
already rides inside `.gazl` carries element information. *Implemented:* definition-side rows emit
**element chains as single-token categories** - `; signature array values[5] : int`,
`; signature global cursor : int-ptr`, `func take(int-ptr-ptr h) -> int-ptr`, const rows likewise -
while untyped declarations keep their 1.0 categories (`ptr`, `unknown`). The validator treats a bare
`ptr` as an element-unknown pointer that matches any chain (the metadata-level analogue of the
erase/assume asymmetry: the typed side states the claim, the untyped side is a wildcard), and
errors on differing chains. Call-site `; expects` rows deliberately keep bare categories: `&` is
type-producing, so 1.0 sources can pass element-carrying arguments, and chain-rendering there would
churn old units' output - the bridge rule makes bare-vs-chain compatible, so nothing is lost.
The concatenation-linking model is untouched; single-token chains keep every row parseable by
older tooling.

The boundary asymmetry has an exact link-level analogue via that wildcard. A typed
`extern int array data` in one unit binding an untyped `array data` definition in another is
*concrete-vs-unknown* - the validator lets it match. That is not an unmarked assumption: **the typed
extern declaration is itself the loud, in-source statement of the claim**, playing at link level the
role a cast plays in an expression. Typed-vs-typed disagreement across units (`int array` here,
`float array` there) is a validator error. One shape at both levels: *silent down, stated up, error
sideways.*

---

## Backward compatibility

100% backward compatibility is a hard gate. The typed-declaration grammar meets it, and the claim is
both provable and empirically checked.

### Why it is non-breaking

1. **The new forms are unreachable from valid 1.0 input.** `pointer` and `array` are reserved
   keywords, so a base type followed by a modifier keyword (`int pointer`, `float array`, …) is a
   *parse error* in 1.0 today - a `VarDecl` is `BASE_TYPE Identifier`, and `pointer` is not an
   identifier. Assigning meaning to previously-rejected input cannot change any program that
   currently compiles.
2. **The modifier loop cannot reinterpret or greedily absorb existing declarations.** It consumes
   `pointer`/`array` keywords only between the base type and the name, and the name is always a
   non-keyword (`!KEYWORD`), so the loop stops there. The only tokens it could otherwise grab are
   keywords appearing where 1.0 expected the name - which 1.0 already forbade.

### Empirical corpus scan

Across all 78 `.impala` files in the repository:

| Check | Result |
|---|---|
| Base type immediately followed by `pointer`/`array` (new-form collisions) | 0 |
| Any two adjacent type/modifier keywords | 0 |
| Bare `pointer NAME` followed by a modifier (greedy-absorption risk) | 0 |
| Untyped `pointer NAME` usages that must remain valid | 583 |
| Untyped `array NAME` usages that must remain valid | 636 |
| `(pointer)` reinterpret casts | 77 |
| `extern array` (size omitted) | present |

Zero collisions; the ~1,200 existing untyped usages all fall through unchanged as the "base type
omitted" degenerate case.

### Acceptance gate (at implementation time)

The scan reasons about today's corpus; the airtight proof once the 2.0 compiler exists is a
**byte-diff**: compile all buildable sources with the 2.0 compiler and diff the generated `.gazl`
against a frozen 1.0 baseline. **The output must be byte-identical.** Any diff is a compatibility
regression, full stop. This is wired into the existing JSPEG parity harness
(`impala/runJspegTests.js`, `impala/jspegCompilerTests.js`) as a permanent regression test.

---

## Step 2: Structs (implemented)

> **Slice 1 implemented** (`tests/impala/sources/structPointers.impala`): `struct` definitions,
> field layout, `sizeof(Type)`, struct-typed **pointers**, struct-pointer casts
> `(Filter pointer) raw`, `->`/`.` field access to **scalar** fields through a pointer, and
> `extern struct` forward declarations (mutual pointer types). Deferred to later slices - the
> multi-word-value rework: struct **values** as locals/params/returns, nested inline field chains
> (`fp->sub.field`), struct arrays, brace initializers, and by-value passing/return. The remainder
> of this section is the settled design for those slices.

A struct is a **named heterogeneous layout over words** - pure naming sugar over the offset
constants firmware authors hand-roll today (`const int Filter_cutoff = 0` plus casts). It erases to
the same GAZL and deletes the casts.

### Definition

A struct body is a run of declarations using **exactly the Step 1 declaration grammar**, one per
line, semicolons optional (as for globals). No new syntax appears inside the braces:

```impala
struct Filter {
	float cutoff                    // word offset 0
	float resonance                 // word offset 1
	float array state[4]            // offsets 2..5 - inline fixed array field
	int mode                        // word offset 6
	Filter pointer next             // word offset 7 - self-reference legal by pointer
}                                   // sizeof(Filter) = 8
```

Fields are laid out in declaration order, one word per primitive/pointer slot, array fields inline.
A by-value self-referential field (`Filter next`) is an error (infinite size); `Filter pointer` is
fine.

**Inline nesting is supported** *(decided)*: fields may be struct-typed by value, and offsets
compose - `v.low.z1` is still a single free constant offset. By-value recursion (direct or mutual)
remains an error:

```impala
struct Biquad { float b0; float b1; float a1; float z1 }
struct Voice {
	Biquad low                      // inline: offsets 0..3
	Biquad high                     // inline: offsets 4..7
	float gain                      // offset 8
}                                   // sizeof(Voice) = 9
```

**Forward declarations** *(decided)*: `extern struct B` makes a struct name legal for pointer
fields (and pointer declarations) before - or without - its definition. This is single-pass
friendly, resolves mutual references explicitly, and is the same construct as the opaque-handle
mechanism in *Identity across concatenation* below:

```impala
extern struct B                     // forward declaration
struct A { B pointer next }
struct B { A pointer back }         // completes B
```

Struct definitions are top-level only; **one definition per unit** (a duplicate is an
already-declared error). Struct names share the flat namespace with functions, globals, and
constants.

The struct name becomes a `BASE_TYPE`, so usage needs **no new declaration syntax** (as anticipated
by the Step 1 grammar):

```impala
Filter f                            // a struct value
Filter pointer fp                   // pointer to Filter
Filter array banks[4]               // array of 4 Filters
global Filter voice
```

`sizeof(Filter)` is a new compile-time `int` (C-style spelling; the type-name form only - a
`sizeof expr` form can come later if needed). New reserved words `struct` and `sizeof` have
**zero identifier collisions** in the 78-file corpus (verified; note `type` has 23 uses as an
identifier and must never become a keyword). The `.` character appears in no existing source
outside float literals, so the member operator is collision-free too.

### Initializers

*(decided)* Global and readonly struct variables (and struct arrays) take **nested-brace
initializers**, one brace group per struct or array field, each value checked against the field's
type. Values must be compile-time constants, as for 1.0 globals.

A struct value is initialized **by field name**, `field: value`. Any field may be omitted and is
zero-filled, and the entries may appear in any order - the words are always emitted in layout order.
Array levels stay **positional**, both for a struct's array field and for an array *of* structs,
because there the index already does the naming; a `field:` in an array slot is `E458`.

```impala
global Filter voice = { cutoff: 0.5, resonance: 0.7, state: { 0.0, 0.0, 0.0, 0.0 }, mode: 2 }
readonly Voice array PRESETS[2] = {
	{ low: { b0: 0.1 }, high: { b0: 0.2 }, gain: 1.0 },
	{ low: { b0: 0.3 }, high: { b0: 0.4 }, gain: 0.5 }
}
```

Note `next` and the unset `Biquad` fields are simply left out rather than padded with `null`/`0.0`.

The 1.0 **positional** form (`{ 0.5, 0.7, ... }`) is `E455`. It was silently order-dependent: inserting,
removing or reordering a struct field changed what every existing initializer meant, with nothing in
those initializers needing to change for it to happen. `--legacy` still maps positionally, so 1.x
sources keep building.

Uninitialized **global** struct storage is zero-filled - the globals and consts regions are cleared
once at load (`src/GAZL.cpp:880`). **Locals are not.** A `call` only bumps the frame pointer, so an
uninitialized struct local holds whatever the previous frame left there. Initialize struct locals
before reading them.

The lowering is the flat `DATA` row the braces describe - the nesting and the names exist for
readability and per-field checking, not for any runtime structure.

### Field access: `.` and `->`

- **`.` applies to a *place*** - a struct value (`f.cutoff`), or a parenthesized dereference
  (`(*fp).cutoff`). It never applies directly to a pointer.
- **`->` applies to a typed struct pointer** and is defined as sugar: `fp->cutoff` ≡ `(*fp).cutoff`.
  Both spellings are legal and compile byte-identically; `->` is idiomatic.
- **`*fp` is a place, not a load.** It denotes the pointed-at struct and emits nothing by itself;
  the consuming operation decides the instruction. Bare `*fp` in scalar context is an error (a whole
  struct doesn't fit an expression slot).
- Wrong operator is a hard error with the fix in the message (`` `fp` is a pointer - use `->` ``).
- Untyped `pointer` has no fields (no struct type) - the escape hatch stays cast-first.

Multi-level pointers compose from existing pieces; nothing new is invented:

```impala
Filter pointer pointer pp
x = pp[0]->cutoff                   // or (*pp)->cutoff - 2 markers, 2 loads
```

### The cost model: dots are free

This is the property that justifies the `.`/`->` split (which is *not* C ceremony here - in Impala
it is a cost annotation with exact GAZL meaning):

- **`.` contributes zero instructions, always.** It is compile-time offset arithmetic, folded into
  operand syntax. `f.a.b.c` nested arbitrarily deep is still free.
- **`->` contributes exactly one `PEEK`/`POKE`, always.**

Combined with the existing markers, every memory access in an expression is visible in the source:

> **Instruction count = marker count.** Each `global`, `*`, `->`, and `[]`-on-a-pointer costs one
> load. Each `[[ ]]` costs one load plus one `MULi`. Dots are free.

```impala
f.cutoff                    // 0 loads - direct operand
fp->cutoff                  // 1 load
global voice.cutoff         // 1 load - from `global`; the . is still free
global gfp->cutoff          // 2 loads - one for the global pointer, one through it
head->next->next->value     // 3 loads - each hop visible
```

The principle used to be stated as "instruction count = marker count", which is no longer literally
true and was reworded for that reason. Two exceptions:

- **1.0's `p[i]`** PEEKs without a distinct marker, unlike `a[i]`'s local access - grandfathered by
  backward compatibility. New syntax is held to the stricter rule.
- ~~**An `inline` call site carries no marker at all**~~ - moot in 2.0, since `inline` is parked
  (`E439`). It is recorded because it is the exception that returns the moment the feature does: an
  expansion emits an arbitrary number of instructions with no marker at the call site, and argument
  substitution deletes marshalling moves a normal call would emit. That is why the rule below is worded
  as *predictable* rather than *countable*. See "Inline functions" below.

Structs are what makes a subscript able to cost more than one instruction, and that is exactly why
they get their own bracket. `[i]` strides one word; `[[i]]` strides `sizeof(element)` and pays one
`MULi` for it. Each is an error where the other is correct, so there is one legal spelling per access
and the stride is never something you have to look up:

```impala
words[i]                    // 1 instruction  - stride 1
voices[[i]]                 // 2 instructions - stride sizeof(Voice)
&voices[[i]]                // the only way to move a Voice pointer
```

The residual multiply is the **floor**, not overhead: the address of element `i` of a 3-word struct
genuinely is `base + i*3`, and no language or ISA can form it without a multiply. A constant index
folds at assembly time (`! MULi`), so it costs nothing at run time - but **today that fold only fires
for a bare decimal literal** (`impala.jspeg:2158` tests `/^#[0-9]+$/`). `bank[[2]]` folds to
`! MULi <A> #2 #.z.S`; `bank[[K]]` for `const int K = 2`, `bank[[HOST_I]]`, `bank[[K + 1]]` and
`p[[-1]]` all fall through to a RUNTIME `MULi` + `ADDp`, even though `! MULi` accepts every one of
those operands (the compiler itself emits `! MULi <A> #HOST_N #.z.S` when sizing an array). Measured:
identical programs differing only in `2` vs `K` assemble to code size 3 vs 5. See `docs/Impala2Review.md`,
"the scaled subscript is spelled `[[ ]]`", for why arithmetic on a struct pointer is rejected rather
than scaled.

### Verified lowering

Checked against the GAZL operand grammar and live usage in `src/UnitTest.gazl`. The key mechanisms:
the `local:const` operand form (a local plus compile-time offset is a *direct operand* - no address
materialization), `GETL`/`SETL` (local access with runtime offset, no pointer involved), and
`PEEK`/`POKE` with a constant-offset immediate.

```gazl
; A) local value, constant field - direct operand, zero extra instructions
$f: LOCA *8
MOVf $f:1 #0.5              ; f.resonance = 0.5
MOVf $x  $f:1              ; x = f.resonance

; B) local struct array, dynamic index - GETL with the field offset folded into the
;    BASE OPERAND (precedent: UnitTest.gazl:794 "GETL i0 lArray:4 i1"). No ADDi.
$voices: LOCA *64
MULi %0 $i #8              ; i * sizeof(Filter) - the stride multiply, marked by [[ ]]
GETL $x $voices:6 %0      ; x = voices[[i]].mode   (constant :6 rides the operand)

; C) through a pointer - one real load, constant offset immediate
PEEK $x $fp #6            ; x = fp->mode
POKE $fp #6 $val         ; fp->mode = val

; D) dynamic index through a pointer - the one place an ADDi survives,
;    because PEEK's offset operand is spent on the computed index
MULi %0 $i #8
ADDi %0 %0 #6
PEEK $x $fp %0            ; x = fp[i].mode
```

Honest cost summary (the claim is "no worse than hand-rolled offsets, casts deleted" - *not*
"free" and *not* "optimal"):

| Access | Cost vs hand-written 1.0 |
|---|---|
| Local value, constant field | identical - single direct instruction |
| Local array, dynamic index | identical - stride `MULi` + `GETL`/`SETL` |
| Pointer, constant field | identical - one `PEEK`/`POKE` |
| Pointer, dynamic index | identical - `MULi` + `ADDi` + `PEEK` |

The stride `MULi` is real and is the only cost flat untyped arrays don't pay; it exists in the
hand-rolled `voices[i*8 + 6]` version too. A local struct is always one contiguous `LOCA` (fields
are direct `:const` operands), so taking `&f` is a plain `ADRL` with no representation change -
codegen never depends on how the struct is used elsewhere in the function.

### Passing, returning, copying

> **Passing and returning a struct BY VALUE is PARKED for Impala 3.0** (`E426`, `E427`); preserved on
> the `Impala3-byvalue-multireturn` branch. Whole-struct assignment (`a = b`, `*p = v`), struct
> locals/globals, struct pointers, field access and `sizeof` are all still supported. Pass and return
> structs through pointers. See [`docs/ParkedFeatures.md`](ParkedFeatures.md).

**Both by-value and by-pointer are legal, chosen by the parameter/return declaration** *(decided
2026-07-20)*. An earlier draft deferred by-value out of a vague "one convention beats two" instinct;
the `a = b` decision already demolished that - a by-value struct is the *same* `COPY *sizeof`
instruction at a call boundary, and by the project's own sugar rule the two modes are not two
spellings of one thing: `f(v)` and `f(&v)` compile to different GAZL (an N-word copy vs one pointer
word) with different semantics (isolation vs aliasing). Keeping both is the rule that killed `+=`
working *for* us.

- **By-value parameter** (`function tick(Filter f)`): the caller copies `sizeof(Filter)` words into
  the callee's parameter window; inside the callee the fields are **direct, free window operands**
  (no `PEEK`). The window is **read-only**, inheriting 1.0's `INP`/parameter semantics - you cannot
  assign to `f` or a field of it, and you cannot take `&f` of a parameter (already illegal in 1.0).
  This makes by-value the read-only, isolated, cache-friendly mode; it is *strictly faster* than a
  pointer for hot inner loops on small structs (stereo frames, complex pairs, biquad coefficients).
- **By-pointer parameter** (`function tick(Filter pointer f)`): one pointer word; fields are `PEEK`
  through the pointer; the callee can mutate the caller's struct and alias it. This is the
  mutation/aliasing mode.
- **No decay in either direction.** `tick(v)` where the parameter is `Filter pointer` is an error
  (`note: pass a pointer: &v`); `tick(&v)` where the parameter is `Filter` is an error
  (`note: pass the value: v`). The call site therefore always shows which mode is in effect -
  more marker-honest than C, where the same `f(v)` silently copies or not depending on the
  callee. (Array *fields* still follow 1.0 array decay: `f.state` yields a typed pointer.)
- **By-value return** (`returns Filter out`): N `OUT` words - a struct return *is* a multiple return
  whose slots carry names and offsets. The callee writes `out.cutoff` as free direct `OUT`-slot
  access; the caller consumes it into a struct place, statement-level (`v = makeFilter(...)`), the
  same restriction as `a = b`. **This makes [Step 4 (multiple return values)](#step-4-multiple-return-values-parked)
  a prerequisite of struct returns** - same multi-`OUT` window layout, one implementation. By-value
  *parameters* need only `PARA` sections and can land without Step 4; by-value *returns* need it.
- **Whole-struct assignment `a = b` is allowed, statement-level only.** It lowers to exactly one
  `COPY *sizeof(T)` - the instruction one would hand-write - with the size known at compile time
  from the declared types. It is not an expression: a struct value does not fit an expression slot,
  so `a = b = c` and struct assignment nested in a larger expression are errors. `*dst = *src`
  (both typed struct pointers) is the same statement through places. Explicit `copy()` remains
  available and equivalent.
- **Cost model in one sentence:** a call copies `sizeof` words for each by-value struct argument
  and each by-value return, exactly like `a = b`; a by-pointer argument copies one word and pays a
  `PEEK` per field access. Both are visible at the call site.
- **Whole-struct comparison is rejected.** GAZL has no multi-word compare; `a == b` on struct
  values is an error - compare fields, or compare pointers.

### Identity across concatenation

Impala has no `#include`, so a struct shared between separately compiled units has to be spelled in
each source. **A struct is defined exactly once in a linked set** *(decided)* - in an import-driven
build (Step 5) that is the closure; under manual concatenation it is the set of units you assemble
together. Every other unit declares it `extern`.

This is no longer just a rule, it is the only thing that links. The **1.0 copy-paste model - textually
re-declaring the same `struct` in each unit - is dead**, because since Phase 2a every non-extern
`struct` emits `! DEFi` layout rows:

```gazl
                    ! MOVi <a> #0            ; layout of struct Filter
.o.Filter.cutoff:   ! DEFi #<a>
                    ! ADDi <a> #<a> #1
.o.Filter.mode:     ! DEFi #<a>
                    ! ADDi <a> #<a> #1
.z.Filter:          ! DEFi #<a>
```

Two units carrying byte-identical copies of that struct now fail to assemble at all, with
`Symbol already defined: .o.Filter.cutoff`. Agreement is not the question; the second copy never gets
that far.

**The working pattern.** Exactly one unit writes the definition; the others write a **body-carrying
`extern struct`**:

```impala
// filter.impala - the one definition
struct Filter { float cutoff; int mode }

// voice.impala - every other unit
extern struct Filter { float cutoff; int mode }
```

The `extern` form emits no layout rows - only a `; signature extern struct Filter { cutoff : float,
mode : int }` row plus symbolic references to `.o.Filter.*` and `.z.Filter` - so it cannot collide, and
it adapts if the definition's layout changes. It is the same mechanism as a host-owned layout
(`docs/StructLayoutConstants.md`), pointed at another Impala unit instead of at a host.

**Identity is nominal, layout-verified.** The struct *name* is the identity. GAZL's namespace is
already flat (function and global names collide across concatenated units today; struct names join
that club), and every `extern struct` declaration of a name must agree **exactly** with the definition
and with every other declaration of it: field count, field order, per-field name, per-field type, and
array sizes. Any disagreement is `E438`, citing both source locations. Field *names* are deliberately
part of the match: a renamed field is drift evidence even when the layout still happens to agree.

Structural identity (same layout ⇒ same type, names irrelevant) is rejected: a `Filter` and a
`Voice` that both happen to be `{float, float, int}` must not silently interchange - accidental
compatibility hides bugs, and no language the agent audience is trained on works that way.

**Metadata.** Struct definitions emit no GAZL instructions, so each definition contributes a
standalone comment row (the channel already has standalone rows for externs):

```gazl
; signature struct Filter { float cutoff, float resonance, int mode } @ a.impala:3:1
```

Function rows reference struct names as new type atoms - `; signature func setMode(Filter pointer)
-> void` - and the validator resolves them against the merged struct rows using today's machinery.
Nested struct references (`Filter pointer next`) resolve by name, recursively.

**Opaque structs.** A unit that only passes a `Filter pointer` *through* - no field access, no
`sizeof` - does not need the layout copy:

```impala
extern struct Filter                 // incomplete type: pointers only
```

Pointer declarations and pass-through are legal; field access and `sizeof(Filter)` are compile
errors (the layout is absent). This is C's incomplete-type pattern: it minimizes the copy-paste
surface, its metadata row (`; signature extern struct Filter`) is a name-only wildcard matched
against any full definition, and handle/token APIs get real encapsulation for free.

The same construct doubles as the **forward declaration** for mutually-referencing structs within
a unit (see *Definition* above): `extern struct B` before `struct A { B pointer next }`, with
`struct B { ... }` completing the type later in the same unit. One mechanism, two uses.

In metadata categories, struct-typed pointers render nominally in the element-chain notation
(`Filter-ptr`, `Filter-ptr-ptr`); the bare-`ptr` bridge rule applies unchanged, so 1.0 units and
opaque consumers interoperate.

---

## Step 3: Typed function pointers (implemented)

Mirrors structs exactly: **named funcptr types that become base types** - the typedef the language
never had. *(Implemented: `functype Name(params) [returns ...]` registers a signature usable as a base
type; funcptr variables carry the type tag, assignments and indirect calls are checked against it, and
struct/multi-value returns flow through funcptrs. VM-verified in `tests/impala/sources/funcType.impala`.)* Type definitions are introduced by their own keyword, **`functype`** *(decided - an
earlier draft reused `funcptr`, but `funcptr Name` already means a variable declaration, and a
keyword that means "variable" in one position and "type" in another is exactly the kind of dual
reading agents misparse; `functype` has zero identifier collisions in the corpus)*. The signature
syntax reuses the function-declaration grammar:

```impala
functype ProcessFn(int count, int pointer data) returns int   // a named funcptr TYPE
functype TickFn(int phase)                                     // no `returns` = void

ProcessFn cb                        // a funcptr of that signature
ProcessFn array handlers[8]         // composes with Step 1 modifiers for free
global TickFn onTick = tickHandler  // checked: tickHandler must match TickFn's signature
```

- **Bare `funcptr fp` stays valid** - untyped signature, 1.0 behaviour, the escape hatch parallel
  to bare `pointer`/`array`. `funcptr` never introduces types; `functype` never declares
  variables.
- Parameter names are optional (types-only allowed). This is the one place they are: `function` and
  `extern function` require a name on **every parameter and on the return**, so `function f(int)` and
  `function h(int a) returns int` are both `E001`. The three signature grammars do **not** mirror each
  other, and a `functype` is the only types-only form.
- Assignments and indirect calls through a named type are checked against its signature; the
  contract rides the existing `; signature` metadata channel for cross-unit checking. `nullfunc`
  remains assignable and testable.
- Named types are chosen over inline anonymous signatures (`funcptr(int) returns int fp`) because
  they compose cleanly with `pointer`/`array` modifiers and make structs and funcptrs the *same*
  mechanism: a named type is a `BASE_TYPE`. Anonymous inline signatures are omitted unless a real
  need appears.

---

## Step 4: Multiple return values (parked)

> **PARKED for Impala 3.0.** Implemented and then removed; the work is preserved on the
> `Impala3-byvalue-multireturn` branch. Impala 2.0 rejects a second return value (`E428`) and
> destructuring assignment (`E429`). Return extra results through pointer out-parameters instead.
> This section is kept as the design record. See [`docs/ParkedFeatures.md`](ParkedFeatures.md).

Impala 1.0 supports a single return value while GAZL supports many - the demo has apologized for
this since 2012 ("GAZL supports multiple 'OUT' variables per function and the intention is to
eventually support this in Impala too"). This step closes the gap. **The VM needs zero changes.**

### The calling convention (verified)

From `docs/InstructionSet.md` and the compiler's own output (`impala/testdata/perfTest2.expected.gazl`,
`src/UnitTest.gazl`):

- **Callee:** `OUT` declarations first, then `INP` declarations, in order
  (`fib`: `$x: OUTi` then `$y: INPi`).
- **Caller:** picks a window base `%b`, writes arguments at `%b+N...`, executes
  `CALL &f %b *size` where `*size` counts outputs *and* inputs (the `CALL` documentation says so
  explicitly), and reads results from `%b+0..%b+N-1`. The `fib` fixture even shows window
  *sliding*: a second call uses base `%1` so the first result parked in `%0` survives
  (`ADDi $x %0 %1`).

N returns simply occupy the first N window words. The convention was designed for this from the
start; only the Impala surface was missing.

### Syntax

**Callee - `returns` becomes a comma list**, mirroring `locals`:

```impala
function polarToRect(float mag, float phase)
returns float x, float y
{
	x = mag * cosApprox(phase)
	y = mag * sinApprox(phase)
}
```

**Caller - destructuring assignment, statement-level**, with `_` as the discard marker:

```impala
x, y = polarToRect(m, p)     // receive both
x, _ = polarToRect(m, p)     // keep x, discard y
_, y = polarToRect(m, p)     // discard x, keep y
polarToRect(m, p)            // bare call statement: discards all - 1.0 already
                             // idiomatically discards the return of a bare call
```

### Lowering

```gazl
polarToRect:	FUNC
	$x:			OUTf
	$y:			OUTf
	$mag:		INPf
	$phase:		INPf
; caller of "x, _ = polarToRect(m, p)":
	MOVf %2 $m
	MOVf %3 $p
	CALL &polarToRect %0 *4
	MOVf $x %0					; y at %1 discarded - no instruction emitted
```

Discarding is free: the callee writes its `OUT` slots regardless; the caller emits no `MOV` for a
skipped position.

### Rules

| Decision | Rule |
|---|---|
| Arity | all N positions must be written - `_` skips a *value*, never a *position*. Adding a return to a function breaks every call site loudly instead of silently shifting meanings. |
| `_` semantics | inside a destructuring LHS, `_` is unconditionally the discard marker - no scope lookup. Plain `_ = expr` outside destructuring remains an ordinary 1.0 assignment to a variable named `_` (corpus: zero real uses; all hits are inside string literals). |
| Multi-return call in an expression | error, with fix-it: "destructure the call". No silent dropping of values in value position. |
| Bare call statement | legal, discards all returns - consistent with 1.0's idiom for single returns. |
| LHS forms | any lvalue: locals, `global x`, `arr[i]`, `fp->field` (if Step 2 is adopted). |
| Single-return functions | completely unchanged - expressions, chaining, byte-identical output (N=1 *is* today's layout). |
| Named funcptr types | signatures extend naturally: `functype SplitFn(float in) returns float lo, float hi` (if Step 3 is adopted). |
| Metadata | the `->` row grows a tuple form: `; signature func polarToRect(float, float) -> (float, float)`; the validator checks return arity and types cross-unit; `unknown` stays the legacy wildcard. |
| Natives | host natives already write the window via `accessParams`, so multi-out natives are expressible; extend `docs/nativeCallbackSignatures.gazl` when a host wants one. |

### Compatibility

All new forms occupy previously rejected syntactic space: `returns a, b` and `x, y = f()` are 1.0
parse errors (comma is not an operator in either position). Single-return code paths are
byte-identical. Purely additive; no gating needed.

### Interaction with structs

A **by-value struct return *is* a multiple return** whose `OUT` slots carry names and offsets - the
same window layout. Step 4 is therefore a **prerequisite of by-value struct returns** (Step 2): one
multi-`OUT` implementation serves both `returns float l, float r` and `returns Filter out`. The
small-aggregate case (a stereo frame, a complex pair) is expressible either way - as explicit
scalars now, or as a named struct once Step 2 lands.

---

## Step 5: Import (implemented, except cycles)

*(Implemented: `import "path"` + `impala compile root.impala` walks the closure and compiles the
concatenated units in one pass - cross-unit structs, struct/multi-value returns, and functypes all
resolve with no header drift (visited-set dedups diamonds and breaks cycles). `export` marks
host-visible symbols in the `; signature` metadata, and `--dead-strip` drops any FUNC/data block not
reachable from an export. VM-verified in `tests/impala/sources/import/` and `tests/impala/sources/deadstrip/`.
The one deviation from the design below: the builder concatenates and compiles the sources rather than
emitting each unit separately. Two consequences - `.gazl` (precompiled-blob) imports are not yet
supported, and import cycles only half-resolve; see [Cycles](#cycles) and
[Deferred to 3.0: collect mode](#deferred-to-30-collect-mode-the-declaration-pre-pass).)*

### The problem

Impala has no `#include`: sharing declarations between units means textual copying - and the 2.0
type system makes that materially *worse* than 1.0. A 1.0 extern is an information-free one-liner
(`extern function foo`); a 2.0 interface is a struct layout plus typed signatures - a real,
drift-prone surface, hand-synchronized in N copies. The validator turns drift into loud errors
instead of silent garbage, but managing the pain is not removing it.

### The design: import source as interface

```impala
import "filter.impala"
```

The imported file is a **normal compilable unit - not a header**. The compiler parses it and takes
its interface:

- `struct`, `const`, and `functype` declarations enter scope directly;
- **function and global definitions are converted to typed extern declarations automatically**,
  with their full signatures (including multi-return, if Step 4 is adopted);
- `extern` and `extern native` declarations pass through as-is;
- its own `import`s are processed transitively;
- the importing unit's own `.gazl` output gains no code, data, or directives from the import -
  imported units are emitted once, as themselves, by the build (below).

There is exactly **one source of truth**: the struct and the functions live in `filter.impala` and
nowhere else. No second artifact exists to drift.

**Import is linking** *(decided - an earlier draft separated them; the user's observation "it
could be" is right, and the benefits compound)*. The `import` statements already describe the
complete program graph, so making the programmer restate that graph as a manual concatenation
list is redundancy with failure modes: a forgotten unit, a stale artifact, a wrong order. Instead,
the build is driven from a root unit:

```
impala compile main.impala → main.gazl      (the complete, linked program)
```

The toolchain walks the import closure (visited-set, cycles legal), compiles each unit exactly
once, and emits the concatenated program itself. **Concatenation still happens - the tool performs
it.** The GAZL assembler and loader are untouched; the transliterator property is untouched.
Consequences:

- **Staleness vanishes** for source imports: everything is compiled from source, together.
- `import "x.gazl"` drops a precompiled or hand-written unit into the closure as-is - its
  interface read from the `; signature` rows (and structural facts: `! DEF` values, `GLOB`
  sizes), its text emitted verbatim into the linked output. This is how third-party blobs,
  hand-written GAZL, and a precompiled stdlib participate.
- **The validator becomes internal to the build** - the link set *is* the closure, checked during
  compilation. The standalone `gazl-validate` remains for the legacy workflow only.
- **Single definition, enforced**: any symbol - and in particular any struct - defined more than
  once in the closure is an error. The copy-paste model and its layout-agreement machinery apply
  only to the legacy manual workflow.
- The legacy workflow (compile units separately, concatenate by hand, run the validator) remains
  fully supported - import-driven builds are the front door, not a replacement.

**Rejected alternative**, for the record: *C-style declaration files* (a hand-maintained
`filter_types.impala` of declarations only) - the C header disease; a second artifact that drifts
from the definitions it describes. Moves the copy, doesn't kill it.

### Semantics

- `import "path"` is a top-level statement; the path is a string literal resolved **relative to
  the importing file's directory**.
- Symbols arriving from two *different* files collide as duplicate declarations (the namespace is
  flat, as it already is for functions and globals across concatenated units). The same file
  reached via two paths is deduplicated by canonical path.
- **Valued constants are inlined** at the importing side; only the defining unit emits its
  `! DEF` directive. (Re-emitting `DEF`s from every importer would collide in the linked output.
  Consequence: retuning a shared constant by editing `.gazl` text is done in the defining unit's
  section - where it belongs.) Host-supplied valueless constants (`const int DEBUG`) emit
  references by name, unchanged.
- In an import-driven build there is nothing left for a separate validation step to check - the
  closure is compiled together and linked by the tool. The standalone `; signature` validator
  remains for the legacy manual-concatenation workflow, where stale artifacts and hand-assembled
  link sets are still possible.

### Cycles

Import cycles are **legal to write, and resolve in one direction** - the backwards reference needs a
forward `extern`. Read the rule box below before relying on this section. Cycles are not rejected
because mutual dependency is a supported pattern (concatenation is order-independent) and
mutually-dependent units are exactly the ones with the most shared interface surface - erroring on
them would push those units back to hand-written externs wholesale, the boilerplate this feature
exists to kill. Requiring one `extern` at the cycle edge keeps the rest of that saving.

The closure *walk* delivers on this: the builder keeps a **visited set keyed by canonical path**,
seeded with the root unit. Each file in the import closure is parsed exactly once and emitted
exactly once; an `import` naming an already-visited file is skipped. The self-import-via-cycle case
(B importing the root) needs no special rule - the seeding handles it. Diamonds dedupe the same way.

> #### The 2.0 rule: cycles GATHER but do not fully RESOLVE
>
> Settled 2026-07-29: this is Impala 2.0 behaviour, not a temporary state. The pre-pass that would
> lift it is deferred to 3.0 (below), because `extern` covers the cases that matter and lifting it
> later breaks nothing.
>
> The design assumed name resolution runs after the whole closure is gathered. **It does not.**
> The shipped builder concatenates the closure dependency-first and hands it to the
> single-pass compiler in one piece (the deviation noted at the top of this step), so a unit can
> only reference names that appear *earlier* in the concatenation. In a cycle no order satisfies
> both directions, so one of them fails:
>
> - a cross-unit function call backwards across the cycle -> `E403 Undeclared identifier`
> - a cross-unit struct type backwards across the cycle -> `E413 Unknown type`
>
> The asymmetry is the tell: the **same two files** build or fail depending only on which one you
> name as the root, because that decides the concatenation order. Fixture:
> `tests/impala/sources/importcycle/` (mutually recursive `isEven`/`isOdd`), both halves pinned in
> `impala/importBuildTests.js`.
>
> The diagnostic names the unit the text actually came from (multi-unit builds used to report the
> root unit and a line number that only indexed the concatenation) and adds a note pointing at the
> definition it cannot see yet, with the remedy that applies - a forward `extern` for a function or
> global, and for a type, that there isn't one short of breaking the cycle.
>
> **The 2.0 answer is a hand-written forward `extern`** in whichever unit is emitted first. It is
> the one place this feature does not remove boilerplate, but it is boilerplate 1.0 users already
> write, and since E437 it is checked against the real definition rather than silently trusted
> (`docs/ExternPrototypes.md`). A cross-cycle *struct type* is the one case with no workaround
> short of breaking the cycle.
>
> **Where this is heading: collect mode, in 3.0.** See "Deferred to 3.0: collect mode" below. An
> `extern` written today stays valid and keeps compiling once it lands - the pre-pass makes it
> unnecessary, never wrong.

What *does* error is definitional cycles in content - which are errors within a single file too;
imports merely let them span files:

| Cycle kind | Verdict |
|---|---|
| Import cycle (A↔B, any depth) | gathering: **legal** - visited-set memoization, each file parsed once. Resolution: **one direction in 2.0** - the other needs a forward `extern`; lifted in 3.0 by collect mode |
| Same file reached via two paths | legal - canonical-path dedup |
| Same symbol from two different files | error - flat namespace, duplicate declaration |
| Const *value* cycle across the closure | error, diagnostic cites the dependency chain |
| **By-value** struct containment cycle (`struct A { B b }` / `struct B { A a }`) | error (infinite size) - the mutual generalization of the self-reference rule |
| By-pointer struct cycle | legal, exactly like self-reference |

### Deferred to 3.0: collect mode (the declaration pre-pass)

This is the one piece of Step 5 that was designed and not built, and it is what would let a cycle
resolve in both directions. **Deferred to Impala 3.0 on 2026-07-29** (`docs/ParkedFeatures.md`):
`extern` covers the function and global cases, and landing the pre-pass later only *removes* the
need for those externs, so waiting costs nothing a 2.0 program has to unlearn. The design stands as
written - the full plan is `impala/Impala2Slices.md:143-190`; the essentials:

**It is NOT gated on the JSPEG rework.** `Impala2Slices.md:155-163` splits two-phase compilation in
two, and only the cheap half is needed here:

- *Declaration-level two-phase* - gather declarations across the closure, then resolve names.
  Bounded, and it is all cycles need.
- *Body-level two-phase* - the AST rework of `docs/JSPEGFuture.md` Problem 1. Cycles do **not**
  need it; still deferred. `JSPEGFuture.md:39-46` mentions interface mode as something that rework
  would unlock "as a trivial variant", which is easy to misread as a dependency. It is not one.

**Shape.** `$$parser` already *is* the semantic-handler object the grammar dispatches to, so this
is a mode on it rather than a second implementation of anything
(`Impala2Slices.md:147-154`):

- **emit mode** - today's codegen.
- **collect mode** - declarations register in the symbol/struct/type tables **with type references
  recorded by NAME, not eagerly resolved**; bodies are parsed but emit nothing.

**What actually remains**, given what shipped:

1. *Precondition, partly done* - finish thinning the remaining fat inline actions into `$$parser`
   methods, so the mode switch covers all the semantics. Roughly a quarter of the rule region
   dispatches to `$$parser.` today; the rest is still inline JS. `impala/RefactorPlan.md` is the
   adjacent (return-style helper) cleanup on the same surface.
2. *Suppress emission in collect mode* - the small half. Emission is already funnelled through a
   handful of chokepoints (`output`, `declare`, `makeMeta`, `emit`).
3. *Defer type resolution* - the expensive half. `declare`/`lookup` resolve types on the spot
   today, which is exactly why a backwards struct reference dies with `E413`.

**The shipped shortcut made this cheaper, not harder.** The original plan needed per-unit collect
parsing, a merge into closure-wide tables, and per-unit random seeds (mandatory, per
`Impala2Slices.md:174-179`, because two units sharing a seed collide on identical string
constants). None of that applies now: the builder hands the compiler *one* concatenated source
compiled *once*, so "gather" is a pre-scan of that single source and the seed-collision problem
cannot arise.

**Done when** `tests/impala/sources/importcycle/odd.impala` builds as a root, its
`extern function isEven` can be deleted, and the negative assertion in `impala/importBuildTests.js`
is rewritten as a positive one.

### Dead-code elimination and `export`

Because an import-driven build owns the whole program graph, it *can* drop code nothing reaches -
a firmware that `import "math.impala"` for `sqrt` need not ship `sin`/`cos`/`tan` and their tables.
This is link-time dead-code elimination (`ld --gc-sections` / `-dead_strip`), **not** a runtime
collector - Impala has no heap and nothing to collect at run time.

The hazard is "reachable from *what*?": Impala has no in-language `main`, and hosts call entry
points *by name* (`findFunction("process")`), so naive reachability would strip every firmware
entry point. The resolution is a **compile-time flag, off by default** - mirroring `--legacy`:

- **By default `impala compile` trims nothing.** Every unit in the closure is emitted whole, exactly
  like manual concatenation but tool-performed. An unmodified 1.0 firmware builds to a working
  program with no annotations. **100% backward compatible** - no positional "root is special" rule,
  no behavior that depends on which file is the build root.
- **`--dead-strip` enables trimming**, uniformly across *all* units including the root: any symbol
  that is neither `export`ed nor reachable from an `export`ed symbol is removed. This is the "I have
  annotated my host surface, strip the rest" switch, flipped only when the smaller/faster program
  is wanted. (Name honors Apple `ld`'s `-dead_strip`.)
- **`export` marks host-visible symbols** - functions and globals the host looks up or pokes:

  ```impala
  export function process()                 // the host calls this
  export global int array params[PARAM_COUNT]
  ```

  `export` is an additive keyword (zero corpus collisions) and is **always legal but only
  load-bearing under `--dead-strip`**; without the flag it is pure machine-checked documentation of
  the host contract (and rides the `; signature` metadata so tooling sees the retained surface).
  Adding `export`s therefore never changes behavior until trimming is requested - additive all the
  way down. It doubles as the answer to "which symbols does the host call?" - greppable, where
  agents will look.
- **Reachability is complete and static.** Impala has no name-based dispatch in-language, so the
  edge set is exactly: direct calls, address-taken functions (`&f`, funcptr assignment), function
  references in data (`DATA`/`! DEFp &f`), and global references. Same analysis as a linker's
  `--gc-sections`, not compiler cleverness.
- **Failure mode is loud, not silent.** Flip `--dead-strip`, forget an `export` on a host entry
  point, and it is trimmed → `findFunction` fails at load. Ugly but immediate, fixed by one
  `export`.
- **Never in the legacy manual workflow.** Hand concatenation stays byte-faithful; trimming exists
  only where the tool owns the link.

One caveat worth stating: trimming changes the global-section layout between builds, which matters
if host state serialization is layout-dependent - but *any* source edit already has that property,
so `--dead-strip` introduces no new class of hazard.

### Compatibility

`import` and `export` are new reserved words with **zero** identifier collisions in the 78-file
corpus (`include` has 8 uses and is avoided for that reason). Same policy as `struct` and `sizeof`:
a hypothetical wild source using one as an identifier fails loudly at parse with a rename as the
mechanical fix. The statement forms occupy previously rejected syntactic space, and both DCE and
strict `export` semantics are gated behind the default-off `--dead-strip` flag, so no existing
program's build behavior changes.

### What this unlocks

The same mechanism is the standard-library story the snippets-`.txt` model never had:
`import "math.impala"` in the root unit and `sin`/`sqrt`/`strlen` are declared, compiled, and
linked in - discoverable by strangers and agents from the source itself, with no per-firmware
copy-paste and no separate link list to maintain.

The two import forms follow one rule of thumb: *import the source of what you're building, import
the artifact of what you're using.* `.gazl` imports read interfaces from the `; signature` rows -
now carrying full element chains - so precompiled units participate with typed checking, and
"import what you link" holds by construction since the build emits exactly what it imported.

### Implementation coupling

Step 5 should be implemented **with or before Step 2 (structs)**: structs are new syntax with no
legacy copy-paste to protect, so if import lands first, the single-definition rule applies to
structs from day one and the layout-agreement machinery for duplicated struct definitions only
ever needs to exist in the legacy validator path.

---

## Inline functions (PARKED for 3.0 - not in Impala 2.0)

> **Status: parked, not available.** `inline function` was implemented, reviewed, and then taken back
> out; it now lives on the `GAZL2` branch. Writing `inline` is **`E439`**. An expansion has to place its
> locals with GAZL 2 `SCOP` / `ENDS`, and Impala 2 must keep running on GAZL 1.0 engines, which reject
> `SCOP` with `Unknown mnemonic`. See [`ParkedFeatures.md`](ParkedFeatures.md) and
> [`Inlining.md`](Inlining.md). **The codes below (`E432`-`E436`) are retired with the feature and must
> not be reused**, and its fixtures (`inlineEquivalence*`, `inlineFunctions`, `inlineReview*`) were
> removed. What follows is the design record for the parked feature, not 2.0 behaviour.

`inline` before `function` makes a function expand at each call site instead of being emitted once and
called:

```impala
inline function clamp(int v, int lo, int hi) returns int r {
	r = v;
	if (r < lo) r = lo;
	if (r > hi) r = hi;
}
```

The declaration is the whole interface. There is **no out-of-line copy** - no `FUNC` label, no body, no
`; signature` row - so the name exists only to expand. Every direct call is replaced by the captured
body; there is no size budget, no heuristic and no fall back to a real call. A call that cannot expand
is a diagnostic, never a silent reversion.

This is why `inline` does not violate design principle #1 ("2.0 adds no hidden optimization passes"):
it is an optimization pass, but it is opt-in by keyword and stated at the callee, not inferred. It is
also the reason the cost model above is worded as *predictable* rather than *countable* - see "The cost
model: dots are free".

Arguments are **substituted** where that is safe (literals, transparent caller locals) and
**materialized** otherwise; a global still pays its `PEEK`, and a computed argument keeps its window
slot. Locals become caller transients, which is what forces the compile-time size rule below. Nesting
is free - an inner inline is already expanded inside the outer's captured body - and an inline function
imported from another unit works, because the closure concatenates the defining unit first.

What is rejected:

| Shape | Code |
|---|---|
| Direct recursion | `E432` an inline function cannot call itself |
| `&f`, or using it as a funcptr value | `E435` cannot take the address of an inline function |
| `export inline function` | `E434` an inline function cannot be exported |
| Forward-declared, redeclared, or also declared `extern` | `E436` the inline function was already declared |
| ~~A local whose array extent is not a literal~~ | ~~`E433`~~ - deleted by the `.x.` rework; every extent is a named constant now |
| A call placed **before** the definition | `E403` undeclared identifier - there is no forward form to add |

Struct locals and array locals are fine; only a *non-literal* extent is not. The design spec is
[`docs/Inlining.md`](Inlining.md); the behavioural oracle was the fixture pair
`tests/impala/sources/inlineEquivalence.impala` and `inlineEquivalenceCall.impala`, which had to produce
identical output inlined and not. Both were removed when the feature was parked - restoring that oracle
(and the fuzzer's inline differential, which went with it) is part of unparking.

---

## Strict expressions: mixed bitwise operators

1.0 flattens `<< >> >>> & ^ |` into a single left-associative level; C ladders them internally
(`<<`/`>>` > `&` > `^` > `|`). Contrary to the reference doc's examples, 1.0 and C actually *agree*
on bitwise-vs-arithmetic (`x & 0xFF + 1` is `x & (0xFF + 1)` in both, since C's `+` also binds
tighter than `&`). The silent divergence is only **within the bitwise/shift family**:

```impala
a | b & c        // C: a | (b & c)     Impala: (a | b) & c    - divergent
a & b << 2       // C: a & (b << 2)    Impala: (a & b) << 2   - divergent
a & b | c        // (a & b) | c in both - left-assoc happens to match
```

**Rule:** mixing *different* operators from `{<< >> >>> & ^ |}` at the same parenthesization level
is a compile error: *"mixed bitwise operators require parentheses."* Same-op chains stay legal
(`a | b | c`). Parenthesized code is untouched. C's ladder is **never adopted** - no expression is
ever silently reparsed; code either compiles with its 1.0 meaning or errors with a mechanical fix.
Every accepted expression therefore reads identically to a C-trained human or agent.

**Gating - strict by default, one compiler argument to lower.** Impala sources carry no version
numbers, pragmas, or inferred dialects: **behavior is identical for every file.** The check is an
error by default, including for untouched 1.0 sources. A compile-time argument (e.g. `--legacy`)
downgrades strictness errors to stderr warnings for code that cannot be updated yet. Old code
should simply be updated - the fix is small, mechanical, and *meaning-preserving*: adding
parentheses that match the 1.0 left-associative parse yields the identical parse tree, so the
generated `.gazl` is **byte-identical** after the edit. The byte-diff acceptance gate is therefore
unaffected: parenthesized corpus sources produce the same bytes under both compilers.

**Corpus evidence:** exactly one line in all 78 `.impala` files mixes distinct bitwise operators
unparenthesized at top level - `ImpalaDemo.impala:216`, the line written to demonstrate the flat
precedence (duplicated in `tests/`). All real firmware sources parenthesize. That one line gets
parenthesized (identical output), and the demo text becomes a place to teach the 2.0 rule instead.

**Implementation sketch (JSPEG):** the `Bitwise` rule tracks the first operator of its own
invocation in a rule-local (`$first`) and reports later differing operators. Parenthesization
scoping is automatic - `Group <- '(' Expr ')'` recurses into a fresh `Bitwise` invocation with its
own `$first`. Strictness is known at startup (a runner argument), so detection resolves
immediately: `$$parser.fail(msg, $$s, $$i)` by default, an immediate stderr warning under
`--legacy`. No deferral machinery is needed.

**Adopted extension - bitwise vs comparison in conditions.** An unparenthesized bitwise/shift
operator directly against a comparison in a condition is the same error: `if (a & 3 == 0)` must be
written `if ((a & 3) == 0)`. Impala's own parse is the sane one (`(a & 3) == 0`), but a C-trained
reader misreads the unparenthesized form as `a & (3 == 0)`, which breaks the invariant that every
accepted expression reads identically to a C-trained reader - and invariants with one exception
stop being invariants. Corpus evidence: exactly one line in 78 files
(`rpm16_code.impala:156`, `if ((tmp = global clock) != clock & 0xFFFF)`), and it is a genuinely
divergent reading - Impala means `tmp != (clock & 0xFFFF)`; C's ladder would mean
`(tmp != clock) & 0xFFFF`. Same gating (`--legacy` downgrades to a warning), same
meaning-preserving parenthesization fix, byte-identical output after the edit.

---

## Compound assignment - rejected

The `<op>=` family (`+=`, `-=`, …) and `++`/`--` are **not adopted**. An earlier draft of this
document adopted them; the decision was reversed.

**The rule that decides sugar questions:** a second spelling is admitted only when the spellings
compile to **different GAZL** - i.e. when the syntax carries information. `a += 1` compiles to the
*identical* instructions as `a = a + 1`, so it would be two representations of the same thing,
leaving every author (and agent) wondering which one is preferred - neither is. It dilutes the 1:1
GAZL↔Impala feel for zero information. Contrast `.`/`->`, which were kept precisely because they
compile *differently* (a free constant offset vs a real load) - that split is a cost annotation,
not sugar.

(The single-evaluation argument - `a[f()] += 1` calling `f` once - was considered and does not
outweigh this: it makes `+=` *semantically different* from the longhand in exactly the cases where
readers would assume it's the same, which is its own trap.)

---

## Diagnostics

The error format is part of the language's contract with its audience - AI agents iterate against
diagnostics, so the format is specified, stable, and machine-parseable. **Implemented:**

```
foo.impala:12:9: error[E201]: Pointer element type mismatch (expected int elements, got float elements)
        p = fp;
              ^
foo.impala:12:9: note: use a cast: (int pointer)
```

- **GCC-style line format** (`path:line:col: severity[code]: message`), followed by the source line
  and a caret, followed by `note:` lines carrying mechanical fix-its. `--legacy` renders the same
  shape with `warning[…]` severity. The NuXJS CLI prints warnings as `;`-prefixed comment lines so
  a `-` stdout stream stays valid GAZL.
- **Stable error codes**, never reused; message wording stays free to improve.
- **First-error stop.** The compiler is single-pass with immediate code generation; error recovery
  in that architecture produces cascading nonsense. One correct error beats five speculative ones.
- A structured `--json` output mode can be added later if tooling demands it; the line format is
  the contract. **It does not exist today** - the complete flag set is `--legacy` and `--dead-strip`.

### Code registry

| Code | Meaning |
|---|---|
| E001 | syntax error (parse failure; expected-set reporting is future JSPEG work) |
| E101 | mixed bitwise operators require parentheses |
| E102 | comparison mixed with bitwise operators requires parentheses |
| E103 | `!` binds below comparison; its operand must be parenthesised (`!(a == b)`) |
| E201 | pointer element type mismatch in assignment |
| E202 | pointer element type mismatch in call argument |
| E203 | element type mismatch with previous declaration |
| E204 | plain `[]` on a struct element - write `[[ ]]` |
| E205 | scaled `[[ ]]` on a one-word element - write `[]` |
| E301 | invalid operand types for operator |
| E302 | invalid operand type for unary operator |
| E303 | incompatible types for assignment |
| E304 | return type disagreement (mismatch / conflicting expectations / previous uses) |
| E305 | `for` variable must be a local modifiable int or pointer |
| E306 | `switch` expression must be int |
| E307 | arithmetic on a struct pointer - move it with `&p[[i]]` |
| E308 | difference between struct pointers - divide by `sizeof` yourself |
| E309 | `for` variable is a struct pointer - `FORp` cannot stride |
| E401 | identifier already declared |
| E402 | type mismatch with previous declaration |
| E403 | undeclared identifier |
| E404 | invalid lvalue |
| E405 | invalid argument count |
| E406 | argument type mismatch |
| E407 | constant expression expected |
| E408 | invalid type for function call |
| E409 | `default` case already defined |
| E410 | struct already defined |
| E411 | duplicate field in struct |
| E412 | field has an incomplete struct type (define it, or use a pointer) |
| E413 | unknown type |
| E414 | an initialized struct-element array needs a literal size |
| E415 | field access requires a struct or struct pointer |
| E416 | wrong field operator (`.` on a pointer, or `->` on a value) |
| E417 | struct has no such field |
| E419 | `sizeof` of an incomplete struct |
| E420 | whole-struct assignment needs a struct on both sides / struct type mismatch |
| E421 | a struct value needs a brace initializer / struct type mismatch in a call argument |
| E422 | malformed brace initializer (too many braces, type mismatch, or missing nesting) |
| E423 | cannot access a field directly on a returned struct value |
| E426 | passing a struct by value is not supported in Impala 2.0 |
| E427 | returning a struct by value is not supported in Impala 2.0 |
| E428 | multiple return values are not supported in Impala 2.0 |
| E429 | destructuring assignment is not supported in Impala 2.0 |
| E430 | an `extern struct` array field must not state a size |
| E431 | array needs a size |
| E432 | *retired with `inline function`* - do not reuse |
| E433 | *retired with `inline function`* - do not reuse |
| E434 | *retired with `inline function`* - do not reuse |
| E435 | *retired with `inline function`* - do not reuse |
| E436 | *retired with `inline function`* - do not reuse |
| E437 | `extern` declaration of a function disagrees with its definition, or with another `extern` |
| E438 | `extern struct` declarations disagree, or disagree with the definition |
| E439 | `inline function` is parked for 3.0; it needs GAZL 2 `SCOP`/`ENDS` |
| E440 | type name already used by a struct / `functype` redeclared with a different shape |
| E441 | function or value does not match the funcptr type |
| E442 | malformed argument list |
| E443 | duplicate `case` value |
| E444 | `case` value outside the switch range |
| E445 | `goto` to an undefined label |
| E446 | a label is defined twice in one function |
| E447 | a `const` cannot be a struct value (use a struct pointer) |
| E448 | `return` does not take a value; assign to the named return variable, then `return;` |
| E449 | `return`/`break`/`continue` is a reserved word and cannot name a label (a warning under `--legacy`) |
| E450 | `break`/`continue` is not supported; exit or repeat a loop with `goto` to a label |
| E451 | a `;` after an `if` body leaves the following `else` with nothing to attach to |
| E452 | a `global` prefix on a function or a const (a warning under `--legacy`) |
| E453 | `export` on a valueless `const`; the two contradict (a valued `export const` is fine) |
| E454 | a struct field initialized after one whose symbolic extent makes its offset uncountable |
| E455 | a struct initializer must name its fields, and must not mix named with positional (`--legacy` maps by position) |
| E456 | a struct initializer names a field the struct does not have |
| E457 | a struct initializer names the same field twice |
| E458 | a `field:` name in an array slot, where the index already does the naming |

E418, E424 and E425 are **not allocated to anything that fires**. They were reserved for extern-struct
guards that were never needed once the features shipped (`docs/StructLayoutConstants.md` records the
correction); they stay burned rather than reused, per "stable error codes, never reused".

E432-E436 are a different case: they DID fire, and were retired when `inline function` was parked. They
are burned too. (This paragraph used to list **E439** as unallocated - that is now wrong: E439 is the
live diagnostic that rejects `inline`.)

---

## Open questions

- **Adoption of Steps 2-5 themselves.** Structs, typed function pointers, multiple return values,
  and import are worked proposals, not commitments: their syntax, semantics, lowering, and
  identity rules are specified above so the adoption decision can be made on a concrete design -
  but that decision has not been made. The committed scope is Step 1 plus the cross-cutting rules
  (strict expressions, the compound-assignment rejection, diagnostics).
- Name of the strictness-lowering compiler argument (`--legacy` is the working name).
- By-value struct parameters/returns: deferred, revisit if the small-struct performance case
  materializes in real firmware (see Step 2, *Passing, returning, copying*).
