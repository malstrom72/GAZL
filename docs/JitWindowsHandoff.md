# Windows x64 JIT - build recipe + validation record

**Repo:** GAZL, branch **`jit-compiler`**. **Owner:** Magnus Lidström.

**Status: DONE and validated on native Windows.** The x86-64 JIT backend runs on Windows with the Win64 calling
convention. Built and tested on **SILICON** (Windows 10 22H2, x64) with **MSVC 19.44** (Visual Studio 2022 Community):
both the Win64 ABI backend and the `VirtualAlloc` executable-memory backend compiled clean first try, and the JIT
produces byte-identical results to the interpreter across the whole sample set.

## How to build (Windows)

The Windows build is repeatable through the normal script - no manual `cl` line:

```
tools\buildGAZLCmd.cmd release      REM or: beta
```

`buildGAZLCmd.cmd` detects the host architecture (AMD64 → `GAZLJitX64.cpp`, ARM64 → `GAZLJitArm64.cpp`), adds the
shared `GAZLJit.cpp` and the Windows W^X backend `GAZLJitMemWindows.cpp`, and passes `/DGAZL_JIT` - mirroring the POSIX
`buildGAZLCmd.sh`. On an unrecognized architecture the JIT sources are dropped and `--jit` falls back to the
interpreter. `build.cmd` (which calls `buildGAZLCmd.cmd beta`/`release`) therefore builds a JIT-enabled binary too; the
JIT is inert unless a run passes `--jit`, so the rest of `build.cmd` is unaffected.

Compiler is MSVC via `vcvarsall` (found by `BuildCpp.cmd` through `vswhere`). No clang needed - the JIT emits its own
machine-code bytes, so the host compiler only builds the C++.

## Validation results

Every program run interpreter-vs-`--jit`, output diffed - **16 / 16 byte-identical**:
- op-tests (`tests\bench\golden\op_*.gazl`): `abs add div ftoi mod mul shl shr shru sub xor`
- benchmarks (`benchmarks\suite\golden\*.gazl`): `mandelbrot montecarlo sieve sor spectralnorm`

Native x64 JIT speedup (min-of-10, MSVC `/O2` interpreter baseline):

| bench | interp | JIT | speedup |
|---|---|---|---|
| sieve | 395 ms | 88 ms | 4.48× |
| spectralnorm | 431 ms | 127 ms | 3.4× |
| sor | 469 ms | 196 ms | 2.39× |
| montecarlo | 320 ms | 157 ms | 2.04× |
| mandelbrot | 293 ms | 234 ms | 1.25× |

Modest and workload-dependent vs the arm64 (5×) / Rosetta (7×) figures - expected, because MSVC `/O2` is a fast native
interpreter baseline and this is still the v1 lowering with **no register allocation** (roadmap #20).

## The port, and the Win64 ABI reasoning (reference)

Two pieces, both now runtime-verified:

1. **`src/GAZLJitMemWindows.cpp`** - the W^X backend the header (`src/GAZLJitMem.h`) always named as the third platform.
   `VirtualAlloc(PAGE_READWRITE)` → `memcpy` → `VirtualProtect(PAGE_EXECUTE_READ)` → `FlushInstructionCache`;
   `VirtualFree(MEM_RELEASE)` to free. No special entitlement (unlike macOS' hardened runtime). The only TU that
   includes `<windows.h>`.

2. **Win64 ABI in `src/GAZLJitX64.cpp`**, all behind `#if defined(_WIN32)`. The insight that keeps it small: the pinned
   registers (`rbx/r12/r14/r15`) are callee-saved and the scratch (`rax/rcx/rdx/xmm0/xmm1`) is caller-saved on **both**
   SysV AMD64 and Win64, so every instruction *body* is ABI-neutral. Only boundary code differs, through two constants:
   - `ARG_0..ARG_3` - the entry ABI `Status fn(Value* dsp, Value* memory, Value* dataStackEnd, JitProcessor* ctx)`.
     SysV: `rdi/rsi/rdx/rcx`. Win64: `rcx/rdx/r8/r9`.
   - `CALL_FRAME` - bytes subtracted from `rsp` per frame. SysV `8` (16-align pad only). Win64 `40` = **32-byte shadow
     space** (every callee may spill its 4 register args there) **+ 8 align**. Reserved once in the prologue and once in
     the dispatcher; inner calls reuse it (all calls have ≤4 args, no stack args).
   - **`rep movsd` COPY** is the one place Win64's *callee-saved* `rsi`/`rdi` bite (caller-saved on SysV): the block
     `push`/`pop`s them around the copy, Win64-only.

   xmm6-xmm15 are callee-saved on Win64 but the backend only uses xmm0/xmm1, so nothing to save there.

**Alignment math:** at entry `rsp % 16 == 8`. Prologue's 4 pushes add 32 (still `% 8`); `sub rsp, CALL_FRAME` (40) →
`% 0` before the `call`. The dispatcher makes no pushes; `sub rsp, 40` from `% 8` → `% 0`. Both leave 32 bytes of shadow.

The SysV path is byte-for-byte unchanged by this refactor - the engine test still passes 14/14 under Rosetta.

## Failure modes → where to look (if a future change regresses Windows)

- **Crash on the first call or return** → shadow space / alignment. Recheck `CALL_FRAME`, the prologue `sub` and the
  matching epilogue `add`, and the dispatcher `sub`/`add`.
- **Garbage right after a COPY** → the Win64 `push RSI/RDI … pop RDI/RSI` guard around `rep movsd`.
- **Crash entering JIT code at all** → the dispatcher's `ARG_n` loads, or an entry expecting `rcx=dsp` (Win64 ARG_0).
- **`makeExecutable` returns 0 / access violation on alloc** → a policy like Arbitrary Code Guard; the compiler then
  falls back to the interpreter (`out.ok()==false`).

## Still open / out of scope

- **Kernel test** - the x64 backend now runs the unified `tools/GAZLJitLowerTest.cpp` (via
  `tools/buildAndRunGAZLJitLowerTest.cmd`), which diffs JIT vs interpreter over the whole memory image at full fuel
  *and* tiny fuel (forcing suspend/resume). The old run-to-completion `GAZLJitX64EngineTest` has been retired.
- **Execution model (updated - encoding b).** The x64 backend runs the §5.4 dispatcher model at parity with arm64, now
  **direct-threaded** (roadmap #19): the dispatcher owns the single native frame and reloads the pins once, then GAZL→GAZL
  calls/RETU **tail-branch directly** (`jmp`/`jmpReg`, the ipStack holds the return address) and native calls run **inline**
  (`call [natives[ordinal]]`, ctx in the ABI arg register) - no per-call TRANSFER round-trip. A segment returns to the
  dispatcher only to suspend (TIME_OUT), finish (OK), or trap. Fuel timeouts still suspend and resume from any point,
  including inside nested GAZL calls. Win64 note: because the pins are callee-saved, an inline native preserves them, and
  the native's Status returns in `eax` (never clobbered by ctx, which lives in `r12`); only the fuel pin (a native may
  `resetTimeOut(0)`) and the window dsp are refreshed from ctx after the call. On the call-heavy fib benchmark this took
  native-Windows JIT from **118 ms → 53 ms** (5.0× vs the 267 ms interpreter). The earlier "run-to-completion / native
  call stack" and per-call TRANSFER descriptions elsewhere in this doc are historical.
- **Windows-on-ARM64.** The arm64 backend already avoids `x18` (the Windows platform register) and matches base
  AAPCS64, and `GAZLJitMemWindows.cpp` is architecture-neutral, so `buildGAZLCmd.cmd` already selects `GAZLJitArm64.cpp`
  on an ARM64 host - but that path is unbuilt/untested. The remaining gap is SEH unwind metadata, only relevant if C++
  exceptions unwind through JIT frames, which GAZL avoids (it returns `Status`).
- **v2 register allocation** (roadmap #20) is the lever for the perf gap above.
