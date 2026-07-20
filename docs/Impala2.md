# Impala 2.0 Design

> **Status: partially implemented.** Step 1 (typed pointers and arrays) and the strict-expression
> rules are implemented in the JSPEG compiler with `--legacy` gating; see `tests/impala/sources/`
> (`typedPointers.impala`) and the regression suites. Element types are enforced within a unit at
> assignments and call arguments (full depth), and across units via element chains in the
> `; signature` channel (arrays, globals, consts, function params/returns; the validator errors on
> mismatched chains and bridges bare `ptr` to any chain). Steps 2–5 remain proposals.

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

2. **Types are a zero-cost compile-time overlay.** This is the spine of the whole release. Every
   type in 2.0 **erases to exactly the GAZL that 1.0 would emit.** Types exist so the *compiler*
   stops throwing away information it already computes at parse time — not to change what runs.
   1.0's original sin is discarding known types (its per-slot argument types are recorded and then
   dropped), which is why a whole external `; signature` metadata channel and `gazl-validate` pass
   had to be bolted on *after* codegen. 2.0 keeps the types instead.

3. **Audience: strangers and AI agents.** 2.0 is designed to be written by people who have never
   seen Impala and by AI agents generating code. That drives concrete choices:
   - **Regularity over cleverness** — one obvious way, minimal special cases.
   - **Match convention or hard-error, never silently differ.** Where Impala resembles C, it must
     either behave like C or reject the construct — never quietly mean something else. (The 1.0
     bitwise-precedence inversion, where `x & 0xFF + 1` parses as `x & (0xFF + 1)`, is exactly the
     silent-difference trap to avoid.)
   - **Types as guardrails** — an agent produces type errors constantly; the compiler should turn
     them into crisp, machine-readable diagnostics instead of silent runtime garbage.
   - **No tribal knowledge** — features must be discoverable from the source, not from a snippet
     `.txt` file the reader has to already know exists.

4. **Backward compatibility: never silent, and additive where possible.** Silently changing the
   meaning of any 1.0 construct is absolutely forbidden. New *syntax* is purely additive — every
   existing `.impala` source parses as before and compiles to byte-identical `.gazl` (a gate, not
   an aspiration; see [Backward compatibility](#backward-compatibility)), with exactly one
   sanctioned deviation: type refinement may upgrade an untyped `MOVE` to the typed variant of the
   same operation (`MOVi`/`MOVf`/`MOVp`) — semantically identical instructions, verified
   variant-only before regolding. New *strictness rules* may reject old code, but only **loudly**,
   only with a **mechanical, meaning-preserving fix** (byte-identical output after the edit), and
   always with a compile-time argument (`--legacy`) that downgrades the errors to warnings.
   Behavior never varies per file: no version markers, no pragmas, no dialect inference — one
   language, one set of rules, one escape flag at the invocation.

## Roadmap

The features are ordered by dependency, not ambition:

1. **Typed pointers and arrays** — *this document covers this step in depth.* A typed array is the
   degenerate case of a struct (one repeated field type), so the "slots carry a type, and the
   compiler picks the typed instruction from the slot type" machinery must exist first.
2. **Structs** — named heterogeneous layouts over words. Introduces the first multi-word elements,
   which is what changes pointer stride. Proposed design in
   [Step 2: Structs](#step-2-structs-proposed).
3. **Typed function pointers** — named signature types for `funcptr`, riding the existing
   `; signature` metadata channel. Proposed design in
   [Step 3: Typed function pointers](#step-3-typed-function-pointers-proposed).
4. **Multiple return values** — closing a known 1:1 gap: GAZL has supported multiple `OUT` words
   per function since 1.0, and Impala never exposed it. Proposed design in
   [Step 4: Multiple return values](#step-4-multiple-return-values-proposed).
5. **Import** — sharing typed interfaces between units without textual copying. Proposed design in
   [Step 5: Import](#step-5-import-proposed).

Cross-cutting decisions — strict expressions, the rejection of
[compound assignment](#compound-assignment--rejected), and the [diagnostic format](#diagnostics) —
have their own sections below.
Other long-standing gaps (richer `for`) are out of scope for this document and tracked separately.

---

## Step 1: Typed pointers and arrays

### The idea

A pointer or array gains a **compile-time element type**. At runtime it is still exactly what it was
in 1.0 — a word (pointer) or N words (array). The element type is information the compiler carries so
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
backtracking — deliberately unlike C's inside-out declarators (`int **p`, `int (*fp)(int)`), which
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
  therefore change pointer arithmetic **not at all** — `p + 3` is still three words. (Stride only
  stops being 1 when elements become multi-word, i.e. structs — which is precisely why structs come
  later.)
- **The compiler selects the typed instruction from the element type.** Reading `a[i]` where `a` is
  `int array` yields an `int` and emits the int-typed peek; where `a` is `float array` it yields a
  `float` and emits the float-typed peek. No cast required, and the result type flows into
  surrounding type-checking.
- **Array-to-pointer decay is unchanged.** Passing an `int array` where an `int pointer` is expected
  works as in 1.0; the element types must agree (or the target is untyped — see below).

### The untyped escape hatch

Omitting the base type gives the 1.0 behaviour: **untyped, raw-word storage.**

```impala
pointer raw            // untyped pointer — the "word*/void*" of Impala
array scratch[256]     // untyped array
```

Untyped pointers/arrays are where reinterpretation legitimately lives — host/native boundaries such
as `read(offset, count, values)` fill raw words. Because typing is opt-in per declaration, an
untyped value requires a cast to enter the typed world, which makes every cast a **loud, greppable
signal that you are leaving the type system** — the desired inversion of 1.0, where casts are
everywhere and meaningless.

### What it buys: the casting churn disappears

This is the payoff, and it's a direct hit on the biggest 1.0 pain. From the demo's `findSmallest`:

```impala
// 1.0 — every untyped element access needs a cast
function findSmallest(int n, pointer vector) returns int j locals int i {
    j = 0
    for (i = 1 to n)
        if ((int) vector[i] < (int) vector[j])   // casts required
            j = i
}

// 2.0 — the element type is known
function findSmallest(int n, int pointer vector) returns int j locals int i {
    j = 0
    for (i = 1 to n)
        if (vector[i] < vector[j])               // no casts
            j = i
}
```

Real firmwares benefit the same way: `global array params[PARAM_COUNT]` is all ints, so today every
`global params[X]` read is cast. Type the array once (`global int array params[PARAM_COUNT]`) and the
casts vanish at every use site — while the emitted GAZL is identical.

### Casts generalize with the type grammar

A cast names a type using the same grammar, so it extends naturally:

```impala
p = (int pointer) raw          // reinterpret raw words as a pointer to int
h = (funcptr array) table      // ... and (Filter pointer) once structs exist
```

`(pointer)` and `(int)` continue to mean exactly what they mean in 1.0 (the zero-modifier cases).

### The typed–untyped boundary

If typed code needed a cast every time it touched untyped code (natives, 1.0 units, raw buffers),
casts would stop being signals and become 1.0-style noise again. If everything flowed implicitly,
the types would be decorative. The resolving principle is an **asymmetry**:

> **Erasing type information is silent. Assuming type information is loud.**

Dropping to untyped cannot create a wrong belief — the consumer treats words as words on purpose.
Climbing to typed *mints a claim* the compiler cannot verify, and unverifiable claims are exactly
what should be greppable. The rules:

| Flow | Rule | Example |
|---|---|---|
| `T pointer` → `pointer` | **implicit** (erase) | `read(off, n, samples)` with `samples : float pointer` — no cast |
| `T array` → `T pointer` | **implicit** (decay — 1.0's existing decay, now typed) | `findSmallest(n, mydata)` |
| `T array` → `pointer` | **implicit** (decay + erase) | passing a typed buffer to a native |
| `pointer` → `T pointer` | **explicit cast** | `fp = (Filter pointer) alloc(...)` |
| `T pointer` → `U pointer` | **explicit cast** (reinterpret) | `(int pointer) floatBuf` — legal, loud |
| `T pointer`/`T array` element, field read | no cast — the whole point | `vector[i]`, `fp->mode` |
| untyped word → typed scalar | **1.0's rules, unchanged** (frozen by the byte-diff gate) | `z = lfoVal(...)` stays legal; arithmetic still needs `(int)` |
| int ↔ float | **never by cast** — `itof`/`ftoi` only; `(float pointer)` on an `int pointer` reinterprets words, converts nothing | |
| named funcptr type → `funcptr` | implicit (erase) | |
| `funcptr` → named funcptr type | **explicit cast** (mints a signature claim) | |
| data pointer ↔ `funcptr` | **never** (1.0 wall — different address spaces in GAZL) | |
| `null` / `nullfunc` | implicit into any pointer / funcptr type | |

**Comparisons never element-check** (revised during implementation — an earlier draft made
`T pointer` vs `U pointer` an error). Two reasons: a comparison mints no lasting assumption, so
there is nothing to state loudly; and since `&` is type-producing, 1.0 sources that legitimately
compare addresses of differently-typed globals (`&global aFloat == &global marker`) would
otherwise become errors, violating compatibility. Pointer comparisons erase, always.

Sideways reinterpretation (`T pointer` → `U pointer`) takes a **single cast** — no forced
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
(`MOVi`/`MOVf`/`MOVp` — e.g. `x = ("0123456789abcdef")[v & 0xf]`). These are **semantically
identical instruction variants**, and they are the *only* permitted deviation from byte-identity;
every such golden change is verified to differ in MOV-variant mnemonics alone before regolding.

### Cross-unit checking

An `extern int array futureArray` now advertises an element type, so the `; signature` metadata that
already rides inside `.gazl` carries element information. *Implemented:* definition-side rows emit
**element chains as single-token categories** — `; signature array values[5] : int`,
`; signature global cursor : int-ptr`, `func take(int-ptr-ptr h) -> int-ptr`, const rows likewise —
while untyped declarations keep their 1.0 categories (`ptr`, `unknown`). The validator treats a bare
`ptr` as an element-unknown pointer that matches any chain (the metadata-level analogue of the
erase/assume asymmetry: the typed side states the claim, the untyped side is a wildcard), and
errors on differing chains. Call-site `; expects` rows deliberately keep bare categories: `&` is
type-producing, so 1.0 sources can pass element-carrying arguments, and chain-rendering there would
churn old units' output — the bridge rule makes bare-vs-chain compatible, so nothing is lost.
The concatenation-linking model is untouched; single-token chains keep every row parseable by
older tooling.

The boundary asymmetry has an exact link-level analogue via that wildcard. A typed
`extern int array data` in one unit binding an untyped `array data` definition in another is
*concrete-vs-unknown* — the validator lets it match. That is not an unmarked assumption: **the typed
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
   *parse error* in 1.0 today — a `VarDecl` is `BASE_TYPE Identifier`, and `pointer` is not an
   identifier. Assigning meaning to previously-rejected input cannot change any program that
   currently compiles.
2. **The modifier loop cannot reinterpret or greedily absorb existing declarations.** It consumes
   `pointer`/`array` keywords only between the base type and the name, and the name is always a
   non-keyword (`!KEYWORD`), so the loop stops there. The only tokens it could otherwise grab are
   keywords appearing where 1.0 expected the name — which 1.0 already forbade.

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

## Step 2: Structs (proposed)

> Proposed design — settled at the syntax/semantics level, not scheduled for implementation until
> Step 1 lands. Recorded in full so decisions made here aren't relitigated by accident.

A struct is a **named heterogeneous layout over words** — pure naming sugar over the offset
constants firmware authors hand-roll today (`const int Filter_cutoff = 0` plus casts). It erases to
the same GAZL and deletes the casts.

### Definition

A struct body is a run of declarations using **exactly the Step 1 declaration grammar**, one per
line, semicolons optional (as for globals). No new syntax appears inside the braces:

```impala
struct Filter {
	float cutoff                    // word offset 0
	float resonance                 // word offset 1
	float array state[4]            // offsets 2..5 — inline fixed array field
	int mode                        // word offset 6
	Filter pointer next             // word offset 7 — self-reference legal by pointer
}                                   // sizeof(Filter) = 8
```

Fields are laid out in declaration order, one word per primitive/pointer slot, array fields inline.
A by-value self-referential field (`Filter next`) is an error (infinite size); `Filter pointer` is
fine.

**Inline nesting is supported** *(decided)*: fields may be struct-typed by value, and offsets
compose — `v.low.z1` is still a single free constant offset. By-value recursion (direct or mutual)
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
fields (and pointer declarations) before — or without — its definition. This is single-pass
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

`sizeof(Filter)` is a new compile-time `int` (C-style spelling; the type-name form only — a
`sizeof expr` form can come later if needed). New reserved words `struct` and `sizeof` have
**zero identifier collisions** in the 78-file corpus (verified; note `type` has 23 uses as an
identifier and must never become a keyword). The `.` character appears in no existing source
outside float literals, so the member operator is collision-free too.

### Initializers

*(decided)* Global and readonly struct variables (and struct arrays) take **nested-brace
initializers**, one brace group per struct or array field, each value checked against the field's
type. Values must be compile-time constants, as for 1.0 globals; trailing fields may be omitted
and are zero-filled (the 1.0 array rule). Uninitialized struct storage is zero-filled.

```impala
global Filter voice = { 0.5, 0.7, { 0.0, 0.0, 0.0, 0.0 }, 2, null }
readonly Voice array PRESETS[2] = {
	{ { 0.1, 0.0, 0.0, 0.0 }, { 0.2, 0.0, 0.0, 0.0 }, 1.0 },
	{ { 0.3, 0.0, 0.0, 0.0 }, { 0.4, 0.0, 0.0, 0.0 }, 0.5 }
}
```

The lowering is the flat `DATA` row the braces describe — the nesting exists for readability and
per-field type checking, not for any runtime structure.

### Field access: `.` and `->`

- **`.` applies to a *place*** — a struct value (`f.cutoff`), or a parenthesized dereference
  (`(*fp).cutoff`). It never applies directly to a pointer.
- **`->` applies to a typed struct pointer** and is defined as sugar: `fp->cutoff` ≡ `(*fp).cutoff`.
  Both spellings are legal and compile byte-identically; `->` is idiomatic.
- **`*fp` is a place, not a load.** It denotes the pointed-at struct and emits nothing by itself;
  the consuming operation decides the instruction. Bare `*fp` in scalar context is an error (a whole
  struct doesn't fit an expression slot).
- Wrong operator is a hard error with the fix in the message (`` `fp` is a pointer — use `->` ``).
- Untyped `pointer` has no fields (no struct type) — the escape hatch stays cast-first.

Multi-level pointers compose from existing pieces; nothing new is invented:

```impala
Filter pointer pointer pp
x = pp[0]->cutoff                   // or (*pp)->cutoff — 2 markers, 2 loads
```

### The cost model: dots are free

This is the property that justifies the `.`/`->` split (which is *not* C ceremony here — in Impala
it is a cost annotation with exact GAZL meaning):

- **`.` contributes zero instructions, always.** It is compile-time offset arithmetic, folded into
  operand syntax. `f.a.b.c` nested arbitrarily deep is still free.
- **`->` contributes exactly one `PEEK`/`POKE`, always.**

Combined with the existing markers, every memory access in an expression is visible in the source:

> **Instruction count = marker count.** Each `global`, `*`, `->`, and `[]`-on-a-pointer costs one
> load. Dots are free.

```impala
f.cutoff                    // 0 loads — direct operand
fp->cutoff                  // 1 load
global voice.cutoff         // 1 load — from `global`; the . is still free
global gfp->cutoff          // 2 loads — one for the global pointer, one through it
head->next->next->value     // 3 loads — each hop visible
```

(The one grandfathered exception: 1.0's `p[i]` PEEKs without a distinct marker vs `a[i]`'s local
access — nailed by backward compatibility. New syntax is held to the stricter rule.)

### Verified lowering

Checked against the GAZL operand grammar and live usage in `src/UnitTest.gazl`. The key mechanisms:
the `local:const` operand form (a local plus compile-time offset is a *direct operand* — no address
materialization), `GETL`/`SETL` (local access with runtime offset, no pointer involved), and
`PEEK`/`POKE` with a constant-offset immediate.

```gazl
; A) local value, constant field — direct operand, zero extra instructions
$f: LOCA *8
MOVf $f:1 #0.5              ; f.resonance = 0.5
MOVf $x  $f:1              ; x = f.resonance

; B) local struct array, dynamic index — GETL with the field offset folded into the
;    BASE OPERAND (precedent: UnitTest.gazl:794 "GETL i0 lArray:4 i1"). No ADDi.
$voices: LOCA *64
MULi %0 $i #8              ; i * sizeof(Filter) — the stride multiply
GETL $x $voices:6 %0      ; x = voices[i].mode   (constant :6 rides the operand)

; C) through a pointer — one real load, constant offset immediate
PEEK $x $fp #6            ; x = fp->mode
POKE $fp #6 $val         ; fp->mode = val

; D) dynamic index through a pointer — the one place an ADDi survives,
;    because PEEK's offset operand is spent on the computed index
MULi %0 $i #8
ADDi %0 %0 #6
PEEK $x $fp %0            ; x = fp[i].mode
```

Honest cost summary (the claim is "no worse than hand-rolled offsets, casts deleted" — *not*
"free" and *not* "optimal"):

| Access | Cost vs hand-written 1.0 |
|---|---|
| Local value, constant field | identical — single direct instruction |
| Local array, dynamic index | identical — stride `MULi` + `GETL`/`SETL` |
| Pointer, constant field | identical — one `PEEK`/`POKE` |
| Pointer, dynamic index | identical — `MULi` + `ADDi` + `PEEK` |

The stride `MULi` is real and is the only cost flat untyped arrays don't pay; it exists in the
hand-rolled `voices[i*8 + 6]` version too. A local struct is always one contiguous `LOCA` (fields
are direct `:const` operands), so taking `&f` is a plain `ADRL` with no representation change —
codegen never depends on how the struct is used elsewhere in the function.

### Passing, returning, copying

**Both by-value and by-pointer are legal, chosen by the parameter/return declaration** *(decided
2026-07-20)*. An earlier draft deferred by-value out of a vague "one convention beats two" instinct;
the `a = b` decision already demolished that — a by-value struct is the *same* `COPY *sizeof`
instruction at a call boundary, and by the project's own sugar rule the two modes are not two
spellings of one thing: `f(v)` and `f(&v)` compile to different GAZL (an N-word copy vs one pointer
word) with different semantics (isolation vs aliasing). Keeping both is the rule that killed `+=`
working *for* us.

- **By-value parameter** (`function tick(Filter f)`): the caller copies `sizeof(Filter)` words into
  the callee's parameter window; inside the callee the fields are **direct, free window operands**
  (no `PEEK`). The window is **read-only**, inheriting 1.0's `INP`/parameter semantics — you cannot
  assign to `f` or a field of it, and you cannot take `&f` of a parameter (already illegal in 1.0).
  This makes by-value the read-only, isolated, cache-friendly mode; it is *strictly faster* than a
  pointer for hot inner loops on small structs (stereo frames, complex pairs, biquad coefficients).
- **By-pointer parameter** (`function tick(Filter pointer f)`): one pointer word; fields are `PEEK`
  through the pointer; the callee can mutate the caller's struct and alias it. This is the
  mutation/aliasing mode.
- **No decay in either direction.** `tick(v)` where the parameter is `Filter pointer` is an error
  (`note: pass a pointer: &v`); `tick(&v)` where the parameter is `Filter` is an error
  (`note: pass the value: v`). The call site therefore always shows which mode is in effect —
  more marker-honest than C, where the same `f(v)` silently copies or not depending on the
  callee. (Array *fields* still follow 1.0 array decay: `f.state` yields a typed pointer.)
- **By-value return** (`returns Filter out`): N `OUT` words — a struct return *is* a multiple return
  whose slots carry names and offsets. The callee writes `out.cutoff` as free direct `OUT`-slot
  access; the caller consumes it into a struct place, statement-level (`v = makeFilter(...)`), the
  same restriction as `a = b`. **This makes [Step 4 (multiple return values)](#step-4-multiple-return-values-proposed)
  a prerequisite of struct returns** — same multi-`OUT` window layout, one implementation. By-value
  *parameters* need only `PARA` sections and can land without Step 4; by-value *returns* need it.
- **Whole-struct assignment `a = b` is allowed, statement-level only.** It lowers to exactly one
  `COPY *sizeof(T)` — the instruction one would hand-write — with the size known at compile time
  from the declared types. It is not an expression: a struct value does not fit an expression slot,
  so `a = b = c` and struct assignment nested in a larger expression are errors. `*dst = *src`
  (both typed struct pointers) is the same statement through places. Explicit `copy()` remains
  available and equivalent.
- **Cost model in one sentence:** a call copies `sizeof` words for each by-value struct argument
  and each by-value return, exactly like `a = b`; a by-pointer argument copies one word and pays a
  `PEEK` per field access. Both are visible at the call site.
- **Whole-struct comparison is rejected.** GAZL has no multi-word compare; `a == b` on struct
  values is an error — compare fields, or compare pointers.

### Identity across concatenation

Impala has no `#include`: sharing a struct between separately compiled units means **textually
re-declaring it** in each source (the established copy-paste model). Nothing in the assembled
output forces the copies to agree — types erase completely — so offset drift between two copies of
a struct is silent runtime garbage unless the metadata channel catches it. It does:

**In an import-driven build (Step 5), the rule is stricter and simpler: a struct is defined
exactly once in the closure** *(decided)* — a second definition anywhere is an error, agreeing or
not. Single source of truth, enforced. Everything below applies only to the **legacy
manual-concatenation workflow**, where units may still carry textual copies:

**Identity is nominal, layout-verified.** The struct *name* is the identity. GAZL's namespace is
already flat (function and global names collide across concatenated units today; struct names join
that club), and every definition of the same struct name in a linked set must agree **exactly**:
field count, field order, per-field name, per-field type, and array sizes. Any disagreement is a
validator error citing both source locations. Field *names* are deliberately part of the match: in
a copy-paste culture, a renamed field is drift evidence even when the layout still happens to
agree.

Structural identity (same layout ⇒ same type, names irrelevant) is rejected: a `Filter` and a
`Voice` that both happen to be `{float, float, int}` must not silently interchange — accidental
compatibility hides bugs, and no language the agent audience is trained on works that way.

**Metadata.** Struct definitions emit no GAZL instructions, so each definition contributes a
standalone comment row (the channel already has standalone rows for externs):

```gazl
; signature struct Filter { float cutoff, float resonance, int mode } @ a.impala:3:1
```

Function rows reference struct names as new type atoms — `; signature func setMode(Filter pointer)
-> void` — and the validator resolves them against the merged struct rows using today's machinery.
Nested struct references (`Filter pointer next`) resolve by name, recursively.

**Opaque structs.** A unit that only passes a `Filter pointer` *through* — no field access, no
`sizeof` — does not need the layout copy:

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

## Step 3: Typed function pointers (proposed)

Mirrors structs exactly: **named funcptr types that become base types** — the typedef the language
never had. Type definitions are introduced by their own keyword, **`functype`** *(decided — an
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

- **Bare `funcptr fp` stays valid** — untyped signature, 1.0 behaviour, the escape hatch parallel
  to bare `pointer`/`array`. `funcptr` never introduces types; `functype` never declares
  variables.
- Parameter names are optional (types-only allowed), mirroring `extern`/`function`.
- Assignments and indirect calls through a named type are checked against its signature; the
  contract rides the existing `; signature` metadata channel for cross-unit checking. `nullfunc`
  remains assignable and testable.
- Named types are chosen over inline anonymous signatures (`funcptr(int) returns int fp`) because
  they compose cleanly with `pointer`/`array` modifiers and make structs and funcptrs the *same*
  mechanism: a named type is a `BASE_TYPE`. Anonymous inline signatures are omitted unless a real
  need appears.

---

## Step 4: Multiple return values (proposed)

Impala 1.0 supports a single return value while GAZL supports many — the demo has apologized for
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

**Callee — `returns` becomes a comma list**, mirroring `locals`:

```impala
function polarToRect(float mag, float phase)
returns float x, float y
{
	x = mag * cosApprox(phase)
	y = mag * sinApprox(phase)
}
```

**Caller — destructuring assignment, statement-level**, with `_` as the discard marker:

```impala
x, y = polarToRect(m, p)     // receive both
x, _ = polarToRect(m, p)     // keep x, discard y
_, y = polarToRect(m, p)     // discard x, keep y
polarToRect(m, p)            // bare call statement: discards all — 1.0 already
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
	MOVf $x %0					; y at %1 discarded — no instruction emitted
```

Discarding is free: the callee writes its `OUT` slots regardless; the caller emits no `MOV` for a
skipped position.

### Rules

| Decision | Rule |
|---|---|
| Arity | all N positions must be written — `_` skips a *value*, never a *position*. Adding a return to a function breaks every call site loudly instead of silently shifting meanings. |
| `_` semantics | inside a destructuring LHS, `_` is unconditionally the discard marker — no scope lookup. Plain `_ = expr` outside destructuring remains an ordinary 1.0 assignment to a variable named `_` (corpus: zero real uses; all hits are inside string literals). |
| Multi-return call in an expression | error, with fix-it: "destructure the call". No silent dropping of values in value position. |
| Bare call statement | legal, discards all returns — consistent with 1.0's idiom for single returns. |
| LHS forms | any lvalue: locals, `global x`, `arr[i]`, `fp->field` (if Step 2 is adopted). |
| Single-return functions | completely unchanged — expressions, chaining, byte-identical output (N=1 *is* today's layout). |
| Named funcptr types | signatures extend naturally: `functype SplitFn(float in) returns float lo, float hi` (if Step 3 is adopted). |
| Metadata | the `->` row grows a tuple form: `; signature func polarToRect(float, float) -> (float, float)`; the validator checks return arity and types cross-unit; `unknown` stays the legacy wildcard. |
| Natives | host natives already write the window via `accessParams`, so multi-out natives are expressible; extend `docs/nativeCallbackSignatures.gazl` when a host wants one. |

### Compatibility

All new forms occupy previously rejected syntactic space: `returns a, b` and `x, y = f()` are 1.0
parse errors (comma is not an operator in either position). Single-return code paths are
byte-identical. Purely additive; no gating needed.

### Interaction with structs

A **by-value struct return *is* a multiple return** whose `OUT` slots carry names and offsets — the
same window layout. Step 4 is therefore a **prerequisite of by-value struct returns** (Step 2): one
multi-`OUT` implementation serves both `returns float l, float r` and `returns Filter out`. The
small-aggregate case (a stereo frame, a complex pair) is expressible either way — as explicit
scalars now, or as a named struct once Step 2 lands.

---

## Step 5: Import (proposed)

### The problem

Impala has no `#include`: sharing declarations between units means textual copying — and the 2.0
type system makes that materially *worse* than 1.0. A 1.0 extern is an information-free one-liner
(`extern function foo`); a 2.0 interface is a struct layout plus typed signatures — a real,
drift-prone surface, hand-synchronized in N copies. The validator turns drift into loud errors
instead of silent garbage, but managing the pain is not removing it.

### The design: import source as interface

```impala
import "filter.impala"
```

The imported file is a **normal compilable unit — not a header**. The compiler parses it and takes
its interface:

- `struct`, `const`, and `functype` declarations enter scope directly;
- **function and global definitions are converted to typed extern declarations automatically**,
  with their full signatures (including multi-return, if Step 4 is adopted);
- `extern` and `extern native` declarations pass through as-is;
- its own `import`s are processed transitively;
- the importing unit's own `.gazl` output gains no code, data, or directives from the import —
  imported units are emitted once, as themselves, by the build (below).

There is exactly **one source of truth**: the struct and the functions live in `filter.impala` and
nowhere else. No second artifact exists to drift.

**Import is linking** *(decided — an earlier draft separated them; the user's observation "it
could be" is right, and the benefits compound)*. The `import` statements already describe the
complete program graph, so making the programmer restate that graph as a manual concatenation
list is redundancy with failure modes: a forgotten unit, a stale artifact, a wrong order. Instead,
the build is driven from a root unit:

```
impala build main.impala → main.gazl        (the complete, linked program)
```

The toolchain walks the import closure (visited-set, cycles legal), compiles each unit exactly
once, and emits the concatenated program itself. **Concatenation still happens — the tool performs
it.** The GAZL assembler and loader are untouched; the transliterator property is untouched.
Consequences:

- **Staleness vanishes** for source imports: everything is compiled from source, together.
- `import "x.gazl"` drops a precompiled or hand-written unit into the closure as-is — its
  interface read from the `; signature` rows (and structural facts: `! DEF` values, `GLOB`
  sizes), its text emitted verbatim into the linked output. This is how third-party blobs,
  hand-written GAZL, and a precompiled stdlib participate.
- **The validator becomes internal to the build** — the link set *is* the closure, checked during
  compilation. The standalone `gazl-validate` remains for the legacy workflow only.
- **Single definition, enforced**: any symbol — and in particular any struct — defined more than
  once in the closure is an error. The copy-paste model and its layout-agreement machinery apply
  only to the legacy manual workflow.
- The legacy workflow (compile units separately, concatenate by hand, run the validator) remains
  fully supported — import-driven builds are the front door, not a replacement.

**Rejected alternative**, for the record: *C-style declaration files* (a hand-maintained
`filter_types.impala` of declarations only) — the C header disease; a second artifact that drifts
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
  section — where it belongs.) Host-supplied valueless constants (`const int DEBUG`) emit
  references by name, unchanged.
- In an import-driven build there is nothing left for a separate validation step to check — the
  closure is compiled together and linked by the tool. The standalone `; signature` validator
  remains for the legacy manual-concatenation workflow, where stale artifacts and hand-assembled
  link sets are still possible.

### Cycles

Import cycles are **legal**. Mutual dependency between units is a supported pattern today
(concatenation is order-independent), and mutually-dependent units are exactly the ones with the
most shared interface surface — erroring on cycles would push them back to hand-written externs,
the boilerplate this feature exists to kill.

Cycles are harmless because the closure walk separates gathering from emission: the compiler keeps
a **visited set keyed by canonical path**, seeded with the root unit. Each file in the import
closure is parsed exactly once and emitted exactly once; an `import` naming an already-visited
file is skipped. The self-import-via-cycle case (B importing the root) needs no special rule — the
seeding handles it. Diamonds dedupe the same way. Name resolution runs after the whole closure is
gathered, so mutually-referencing types resolve regardless of parse order.

What *does* error is definitional cycles in content — which are errors within a single file too;
imports merely let them span files:

| Cycle kind | Verdict |
|---|---|
| Import cycle (A↔B, any depth) | **legal** — visited-set memoization, each file parsed once |
| Same file reached via two paths | legal — canonical-path dedup |
| Same symbol from two different files | error — flat namespace, duplicate declaration |
| Const *value* cycle across the closure | error, diagnostic cites the dependency chain |
| **By-value** struct containment cycle (`struct A { B b }` / `struct B { A a }`) | error (infinite size) — the mutual generalization of the self-reference rule |
| By-pointer struct cycle | legal, exactly like self-reference |

### Dead-code elimination and `export`

Because an import-driven build owns the whole program graph, it *can* drop code nothing reaches —
a firmware that `import "math.impala"` for `sqrt` need not ship `sin`/`cos`/`tan` and their tables.
This is link-time dead-code elimination (`ld --gc-sections` / `-dead_strip`), **not** a runtime
collector — Impala has no heap and nothing to collect at run time.

The hazard is "reachable from *what*?": Impala has no in-language `main`, and hosts call entry
points *by name* (`findFunction("process")`), so naive reachability would strip every firmware
entry point. The resolution is a **compile-time flag, off by default** — mirroring `--legacy`:

- **Default `impala build` trims nothing.** Every unit in the closure is emitted whole, exactly
  like manual concatenation but tool-performed. An unmodified 1.0 firmware builds to a working
  program with no annotations. **100% backward compatible** — no positional "root is special" rule,
  no behavior that depends on which file is the build root.
- **`--dead-strip` enables trimming**, uniformly across *all* units including the root: any symbol
  that is neither `export`ed nor reachable from an `export`ed symbol is removed. This is the "I have
  annotated my host surface, strip the rest" switch, flipped only when the smaller/faster program
  is wanted. (Name honors Apple `ld`'s `-dead_strip`.)
- **`export` marks host-visible symbols** — functions and globals the host looks up or pokes:

  ```impala
  export function process()                 // the host calls this
  export global int array params[PARAM_COUNT]
  ```

  `export` is an additive keyword (zero corpus collisions) and is **always legal but only
  load-bearing under `--dead-strip`**; without the flag it is pure machine-checked documentation of
  the host contract (and rides the `; signature` metadata so tooling sees the retained surface).
  Adding `export`s therefore never changes behavior until trimming is requested — additive all the
  way down. It doubles as the answer to "which symbols does the host call?" — greppable, where
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
if host state serialization is layout-dependent — but *any* source edit already has that property,
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
linked in — discoverable by strangers and agents from the source itself, with no per-firmware
copy-paste and no separate link list to maintain.

The two import forms follow one rule of thumb: *import the source of what you're building, import
the artifact of what you're using.* `.gazl` imports read interfaces from the `; signature` rows —
now carrying full element chains — so precompiled units participate with typed checking, and
"import what you link" holds by construction since the build emits exactly what it imported.

### Implementation coupling

Step 5 should be implemented **with or before Step 2 (structs)**: structs are new syntax with no
legacy copy-paste to protect, so if import lands first, the single-definition rule applies to
structs from day one and the layout-agreement machinery for duplicated struct definitions only
ever needs to exist in the legacy validator path.

---

## Strict expressions: mixed bitwise operators

1.0 flattens `<< >> >>> & ^ |` into a single left-associative level; C ladders them internally
(`<<`/`>>` > `&` > `^` > `|`). Contrary to the reference doc's examples, 1.0 and C actually *agree*
on bitwise-vs-arithmetic (`x & 0xFF + 1` is `x & (0xFF + 1)` in both, since C's `+` also binds
tighter than `&`). The silent divergence is only **within the bitwise/shift family**:

```impala
a | b & c        // C: a | (b & c)     Impala: (a | b) & c    — divergent
a & b << 2       // C: a & (b << 2)    Impala: (a & b) << 2   — divergent
a & b | c        // (a & b) | c in both — left-assoc happens to match
```

**Rule:** mixing *different* operators from `{<< >> >>> & ^ |}` at the same parenthesization level
is a compile error: *"mixed bitwise operators require parentheses."* Same-op chains stay legal
(`a | b | c`). Parenthesized code is untouched. C's ladder is **never adopted** — no expression is
ever silently reparsed; code either compiles with its 1.0 meaning or errors with a mechanical fix.
Every accepted expression therefore reads identically to a C-trained human or agent.

**Gating — strict by default, one compiler argument to lower.** Impala sources carry no version
numbers, pragmas, or inferred dialects: **behavior is identical for every file.** The check is an
error by default, including for untouched 1.0 sources. A compile-time argument (e.g. `--legacy`)
downgrades strictness errors to stderr warnings for code that cannot be updated yet. Old code
should simply be updated — the fix is small, mechanical, and *meaning-preserving*: adding
parentheses that match the 1.0 left-associative parse yields the identical parse tree, so the
generated `.gazl` is **byte-identical** after the edit. The byte-diff acceptance gate is therefore
unaffected: parenthesized corpus sources produce the same bytes under both compilers.

**Corpus evidence:** exactly one line in all 78 `.impala` files mixes distinct bitwise operators
unparenthesized at top level — `ImpalaDemo.impala:216`, the line written to demonstrate the flat
precedence (duplicated in `tests/`). All real firmware sources parenthesize. That one line gets
parenthesized (identical output), and the demo text becomes a place to teach the 2.0 rule instead.

**Implementation sketch (JSPEG):** the `Bitwise` rule tracks the first operator of its own
invocation in a rule-local (`$first`) and reports later differing operators. Parenthesization
scoping is automatic — `Group <- '(' Expr ')'` recurses into a fresh `Bitwise` invocation with its
own `$first`. Strictness is known at startup (a runner argument), so detection resolves
immediately: `$$parser.fail(msg, $$s, $$i)` by default, an immediate stderr warning under
`--legacy`. No deferral machinery is needed.

**Adopted extension — bitwise vs comparison in conditions.** An unparenthesized bitwise/shift
operator directly against a comparison in a condition is the same error: `if (a & 3 == 0)` must be
written `if ((a & 3) == 0)`. Impala's own parse is the sane one (`(a & 3) == 0`), but a C-trained
reader misreads the unparenthesized form as `a & (3 == 0)`, which breaks the invariant that every
accepted expression reads identically to a C-trained reader — and invariants with one exception
stop being invariants. Corpus evidence: exactly one line in 78 files
(`rpm16_code.impala:156`, `if ((tmp = global clock) != clock & 0xFFFF)`), and it is a genuinely
divergent reading — Impala means `tmp != (clock & 0xFFFF)`; C's ladder would mean
`(tmp != clock) & 0xFFFF`. Same gating (`--legacy` downgrades to a warning), same
meaning-preserving parenthesization fix, byte-identical output after the edit.

---

## Compound assignment — rejected

The `<op>=` family (`+=`, `-=`, …) and `++`/`--` are **not adopted**. An earlier draft of this
document adopted them; the decision was reversed.

**The rule that decides sugar questions:** a second spelling is admitted only when the spellings
compile to **different GAZL** — i.e. when the syntax carries information. `a += 1` compiles to the
*identical* instructions as `a = a + 1`, so it would be two representations of the same thing,
leaving every author (and agent) wondering which one is preferred — neither is. It dilutes the 1:1
GAZL↔Impala feel for zero information. Contrast `.`/`->`, which were kept precisely because they
compile *differently* (a free constant offset vs a real load) — that split is a cost annotation,
not sugar.

(The single-evaluation argument — `a[f()] += 1` calling `f` once — was considered and does not
outweigh this: it makes `+=` *semantically different* from the longhand in exactly the cases where
readers would assume it's the same, which is its own trap.)

---

## Diagnostics

The error format is part of the language's contract with its audience — AI agents iterate against
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
  the contract.

### Code registry

| Code | Meaning |
|---|---|
| E001 | syntax error (parse failure; expected-set reporting is future JSPEG work) |
| E101 | mixed bitwise operators require parentheses |
| E102 | comparison mixed with bitwise operators requires parentheses |
| E201 | pointer element type mismatch in assignment |
| E202 | pointer element type mismatch in call argument |
| E203 | element type mismatch with previous declaration |
| E301 | invalid operand types for operator |
| E302 | invalid operand type for unary operator |
| E303 | incompatible types for assignment |
| E304 | return type disagreement (mismatch / conflicting expectations / previous uses) |
| E305 | `for` variable must be a local modifiable int or pointer |
| E306 | `switch` expression must be int |
| E401 | identifier already declared |
| E402 | type mismatch with previous declaration |
| E403 | undeclared identifier |
| E404 | invalid lvalue |
| E405 | invalid argument count |
| E406 | argument type mismatch |
| E407 | constant expression expected |
| E408 | invalid type for function call |
| E409 | `default` case already defined |

---

## Open questions

- **Adoption of Steps 2–5 themselves.** Structs, typed function pointers, multiple return values,
  and import are worked proposals, not commitments: their syntax, semantics, lowering, and
  identity rules are specified above so the adoption decision can be made on a concrete design —
  but that decision has not been made. The committed scope is Step 1 plus the cross-cutting rules
  (strict expressions, the compound-assignment rejection, diagnostics).
- Name of the strictness-lowering compiler argument (`--legacy` is the working name).
- By-value struct parameters/returns: deferred, revisit if the small-struct performance case
  materializes in real firmware (see Step 2, *Passing, returning, copying*).
