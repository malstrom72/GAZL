# A Just‑Before‑Running Native Compiler for GAZL — Research & Design

**Status:** research / design exploration (not a commitment). **Scope:** compiling GAZL bytecode to x86‑64 and
AArch64 machine code at program *load* time, inside an audio‑plugin process, while keeping the existing interpreter as
a universal fallback. **Audience:** GAZL/Impala maintainers.

This document surveys the state of the art (2025–2026), maps it onto GAZL's specific semantics, and proposes a concrete
architecture with a phased roadmap. It is deliberately opinionated: the goal is to reach *one* buildable plan, not to
list every option neutrally. Repo facts are cited as `file:line`; external claims link to primary sources.

---

## 0. TL;DR — the recommendation

Build a **single‑pass, load‑time "baseline" JIT** that runs off the audio thread when a patch is loaded, keeping the
current interpreter as **tier 0** (reference semantics + fallback). Emit code through a **thin hand‑written
`Emitter` layer** with two backends (x64, AArch64), not through LLVM/Cranelift/wasm. Keep GAZL's exact
software bounds‑checks (**no guard pages, no signal handlers** — a plugin must never touch process‑wide signal state).
Make the JIT's state layout *bit‑identical* to the interpreter's at safepoints so the two engines are interchangeable and
**differentially testable in lockstep**.

Why this shape:

- **GAZL is already 80 % of a wasm‑class safe VM**, but simpler. It is statically typed, 3‑address, has no GC, no
  deopt, no speculation, immutable code, structured intra‑function control flow, and it already bounds‑checks every
  memory op (`GAZL.h:379`). Those are exactly the properties that make a JIT *small and safe*, and they structurally
  eliminate the bug classes that dominate V8/JSC JIT CVEs (type confusion via speculation, deopt‑state mismatches,
  GC/ic races). We are building the easy 90 % of a baseline compiler and skipping the hard, dangerous 10 %.
- **The gating constraint is not codegen — it is executable memory inside someone else's process.** On Apple Silicon a
  plugin can only allocate JIT memory if *the host DAW* carries a JIT entitlement, and on iOS it cannot at all. So the
  interpreter fallback is not optional polish; it is the product on a meaningful fraction of installs. This forces a
  design where JIT is a bolt‑on accelerator over an authoritative interpreter, which happens to also be the safest and
  most testable design.
- **A hand‑written baseline beats every framework on the axes we actually care about**: binary size (single header,
  BSD‑2, no Rust/LLVM dependency), compile latency, control over the exact sandbox check sequence, and auditability of
  emitted code. We give up peak throughput — but a baseline JIT over frame slots already buys the bulk of the win
  (roughly 3–10× over a switch interpreter for numeric loops; see §9), and DSP inner loops that need more should be
  written as native ops anyway.

Expected effort: a usable x64+ARM64 baseline is a **few months for one experienced engineer**, dominated by the
test harness, not the emitters. The emitters are small because GAZL has ~120 already‑specialized opcodes that map
almost 1:1 to machine instructions.

---

## 1. Why GAZL is an unusually good JIT target

Most of the pain in JIT engineering comes from dynamic language features GAZL simply does not have. Cataloguing GAZL's
semantics against a would‑be compiler (all verified against `src/GAZL.cpp` @ `a29bd02`):

| Property | GAZL reality | Consequence for a JIT |
|---|---|---|
| Types | Static; every value is one 32‑bit word, `int`/`float`/`ptr` distinguished per‑opcode (`GAZL.h:91`, `GAZL.h:97`) | No boxing, no type guards, no speculation, no deopt. Each opcode has one native lowering. |
| Instruction form | 3‑address, mode‑specialized (`V`=frame slot, `C`=inline constant, `B`=relative branch); ~120 opcodes (`GAZL.cpp:194`) | Near 1:1 opcode→instruction. The assembler already did constant folding, operand‑commutation (`SWAP_0_AND_1`), NOOP removal, and label resolution. |
| Locals | Frame slots at fixed `dsp`‑relative offsets, resolved at assembly time (`GAZL.cpp:1162`) | No register allocation *required* for correctness — slots live in memory; regs are a pure optimization. |
| Control flow | Relative branches, **intra‑function only** (labels are function‑local and cleared per function, `GAZL.cpp:1176`); fused `FORi`/`FORp`; `SWCH` jump table in read‑only memory (`GAZL.cpp:1757`) | A function is a clean CFG with statically known edges → trivial basic‑block construction, no cross‑function branch fixups. |
| Memory | One `Value[]` array `[globals | data stack | consts]`; reads checked `< memorySize`, writes `< rwMemorySize` (`GAZL.h:379`–`389`); 32‑bit biased pointers (`MEMORY_OFFSET`) | wasm32‑like linear memory, but every access is *already* an explicit range check. We just emit that check. |
| Calls | Return addr + caller `dsp` pushed to a **non‑VM‑addressable** `CallStackEntry` stack (`GAZL.cpp:1614`); zero‑copy args via transient window; `FUNC` prologue checks stack overflow (`GAZL.cpp:1607`) | Return addresses cannot be corrupted by VM code — CFI for free. Calls lower to native calls or to a dispatch trampoline. |
| Indirect calls | Runtime‑checked: target index `< codeSize` **and** `code[idx].opcode == FUNC_CC_` (`GAZL.cpp:1609`) | Emit an entry‑offset table lookup with the same two checks → safe indirect branch. |
| Interruption | Fuel: `--clockCyclesLeft >= 0`, 1/instr (`GAZL.cpp:1605`); native returning nonzero suspends *at* the CALL for retry (`GAZL.cpp:1620`) | Cooperative; we insert fuel checks at back‑edges. No preemption, no async signals needed. |
| Suspend/resume | Full state = `Processor` fields + memory + ipStack; `Processor` is copyable; resumes by re‑`run()` | Requires safepoints where state is interpreter‑identical. Drives the whole architecture (§5). |
| Code | `const Instruction*`, immutable after load, shareable across `Processor`s/threads | Compile once, run many; emitted code must be position‑independent and reentrant (state via context register). |

The "sharp edges" a JIT must respect are the three instructions that expose the data stack to dynamic/address access —
`GETL`, `SETL`, `ADRL`. They do **not** block a correct JIT (v1 keeps every local in memory and is trivially
interpreter‑exact); they only bound how aggressively **v2** may cache locals in registers. Their exact semantics
(verified against source) and an empirical measurement of how much they actually matter are in **§1.1** — the short
version is: they operate on *local arrays*, not scalar working variables, they are rare (~2.8 % of instructions
combined), and the hot numeric kernels contain **none** of them, so whole‑function scalar register allocation is
available exactly where it pays off.

### 1.1 `GETL` / `SETL` / `ADRL` — exact semantics, aliasing, and what it costs register allocation

These three are the only ways a GAZL function reaches stack memory *dynamically* or *by address*, so they are the whole
of the aliasing question. Precise semantics (interpreter `GAZL.cpp:1642`–`1645`, operator table `GAZL.cpp:359`/`469`/
`751`):

- **`GETL dst, arrayBase, index`** → `dst = (dsp + arrayBaseSlot)[index]`. `arrayBase` is a *local variable operand*
  encoded as a **constant slot offset** (the base of a `LOCA` array); `index` is a **dynamic** int read at runtime. It
  is a local‑array indexed load.
- **`SETL arrayBase, index, value`** → `(dsp + arrayBaseSlot)[index] = value`. Indexed store; `value` may be a var or a
  constant (`SETL_VVV`/`SETL_VVC`).
- **`ADRL dstPtr, var, *size`** → `dstPtr = &(dsp + varSlot) − memBase` (a VM pointer to a local). The `*size` hint is
  not ignored: via the `LOCAL_BOUNDS` flag the assembler grows the reserved frame so `&var + size` stays in‑frame
  (`GAZL.cpp:1526`). `ADRL` is how a local array is passed by reference to a function/native or accessed via
  `PEEK`/`POKE`/`COPY`.

**The bounds check is a sandbox bound, not an array bound.** `GETL`/`SETL` check `index < (dataStackEnd − dsp −
base)` with `index` taken as **unsigned** — so a wild index can read/write *other* slots at offset ≥ the array base, up
to the end of the data stack, but can never escape the data stack. It is frame‑unsafe by design, sandbox‑safe by
construction. `ADRL` + pointer arithmetic (`ADDp`/`SUBp`) + `POKE`/`COPY` is even broader: because the data stack lives
inside the `POKE`‑writable RW region, a derived pointer can in principle address any data‑stack slot (still bounds‑
checked against `rwMemorySize`).

**Empirical frequency** (measured across the 57 real compiled programs in `tests/impala/golden/`, 16,704 executable
instructions total):

| Op | Count | Share | For comparison |
|---|--:|--:|---|
| `ADRL` | 327 | 2.0 % | `PEEK` 1425 (8.5 %) |
| `GETL` | 73 | 0.4 % | `POKE` 1294 (7.7 %) |
| `SETL` | 63 | 0.4 % | `CALL` 1875 (11 %) |
| `COPY` | 39 | 0.2 % | |

And the distribution matters more than the totals:

- **`ADRL` targets are local arrays, essentially never scalar working variables** — the measured targets are
  `$buffer`, `$fftBuffer`, `$delays:8`, `$gains:8`, `$samples`, `$moves`, `$tos`, `$cells`, `$line:0`, … taken into a
  transient that is immediately consumed by a call (`print(buffer)`, an FFT, etc.) or a `PEEK`/`POKE`. A few look like
  by‑reference out‑parameters (`$endGain`, `$maxDelay1`). Either way the JIT rule is the same: *the slot whose address
  is taken becomes memory‑resident.*
- **`GETL`/`SETL` are array subscripting with a runtime index** — `counts[color]`, `moves[capturesCount]`,
  `fftInput[idx]`, `mydata[i]`. They only appear alongside arrays that are already memory‑resident.
- **15 of 57 programs contain *zero* `ADRL`/`GETL`/`SETL`** — and they include the compute‑bound numeric kernels where a
  JIT wins most: `MLMoogFilter`, `perfTest1`, `perfTest2`, `BitMaskMod`, `ModTest`, `linsub`. In those, every local is a
  scalar with no address exposure → **fully register‑allocatable across the whole function.**

**Consequence for register allocation — a clean escape analysis.** Do a one‑pass scan of each function and mark a slot
*escaping* if it is ever an `ADRL` operand, or lies within a `LOCA` that is ever a `GETL`/`SETL` base. Then:

- **v1:** ignore all of this — every local is memory‑resident, so the JIT does exactly what the interpreter does and is
  bit‑identical by construction. `GETL`/`SETL`/`ADRL` lower to the same checked memory ops the interpreter runs.
- **v2:** cache **non‑escaping scalar slots** in registers across the whole function; keep **escaping slots** (arrays,
  address‑taken vars) memory‑resident. Write back dirty scalar registers only at **safepoints** (native calls — a
  callee could `enterCall` back in and `POKE` through a passed array pointer; potential‑timeout back‑edges; returns).
  Because escaping slots are precisely the ones reachable by a derived pointer, no non‑escaping scalar can be aliased by
  a `POKE`/`SETL`, so this is safe *and* needs no barriers around `GETL`/`SETL`/`POKE` for cached scalars.

**Spec decision — ADOPTED.** The interpreter today gives a *specific* result when a wild `SETL`/`ADRL`‑pointer reaches a
*different* named local (it writes that slot's memory). We tighten the spec so v2's whole‑function scalar caching is
bit‑identical to the interpreter. The normative rule (to be mirrored into the `GETL`/`SETL`/`ADRL`/`ADDp`/`SUBp` entries
in `src/UnitTest.gazl` and `docs/InstructionSet.md` in Phase 0):

> **Local‑access bounds rule.** A dynamic local index (`GETL`/`SETL`) accesses only the named local array, and a pointer
> obtained from `ADRL` accesses only the named local variable/array, each within its declared `*size`. Reaching a
> *different* named local — whether by an index past the array's declared size or by arithmetic on an `ADRL`‑derived
> pointer — is **memory‑safe but yields an unspecified value** (the access still cannot leave the data stack; a true
> out‑of‑stack access still raises `BAD_PEEK`/`BAD_POKE`, and the fuel limit still applies). Implementations (interpreter,
> JIT, AOT) may therefore assume distinct named locals do not alias.

This mirrors C (out‑of‑bounds access *within* your own frame is UB, but can't corrupt the runtime) and the existing
`*size`/`LOCAL_BOUNDS` machinery that already declares array extents. It changes nothing about the sandbox guarantee and
nothing for v1. The rejected alternative — preserve exact interpreter aliasing — would force v2 into basic‑block‑local
caching with write‑back around every `SETL`/`ADRL`‑`POKE`; still full speed on the 15 zero‑escape kernels, but weaker
elsewhere for no real‑world benefit (no observed Impala output depends on cross‑local aliasing).

---

## 2. The real gating constraint: executable memory inside a host process

This section is first because it determines whether a JIT can run *at all* on a given install, and therefore why the
interpreter fallback is load‑bearing rather than decorative.

### 2.1 macOS (the hard case)

Apple's Hardened Runtime forbids writable‑and‑executable memory unless the **main executable** carries an entitlement.
The three relevant ones ([Apple docs](https://developer.apple.com/documentation/BundleResources/Entitlements/com.apple.security.cs.allow-jit),
[Kyle Avery, *macOS JIT Memory*](https://kyleavery.com/posts/macos-jit-memory/)):

- **`com.apple.security.cs.allow-jit`** — permits `mmap(MAP_JIT)` RWX regions; the standard, Apple‑blessed path. On
  Apple Silicon a `MAP_JIT` region is **never** simultaneously writable and executable; you flip per‑thread with
  `pthread_jit_write_protect_np(0)` (writable) / `(1)` (executable)
  ([man page](https://keith.github.io/xcode-man-pages/pthread_jit_write_protect_np.3.html)).
- **`allow-unsigned-executable-memory`** — functionally similar for our purposes (execute unsigned code in RW→RX
  memory).
- **`disable-executable-page-protection`** — broadest (disables W^X enforcement entirely); discouraged and a
  notarization red flag.

The decisive fact for a plugin: **entitlements are a property of the host process, not the plugin dylib.** A dylib
loaded via `dlopen` inherits the host's security context; *"calls to `mmap()` with `MAP_JIT` will fail"* without the
entitlement, and by default JIT‑write is not even allowed in dynamically loaded frameworks
([pthread_jit man page](https://keith.github.io/xcode-man-pages/pthread_jit_write_protect_np.3.html),
[Kyle Avery](https://kyleavery.com/posts/macos-jit-memory/)). So the plugin's ability to JIT depends entirely on which
DAW loaded it. Consequences:

- We **cannot** assume JIT works. We must *probe* at runtime (attempt a tiny `MAP_JIT` alloc + flip + execute a `ret`)
  and fall back to the interpreter on failure. Never abort, never crash.
- Intel Macs under Hardened Runtime still need the entitlement, but lack the per‑thread `pthread_jit_write_protect_np`
  toggle (it's an Apple‑Silicon SPRR feature); on Intel you use classic `mprotect` RW↔RX flips.
- A dual‑mapping trick (two virtual mappings of one physical page — one RW, one RX — via `vm_remap`/`mach_make_memory_entry`)
  historically sidesteps the per‑thread toggle but is exactly what Hardened Runtime is designed to block without the
  entitlement; it buys nothing here and adds attack surface. Don't.
- After writing, on ARM you must `sys_icache_invalidate()` before executing (§6).

**Which DAWs ship the entitlement?** There is no authoritative *published* table, but it is directly measurable with
`codesign -d --entitlements - --xml <App>`. A sweep of one developer's installed hosts (macOS, 2026‑07) gives the
following — and the result is **much more favourable than "assume off"**: every current third‑party DAW carries at least
`allow-unsigned-executable-memory`, and most carry `allow-jit` too.

| Host (versions sampled) | Hardened RT | `allow-jit` | `allow-unsigned-executable-memory` | `disable-exec-page-protection` | Can host a JIT? |
|---|:--:|:--:|:--:|:--:|:--|
| Cubase 11/14/15, Nuendo 11/12 | ✅ | ✅ | ✅ | — | **Yes — MAP_JIT path** |
| REAPER | ✅ | ✅ | ✅ | — | **Yes — MAP_JIT path** (also entitles its own EEL2 JIT) |
| Bitwig 3.3 / 5.3 | ✅ | ✅ | ✅ | ✅ | **Yes — MAP_JIT path** |
| FL Studio 21 / 2025 | ✅ | ✅ | ✅ | ✅ | **Yes — MAP_JIT path** |
| Reason 11 / 12 | ✅ | ✅ | ✅ | — | **Yes — MAP_JIT path** |
| Studio One 4 / 5 / 6 | ✅ | — | ✅ | — | **Yes — `mprotect` RW→RX path** (no MAP_JIT) |
| Ableton Live 10 / 11 / 12 | ✅ | — | ✅ | — | **Yes — `mprotect` RW→RX path** (no MAP_JIT) |
| Pro Tools (+ Developer) | ✅ | — | ✅ | — | **Yes — `mprotect` RW→RX path** |
| Waveform 12 / 13 | ✅ | — | ✅ | — | **Yes — `mprotect` RW→RX path** |
| GarageBand | ✅ | — | ✅ | — | **Yes — `mprotect` RW→RX path** |
| Logic Pro | ❌ (flags=0x0) | — | — | — | **Special case — test empirically** (Apple platform binary, not hardened‑runtime; also hosts AU out‑of‑process) |
| Studio One 2, Bitwig 1.x/2.x | ❌ (no entitlements) | — | — | — | Legacy pre‑hardened‑runtime builds; permissive on their era, not relevant going forward |

All entitled third‑party hosts also carry `disable-library-validation` (which is *why* a third‑party plugin dylib loads
into them at all). Takeaways:

- **`allow-unsigned-executable-memory` is the common denominator** (present on *every* entitled host), and it is the one
  that matters most: it permits the classic `mmap(RW)` → write → `mprotect(RX)` path, which works **without** `MAP_JIT`.
  So a JIT that prefers `MAP_JIT` where `allow-jit` is present and falls back to the `mprotect` path otherwise runs on
  **essentially every current host measured** — the earlier "assume often off" caution was too pessimistic.
- **`allow-jit` (the MAP_JIT + per‑thread W^X toggle path) is present on roughly half** — the Steinberg, Cockos,
  Image‑Line, Reason and Bitwig families. Prefer it when present (Apple‑blessed, cleanest W^X story on Apple Silicon).
- **Logic Pro is the one genuine unknown.** It is *not* hardened‑runtime (`flags=0x0`, an Apple platform binary) and
  carries none of these entitlements; whether an in‑process plugin JIT works there must be tested on real hardware
  (Logic also hosts many components out‑of‑process, which may change the picture). Treat Logic as "probe and fall back."
- The probe must therefore try, in order: `MAP_JIT`+toggle → `mmap(RW)`+`mprotect(RX)` → interpreter. This ordering
  covers the whole table.

We should still **detect, never assume** (macOS versions and DAW updates change entitlements), but the measured baseline
is: *JIT will be available on the large majority of the installed base, with the interpreter covering Logic, iOS, and
the occasional locked‑down host.* Cockos REAPER runs EEL2/JSFX JIT in its *own* entitled app — the easy case (host ==
JIT author) — but the table shows a third‑party plugin is in good shape too. **Design implication: treat JIT as
opportunistic and the interpreter as the contract — but expect the opportunistic path to win most of the time.**

### 2.2 iOS / AUv3

No JIT, period, for App‑Store distribution — `MAP_JIT` is unavailable to normal apps and AUv3 app extensions run under
tight memory limits. The `com.apple.security.cs.allow-jit` entitlement is a *macOS* Hardened‑Runtime key and does not
grant iOS JIT ([LuaJIT #1072](https://github.com/LuaJIT/LuaJIT/issues/1072)). On iOS, GAZL ships **interpreter‑only**, or
uses **ahead‑of‑time transpilation to C++** for first‑party firmwares (§10). This is the same conclusion Cmajor and
others reach; it is why an interpreter that is "only" 10–25 % of native (`GAZL.h:29`) is strategically valuable.

### 2.3 Windows

Comparatively benign. `VirtualAlloc(MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE)`, write code, `VirtualProtect` to
`PAGE_EXECUTE_READ`, then **`FlushInstructionCache`** (mandatory on ARM64, effectively free/no‑op on x64 but call it
anyway for portability). Concerns:

- **Control Flow Guard (CFG):** if the host is CFG‑instrumented, indirect calls validate targets against a bitmap;
  a call *into* freshly JIT'd pages would fault. We avoid this entirely by **never making a CFG‑checked indirect call
  into JIT code from C++** — entry into JIT code goes through a single non‑indirect trampoline, and JIT‑internal calls
  are direct or table‑dispatched within our own pages. If ever needed, `SetProcessValidCallTargets` +
  `PAGE_TARGETS_INVALID` registers valid entry points.
- **Arbitrary Code Guard (ACG / `ProcessDynamicCodePolicy`)**: if a host enabled it, *all* dynamic code is blocked and
  even `VirtualProtect`→X fails. Rare among DAWs, but the runtime probe handles it → fallback.
- **CET / shadow stacks (user‑mode):** our emitted code uses ordinary `call`/`ret` with a balanced native stack, so CET
  is satisfied. The trap mechanism must *not* do non‑local jumps that unbalance the shadow stack — we use structured
  returns‑with‑status, not `longjmp` (§5.4), which keeps CET happy.
- **Windows‑on‑ARM64 / ARM64EC:** an x64‑built plugin runs under emulation (with ARM64EC thunking); a native ARM64
  plugin JITs ARM64 directly. Build native ARM64 and this is moot.

### 2.4 Linux (for completeness / CI)

`mmap(PROT_READ|PROT_WRITE)` → write → `mprotect(PROT_READ|PROT_EXEC)`. SELinux `execmem` denial is possible on locked‑
down systems → probe + fallback. On aarch64 add `PROT_BTI` if emitting BTI landing pads (§6). Cross‑modifying code across
threads may need `membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE)` — but our compile‑once/publish‑once model
(§6.2) avoids the hard cases.

### 2.5 The portability rule this forces

> **The plugin owns no process‑global state.** No signal handlers, no `mprotect` of memory it didn't allocate, no
> global `MXCSR`/`FPCR` changes, no assumptions about guard pages. Every safety check is an explicit instruction in the
> emitted code. This is more restrictive than a standalone wasm runtime and it is the single most important design
> constraint. It rules out the "guard page + SIGSEGV handler" trick that wasm engines use to elide bounds checks —
> which is fine, because GAZL's checks are cheap (§8).

---

## 3. Backend strategy landscape (scored for *this* problem)

Evaluated against: dual‑arch (x64+ARM64), embeddable from C++ with tiny footprint, BSD‑compatible license, low compile
latency, full control of the sandbox check sequence, auditable output, no process‑global state.

### 3.1 Copy‑and‑patch / stencils
Precompile per‑opcode machine‑code "stencils" (at *our* build time, with the platform C compiler), then at load time
`memcpy` them and patch holes (immediates, branch targets, slot offsets). Introduced by
[Xu & Kjolstad, *Copy‑and‑Patch Compilation* (OOPSLA 2021)](https://fredrikbk.com/publications/copy-and-patch.pdf) —
they report code ~**14× faster to generate than LLVM ‑O0** while running ~ competitively for an interpreter‑replacement
tier. Adopted by **CPython**: shipped experimental in 3.13 (`--enable-experimental-jit`), promoted toward a supported
build in 3.14 ([PEP 836](https://peps.python.org/pep-0836/),
[LWN follow‑up](https://lwn.net/Articles/1029307/)); measured *JIT‑attributable* gains are modest (~4–12 % geomean,
larger on tight pure‑Python loops) because Python's bottleneck is object semantics, not dispatch — **not** representative
of GAZL, whose values are raw words. **EEL2 is essentially this technique done by hand** (see §10). Also
[Deegen/LuaJIT‑remake](https://arxiv.org/abs/2411.11469) generates interpreters+baseline JITs from semantic specs.

- **Pros:** the *fastest* code generation of any approach; the emitter is a `memcpy`+patch loop (trivially auditable and
  fuzzer‑friendly); stencils are written in C and compiled by the real compiler (good codegen for free); no runtime
  assembler dependency.
- **Cons:** the build system grows a stencil‑extraction step (compile C→object, parse relocations per arch — the
  fragile part); calling‑convention/register pinning across stencils needs care (CPython uses `clang` `preserve_none`
  and specific flags); harder to do even light register allocation across opcodes. Cross‑compilation for ARM64 from an
  x64 CI needs the target toolchain.
- **Verdict:** *Strong candidate, and philosophically the most "GAZL"* (compile‑time decisions, portable text→native).
  But the relocation/stencil toolchain is the least portable part of an otherwise portable project, and it fights the
  "single header, just add a .cpp" ethos. **Recommended as a possible v2 codegen strategy, not v1**, unless we decide
  the build complexity is acceptable. If chosen, it pairs beautifully with GAZL's already‑specialized opcodes: one
  stencil per opcode‑mode.

### 3.2 Hand‑written baseline / template JIT (the recommendation)
A single linear pass over each function emitting instructions directly, à la **V8 Sparkplug** (JS baseline — famously
"~2 orders of magnitude faster to compile than TurboFan", a few thousand LOC per arch, no IR, walks bytecode once) and
**V8 Liftoff** / **SpiderMonkey** / **wasmtime Winch** (wasm baselines). Titzer's
[*Whose baseline compiler is it anyway?*](https://arxiv.org/abs/2305.13241) is the reference on single‑pass wasm
baselines: they keep an abstract value stack, do no real register allocation beyond a simple cache, and compile at tens
of MB/s. GAZL is *easier* than wasm here because it is register/slot‑based, not stack‑based, so there is no operand‑stack
abstraction to maintain — operands are already slot indices.

- **Pros:** total control (exact sandbox checks, exact float semantics, exact fuel points); tiny footprint; no external
  deps; output is simple and pattern‑restricted → statically verifiable (§8.4); trivially matches the "single
  documented C++ file" project style. Compile latency is excellent (single pass, no IR).
- **Cons:** two emitters to write and maintain (x64, ARM64); encoding bugs are on us (mitigated by a disassembler‑based
  differential test, §8); no cross‑block optimization in v1.
- **Verdict:** **Chosen.** The Emitter layer is ~1–2k LOC/arch of mechanical encoding; the compiler driver is
  shared. This is the sweet spot of control, size, and safety for a project of GAZL's philosophy.

**Naming.** We call the byte‑emitting layer the **`Emitter`** (one method per machine instruction that appends its
bytes), not "MacroAssembler." The JIT‑engine term "MacroAssembler" (V8/JSC) refers to a *higher* layer of
multi‑instruction convenience idioms built on top of a 1:1 `Assembler`; it also collides confusingly with classic
text macro‑assemblers. We want the 1:1 byte layer, so `Emitter` is the honest name; a thin set of multi‑instruction
helpers can sit on top without needing the "macro" label.

#### 3.2.1 Compiler‑as‑oracle — how we decide *what* to emit
We do **not** guess instruction sequences, and we do **not** paste compiler output at build/run time (that would be
copy‑and‑patch, §3.1, with a worse extractor). Instead, clang is our *design‑time reference implementation*: for each
opcode‑mode we write a tiny C **probe** with the operands as **compile‑time constants** (so it lowers to exactly the
shape the JIT wants — no operand‑fetch indirection), compile it at ‑O2 for each target, disassemble, and transcribe the
instruction *selection* into the `Emitter` by hand.

- Probe, not interpreter: disassembling the *interpreter's* `ADDF_VVC` is wrong — it fetches slot indices from the
  `Instruction` stream at runtime. The probe `void p(float* dsp){ dsp[3] = dsp[7] + 1.5f; }` bakes the operands in and
  shows the real target sequence (`movss/addss/movss`, or ARM64 `ldr/fadd/str`).
- One instance vs the range: a probe shows *one* encoding; the `Emitter` must encode the operation for *arbitrary*
  operands (x64 imm8 vs imm32 / REX / ModRM‑SIB; ARM64 immediate‑fits vs `movz`/`movk` or literal pool). We shrink this
  to near‑nothing by standardizing on **one canonical form per operation** (e.g. always imm32 on x64; always
  `movz`+`movk` or a literal‑pool load for 32‑bit constants on ARM64) — slightly larger code, a much smaller/uniform
  encoder. The disassembly tells us which canonical form to settle on.
- Where it's decisive: the determinism‑sensitive ops (§6). Write the *exact* semantics in C (saturating `FTOI`, the
  `idiv` INT_MIN/‑1 guard, the bounds‑check branch), let clang show the correct, well‑formed lowering on each ISA, and
  copy it. This is the single best way to get those both correct and tidy — and keeps the compiler entirely out of the
  build and the runtime.

### 3.3 Runtime assembler libraries (the pragmatic accelerant)
If we don't want to hand‑roll instruction encoders, embed one:

| Lib | Arch | License | Notes |
|---|---|---|---|
| **AsmJit** | x86/x64, **AArch64** | Zlib (permissive) | Mature x86; AArch64 backend is now solid and used in production (e.g. **HISE SNEX** JITs DSP with it). Rich `a64::Assembler`. C++‑native, header+source. Good fit if we adopt a library. |
| **SLJIT** | x86/x64, ARM32/**ARM64**, RISC‑V, PPC, MIPS, s390x, LoongArch | **BSD** | Powers **PCRE2**'s JIT (battle‑tested on untrusted patterns). Low‑level "platform‑independent assembler". Has **Apple‑Silicon W^X support** (the historical `MAP_JIT` breakage, [sljit #99](https://github.com/zherczeg/sljit/issues/99), is fixed). Smallest, most portable, BSD — the closest license/ethos match. |
| **DynASM** | x86/x64, ARM/**ARM64**, others | MIT (LuaJIT) | Preprocessor‑based (mixes asm templates in C, `.dasc`). Powers LuaJIT. Great codegen ergonomics but adds a Lua build step. |
| **GNU Lightning** | many | **LGPL** | LGPL is awkward for a BSD‑2 library shipped as source; skip. |
| **MIR** (V. Makarov) | x64, aarch64, ppc64, s390x, riscv | MIT | A whole lightweight compiler (C→MIR→machine), ~‑O0/‑O1‑class. More than we need; interesting for a future optimizing tier. |
| **LibJIT** | several | LGPL | Old, LGPL; skip. |

- **Verdict:** **Not adopted — the backend will be hand‑rolled** (decision made). Rationale: a runtime assembler
  library would spare us instruction *encoding*, but encoding is the most mechanical, most testable part (a
  disassembler‑diff harness, §8, nails it), while the parts we most need to control — the exact sandbox check sequence,
  fuel points, trap ABI, and statically‑verifiable output pattern — are things a general library does *not* give us and
  can even get in the way of. A hand‑rolled `Emitter` keeps the project a single self‑contained BSD‑2 unit with
  no third‑party license or supply‑chain surface, which matters more here than saving the encoder LOC. **If** we ever
  want a shortcut to first light, **SLJIT** (BSD, PCRE2‑hardened, Apple‑Silicon W^X support) is the fallback to reach
  for behind the same `Emitter` interface — but the plan of record is our own encoders.

### 3.4 Full compiler frameworks (rejected for v1)
- **LLVM ORC JIT:** best peak code, but multi‑hundred‑MB dependency, slow compile latency unsuitable for load‑time in a
  plugin, and enormous attack/complexity surface. It is what **Cmajor** and **libfaust** use — but they are *tools/DSLs*,
  not a "single BSD‑2 header." Rejected as a hard dependency; keep as an optional AOT path (§10).
- **Cranelift** (wasmtime's non‑LLVM backend): ~**order‑of‑magnitude faster to compile than LLVM/WAVM** while running
  ~2 % slower than TurboFan / ~14 % slower than LLVM ([Titzer](https://arxiv.org/abs/2305.13241)), with a
  symbolic‑checker‑verified register allocator (regalloc2) and a security posture built for untrusted input. **But it is
  Rust**, embedding into a C++ single‑file library is heavy, and it is far more than a load‑time baseline needs.
  Rejected for embedding; noted as best‑in‑class prior art on *safe fast compilation*.

### 3.5 Embed a WebAssembly engine (translate GAZL → wasm) — the serious alternative
Instead of writing a JIT, lower GAZL to wasm32 and let an engine run it. GAZL→wasm is a *natural* lowering (both are
statically typed, linear‑memory, structured control flow, 32‑bit). Options:

- **wasmtime** (Cranelift or the **Winch** baseline): production sandbox, C API, fuel *and* epoch interruption. Crucially
  it can run **without signal handlers**: `Config::signals_based_traps(false)` forces explicit bounds/zero‑div checks and
  drops the guard‑page+`SIGSEGV` scheme ([wasmtime portability](https://bytecodealliance.org/articles/wasmtime-portability),
  [Config docs](https://docs.wasmtime.dev/api/wasmtime/struct.Config.html)) — exactly the mode a plugin needs. **Winch**
  reached complete AArch64 core‑wasm support around Wasmtime 35
  ([announcement](https://bytecodealliance.org/articles/winch-aarch64-support)).
- **WAMR**: interpreter / fast‑interp / AOT / JIT modes, tiny footprint, AOT story usable where JIT is banned (compile
  wasm→native offline, ship the `.aot`) — attractive for iOS via AOT.

- **Pros:** someone else maintains the codegen, the sandbox, and the fuzzing; mature; AOT covers iOS.
- **Cons for real‑time audio, which are decisive:** (1) **it is Rust/C with a big footprint and its own executable‑memory
  needs** — on macOS the *same* entitlement problem applies, we've just moved it into a dependency we don't control;
  (2) fuel/epoch interruption and the ABI boundary add overhead and, more importantly, we lose the *bit‑identical
  interpreter* property that makes GAZL's suspend/resume and lockstep testing clean; (3) a wasm engine is a moving,
  CVE‑bearing target (e.g. the 2026 **Winch** sandbox‑escape [CVE‑2026‑34987](https://github.com/bytecodealliance/wasmtime/security/advisories/GHSA-xx5w-cvp6-jv83))
  — baseline compilers are *new* attack surface even in mature projects; (4) it inverts control: GAZL becomes a guest of
  wasmtime rather than wasmtime a component of GAZL, which is a large philosophical and packaging change for a BSD‑2
  single‑file VM.
- **Verdict:** **Rejected as the primary path, but keep as a documented alternative** — specifically, GAZL→wasm is worth
  a spike as a *portability/AOT escape hatch* (ship AOT wasm on iOS) and as a *differential oracle* (run the same program
  through wasmtime and compare). The core product stays first‑party.

### 3.6 libtcc (TinyCC) — rejected
Translate GAZL→C, JIT with libtcc. TinyCC is LGPL, its aarch64 backend (esp. macOS ARM64) is comparatively immature, and
codegen quality is poor. LGPL + maturity + quality all argue against. Skip.

### 3.7 Scorecard

| Approach | Footprint | License fit | Compile latency | Control/safety audit | Dual‑arch effort | Verdict |
|---|---|---|---|---|---|---|
| Hand‑written baseline | ✅ tiny | ✅ BSD‑2 native | ✅ excellent | ✅ full | ⚠️ 2 emitters | **v1 core** |
| + SLJIT/AsmJit encoder | ✅ small | ✅ BSD / ⚠️ Zlib | ✅ | ✅ | ✅ lib does encoding | **v1 pragmatic** |
| Copy‑and‑patch stencils | ✅ tiny runtime | ✅ | ✅✅ fastest | ✅ | ⚠️ build toolchain | **v2 option** |
| Cranelift | ❌ Rust | ⚠️ | ✅ (~10× < LLVM) | ✅ verified | ❌ embedding | prior‑art only |
| LLVM ORC | ❌ huge | ⚠️ | ❌ slow | ⚠️ | ✅ | AOT option only |
| Embed wasmtime/WAMR | ❌ large | ⚠️ | ✅/✅ | ⚠️ external | ✅ | alt / AOT / oracle |
| libtcc | ⚠️ | ❌ LGPL | ✅ | ⚠️ | ❌ ARM64 weak | reject |

---

## 4. Prior art in the audio world (closest precedents)

| Project | Codegen | Arch | iOS / no‑JIT story | Relevance |
|---|---|---|---|---|
| **Cockos EEL2 / JSFX** (in REAPER) | **Hand copy‑and‑patch**: per‑arch precompiled asm *stencils* (`asm-nseel-x64-macho.o`, `asm-nseel-aarch64-gcc.c`, `asm-nseel-x86-gcc.c`) with `_end` markers, stitched + patched at compile time ([justinfrankel/WDL eel2](https://github.com/justinfrankel/WDL/tree/main/WDL/eel2)) | x86, x64, **ARM/ARM64** | REAPER entitles its *own* app; JSFX also has an eval fallback | **The single most direct precedent.** Confirms the stencil approach is viable and shippable in a real‑time audio product across arches. License: Cockos WDL (permissive, zlib‑style — verify per file). |
| **Cmajor** (Sound Stacks / J. Storer) | LLVM; recent versions lower DSP → **optimised WebAssembly** then JIT (LLVM 18) ([cmajor.dev](https://cmajor.dev/)) | x64, ARM64, wasm | Ships AOT/native and wasm; VST/AU JIT loads patches in any DAW (subject to host entitlements) | Shows the LLVM route and the wasm‑as‑IR route; also shows the *"our plugin JITs inside arbitrary hosts"* problem we face. Actively maintained (2025–2026). |
| **Faust** (libfaust) | LLVM JIT, **interpreter backend as fallback**, **wasm backend** | many | interpreter + wasm + AOT C++ | Canonical "one frontend, many backends incl. interpreter + JIT + wasm" — validates the tiered strategy. |
| **Max/MSP `gen~`** | C codegen → compiler | desktop | export to code | Codegen‑to‑C precedent (cf. our AOT‑to‑C++ idea, §10). |
| **HISE SNEX** | **AsmJit** JIT of DSP snippets | x64/ARM64 | JUCE fallback paths | Direct evidence AsmJit's AArch64 backend is production‑grade for audio DSP. |
| **NI Reaktor Core / Blue Cat Plug'n Script** | proprietary codegen / AngelScript (+ fallbacks) | desktop | — | Confirms the "scriptable DSP with a compiled fast path + interpreted fallback" product pattern. |
| **Bitwig** | **process isolation** (plugins in separate processes) | — | — | An orthogonal sandbox lever (OS process, not in‑VM) — worth noting but out of scope for an *in‑process* VM. |

Takeaway: the winning audio‑world pattern is exactly the proposed one — **a fast compiled path with an authoritative
interpreter/AOT fallback**, and the most similar shipping product (EEL2) uses hand‑stitched per‑arch stencils, not a
framework.

---

## 5. Proposed architecture

### 5.0 Where the JIT sits in the pipeline
The JIT is a **new back‑end that consumes the existing `Instruction[]` VM code** — the same immutable array the
interpreter runs today — **not** a second text parser and **not** a new output mode inside the `Assembler`. Nothing in
`Assembler` changes.

```
GAZL text ──Assembler.feed()/finalize()──▶ Instruction[]  (finalized VM IR, immutable, shared across Processors)
                                               │
                             ┌─────────────────┴──────────────────┐
                             ▼                                     ▼
                    Processor::run()                        JitCompiler  (NEW)
                    interpreter — tier 0                    Instruction[] → native (x64 / AArch64)
                    reference + fallback                    + per‑module entry table
                                                                   │
                                                            run via trampoline; on trap/timeout/
                                                            native‑suspend, return Status exactly
                                                            like run(); if JIT unavailable, use run()
```

Why this boundary (compile to VM IR first, *then* IR → native):

- **The `Instruction[]` IR is already the ideal input.** By `finalize()` the assembler has folded constants, resolved
  every operand to a slot offset or a relative branch, specialized each opcode by mode (`V`/`C`/`B`), computed each
  `FUNC`'s frame sizes (`GAZL.cpp:1172`), built the `SWCH` jump tables, and *validated* constant addresses against
  symbol sizes. The JIT inherits all of that for free and can *trust* it (e.g. emit unchecked direct loads for
  assembler‑validated constant addresses). Re‑parsing text in a second path would duplicate ~1000 lines and risk the
  two paths diverging.
- **It keeps the interpreter as a zero‑duplication reference and fallback.** Both engines consume byte‑for‑byte the
  same IR, which is exactly what makes their states comparable (the §5.2 invariant) and what lets a JIT'd function and
  an interpreted function call each other — they key off the same `IP_OFFSET`‑biased code indices and the same
  frame/stack layout.
- **It respects the "assembler format stays backwards‑compatible" goal** (`GAZL.h`): the JIT is purely *additive* — a
  new consumer of an existing artifact, alongside `Processor::run()`. `--no-jit` or a failed executable‑memory probe
  simply routes to `run()` on the same `Instruction[]`.
- **Timing lines up:** programs are already compiled text→IR at load; the JIT is just a second stage at that same load
  moment (run off the audio thread), turning the per‑module IR into native code once.

So: **compile to VM first, then convert the VM code to ARM/x64.** The JIT walks the finalized `Instruction[]`
function‑by‑function (`FUNC` marks each start), never mutates it, and emits native code + an entry table beside it.

### 5.1 Tiers
- **Tier 0 — Interpreter (exists).** The semantic reference and universal fallback. Every JIT decision is validated
  against it. Runs everywhere (iOS, un‑entitled hosts, `--no-jit`, debug).
- **Tier 1 — Load‑time baseline JIT (new).** Compiles the *whole loaded program* once, off the audio thread, at patch
  load. No profiling, no tiering, no OSR, no deopt. Produces one native function per GAZL `FUNC`.

No tier 2 initially. If ever wanted, tier 2 is "same JIT + register caching + peephole," not speculation. GAZL has no
dynamic types to speculate on, so the deopt machinery that causes most JIT CVEs never enters the design.

### 5.2 The invariant that makes everything safe and testable
> **At every safepoint, JIT VM‑state is byte‑identical to what the interpreter would have at the same GAZL ip:** all
> frame slots written back to the data stack, `dsp`/`ipsp`/`cycles` in the `Processor`, and the GAZL ip materializable.

Safepoints = function entry, before each native call, loop back‑edges (fuel checks), and returns. Between safepoints the
JIT may hold values in registers freely. This invariant delivers, for free:

- **Suspend/resume** (fuel timeout, native‑suspend): stop at a safepoint, state is already interpreter‑shaped, resume by
  re‑entering — into JIT *or* interpreter, interchangeably.
- **Lockstep differential testing** (§8): step both engines to the same safepoint, `memcmp` the state.
- **Mixed execution**: a JIT'd function can call an un‑JIT'd one (or vice versa) through the same call ABI.

### 5.3 Register & memory plan (v1)
Pinned registers (both ABIs have enough callee‑saved regs): `CTX` (`Processor*`), `DSP` (data‑stack pointer, mirrors
interpreter `dsp`), `MEMBASE` (precomputed `memoryBase − MEMORY_OFFSET` scaled to bytes), `FUEL` (clock cycles left), plus
scratch/temp regs and the FP scratch bank. Locals stay memory‑resident (§1, sharp edge #1). An opcode like
`ADDF_VVV %d,%a,%b` lowers to: `ldr s0,[DSP,#a*4]; ldr s1,[DSP,#b*4]; fadd s0,s0,s1; str s0,[DSP,#d*4]` (AArch64) — three
loads/stores that the CPU's store‑to‑load forwarding largely hides, and that v2 register caching removes.

Memory access lowering (the sandbox core), mirroring `GAZL.cpp:1636` exactly:
```
; PEEK V0, ptrReg, offImm    (read; check < memorySize)
    sub   Wt, ptrReg, #MEMORY_OFFSET      ; unbias (32-bit wrap = wrapping semantics preserved)
    add   Wt, Wt, #offImm
    cmp   Wt, memorySizeReg               ; unsigned
    b.hs  trap_BAD_PEEK                   ; branch if >= (single unsigned compare catches negative too)
    ldr   Wout, [MEMBASE, Xt, uxtw #2]    ; base + zext(idx)*4
```
Writes use `rwMemorySizeReg`. Constant addresses were already validated by the assembler against symbol sizes
(`OFFSET_OUT_OF_BOUNDS`), so `POKE_CC_`/`PEEK_VC_` with constant addresses become **direct** `MEMBASE+disp` accesses with
*no* runtime check — a real speedup the interpreter also enjoys. `SWCH` becomes a bounds‑clamped (`min`) table load +
indirect branch into an in‑function jump table; the table lives in our read‑only emitted data, cloned from the const‑
memory table the assembler built (`GAZL.cpp:965`).

### 5.4 Calls, traps, and stack discipline (no signals, no longjmp)
- **Direct call** (`CALL_CVC`): emit native `call` to the callee's entry trampoline; push `{ip, dsp}` to `ipStack` first
  (as the interpreter does, `GAZL.cpp:1614`) so `ipStack` overflow and suspend/resume still work. `dsp += C1` for the
  arg window.
- **Indirect call** (`CALL_VVC`): `idx = ptr − IP_OFFSET; if (idx >= codeSize) trap BAD_CALL;` load
  `entryTable[idx]`; `if (entryTable[idx] == 0) trap BAD_CALL;` (nonzero only where `code[idx].opcode==FUNC_CC_`, mirroring
  the interpreter's two checks) then `call` it.
- **Native call** (`CALL_NVC`): write back state to `Processor` (safepoint), `call natives[idx](CTX)`, check the returned
  `Status`; nonzero → structured unwind. Reentrancy (`enterCall` from a native) just works because state is
  interpreter‑shaped at the boundary.
- **Traps** (bad peek/poke/call, div0, stack/ip overflow, fuel timeout): **no signals, no `longjmp`.** Each function's
  trap sites branch to a per‑function epilogue that stores `{status, ip‑of‑faulting‑instruction, dsp, ipsp, cycles}` into
  the `Processor` and returns the `Status` up the native call chain (each GAZL call frame is a native frame that checks
  the callee's status and propagates). This is CET/shadow‑stack‑safe (balanced `call`/`ret`), Hardened‑Runtime‑safe (no
  signal handler), and reproduces the interpreter's "exit `run()` with `ip` at the faulting instruction" behavior exactly
  (`GAZL.cpp:1762`).

An alternative to per‑frame status propagation is a **single dispatch trampoline** that owns the only native frame and
`call`s each GAZL function, with traps returning to it — simpler shadow‑stack story but an extra indirection per call.
Prototype both; measure. Status‑propagation is likely faster and is the default plan.

### 5.5 Fuel
Interpreter charges 1/instruction (`GAZL.cpp:1605`). The JIT charges **per basic block**: at each block entry (and at
least at every loop back‑edge and before each call) `subs FUEL, FUEL, #blockWeight; b.mi trap_TIMEOUT`. This relaxes
timeout precision to block granularity — **a documented semantic change**: a timeout may now be observed up to
`blockWeight−1` instructions late. Bound it by capping block weight (split huge straight‑line blocks) so worst‑case
latency stays within the host's tolerance. This is the standard baseline‑JIT approach (Liftoff/Winch check fuel at
back‑edges) and preserves the *cooperative, deterministic* nature — no async preemption, so still audio‑thread safe.

### 5.6 Compilation pipeline
1. **Off‑thread, at load:** for each `FUNC`, split into basic blocks (edges are already explicit: relative branches,
   `FORi`/`FORp`, `SWCH`, fallthrough, `RETU`). Single linear pass emits code + records a **safepoint side table**
   (GAZL ip → native offset, live‑slot mask) for resume/suspend and lockstep. Resolve intra‑function branches (all local,
   `GAZL.cpp:1176`) with a second fixup pass over recorded label offsets. Fill the per‑module **entry table**
   (`code index → native entry`, 0 elsewhere).
2. **Publish** (§6.2): make pages executable + i‑cache maintenance.
3. **Runtime:** `enterCall`→ trampoline → JIT entry. On any trap/timeout/native‑suspend, return `Status`; `run()` behaves
   as today. `--no-jit` or probe‑failure → interpreter unchanged.

---

## 6. Per‑ISA codegen specifics & determinism

GAZL promises portable, reproducible results. The JIT must match the interpreter *and* the interpreter across ISAs. Some
of these are latent issues in the interpreter *today* that the JIT project should fix and pin down in the spec — these
are catalogued with recommended fixes in [PortabilityAudit.md](PortabilityAudit.md) and should be resolved in the
current source (Phase 0 / spike B1) before the JIT must match them.

### 6.1 Integer edge cases (define, then guard identically in both engines)
- **`INT_MIN / -1` and `INT_MIN % -1`:** x64 `idiv` raises `#DE` (→ SIGFPE) — a real bug reachable today since the
  interpreter only guards divide‑by‑zero (`GAZL.cpp:1662`), not this. AArch64 `sdiv` returns `INT_MIN` quietly and
  `msub`‑based mod returns 0. **Decision:** define `INT_MIN/-1 = INT_MIN`, `INT_MIN % -1 = 0` (the wasm/AArch64 choice),
  and emit an explicit guard on x64 (`cmp/branch` to a `mov INT_MIN` / `xor` path). Fix the interpreter to match.
- **Shift counts ≥ 32 / negative:** C++ UB, but both ISAs mask the count (x64 `sh* ` masks to 5 bits for 32‑bit ops;
  AArch64 `lsl`/`lsr`/`asr` on `W` regs mask mod 32). **Decision:** specify "count taken mod 32" and emit the natural
  instruction — behavior already matches on both ISAs; just document and add tests. (Interpreter's raw C `<<`/`>>` also
  masks in practice on these targets, but should be made explicit.)
- **`ADDp`/`SUBp`/`DIFp` wrap:** 32‑bit unsigned wrap; ensure emitted address math wraps in 32 bits *before* the bounds
  check (the `sub #MEMORY_OFFSET` in §5.3 does this) so no wrap sneaks past the check.

### 6.2 AArch64 code publication & cache maintenance
Writing instructions to data memory does not make them fetchable — the sequence is mandatory and non‑negotiable:
1. After writing code, for the range: `dc cvau` (clean D‑cache to PoU), `dsb ish`, `ic ivau` (invalidate I‑cache to PoU),
   `dsb ish`, `isb`. Use `__builtin___clear_cache(begin,end)` / `sys_icache_invalidate` (macOS) /
   `FlushInstructionCache` (Windows) — never hand‑roll unless you must.
2. **Cross‑thread execution** (compile on loader thread, first run on audio thread): the executing core needs a context
   sync (`isb`). We rely on the audio thread hitting an `isb` naturally, but to be safe the publish step should ensure a
   barrier is observed on the executing core before first entry. Real engines handle this variously (V8/OpenJDK use
   IPIs/`membarrier`‑style broadcasts on some OSes); for us, because publication happens *once at load* and the audio
   thread only enters JIT code *after* the atomic "jit ready" flag is set (with acquire/release), a `dmb ish` on publish
   + the natural `isb` on the far side is sufficient in practice — but **verify on real M‑series hardware under load**,
   this is a classic source of "works 999/1000 times" bugs.
3. **W^X on Apple Silicon:** `mmap(MAP_JIT)`, `pthread_jit_write_protect_np(0)`, write, `pthread_jit_write_protect_np(1)`,
   `sys_icache_invalidate`. The toggle is *per thread*, so compile+publish must happen on **one** thread.
4. **Branch range:** ±128 MB for `b`/`bl`, ±1 MB for conditional `b.cond`/`cbz`. Our per‑module code is KB‑scale, so
   direct branches always reach; no veneers needed. Long conditional branches (rare) get inverted‑branch‑over‑`b`.
5. **Constants:** materialize with `movz/movk` (up to 2 for a 32‑bit immediate) or a per‑function literal pool loaded via
   `ldr =`. Floats: literal pool + `ldr s`.
6. **BTI:** if the host maps our pages with `PROT_BTI` (or we opt in), indirect‑branch targets (function entries, jump‑
   table targets) need a `bti c`/`bti j` landing pad. Cheap; emit them so we're forward‑compatible. Not required if we
   don't set `PROT_BTI`.

### 6.3 x64 specifics
- `idiv` guard as §6.1; also the shift masking is native.
- **`ENDBR64`/IBT:** not enforced for our pages unless the host enables CET‑IBT and our pages are reached by an indirect
  branch from IBT‑checked code. We enter JIT code via a direct `call` trampoline, and internal indirect branches
  (`SWCH`, indirect `CALL`) stay within our pages. Emit `ENDBR64` at every indirect‑reachable entry anyway (1 byte of
  safety, forward‑compatible). 
- **MXCSR / FTZ‑DAZ:** audio hosts frequently set flush‑to‑zero / denormals‑are‑zero on the audio thread for
  performance. This affects the **interpreter today** too — GAZL never sets MXCSR, so both engines *inherit* whatever the
  host set. **Decision:** document that denormal handling follows the host's FPU mode (both engines identical), and do
  **not** fight it (saving/restoring MXCSR per callback is costly and would surprise host authors). If bit‑exact
  cross‑host reproducibility is ever required, that's a separate opt‑in "strict FP" mode that sets and restores MXCSR/FPCR
  around `run()` — out of scope for v1 but noted. SSE2 scalar ops are otherwise deterministic.

### 6.4 float→int (`FTOI`) — the one genuinely divergent op
`FTOI_VVC` is `(Int)(f*scale)` (`GAZL.cpp:1713`). C's cast is UB on overflow/NaN, and the hardware differs: x64
`cvttss2si` returns `0x80000000` ("integer indefinite") for NaN/overflow; AArch64 `fcvtzs` **saturates** (→ `INT_MAX`/
`INT_MIN`) and returns **0 for NaN**. So the interpreter *already* gives different answers on x64 vs ARM64 for out‑of‑
range inputs. **Decision (pick one, apply to both engines):**
- **(A) Saturating + NaN→0** (the wasm `i32.trunc_sat_f32_s` semantics): free on AArch64 (`fcvtzs`), a few extra
  instructions on x64 (compare/min/max/NaN‑test around `cvttss2si`). Deterministic, no traps. **Recommended.**
- (B) Trap on out‑of‑range (wasm non‑`sat` `i32.trunc`): matches "safety" ethos but adds a runtime error class and
  guards.
- (C) Leave "unspecified for out‑of‑range" — cheapest, but abandons GAZL's portability promise. Not recommended.

Adopt (A), define it in the spec, implement in interpreter and JIT, and make the differential fuzzer assert equality
across ISAs. NaN canonicalization of *results* is not otherwise needed (GAZL doesn't expose bit patterns except via
`PEEK` of stored floats, which round‑trip exactly).

---

## 7. Sandboxing model & threat model

**Threat model.** GAZL scripts ("firmwares") are *semi‑trusted*: authored by the user or third parties, potentially
buggy or hostile‑ish, but not a nation‑state adversary running Spectre gadgets. The security goal is: **a malicious or
buggy GAZL program cannot read/write host memory outside the VM's `Value[]`, cannot execute arbitrary native code, cannot
corrupt return addresses, and cannot hang the audio thread unboundedly.** This is memory safety + control‑flow integrity
+ liveness, not side‑channel resistance.

**What GAZL gives us structurally** (and the JIT must preserve, not weaken):
- Linear memory with explicit per‑access bounds checks → **no OOB** (§5.3). Constants pre‑checked by the assembler.
- Return addresses & call stack in **non‑VM‑addressable** memory (`CallStackEntry`) → **no ROP via stack smashing** from
  VM code.
- Indirect calls checked to land only on `FUNC` entries → **no arbitrary native jumps** (§5.4).
- Typed words, no pointer forging beyond `ADRL` (which yields only in‑range VM offsets) → **no confusable pointers**.
- Fuel + cooperative scheduling → **bounded latency**, no runaway.

**JIT bug taxonomy avoided by construction** (vs the V8/JSC CVE history, which is dominated by these): no speculative
type assumptions → no type‑confusion; no deopt → no deopt‑state divergence bugs; no GC → no GC/barrier races; no inline
caches → no IC‑shape bugs; immutable code → no self‑modifying‑code races. What *remains* is the **baseline‑compiler bug
class**: an emitter that gets a bounds check, a sign‑extension, an immediate width, or a spill wrong. That is precisely
what wasm baseline compilers still get bitten by — e.g. the 2026 Winch sandbox escape via wrong memory‑offset handling
([CVE‑2026‑34987](https://github.com/bytecodealliance/wasmtime/security/advisories/GHSA-xx5w-cvp6-jv83)). So our security
budget goes almost entirely into **verifying the emitter** (§8), not into architecture.

**Bounds‑check strategy.** We use **explicit software checks**, not guard pages + signals, because a plugin must not
install signal handlers or rely on the host's SIGSEGV disposition (§2.5). This is the same mode wasmtime exposes as
`signals_based_traps(false)` ([docs](https://bytecodealliance.org/articles/wasmtime-portability)); the cost is a
`sub/cmp/branch` per dynamic memory access (constant‑address accesses are free). For DSP that mostly indexes arrays with
computed offsets, that's a few instructions the branch predictor handles near‑perfectly. Classic SFI (Wahbe et al. 1993;
NaCl) shows even *masking* (`and` the pointer into range) is viable if we want branchless checks on power‑of‑two memory
sizes — an option for hot loops (round `memorySize` up to a power of two, `and` instead of `cmp/branch`), at the cost of
turning OOB into wrap‑around rather than a trap. Keep the checking form for correctness parity in v1; consider masking as
a v2 perf knob.

**Stack‑overflow protection without signals:** the `FUNC` prologue already checks the data‑stack limit explicitly
(`GAZL.cpp:1607`) and `ipStack` depth is checked on every call — emit both as explicit checks (Go/wasm‑baseline style),
no guard page needed.

**Post‑hoc verification (defense in depth).** Because our emitter produces a *restricted, regular* instruction pattern
(fixed register roles, every memory access preceded by its check, branches only to known labels), a small **VeriWasm‑
style verifier** ([VeriWasm, NDISS/PLDI](https://cseweb.ucsd.edu/~dstefan/pubs/johnson:2021:veriwasm.pdf)) can
statically re‑check emitted code before it's published: walk the machine code, confirm every load/store is dominated by a
proper bounds check against the right limit register, confirm no writes to `CTX`/`MEMBASE`, confirm indirect branches go
through the checked table. This is far more tractable for our tiny fixed‑pattern output than for a general compiler and
gives a strong, independent safety net. **Recommended for v1.1.**

**Spectre / side channels: explicitly out of scope.** wasmtime itself documents that Spectre mitigations for in‑process
sandboxing are partial and best‑effort. For a semi‑trusted audio script sharing a process with the DAW, timing side
channels are not in the threat model; we do not spend cycles on speculation barriers. Document this stance.

---

## 8. Correctness engineering (where the real work is)

A JIT is only worth shipping if it is *provably* as correct as the interpreter. The bit‑identical‑state invariant (§5.2)
makes this unusually achievable. Layered strategy, cheapest/highest‑leverage first:

1. **Spec tightening + golden tests.** Nail down the newly‑defined behaviors (§6: idiv edge, shifts, `FTOI` sat, COPY
   overlap, fuel granularity) in the docs and add them as `.gazl` cases to `UnitTest.gazl`, run through *both* engines.
2. **Per‑opcode property tests.** For each of the ~120 opcode‑modes, generate random operands and assert
   interpreter‑result == JIT‑result (values *and* traps *and* fuel consumed). Small, exhaustive‑ish, catches encoding
   bugs immediately.
3. **Grammar‑based program fuzzer + lockstep differential execution.** Generate random *valid* GAZL programs (reuse the
   assembler as the validity oracle; seed with the 57‑file Impala corpus under `tests/impala/sources`), run interpreter
   vs JIT to completion, `memcmp` full VM state at exit. This is the wasmtime + `wasm-smith` playbook and V8's
   correctness‑fuzzing playbook, adapted. **Crucially, also fuzz the fuel schedule:** run with random `resetTimeOut`
   slices, suspend, resume (possibly switching engines mid‑run at safepoints), and assert the final state equals a single
   uninterrupted run. This exercises safepoints, resume, and the interpreter↔JIT interchange in one test.
4. **In‑process lockstep debug mode.** Because state layout is identical, a debug build can step both engines one
   safepoint at a time and diff — pinpointing the *first* divergent instruction, not just "outputs differ." This is the
   analogue of CPU co‑simulation / QEMU‑plugin lockstep, made trivial by §5.2.
5. **Static verifier** (§7) run in CI on all emitted code from the fuzz corpus.
6. **Sanitizers on the harness.** ASan on the C++ (the emitter and driver); the existing `CATCH_ZONE`/redzone pattern in
   `unitTest()` (`GAZL.cpp:1879`) around VM memory catches JIT OOB writes in tests. Emitted native code isn't ASan‑
   instrumented, but redzoned VM memory + the differential oracle catch its mistakes.
7. **Reuse the existing fuzz target.** `tools/buildGazlFuzz.sh` already builds a libFuzzer harness over `GAZLCmd`; extend
   it to run both engines and compare, so the source→assemble→execute path is fuzzed end‑to‑end.
8. **Dual‑arch CI matrix.** Native ARM64 runners (GitHub‑hosted macOS‑ARM and Linux‑ARM are available now) + x64;
   cross‑check with **QEMU‑user** and **Rosetta 2** to shake out ISA‑specific emitter bugs cheaply. Run the
   suspend/resume fuzzer on *both* arches.
9. **(Optional) third oracle.** Lower the fuzzed program to wasm and run through wasmtime — a fully independent
   implementation to triangulate disagreements (§3.5). High value if a divergence is ever ambiguous about which engine is
   "right."

regalloc2's **symbolic checker** (used by Cranelift) is the model for #4/#5: prove a transformation preserves semantics
rather than test‑and‑hope. Our v1 has no real regalloc, so this mostly applies once v2 register caching lands — at which
point a symbolic check that "every use reads the value its def wrote" is worth adding.

---

## 9. Performance expectations

Calibration from the literature (all baseline‑vs‑interpreter, i.e. our situation):

- **Baseline vs optimizing** is ~**2–3×** apart, and baseline compiles ~**an order of magnitude faster**
  ([Titzer](https://arxiv.org/abs/2305.13241)). Sparkplug is quoted as ~2 orders of magnitude faster to *compile* than
  TurboFan. So the "cheap tier" captures most of the dispatch win at a fraction of the engineering.
- **Cranelift**: ~2 % slower than TurboFan, ~14 % slower than LLVM, ~10× faster compile ([Titzer](https://arxiv.org/abs/2305.13241)).
- **CPython copy‑and‑patch JIT**: only ~4–12 % because Python's cost is object semantics, not dispatch — a *counter*‑
  example that clarifies *why* GAZL will do better: GAZL values are raw 32‑bit words, so removing dispatch overhead
  exposes real arithmetic speedup.

**What to expect for GAZL.** The interpreter is already fast (10–25 % of optimized native, `GAZL.h:29`), because its
opcodes are pre‑specialized and dispatch is a tight switch. A baseline JIT removes: (a) the switch/branch‑misprediction
per instruction, (b) the `Instruction` fetch/decode (16 bytes/op), (c) redundant bounds checks on constant addresses,
(d) the interpreter's memory round‑trip for the ip. For **numeric DSP loops** (the target workload), expect roughly
**3–10× over the interpreter**, i.e. approaching 50–100 %+ of the interpreter's "optimized native" reference for
straight‑line arithmetic — with the gap to hand‑optimized native being SIMD/autovectorization, which is an **explicit
non‑goal** (v1 is scalar; wide DSP kernels belong in native ops or a future vector‑op set). Load‑time compile cost for
KB‑scale programs is sub‑millisecond‑class and happens off the audio thread anyway, so it never touches real‑time budget.

The honest caveat: if a firmware's hot loop is dominated by `PEEK`/`POKE` with *computed* addresses, the mandatory
software bounds check caps the win (that's the price of no‑guard‑page safety). v2 masking (§7) and register caching
recover much of it.

---

## 10. iOS / no‑JIT: the AOT complement

Where JIT is impossible (iOS, un‑entitled hosts), two non‑JIT accelerators exist beyond the interpreter:

- **AOT‑transpile GAZL → C++** at *firmware build time* for first‑party products, compile with the platform toolchain,
  ship native. This reuses almost the entire JIT lowering (same per‑opcode templates, same sandbox checks) but emits C
  instead of machine code — so it's cheap to build once the lowering exists, and it gives *optimizing‑compiler* quality
  for free. This is the `gen~`/Faust‑AOT/Cmajor‑native pattern. Best for shipping fixed firmwares to iOS.
- **AOT‑compile GAZL → wasm → `.aot`** (WAMR/wasmtime AOT) for a portable pre‑compiled artifact where a wasm runtime is
  acceptable. More moving parts; only if the C++ route is insufficient.

Both keep the interpreter as the runtime fallback for *user‑authored* scripts on locked‑down platforms.

---

## 11. Phased roadmap

### 11.0 Phase −1: de‑risking spikes (before committing to the converters)
Throwaway prototypes and measurements, each retiring one specific risk with a pass/fail gate. **Do Tier A first** — it
is cheap and it is where the project could actually die; bail there for the cost of days, not months. Then Tier B (the
JIT needs a *defined* oracle), then Tier C (de‑risk the build in miniature). Note C3+C4 together are essentially a
one‑function JIT: once Tier C is green, "writing the converters" is *generalizing a working slice across ~120 opcodes*,
not starting from zero. None of this code is kept.

| Spike | Retires (risk) | Build (minimal) | Gate |
|---|---|---|---|
| **A1. Exec‑memory in real hosts** | §2.1 — entitlement belongs to the *host*; Logic unknown | Stub AU/VST that on load walks the probe ladder (`MAP_JIT`+toggle → `mmap`+`mprotect` → fail) and logs the winner; load into the surveyed DAWs, esp. `allow-unsigned`‑only ones on Apple Silicon + Logic | Per host, known which strategy succeeds; at least one works everywhere targeted |
| **A2. ARM64 cross‑thread publication** | §6.2#2 / §12#1 — the top correctness unknown | Thread A writes a trivial fn + barrier/i‑cache seq + release flag; thread B spins then executes, millions of iters on real M‑series + Win‑ARM under memory pressure; *also* run with barriers removed to prove the test bites | Survives millions of runs; harness demonstrably detects a bad sequence |
| **A3. Speedup + compile‑latency reality check** | §9 — ROI of the whole project | Hand‑compile one zero‑escape hot kernel (`MLMoogFilter`/`perfTest`) for one arch; measure loop speedup vs interpreter and load‑time compile cost/KB | Speedup in the 3–10× ballpark (not ~1.5×); compile latency sub‑ms‑class off‑thread |
| **B1. Interpreter cross‑arch determinism diff + spec lock** | §6 — JIT must match a *defined* oracle, not a buggy one | Run today's interpreter on x64 + ARM64 over the 57‑program corpus + edge cases; diff. Surfaces `FTOI`/`idiv`/shift/FTZ‑DAZ divergences; forces the §6 + §1.1 spec decisions | Interpreter bit‑identical across arches, or every divergence deliberately defined + documented |
| **C1. Compiler‑as‑oracle probe set** | §3.2.1 — validate the "what to emit" methodology | C probes (const operands) for a float arith, int arith w/ div guard, bounds‑checked `PEEK`, saturating `FTOI`, a branch, a `CALL`; disassemble both arches | A canonical target‑sequence table per arch; 1:1 mapping confirmed, no frame surprises |
| **C2. Emitter + disassembler‑diff harness** | §3.2 — "encoding bugs are on us" | `Emitter` for ~10 instructions + round‑trip test (emit → disassemble → assert intended decode) | Harness reliably catches a deliberately‑corrupted encoding |
| **C3. Vertical slice: one fn, one arch, bit‑identical** | §5 — forces every ABI decision concrete | Hand‑emit one trivial fn (int loop + fuel check + return); run via trampoline; `memcmp` final VM state vs interpreter | Identical state; pinned‑register/trampoline/trap‑ABI/safepoint conventions proven |
| **C4. Suspend/resume + engine interchange + traps** | §5.2 — the bit‑identical‑state linchpin | Run C3 with tiny fuel → timeout mid‑way → resume in the *other* engine; fire an explicit‑check trap (bad poke, div0) under a CFG/CET Windows host | Suspend‑here/resume‑there gives identical results; traps propagate `Status` with no signal handler |

### Build phases (after Phase −1 gates pass)

| Phase | Deliverable | Gate |
|---|---|---|
| **0. Spec** | Pin down idiv/shift/`FTOI`/COPY/fuel semantics **+ the §1.1 local‑aliasing rule** (cross‑local access via wild index / derived pointer = unspecified‑but‑safe); fix interpreter to match; add golden `.gazl` cases | Both‑engine golden tests pass (interpreter‑only until JIT exists) |
| **1. Scaffolding** | Runtime capability probe (macOS `MAP_JIT`, Win `VirtualAlloc`, Linux `mprotect`) + W^X page manager + `--no-jit` + fallback plumbing | Probe correctly detects entitled/un‑entitled hosts; fallback never crashes |
| **2. Emitter** | Hand‑rolled x64 + AArch64 encoder behind one interface (decision: own encoders, no library); disassembler‑diff test of every encoded form | Round‑trip encode↔disassemble matches reference for all forms used |
| **3. Baseline JIT (arithmetic + memory + branches, no calls)** | Compile leaf functions; explicit bounds checks; safepoints; per‑opcode differential tests | #2 (per‑opcode) + #3 (leaf‑program fuzz) green on both arches |
| **4. Calls, indirect calls, natives, traps, fuel, suspend/resume** | Full ABI; status‑propagation traps; block fuel; safepoint side table | Suspend/resume fuzzer (random fuel slices + engine switch) green |
| **5. Hardening** | Static verifier over emitted code; CI matrix (native ARM+x64, QEMU, Rosetta); extend `GAZLFuzz` to dual‑engine | Verifier passes on full corpus; sustained fuzzing finds nothing |
| **6. Ship opportunistically** | JIT on where probe succeeds; interpreter elsewhere; telemetry on JIT‑on rate across hosts | Real‑world A/B shows perf win + zero correctness regressions |
| **v2 (later)** | Whole‑function scalar register allocation via the §1.1 escape analysis (non‑escaping scalars in regs, escaping slots memory‑resident, write‑back at safepoints), constant‑address fast paths, optional masking bounds checks | Symbolic‑check‑backed regalloc; measured 2nd win |
| **AOT (parallel)** | GAZL→C++ transpiler reusing the lowering, for iOS/first‑party | Bit‑identical to interpreter on the test corpus |

---

## 12. Open questions & risks

1. **ARM64 cross‑thread publication** (§6.2#2): the "compile on loader thread, first execute on audio thread" barrier
   story must be validated on real Apple‑Silicon and Windows‑ARM hardware under load. This is the highest‑risk
   *correctness* unknown. Mitigation: publish‑once + acquire/release "ready" flag + explicit barriers; stress test.
2. **DAW entitlement reality** (§2.1): *now measured* (see table). Result: every current third‑party host tested can run
   a JIT via `allow-jit` (MAP_JIT) or at least `allow-unsigned-executable-memory` (`mprotect` path); the only unknowns
   are **Logic Pro** (non‑hardened Apple binary, no entitlements — must be tested empirically, and its out‑of‑process AU
   hosting may matter) and locked‑down/legacy hosts. Remaining work: confirm the `mprotect` RW→RX path actually succeeds
   at runtime in the `allow-unsigned`‑only hosts (Ableton/Studio One/Pro Tools/Waveform) on Apple Silicon specifically —
   the entitlement being present is necessary but the empirical probe is the real test.
3. **Hand‑rolled encoder vs library** (§3.3): *decided — hand‑rolled.* Own x64/AArch64 encoders behind one
   `Emitter` interface; SLJIT kept only as an emergency shortcut to first light. Keeps the project single‑unit,
   BSD‑2, zero third‑party surface, and gives full control of the sandbox/fuel/trap emission a library wouldn't.
4. **`GETL`/`SETL`/`ADRL` aliasing** (§1.1): *investigated and largely resolved.* Measured across the 57‑program golden
   corpus: `ADRL` 2.0 %, `GETL` 0.4 %, `SETL` 0.4 % of instructions; they target *local arrays*, not scalar working
   variables; and 15/57 programs (including the hot numeric kernels) contain none. v1 keeps all locals memory‑resident
   (no analysis needed). v2 uses a one‑pass escape analysis to register‑cache non‑escaping scalars whole‑function. The
   supporting **spec tightening** (the "Local‑access bounds rule" in §1.1 — cross‑local access via a wild index / derived
   pointer is *unspecified but memory‑safe*) is **adopted** and folded into Phase 0.
5. **Trap mechanism** (§5.4): status‑propagation vs single‑trampoline — prototype both, measure call overhead, pick.
6. **Fuel granularity** (§5.5): confirm block‑granular timeout latency is acceptable to the host integration; cap block
   weight accordingly.
7. **License hygiene:** if embedding a library, AsmJit = Zlib, SLJIT/DynASM = BSD/MIT (all BSD‑2‑compatible); avoid
   LGPL (Lightning, LibJIT) and LLVM‑as‑dependency for the core. Verify Cockos WDL/EEL2 license terms before borrowing
   any stencil ideas from their source.

---

## Appendix A — Sources

**Repo:** `src/GAZL.h`, `src/GAZL.cpp` (@ `a29bd02`), `docs/InstructionSet.md`, `docs/Overview.md`, `tools/buildGazlFuzz.sh`,
`tests/impala/sources/`.

**Copy‑and‑patch / CPython:** [Xu & Kjolstad, *Copy‑and‑Patch Compilation* (OOPSLA'21)](https://fredrikbk.com/publications/copy-and-patch.pdf) ·
[PEP 836](https://peps.python.org/pep-0836/) · [LWN: Following up on the Python JIT](https://lwn.net/Articles/1029307/) ·
[Deegen/LuaJIT‑remake](https://arxiv.org/abs/2411.11469).

**Baseline compilers:** [Titzer, *Whose baseline compiler is it anyway?* (arXiv 2305.13241)](https://arxiv.org/abs/2305.13241) ·
[V8 Sparkplug blog](https://v8.dev/blog/sparkplug) · [V8 Liftoff blog](https://v8.dev/blog/liftoff).

**wasmtime / Winch / Cranelift:** [Winch AArch64 support](https://bytecodealliance.org/articles/winch-aarch64-support) ·
[Wasmtime portability (signals‑free)](https://bytecodealliance.org/articles/wasmtime-portability) ·
[Wasmtime Config docs](https://docs.wasmtime.dev/api/wasmtime/struct.Config.html) ·
[CVE‑2026‑34987 Winch escape](https://github.com/bytecodealliance/wasmtime/security/advisories/GHSA-xx5w-cvp6-jv83) ·
[cranelift.dev](https://cranelift.dev/).

**Assembler libs:** [SLJIT](https://github.com/zherczeg/sljit) ([Apple‑Silicon W^X #99](https://github.com/zherczeg/sljit/issues/99)) ·
[AsmJit](https://asmjit.com/) · [DynASM/LuaJIT](https://luajit.org/dynasm.html) · [MIR](https://github.com/vnmakarov/mir).

**Audio prior art:** [Cockos WDL/EEL2 source](https://github.com/justinfrankel/WDL/tree/main/WDL/eel2) ·
[Cmajor](https://cmajor.dev/) · [Cmajor releases](https://github.com/cmajor-lang/cmajor/releases) ·
[Faust](https://faust.grame.fr/).

**macOS / executable memory:** [Apple: allow‑jit entitlement](https://developer.apple.com/documentation/BundleResources/Entitlements/com.apple.security.cs.allow-jit) ·
[pthread_jit_write_protect_np(3)](https://keith.github.io/xcode-man-pages/pthread_jit_write_protect_np.3.html) ·
[Kyle Avery, *macOS JIT Memory*](https://kyleavery.com/posts/macos-jit-memory/) ·
[Outflank, *macOS JIT Memory*](https://www.outflank.nl/blog/2026/02/19/macos-jit-memory/) ·
[LuaJIT #1072 (iOS JIT)](https://github.com/LuaJIT/LuaJIT/issues/1072).

**SFI / verification:** Wahbe, Lucco, Anderson & Graham, *Efficient Software‑Based Fault Isolation* (SOSP'93) ·
Google Native Client (NaCl) · [VeriWasm](https://cseweb.ucsd.edu/~dstefan/pubs/johnson:2021:veriwasm.pdf).

*Uncertainty flags:* CPython JIT speedup figures and Winch/Cmajor status are version‑dependent (2025–2026 snapshot);
the DAW‑entitlement claim is community‑sourced, not an authoritative table (see risk #2); the ARM64 cross‑thread barrier
sufficiency (§6.2#2) is stated from engine practice but must be hardware‑validated for GAZL's exact publish model.
