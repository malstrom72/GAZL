# Hand-off: fix the GAZL undefined-behavior / portability items

**Audience:** an implementer picking up the code changes. Self-contained — you do **not** need the design
conversation that produced it.

**Goal:** eliminate the C++ undefined-behavior and cross-architecture divergences catalogued in
[PortabilityAudit.md](PortabilityAudit.md), in the interpreter (`src/GAZL.cpp`), so the same GAZL program produces the
same result on x86-64 and AArch64 and can never crash the host. This is interpreter-only work; there is no JIT on this
branch. (The fuller design rationale lives in `JitCompilerResearch.md` on the `jit-compiler` branch, but you don't need
it here — PortabilityAudit.md is the spec.)

Line numbers below are as of commit `a29bd02` and **will drift as you apply edits** — always locate the real site by
the `case` label / code shown, not the number.

---

## Ground rules (read first)

1. **Two evaluators must stay in agreement.** Every integer/`FTOI` opcode is evaluated in two places:
   - run-time interpreter — `Processor::run()`, the big `switch` (`GAZL.cpp` ~1606–1760);
   - compile-time constant folder — `calcConstant()` `switch` (`GAZL.cpp` ~1222–1265), used by the assembler to fold
     `_CCC` opcodes (all-constant operands).
   A fix applied to only one path makes `op x #a #b` (folded) silently disagree with `op x aVar bVar` (run-time). **Fix
   both**, and add a test that asserts they match (see Testing).

2. **Do it with shared helpers, not inline casts.** Add small `inline` helpers next to `absolute()` (`GAZL.cpp:89`) and
   call them from *both* evaluators. That is the mechanical guarantee that the two paths can't drift.

3. **These are the chosen, now-normative semantics** (two's-complement / saturating — the AArch64 & WebAssembly
   choices). They must be identical on every target:
   | Op | Defined result |
   |---|---|
   | `INT_MIN / -1` | `INT_MIN` |
   | `INT_MIN % -1` | `0` |
   | `ADDi/SUBi/MULi` overflow | two's-complement wrap |
   | `ABSi(INT_MIN)` | `INT_MIN` |
   | shift count `n` | uses `n & 31` ("count mod 32") |
   | `SHRi` (arithmetic `>>`) of negative | sign-extending |
   | `FTOi` of `> INT_MAX` / `+inf` | `INT_MAX` |
   | `FTOi` of `< INT_MIN` / `-inf` | `INT_MIN` |
   | `FTOi` of `NaN` | `0` |

4. **Sandbox/other behavior unchanged.** Keep the existing div-by-zero error (`CHECK_INT_DIV_BY_ZERO`,
   `GAZL.cpp:1579`) exactly as is; the new `-1` handling is *in addition*. Don't touch bitwise `ANDi/IORi/XORi`,
   `MAXi/MINi`, or any float op other than `FTOi`.

5. **No new includes needed.** The helpers use only comparisons, casts, and `2147483647` / `(-2147483647 - 1)` literals
   — do **not** rely on `INT_MIN`/`INT_MAX` macros (`<climits>` is not included).

---

## Step 1 — add shared helpers (near `GAZL.cpp:89`)

`absolute(int)` currently overflows for `INT_MIN`; replace it and add the rest:

```cpp
// --- defined integer/FTOI helpers: called from BOTH Processor::run() and calcConstant() ---
inline int absolute(int i) { int x = i >> 31; return (Int)(((UInt)i ^ (UInt)x) - (UInt)x); } // INT_MIN -> INT_MIN
inline Int idiv(Int a, Int b) { return (b == -1) ? (Int)(0u - (UInt)a) : a / b; }  // caller guarantees b != 0
inline Int imod(Int a, Int b) { return (b == -1) ? 0 : a % b; }                    // caller guarantees b != 0
inline Int iadd(Int a, Int b) { return (Int)((UInt)a + (UInt)b); }
inline Int isub(Int a, Int b) { return (Int)((UInt)a - (UInt)b); }
inline Int imul(Int a, Int b) { return (Int)((UInt)a * (UInt)b); }
inline Int ishl(Int a, Int n) { return (Int)((UInt)a << (n & 31)); }               // no neg-shift / overflow UB
inline Int ashr(Int a, Int n) { return a >> (n & 31); }                            // arithmetic (sign-extending)
inline Int lshr(Int a, Int n) { return (Int)((UInt)a >> (n & 31)); }               // logical
inline Int ftoi(Float v) {                     // v is the ALREADY-SCALED float (see FTOi note)
    if (v != v)               return 0;                       // NaN
    if (v >=  2147483648.0f)  return 2147483647;              // >= 2^31  (and +inf)
    if (v <  -2147483648.0f)  return (Int)(-2147483647 - 1);  // <  -2^31 (and -inf)
    return (Int)v;                             // in range: truncate toward zero
}
```

Keep the existing `absolute(float)` / `absolute(double)` overloads (`:90`–`:91`) unchanged. Fixing `absolute(int)`
fixes both `ABSI_VV_` (run-time `:1647`) and `ABSI_CC_` (fold `:1225`) automatically — they already call it, so **no
`ABSi` call-site edits are needed.**

---

## Step 2 — run-time call sites (`Processor::run()`)

Route the affected `case`s through the helpers. Div/mod: **keep the `CHECK_INT_DIV_BY_ZERO` exactly where it already
is**; only the division itself changes. Note `DIVI_VVC` / `MODI_VVC` have *no* zero check (constant divisor is
assembler-guaranteed non-zero) — but the constant can be `-1`, so they still must go through `idiv`/`imod`.

```cpp
// ~1649–1682, replace RHS only:
case ADDI_VVV:  V0.i = iadd(V1.i, V2.i); break;
case ADDI_VVC:  V0.i = iadd(V1.i, C2.i); break;
case SUBI_VVV:  V0.i = isub(V1.i, V2.i); break;
case SUBI_VVC:  V0.i = isub(V1.i, C2.i); break;
case SUBI_VCV:  V0.i = isub(C1.i, V2.i); break;
case MULI_VVV:  V0.i = imul(V1.i, V2.i); break;
case MULI_VVC:  V0.i = imul(V1.i, C2.i); break;
case DIVI_VVV:  CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = idiv(V1.i, V2.i); break;
case DIVI_VVC:  V0.i = idiv(V1.i, C2.i); break;
case DIVI_VCV:  CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = idiv(C1.i, V2.i); break;
case MODI_VVV:  CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = imod(V1.i, V2.i); break;
case MODI_VVC:  V0.i = imod(V1.i, C2.i); break;
case MODI_VCV:  CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = imod(C1.i, V2.i); break;
case SHLI_VVV:  V0.i = ishl(V1.i, V2.i); break;
case SHLI_VVC:  V0.i = ishl(V1.i, C2.i); break;
case SHLI_VCV:  V0.i = ishl(C1.i, V2.i); break;
case SHRI_VVV:  V0.i = ashr(V1.i, V2.i); break;
case SHRI_VVC:  V0.i = ashr(V1.i, C2.i); break;
case SHRI_VCV:  V0.i = ashr(C1.i, V2.i); break;
case SHRU_VVV:  V0.i = lshr(V1.i, V2.i); break;
case SHRU_VVC:  V0.i = lshr(V1.i, C2.i); break;
case SHRU_VCV:  V0.i = lshr(C1.i, V2.i); break;

// ~1713, FTOi — the scale multiply happens in float FIRST, then convert:
case FTOI_VVC:  V0.i = ftoi(V1.f * C2.f); break;
```

`ITOF_VVC` (`:1714`) is exact — leave it. `ADDp`/`SUBp`/`DIFp` reuse `ADDI_*`/`SUBI_*` opcodes on unsigned pointers; the
unsigned-based helpers give bit-identical results for them, so this is safe (no separate handling).

---

## Step 3 — compile-time call sites (`calcConstant()`, ~1227–1262)

Mirror exactly. The compound-assignment forms become helper calls:

```cpp
case ADDI_CCC: v1.i = iadd(v1.i, v2.i); break;
case SUBI_CCC: v1.i = isub(v1.i, v2.i); break;
case MULI_CCC: v1.i = imul(v1.i, v2.i); break;
case DIVI_CCC: if (v2.i == 0) throw Exception(CONSTANT_DIVISION_BY_ZERO); v1.i = idiv(v1.i, v2.i); break;
case MODI_CCC: if (v2.i == 0) throw Exception(CONSTANT_DIVISION_BY_ZERO); v1.i = imod(v1.i, v2.i); break;
case SHLI_CCC: v1.i = ishl(v1.i, v2.i); break;
case SHRI_CCC: v1.i = ashr(v1.i, v2.i); break;
case SHRU_CCC: v1.i = lshr(v1.i, v2.i); break;
// ... (ABSI_CC_, ANDI/IORI/XORI/MAXI/MINI CCC unchanged) ...
case FTOI_CCC: v1.i = ftoi(v1.f * v2.f); break;
```

This also removes the latent **assembler crash**: before the fix, folding a constant `INT_MIN / -1` (`DIVi x
#-2147483648 #-1`) would `#DE`/SIGFPE the assembler itself.

---

## Step 4 — spec/doc items (no arithmetic-code change)

- **Item 6 (local aliasing, SPEC).** Already resolved in [PortabilityAudit.md §6](PortabilityAudit.md) — the
  **provenance-bounded** rule. Add its wording to the `GETL`/`SETL`/`ADRL`/`ADDp`/`SUBp` entries in
  `docs/InstructionSet.md` and `src/UnitTest.gazl`, and add golden `.gazl` cases that
  **rely on** the defined cross-local span (a bank `COPY`; a by-ref out-param). Do **not** write "distinct locals do not
  alias" — that is unsound (see the ⚠️ in §6). No interpreter change (today's behavior already conforms).
- **Item 7 (div truncation caveat, SPEC-resolved).** Delete the "not guaranteed by all C/C++ compilers" caveat text —
  it is in `src/UnitTest.gazl` around **lines 472–474** (the `DIVi` test block) and in `InstructionSet.md`. C++11
  guarantees truncation toward zero. Keep the actual test (`-987654321 / 761 / … == 761`).
- **Item 5 (denormals, FP-ENV).** Lowest priority; not an arithmetic-correctness bug. Either (a) document that denormal
  handling follows the host FTZ/DAZ mode, or (b) add an optional strict mode that normalizes `MXCSR` (x86) / `FPCR`
  (ARM) at `run()` entry. Treat as a separate follow-up, not part of the UB fixes.

---

## Suggested commit order

1. **Item 1 first, isolated** — `idiv`/`imod` + the div/mod call sites + tests. This is the only **SAFETY** item (host
   crash on x86), so land it on its own for a clean cherry-pick/backport.
2. **Items 3 + 4** — `iadd/isub/imul/ishl/ashr/lshr` + `absolute` + call sites + tests. Pure latent-UB removal.
3. **Item 2** — `ftoi` + call sites + tests. Fixes a *live* x86-vs-ARM divergence.
4. **Items 6 + 7** (docs/spec + golden `.gazl`), and **Item 5** as a later follow-up.

Optional backstop, orthogonal to the above: add `-fwrapv` (and `-fno-strict-aliasing`) in `tools/buildGAZLCmd.sh`. It
does **not** replace the explicit fixes — it doesn't define `FTOi`/shift semantics or the cross-arch results — but it
removes signed-overflow UB as a class.

---

## Testing

Tests live in `src/UnitTest.gazl` (a GAZL program; assertions are `NEQi <got> <expected> @fail` — control reaching
`fail` fails the suite). It runs via `./output/GAZLCmdBeta`, invoked by `build.sh`.

**Add cases covering both evaluators.** For each, use a *variable* operand to hit run-time and an *all-constant* form to
hit the folder, then assert they agree. Example for item 1 (write `INT_MIN` as `#-2147483648`, or `#0x80000000` if the
parser rejects the decimal form):

```
 MOVi      i0              #-2147483648
 DIVi      i1              i0              #-1      ; run-time idiv
 DIVi      i2              #-2147483648    #-1      ; compile-time fold (would have crashed the assembler pre-fix)
 NEQi      i1              #-2147483648    @fail
 NEQi      i2              #-2147483648    @fail
 MODi      i3              i0              #-1
 NEQi      i3              #0              @fail
```

Cases to add (run-time **and** folded form for each, asserting equality where both are expressible):
- **Item 1:** `INT_MIN/-1`→`INT_MIN`, `INT_MIN%-1`→`0`, `-1/-1`→`1`, `0/-1`→`0`. Keep a `DIVi x y #0` /
  `MODi x y #0` still raising the div-by-zero error.
- **Item 3:** `INT_MAX + 1`→`INT_MIN`, `INT_MIN - 1`→`INT_MAX`, `INT_MIN * -1`→`INT_MIN`, `ABSi(INT_MIN)`→`INT_MIN`.
- **Item 4:** `1 << 32`→`1`, `1 << 33`→`2`, `1 << -1`→`INT_MIN` (`-1 & 31 = 31`), `(-8) >> 1`→`-4` (arithmetic),
  `(-8) SHRu 1`→`0x7FFFFFFC`.
- **Item 2:** `FTOi(1e30)`→`INT_MAX`, `FTOi(-1e30)`→`INT_MIN`, `FTOi(2147483647.0)`→`INT_MAX` (2^31 not representable),
  `FTOi(-2147483648.0)`→`INT_MIN`. `NaN`→`0` is worth covering but awkward to express as a constant (float `0.0/0.0` is
  guarded to a run-time error by default); test it only if you can synthesize a NaN bit-pattern, otherwise note it as
  covered-by-inspection.

## Verify

```
bash build.sh
```
Green means: `GAZLCmdBeta` (unit tests incl. your new cases) passed, the Impala suite passed, and the staged compiler
round-trip ran. For the cross-arch guarantees, if you can, build and run `GAZLCmdBeta` on **both** an x86-64 and an
AArch64 host (or under emulation) and confirm the new arithmetic/`FTOi` cases pass identically on both — that is the
divergence items (1, 2) actually being closed, not just the UB.

## Done when

- All of `src/GAZL.cpp` items 1–4 fixed in **both** evaluators via the shared helpers; new `UnitTest.gazl` cases green.
- `build.sh` passes; ideally verified on both ISAs.
- Items 6–7 doc/spec wording updated (+ golden `.gazl` for the item-6 defined span); item 5 filed as a follow-up.
- Nothing outside the listed opcodes changed; div-by-zero and all float ops except `FTOi` behave exactly as before.
