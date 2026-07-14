# Hand-off: enable the x64 JIT on Windows (Win64 ABI + exec-memory backend)

**For:** the agent taking this over **on a real Windows machine**. Self-contained — read it fully first. **Repo:** GAZL,
branch **`jit-compiler`**. **Owner:** Magnus Lidström.

**Do not `git commit` or `git push`** unless Magnus explicitly says so (standing rule). Leave work in the tree and
report back.

## What this is

The x86-64 JIT backend (`src/GAZLJitX64.cpp`) already works: it was written and verified on Apple Silicon against the
interpreter **under Rosetta** (14/14 engine kernels pass — see `tools/GAZLJitX64EngineTest.cpp`). Rosetta runs an x64
process using the **SysV AMD64** calling convention, same as Linux. Windows x64 uses a *different* convention (**Win64**),
so the backend needed ABI-conditional boundary code, and Windows needs its own W^X executable-memory backend.

Both pieces are **already written** (by the previous agent, on macOS) and **compile clean**, but have **never been
built with a Windows toolchain and never executed on Windows**. Your job is to build them natively and make the test
suite pass — closing the one loop that could not be closed off-Windows.

## What is already done (compile-verified, runtime-UNVERIFIED)

1. **`src/GAZLJitMemWindows.cpp`** — the W^X backend the header (`src/GAZLJitMem.h`) always promised as the third
   platform. `VirtualAlloc(PAGE_READWRITE)` → `memcpy` → `VirtualProtect(PAGE_EXECUTE_READ)` → `FlushInstructionCache`;
   `VirtualFree(MEM_RELEASE)` to free. Needs no special entitlement (unlike macOS' hardened runtime). It is the *only*
   TU that includes `<windows.h>`, so it has never touched this mac's compiler — **expect to smoke-test it first.**

2. **Win64 ABI in `src/GAZLJitX64.cpp`**, all behind `#if defined(_WIN32)`. The design insight that keeps this small:
   the pinned registers (`rbx/r12/r14/r15`) are callee-saved and the scratch (`rax/rcx/rdx/xmm0/xmm1`) is caller-saved
   on **both** ABIs, so every instruction *body* is ABI-neutral. Only the boundary code differs, funnelled through two
   constants near the top of the file:
   - `ARG_0..ARG_3` — the entry ABI `Status fn(Value* dsp, Value* memory, Value* dataStackEnd, JitProcessor* ctx)`.
     SysV: `rdi/rsi/rdx/rcx`. Win64: `rcx/rdx/r8/r9`.
   - `CALL_FRAME` — bytes subtracted from `rsp` per frame. SysV `8` (just the 16-align pad). Win64 `40` = **32-byte
     shadow space** (every callee may spill its 4 register args there) **+ 8 align**. Reserved once in the prologue and
     once in the dispatcher; inner calls reuse it (all our calls have ≤4 args, no stack args).
   - **`rep movsd` COPY** is the one place Win64's *callee-saved* `rsi`/`rdi` bite (they are caller-saved on SysV): the
     block now `push`/`pop`s them around the copy, Win64-only.

   xmm6–xmm15 are callee-saved on Win64 but the backend only uses xmm0/xmm1, so nothing to save there.

**Alignment math to trust (verify if you get a crash at the first call):** at entry `rsp % 16 == 8`. The prologue's 4
pushes add 32 (still `% 16 == 8`); `sub rsp, CALL_FRAME` (40) → `% 16 == 0` before the `call`. The dispatcher makes no
pushes; `sub rsp, 40` from `% 8` → `% 0`. Both leave 32 bytes of shadow below `rsp`.

The SysV path is **byte-for-byte unchanged** by this refactor — the engine test still passes 14/14 under Rosetta.

## What YOU must do (the part that needs Windows)

1. **Build `GAZLJitMemWindows.cpp` alone first** as a smoke test (it is the only unproven `<windows.h>` code).

2. **Build the x64 GAZLCmd** with these sources + `-DGAZL_JIT`, C++11:
   ```
   tools/GAZLCmd.cpp  src/GAZL.cpp  src/GAZLCpp.cpp  src/GAZLJit.cpp  src/GAZLJitX64.cpp  src/GAZLJitMemWindows.cpp
   ```
   Example with clang-cl (adapt to MSVC `cl` if preferred):
   ```
   clang-cl /std:c++14 /EHsc /DGAZL_JIT /I . tools\GAZLCmd.cpp src\GAZL.cpp src\GAZLCpp.cpp ^
            src\GAZLJit.cpp src\GAZLJitX64.cpp src\GAZLJitMemWindows.cpp /Fe:GAZLCmd.exe
   ```
   Note: `src/GAZLJit.h` and `src/GAZL.h` are kept **C++03-clean** (shipped headers); the backend `.cpp` files may use
   C++11. `_WIN32` is predefined by every Windows toolchain, so the ABI branches activate automatically.

3. **Port the engine test.** `tools/GAZLJitX64EngineTest.cpp` assembles kernels, JIT-compiles via `JitCompiler`, runs
   via `JitProcessor`, and diffs status + the whole memory image against the interpreter. It currently builds through a
   bash script (`buildAndRunGAZLJitX64EngineTest.sh`) under Rosetta. Build it natively on Windows with the same source
   list (swap `GAZLCmd.cpp` for the test's own `main`, keep `GAZLJitMemWindows.cpp`). **All 14 kernels must pass** —
   that is the acceptance bar. The one kernel that most exercises the Win64 deltas is the COPY kernel (rsi/rdi) and any
   kernel with GAZL→GAZL / native calls (shadow space).

4. **Sanity-run** a few sample programs: `GAZLCmd --jit prog.gazl` vs `GAZLCmd prog.gazl` (interpreter) and diff output.

## Expected failure modes → where to look

- **Crash on the first call or return** → shadow space / alignment. Recheck `CALL_FRAME`, the prologue `sub` and the
  matching epilogue `add`, and the dispatcher `sub`/`add`.
- **Garbage / corruption right after a COPY** → the Win64 `push RSI/RDI … pop RDI/RSI` guard around `rep movsd`.
- **Crash entering JIT code at all** → the dispatcher's `ARG_n` loads, or an entry expecting `rcx=dsp` (Win64 ARG_0).
- **`makeExecutable` returns 0 / access violation on alloc** → a policy like Arbitrary Code Guard. The compiler must
  then fall back to the interpreter (the `out.ok()==false` path); verify that fallback actually engages.

## Do NOT touch / out of scope

- The **SysV path** (verified under Rosetta), the **arm64 backend**, and shared **`src/GAZLJit.cpp`**. Do not re-open
  any ABI *design* decision above — they are worked out; your job is to make them run.
- **Windows-on-ARM64** is a separate future task. Good news for later: the arm64 backend already avoids `x18` (the
  Windows platform register) and matches base AAPCS64, and `GAZLJitMemWindows.cpp` is architecture-neutral, so it
  serves ARM64 too. The remaining gap there is SEH unwind metadata — only relevant if C++ exceptions unwind through JIT
  frames, which GAZL avoids (it returns `Status`). Leave it unless Magnus asks.

## Report back

Leave everything in the tree, summarize what built, what passed, and any ABI fix you had to make (that feedback is the
whole point of running on real Windows). **No commit/push unless Magnus says so.**
