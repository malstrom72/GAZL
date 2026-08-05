# GAZL C++ Backend - Specification

Status: draft / not yet implemented. Companion to `JitCompilerResearch.md` (§5.6 pipeline) and
`JitEmitterHandoff.md` (arm64 backend).

## 0. Purpose

A third GAZL backend that is a pure **source-to-source translator**: `Instruction[]` → a C++ source
string. It does no I/O, forks no compiler, and touches no platform APIs. What happens to that source -
compile it, embed it, check it in, read it - is entirely the caller's business.

That deliberate narrowness gives it two independent uses:

1. **Source-to-source product.** Translate a GAZL program to standalone C++ that another project can
   embed, AOT-compile, ship, or just inspect. No toolchain needed to *produce* the C++; the backend is
   a self-contained library function.
2. **Benchmark ceiling.** Feed that C++ through a real optimizer (`clang++ -O3`) and you get an answer
   to "how much faster *could* the arm64 JIT be?" If cpp is 3× and arm64 is 2×, there's headroom in the
   hand-written lowering; if they're neck-and-neck, stop optimizing. This is what makes it "fun for
   benchmarks."

The compile→load→run chain that use #2 needs is **not part of the backend** - it lives in the bench
tool (§6). Keeping the translator pure is what lets use #1 exist at all.

Non-goal: replacing the arm64 JIT for low-latency compile. The (external) `clang` step costs hundreds
of ms; anything that runs the emitted C++ is a batch/bench path, not an interactive JIT.

## 1. Position in the pipeline

Same facade as the machine-code backends. `compile()` stays arch-neutral; the cpp path is selected at
**run time** by a flag (unlike arm-vs-x86, which is a compile-time host decision - see the backend
split notes). cpp and native can coexist in one binary because cpp is host-independent.

```
source ── assemble ──▶ Instruction[] ──┬─▶ interpreter (Processor)
                                        ├─▶ arm64 JIT   (JitProcessor + mmap page)
                                        └─▶ C++ backend (C++ source string)
                                              └─ bench tool: clang + dlopen → JitProcessor
```

Unlike the machine-code backends, the cpp translator does **not** fill a `JitModule` or bind a
`JitProcessor` - those are runtime artifacts, and the translator stops at source. Wrapping the compiled
source in a `JitModule`/`JitProcessor` so it can run behind the `Processor` interface is the bench
tool's job (§6); the translator has no dependency on it.

## 2. Artifact & lifecycle

The **backend's** artifact is exactly one thing: a C++ source string (returned, or written to a stream
the caller supplies).

```
Instruction[] + functionTable + const memory
      │  CppEmitter (pass-1 analysis shared with arm64, pass-2 emits text)
      ▼
  C++ source string                         ◀── the backend ends here
```

Everything downstream is a **consumer** choice, drawn separately so the coupling is obvious:

```
  C++ source string
      │  (source-to-source use: save / embed / read - done)
      │
      │  (benchmark use, in the bench tool - §6:)
      ├─ write to temp file in the scratch dir
      ├─ fork clang++ -O3 -shared -fPIC -o module.dylib
      ├─ dlopen + dlsym("gGazlEntries")
      └─ wrap in JitModule{ dlHandle, entries, count } + JitProcessor to run
```

When the bench tool does wrap it, `JitModule` keeps its shape and RAII discipline (frees in dtor,
non-copyable, must outlive bound `JitProcessor`s), with `ownedPage`/`freeExecutable` replaced by
`dlHandle`/`dlclose`; a tag distinguishes the two module flavors. But that lives in the tool, not the
translator - on any downstream failure (no compiler in `PATH`, compile error, dlopen error) the tool
reports `ok() == false` and falls back to the interpreter, exactly like the arm64 unsupported-opcode
fallback.

## 3. Generated-code model

### 3.1 Context struct

Generated code operates on a small POD passed by pointer. It carries the hot state directly plus the
`Processor*` for anything that must go back through the base class (native calls, bounds helpers):

```cpp
struct GazlCtx {
    Value*      mem;        // memory base (already unbiased: mem[0] is pointer MEMORY_OFFSET)
    UInt        memSize;
    Value*      dsp;        // data-stack pointer (frame base for the running function)
    int32_t     fuel;       // decremented at safepoints; see §4
    Status      status;     // trap channel; OK unless a check failed
    Processor*  self;       // for nativeTable[i](self), accessParams, accessMemory
};
```

`JitProcessor` populates a `GazlCtx` from its own fields before each `run()` and reads `status` back
out. It does **not** need `layout()` offsets - the generated code is C++ and uses named fields - but the
field set is deliberately the same subset the arm64 `layout()` exposes, so the two backends stay in
lockstep semantically.

### 3.2 Frame slots → C++ locals (the whole point)

The interpreter keeps every local in the data-stack frame (`dsp[slot]`). The faithful translation would
do the same. The **fast** translation lifts frame slots to plain C++ locals:

```cpp
Value v0, v1, v2;   // was dsp[0], dsp[1], dsp[2]
```

and lets clang register-allocate, constant-fold, vectorize, and hoist. This is where the "optimizing
compiler ceiling" comes from - a slot that never escapes (no address taken via `ADRL`, not aliased by a
checked store) becomes an SSA value clang can keep in a register across a whole loop.

Two tiers, selectable:

- **Tier 0 (faithful):** slots stay as `dsp[slot]` (or the whole frame as one `Value frame[N]`). Trivial
  to emit, easy to verify, wins only the dispatch overhead. Good first milestone / cross-check.
- **Tier 1 (lifted):** emit slots as **bare C++ locals** and `ADRL $x` as `&x`. **clang's own
  mem2reg/SROA does the escape analysis and register promotion** - including keeping a local in memory
  exactly when its address escapes (`func(&x)`) and knowing a `mem[]` store can't alias a bare local. So
  the backend does **not** run its own escape/alias pass; it just must not lie to clang. The **only**
  backend-side classification is **span detection**: a `COPY` / pointer arithmetic that reaches
  across several named locals must group them into one `Value[]` sub-array, because C++ pointer arithmetic
  across separate objects is UB and clang can't rescue it. This is the same `*N`/region detection the
  machine backend needs, and it bites only the 4 known bank programs. See
  [JitAliasingRegAlloc.md](JitAliasingRegAlloc.md) §9.

Ship Tier 0 first (proves the pipeline), then Tier 1.

### 3.3 One function per GAZL function

Each GAZL function ordinal → one `static Status gazl_fn_<ord>(GazlCtx* c)`. An exported table lets the
loader and indirect calls resolve ordinals:

```cpp
extern "C" {
    typedef Status (*Entry)(GazlCtx*);
    const Entry  gGazlEntries[] = { gazl_fn_0, gazl_fn_1, /* ... */ };
    const size_t gGazlEntryCount = /* functionCount */;
}
```

### 3.4 Control flow

Branch targets found in pass-1 become C++ labels; GAZL branches become `goto`. No loop reconstruction -
clang recovers structure from the CFG. Straight, mechanical, and clang optimizes goto-form fine.

```cpp
L12:
    if (v3 >= v4) goto L20;     // BLT/BGE etc.
    v5 = v5 + v3;
    v3 = v3 + 1;
    goto L12;
L20:
```

### 3.5 Operators

One emission rule per GAZL opcode - the same `switch(op)` shape as the arm64 lowering, writing text
instead of machine words. Integer/float arithmetic maps to native C++ operators on `Value::i` / `.f`;
`FTOI`/`ITOF` to casts. Because both backends share pass-1 and the opcode set, adding an opcode is a
two-line change in each pass-2.

### 3.6 Memory & traps

GAZL memory ops are bounds-checked and raise `ACCESS_VIOLATION`. Two modes:

- **Checked (default):** each load/store validates like `accessMemory` and, on failure, sets
  `c->status = ACCESS_VIOLATION` and returns. This is the apples-to-apples comparison against the
  checked interpreter and checked arm64 JIT, and the mode used for verification.
- **Unchecked (`--fast`):** drop the checks - the "theoretical ceiling," UB on a bad access. Report it
  as a separate column, never as the default, and only for programs already proven trap-free by the
  checked run.

Pointer math mirrors the interpreter: a GAZL pointer `p` addresses `mem[p - MEMORY_OFFSET]`.

### 3.7 Calls

- **Direct GAZL call** (known ordinal): `Status s = gazl_fn_<ord>(c); if (s != OK) return s;` - clang may
  inline across these, a real advantage over the arm64 backend.
- **Indirect call** (runtime function pointer): `gGazlEntries[fp - IP_OFFSET](c)`.
- **Native call** (`CALL_NVC`): reuse the base machinery - params already sit on the data stack, so call
  `nativeTable[idx](c->self)` exactly as the interpreter does; propagate its `Status`.
- **Blocking-retry native** (`BLOCK_RETRY`): in run-to-completion mode, loop the native call until it
  returns something other than `BLOCK_RETRY` (equivalent to the interpreter re-issuing from the call
  site for a self-driven run).

## 4. Fuel / suspension - scope decision

The interpreter and arm64 JIT support fuel-limited execution: run N instructions, suspend with
`TIME_OUT`, resume mid-function (§5.7.5 RESUME). Reproducing arbitrary mid-function resume in generated
C++ needs safepoint checkpointing and a resume-dispatch `switch` - real machinery.

**v1: run to completion.** Benchmarks call `run()` with max fuel and finish in one shot, so the
suspend path is dead code for the bench scenario. v1 therefore does **not** support mid-run suspend;
if fuel is exhausted it returns a hard status rather than a resumable `TIME_OUT`. This makes the cpp
backend a benchmark/AOT reference, explicitly **not** a drop-in for cooperative scheduling.

Consequence for verification: cross-check against the interpreter at **full fuel only** (§7). The
tiny-fuel suspend/resume pass that `GAZLJitLowerTest` runs for arm64 is skipped for cpp.

**v2 (optional, later):** honor fuel by checking at loop back-edges and call sites, saving a resume
token (function ordinal + label id), and re-entering via `switch (c->resumePoint) { ... goto Lk; }`.
This reuses the pass-1 safepoint analysis the arm64 backend already computes.

## 5. Class structure

The backend (the pure translator) is small and self-contained:

- **`CppEmitter`** - analogue of `Emitter`, but appends C++ text to a `std::string`. One method per
  emitted construct.
- **`translateToCpp(code, functionCount, functionTable, memory) → std::string`** - the whole public
  surface of the backend. No toolchain, no `dlopen`, no `JitModule`. Deterministic text out.
- **Shared pass-1.** Factor the arch-neutral analysis (branch targets, loop heads, safepoints, SWCH
  table decode, and - for Tier 1 - slot escape analysis) out of `lowerFunction` into a helper both the
  arm64 and cpp pass-2 call. This is the refactor flagged in the backend-split discussion; the cpp
  backend is the forcing function for it.

Lives in `src/GAZLCpp.cpp` / `.h` (a translator, not a JIT - distinct from `GAZLJit.*`).

The run-it machinery is **not** in the backend - it's in the bench tool (§6): the `dlopen`-backed
`JitModule` path and the reused `JitProcessor` (whose `run()` calls the module's entry, `enterCall()`
binds the target ordinal - no native dispatcher needed) are assembled there.

Rename note: once cpp lands alongside a future x86 emitter, `Emitter` should become `Arm64Emitter` so
`CppEmitter` / `X86Emitter` read as peers. The pass-1 split above is the prerequisite.

## 6. Reference runner (bench tool, not the backend)

This is what turns translated source into something runnable for use #2. It belongs to the bench tool -
the backend has no part in it and never links it.

- Call `translateToCpp(...)`; write the returned string to the session scratch dir.
- Fork `clang++ -O3 -shared -fPIC -std=c++03 -o module.dylib module.cpp` (Posix: `-std=c++03`, `.so`).
  Capture stderr for diagnostics on failure.
- `dlopen(RTLD_NOW|RTLD_LOCAL)`, `dlsym("gGazlEntries")` + `gGazlEntryCount`.
- Wrap the handle in a cpp-flavored `JitModule` and drive it with `JitProcessor` (§2), so it runs behind
  the same `Processor` interface as the interpreter and arm64 JIT - uniform bench harness.
- **Caching (optional, nice for repeated bench runs):** hash the GAZL `Instruction[]` + flags → reuse a
  cached `.dylib` keyed on that hash. Skips the clang fork on a warm run.
- **Failure → fallback:** any step failing sets `module.ok() = false`; the tool uses the interpreter.
- **Timing:** the clang fork dominates "compile"; report it in `--jit-stats` as `compile_ms` (already the
  column name) so cpp and arm64 compile costs sit side by side, honestly labeled.

## 7. Verification

Reuse the `GAZLJitLowerTest` harness at **full fuel**: for each kernel, compile through the cpp backend,
run, and compare the whole memory image + final `Status` against the interpreter (the semantic oracle).
Since arm64 is already verified against the interpreter, an interpreter-vs-cpp check closes the loop
between all three backends. Run the checked-memory mode for verification; the unchecked mode is
benchmark-only and is not asserted for correctness. A new `GAZLJitCppTest` (or a `--backend` axis added
to the existing test) drives it.

## 8. Benchmark integration

- `GAZLCmd` gains `--backend=native|cpp` (default `native` where a JIT exists; `--jit` stays an alias
  for `native`). arm-vs-x86 never appears here - the binary holds only the host's machine-code emitter.
- `bench.sh` adds a **cpp** column next to interp and arm-jit: speedup vs interpreter, `compile_ms`,
  and `code_bytes`/source size, plus the checksum verification already in the suite.
- The headline number is `cpp_speedup / arm64_speedup` per kernel - the remaining headroom in the
  hand-written arm64 lowering, and the whole reason to build this.

## 9. Non-goals (v1)

- The backend does **not** fork a compiler, `dlopen`, or run anything - it emits text and stops (§0,
  §5). All of that is the bench tool's runner (§6).
- No mid-run suspend/resume (full-fuel only; §4).
- No W^X handling (the OS loader maps the `.dylib`, and that's the runner's concern anyway).
- Unchecked memory mode is opt-in and never the correctness baseline.

The **translator** is a candidate for the shipped library (it's pure and portable, useful as a
source-to-source tool); the **runner** is bench-tool-only and needs `clang` at run time.

## 10. Decisions needed before coding

1. Tier 0 only for the first cut, or straight to Tier 1 slot-lifting? (Recommend: Tier 0 to prove the
   pipeline + verification, then Tier 1 for the real number.)
2. Do the pass-1 refactor (shared analysis) up front, or copy analysis into the cpp pass first and
   unify once x86 forces it? (Recommend: refactor now - cpp is a clean second consumer and the split is
   cheap today.)
3. Ship the `.dylib` cache in v1, or defer? (Recommend: defer; correctness first.)
