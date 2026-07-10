# GAZL Correctness, Portability & Undefined-Behavior Audit

**Scope:** issues in the **current** GAZL source (`src/GAZL.cpp` @ `a29bd02`), independent of any future JIT. They were
found while researching a native compiler (see [JitCompilerResearch.md](JitCompilerResearch.md)), but every one of them
already affects the interpreter today. They matter because GAZL's stated goals include *"run-time should be sandboxed,
100 % safe"*, *"portable and CPU-agnostic"*, and *"possible to suspend and resume full machine state"* (`src/GAZL.h`) —
and several of the items below break one of those promises on some input or some platform.

A structural point that applies throughout: **most arithmetic opcodes have two evaluators that must agree** — the
run-time interpreter in `Processor::run()` and the compile-time constant folder used by the assembler (the `_CCC`
opcodes around `GAZL.cpp:1225`–`1263` and `calcConstant`). If a fix changes run-time semantics but not the compile-time
path, a constant-folded expression (`fTOi x #BIG`) will silently disagree with the same computation on run-time values
(`fTOi x bigVar`). **Every fix below must be applied to both paths**, and a regression test should assert they match.

## Severity legend

| Tag | Meaning |
|---|---|
| **SAFETY** | Can crash or escape the sandbox — violates the "100 % safe" guarantee. Fix first. |
| **DIVERGENCE** | Same input already yields *different results* on x86-64 vs AArch64 today. Breaks portability/reproducibility. |
| **UB** | Latent undefined behavior in C++. Works today by luck on current targets/compilers; an optimizer (`-Os` is used, with no `-fwrapv`) or a new CPU can expose it. |
| **FP-ENV** | Result depends on host floating-point environment (denormal mode). Cross-environment non-determinism. |
| **SPEC** | Behavior is defined only by the current implementation; should be made *officially* specified (or officially unspecified). |

## Summary

| # | Issue | Tags | Location(s) | One-line fix |
|--:|---|---|---|---|
| 1 | `INT_MIN / -1` and `INT_MIN % -1` trap (`#DE`→SIGFPE) on x86 | **SAFETY** **DIVERGENCE** | `GAZL.cpp:1662`–`1667`, `:1663`, compile-time `DIVI_CCC`/`MODI_CCC` | Guard; define `INT_MIN/-1 = INT_MIN`, `INT_MIN%-1 = 0` |
| 2 | `FTOI` on NaN / out-of-range diverges x86 vs ARM | **DIVERGENCE** **UB** | `GAZL.cpp:1713` (run), `:1262` (compile) | Define saturating + `NaN→0`; implement explicitly on both paths |
| 3 | Signed integer overflow in `ADDi`/`SUBi`/`MULi`/`ABSi` | **UB** | `:1649`–`1661`, `absolute()` `:89` | Define two's-complement wrap; use unsigned math or build `-fwrapv` |
| 4 | Shift count ≥ 32 or negative is UB; `SHRi` of negative impl-defined pre-C++20 | **UB** | `:1674`–`1682` | Define "count mod 32"; mask `& 31`; guarantee arithmetic `>>` |
| 5 | Denormal handling follows host FTZ/DAZ | **FP-ENV** | (no MXCSR/FPCR control; `:35`–`50`) | Document as host-defined; optional strict mode |
| 6 | `GETL`/`SETL`/`ADRL` cross-local access is defined-by-accident | **SPEC** | `:1642`–`1645` | Adopt the *provenance-bounded* rule (defined within `[deriv, frame.dsp)`, incl. cross-local; unspecified beyond). **Not** "distinct locals don't alias" — that's unsound |
| 7 | `DIVi`/`MODi` truncation-toward-zero flagged as an *assumption* | **SPEC** (resolved) | `UnitTest.gazl` / `InstructionSet.md` notes | Retire the caveat — C++11 guarantees it |

(`COPY` overlap is *correctly* specified as undefined already — see [Examined and found OK](#examined-and-found-ok).)

---

## 1. `INT_MIN / -1` and `INT_MIN % -1` — hardware trap on x86 (SAFETY, DIVERGENCE)

**Current behavior.** Integer divide/modulo guard only against a *zero* divisor:

```cpp
case DIVI_VVV:  CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = V1.i / V2.i; break;   // :1662
case DIVI_VVC:  V0.i = V1.i / C2.i; break;                                 // :1663  (no guard: constant divisor)
case MODI_VVV:  CHECK_INT_DIV_BY_ZERO(V2.i); V0.i = V1.i % V2.i; break;   // :1665
```

`CHECK_INT_DIV_BY_ZERO` catches divisor `0` but **not** the `INT_MIN / -1` overflow case. On x86-64 the `idiv`
instruction raises `#DE` for `INT_MIN / -1` (quotient `+2^31` is unrepresentable), which the OS delivers as **SIGFPE**.
GAZL installs no handler, so **the host DAW process crashes** — from inside a VM that promises to be "100 % safe."
On AArch64 `sdiv` does *not* trap: it returns `INT_MIN` (and the `msub`-based modulo returns `0`). So the same program
crashes on Intel and runs on Apple Silicon — both a safety hole and a portability divergence.

**Reachable** with run-time values (`DIVI_VVV`), and also with a constant `-1` divisor (`DIVI_VVC`/`MODI_VVC`), and at
**assembly time** if a constant `INT_MIN / -1` is folded (`DIVI_CCC`/`MODI_CCC`, `GAZL.cpp:~1250`) — that would crash the
*assembler/host*, not just a running program.

**Recommended resolution.** Define `INT_MIN / -1 = INT_MIN` and `INT_MIN % -1 = 0` (the AArch64 / wasm choice — the
least-surprising two's-complement result). Guard explicitly on every signed div/mod variant, run-time and compile-time:

```cpp
// divisor already known non-zero here
Int a = V1.i, b = V2.i;
V0.i = (b == -1) ? (Int)(0u - (UInt)a)        // negate with defined wrap; INT_MIN stays INT_MIN
                 : a / b;
// modulo:
V0.i = (b == -1) ? 0 : a % b;
```

Keep the divide-by-zero error as-is. Add golden cases for `INT_MIN/-1`, `INT_MIN%-1`, `-1/-1`, `0/-1`.

---

## 2. `FTOI` on NaN / out-of-range — different result per architecture (DIVERGENCE, UB)

**Current behavior.**

```cpp
case FTOI_VVC:  V0.i = (Int)(V1.f * C2.f); break;   // :1713 run-time
case FTOI_CCC:  v1.i = (Int)(v1.f * v2.f); break;   // :1262 compile-time
```

Casting an out-of-range or NaN `float` to `int` is UB in C++, and the two target ISAs realize it differently:

| Input to `(int)` | x86-64 `cvttss2si` | AArch64 `fcvtzs` |
|---|---|---|
| `> INT_MAX` | `0x80000000` (INT_MIN, "integer indefinite") | `INT_MAX` (saturates) |
| `< INT_MIN` | `0x80000000` | `INT_MIN` (saturates) |
| `NaN` | `0x80000000` | `0` |

So a GAZL program that converts, say, `1e30` already yields `INT_MIN` on Intel and `INT_MAX` on Apple Silicon **today**.
This silently breaks reproducibility and any firmware that relies on clamping behavior.

**Recommended resolution.** Adopt **saturating conversion with `NaN → 0`** (the wasm `i32.trunc_sat_f32_s` semantics —
free on AArch64, a few compares on x86). Implement it explicitly so it no longer depends on the cast's UB, on **both**
paths (note the `* scale` happens first, then the conversion):

```cpp
static inline Int ftoi(Float v) {           // v already scaled
    if (v != v) return 0;                    // NaN
    if (v >=  2147483648.0f) return  2147483647;   // >= 2^31  -> INT_MAX
    if (v <  -2147483648.0f) return -2147483648;   // <  -2^31 -> INT_MIN
    return (Int)v;                            // in range: exact truncation toward zero
}
```

(Use the compiler-as-oracle trick when this is later lowered in the JIT: this exact C compiles to `fcvtzs` alone on ARM
and `cvttss2si` + clamp on x86.) `ITOF` is exact and needs no change. Update the `FTOI` spec text to state the defined
result for out-of-range/NaN.

---

## 3. Signed integer overflow in `ADDi` / `SUBi` / `MULi` / `ABSi` (UB)

**Current behavior.** All signed integer arithmetic uses plain C `+`/`-`/`*` on `Int`:

```cpp
case ADDI_VVV:  V0.i = V1.i + V2.i; break;   // :1649
case SUBI_VVV:  V0.i = V1.i - V2.i; break;   // :1651
case MULI_VVV:  V0.i = V1.i * V2.i; break;   // :1660
```

and `absolute(INT_MIN)` overflows too:

```cpp
inline int absolute(int i){ int x = i >> 31; return (i ^ x) - x; }   // :89  (INT_MAX + 1 for INT_MIN)
```

Signed overflow is **undefined behavior** in C++. The project builds with `-Os` and **no** `-fwrapv` /
`-fno-strict-overflow` / UBSan (`tools/BuildCpp.sh:21`–`23`), so this is genuinely unguarded: it happens to wrap
two's-complement on today's x86/ARM, but an optimizer is entitled to assume it never occurs (e.g. fold `x+1 > x` to
`true`), and UBSan builds will trap. GAZL is low-level assembly where wraparound is a *reasonable and expected*
semantic, so the fix is to *define* it, not forbid it.

**Recommended resolution.** Specify **two's-complement wraparound** for all integer arithmetic, and implement it without
UB by computing in `UInt` and storing back:

```cpp
case ADDI_VVV:  V0.i = (Int)((UInt)V1.i + (UInt)V2.i); break;
case SUBI_VVV:  V0.i = (Int)((UInt)V1.i - (UInt)V2.i); break;
case MULI_VVV:  V0.i = (Int)((UInt)V1.i * (UInt)V2.i); break;
// absolute(): return (Int)((i ^ x) - (UInt)x);   // INT_MIN -> INT_MIN, no UB
```

Belt-and-suspenders: also add `-fwrapv` (GCC/Clang) / `/d2UndefIntOverflow-`-equivalent to the build so the
*compile-time* folder (which uses the same `+`/`-`/`*`) is covered too, or route the folder through the same unsigned
helpers. This touches every integer arithmetic mode (`_VVV`/`_VVC`/`_VCV`/`_CCC`) — do them uniformly.

---

## 4. Shift count ≥ 32 or negative (UB); arithmetic right shift (impl-defined pre-C++20)

**Current behavior.**

```cpp
case SHLI_VVV:  V0.i = V1.i << V2.i; break;             // :1674
case SHRI_VVV:  V0.i = V1.i >> V2.i; break;             // :1677  arithmetic (sign-replicating) intended
case SHRU_VVV:  V0.i = (UInt)(V1.i) >> V2.i; break;     // :1680  logical
```

Two issues:

1. A shift **count `>= 32` or `< 0` is UB** in C++. Both target ISAs happen to mask the count mod 32 for 32-bit operands
   (x86 masks to 5 bits; AArch64 `lsl`/`lsr`/`asr` on `W` registers mask mod 32), so results *agree* today — but it is
   UB, so an optimizer/UBSan can still misbehave, and the *guaranteed* behavior should be pinned.
2. `SHRi` relies on `>>` of a **negative signed** value being arithmetic. That was *implementation-defined* before
   C++20 (defined as arithmetic only since C++20). On the mainstream compilers it is arithmetic, but it should not be
   left to chance.

**Recommended resolution.** Define shift semantics as **"count taken mod 32"** and mask explicitly; keep `SHRi`
arithmetic via a defined idiom:

```cpp
case SHLI_VVV:  V0.i = (Int)((UInt)V1.i << (V2.i & 31)); break;
case SHRU_VVV:  V0.i = (Int)((UInt)V1.i >> (V2.i & 31)); break;
case SHRI_VVV: { unsigned n = V2.i & 31;                               // arithmetic, no impl-defined dependence
                V0.i = (Int)(V1.i < 0 ? ~(~(UInt)V1.i >> n) : (UInt)V1.i >> n); } break;
```

(If you are comfortable requiring C++20, `V1.i >> (V2.i & 31)` is sufficient for `SHRi`.) Document "count mod 32" in the
`SHLi`/`SHRi`/`SHRu` spec. Apply to `_VVC`/`_VCV`/`_CCC` variants identically.

---

## 5. Denormals depend on the host floating-point environment (FP-ENV)

**Current behavior.** GAZL guards *compile-time* FP correctness (`#error` on `-ffast-math`, GCC `no-finite-math-only`,
MSVC `float_control(precise)`, `GAZL.cpp:35`–`50`) but never sets or restores the run-time FPU control word. So
`float` operations inherit whatever **FTZ/DAZ** (flush-to-zero / denormals-are-zero) state the caller left in `MXCSR`
(x86) / `FPCR` (ARM). Audio hosts *very commonly* enable FTZ/DAZ on the audio thread to avoid denormal stalls. The
consequence: the same GAZL program can produce **different float results** depending on which host/thread called it —
denormal inputs/outputs are silently zeroed in one environment and not another. This affects the interpreter now.

**Recommended resolution.** This is not a bug to "fix" so much as a behavior to **specify**: document that *GAZL float
arithmetic follows the host's current FPU rounding/denormal mode; results involving denormals are therefore
host-defined.* If bit-exact cross-host reproducibility is ever required, add an **opt-in strict-FP mode** that saves the
control word on entry to `run()`, sets round-to-nearest + denormals-on, and restores it on return — but do **not** make
that the default (per-callback save/restore has a cost and would surprise host authors who deliberately set FTZ). Note
this must be a conscious choice, identical for interpreter and any future JIT.

---

## 6. `GETL` / `SETL` / `ADRL` cross-local access is defined only by accident (SPEC)

**Current behavior.** Dynamic local indexing bounds-checks against the *remaining data stack*, not the target array, so
a wild index (or an `ADRL`-derived pointer plus `ADDp`/`POKE`) can read/write *other* named locals — memory-safely, but
with a result that is an artifact of frame layout (`GAZL.cpp:1642`–`1645`). It is not UB and not cross-arch divergent
today, but it is **unspecified** at the language level.

**Recommended resolution — the *provenance-bounded* local-access rule.**
> ⚠️ An earlier draft proposed "reaching a different named local is unspecified; a JIT may assume distinct locals do
> not alias." **That is unsound — do not add it to the spec or tests.** Real compiled programs (`perfTest.gazl`,
> `buffer.gazl`, `nobuffer.gazl`) deliberately `ADRL` one local and `COPY` a block spanning a whole run of adjacent
> named locals (bulk load/store of a global-state struct into a bank of register-like scalars), and the observed output
> depends on that cross-local write. Every `ADRL` in the corpus also carries `*0`, so "within its declared `*size`" is
> not even a real bound.

Draw the defined/unspecified line by **provenance span** instead: a pointer derived from `ADRL var` in frame `F`
(through any `ADDp`/`SUBp`/`COPY`/arg-passing chain) is **defined** across `[&var, F.dsp)` — the target *upward through
the end of `F`'s locals* — and a dynamic `GETL`/`SETL` index is defined across `[base, F.dsp)`. **Crossing into
adjacent named locals *within* that span is defined behavior**: frame layout (declaration order, sizes, offsets) is
ABI, reproduced bit-for-bit. **Outside** the span (below the derivation point, past the owning frame's `dsp`, or into a
younger frame) the access is memory-safe but **unspecified**; a global-derived pointer is defined only within its own
declared section. The sandbox guarantee is unchanged (`BAD_PEEK`/`BAD_POKE` still fire at the true data-stack / RW
edges), and **no interpreter change is required** — today's contiguous-memory behavior is the conforming instance. This
is the weakest rule that keeps the bank idiom defined while still permitting a future optimizer/JIT to cache locals
that lie *below* every access's provenance floor. Add this wording — not the retracted version — to the
`GETL`/`SETL`/`ADRL`/`ADDp`/`SUBp` entries in `src/UnitTest.gazl` and `docs/InstructionSet.md`, and add golden `.gazl`
cases that *rely* on the defined span (a bank `COPY`, a by-ref out-param). *(Full rationale + the escape-floor
register-allocation analysis: JitCompilerResearch.md §1.1 / §5.7 on the `jit-compiler` branch.)*

---

## 7. `DIVi` / `MODi` truncation-toward-zero is now standard-guaranteed (SPEC, resolved)

**Current state.** The instruction notes warn that truncation toward zero for negative operands is *"not guaranteed by
all C/C++ compilers"* (`InstructionSet.md`, `UnitTest.gazl`). Since **C++11** this *is* guaranteed: integer division
truncates toward zero and `a%b` has the sign of `a` (`[expr.mul]`). GAZL already requires a modern toolchain.

**Recommended resolution.** Retire the caveat text (keep a regression test for `-7/2 == -3`, `-7%2 == -1`). No code
change. Bundle this with the item 1 div/mod fixes since they touch the same opcodes.

---

## Examined and found OK (no action)

Recorded so they are not re-litigated:

- **NaN comparisons in branches** (`EQUf`/`LSSf`/`NLSf`, `GAZL.cpp:1746`–`1755`). IEEE-754 makes `<`/`==` with NaN
  always false, identically on SSE2 and AArch64 scalar compares. `NLSf` = `!(a<b)` correctly yields *true* for NaN on
  both. Consistent; no change.
- **Unsigned pointer/address wraparound.** `ADDp`/`SUBp`/`DIFp` and the bounds checks operate on `UInt`/`Pointer`
  (`Pointer` is `unsigned`, `GAZL.h:81`); unsigned overflow is well-defined, and the `- MEMORY_OFFSET` unbias before the
  unsigned range compare (`GAZL.cpp:1636`) correctly catches under/overflow in one comparison. Sound as written.
- **Float division by zero.** Guarded to a runtime error by default (`GAZL_CHECK_FLOAT_DIVS_BY_ZERO`, `GAZL.h:73`); a
  deliberate, documented design choice, not UB.
- **`COPY` overlap.** Already *correctly* specified as undefined on overlap (`InstructionSet.md`) — a deliberate choice,
  exactly like C `memcpy` vs `memmove`. The implementation is a plain forward copy (`GAZL.cpp:1721`–`1728`), identical on
  every platform (no UB, no crash, no cross-arch divergence), so it is well-defined for the non-overlapping and
  `dst < src` cases callers actually use. Not a defect. The only latent subtlety is that the implementation is *stricter*
  than the spec (a program could accidentally depend on forward-copy behavior); if that ever matters, add a one-line note
  that the copy is ascending — but no fix is required, and any future JIT must simply match the forward-copy order.

---

## Suggested fix order

1. **Item 1** (`INT_MIN/-1` trap) — it is an outright crash / sandbox escape on x86. Highest priority regardless of JIT.
2. **Item 3** (signed-overflow UB) and **Item 4** (shift UB) — remove the latent UB and add `-fwrapv`; cheap, protects
   against future compiler/CPU changes.
3. **Item 2** (`FTOI`) — fix the existing cross-arch divergence and lock the semantics.
4. **Items 5–7** — specification/documentation decisions (denormals, local aliasing, div truncation note), plus the
   matching golden `.gazl` tests. These align with Phase 0 / Phase −1 B1 of the JIT plan but stand on their own.
   **Item 6 must ship the provenance-bounded wording, with tests that *rely* on the defined cross-local span** — a bank
   `COPY` and a by-ref out-param — so a later "distinct locals don't alias" optimization can never regress it.

Every code fix should be mirrored in the **compile-time constant folder** and covered by a test asserting the run-time
and compile-time results agree.
