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
  (roughly 3–10× over a switch interpreter for numeric loops; see §9), the committed **v2 register allocator** (§5.7,
  designed up front so v1 can't preclude it) closes most of the rest, and DSP inner loops that need more should be
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
| Calls | Return addr + caller `dsp` pushed to a **non‑VM‑addressable** `CallStackEntry` stack (`GAZL.cpp:1614`); zero‑copy args via transient window; `FUNC` prologue checks stack overflow (`GAZL.cpp:1607`) | Return addresses cannot be corrupted by VM code — CFI for free. Calls lower to dispatcher‑threaded segment transfers (§5.4), never nested host frames — required by suspend/resume. |
| Indirect calls | Runtime‑checked: target index `< codeSize` **and** `code[idx].opcode == FUNC_CC_` (`GAZL.cpp:1609`) | Emit an entry‑offset table lookup with the same two checks → safe indirect branch. |
| Interruption | Fuel: `--clockCyclesLeft >= 0`, 1/instr (`GAZL.cpp:1605`); native returning nonzero suspends *at* the CALL for retry (`GAZL.cpp:1620`) | Cooperative; we insert fuel checks at back‑edges. No preemption, no async signals needed. |
| Suspend/resume | Full state = `Processor` fields + memory + ipStack; `Processor` is copyable; resumes by re‑`run()` | Requires safepoints where state is interpreter‑identical. Drives the whole architecture (§5). |
| Code | `const Instruction*`, immutable after load, shareable across `Processor`s/threads | Compile once, run many; emitted code must be position‑independent and reentrant (state via context register). |

The "sharp edges" a JIT must respect are the three instructions that expose the data stack to dynamic/address access —
`GETL`, `SETL`, `ADRL`. They do **not** block a correct JIT (v1 keeps every local in memory and is trivially
interpreter‑exact); they only bound how aggressively **v2** may cache locals in registers. Their exact semantics
(verified against source) and an empirical measurement of how much they actually matter are in **§1.1** — the short
version is: they are rare (~2.8 % of instructions combined) and the hot numeric kernels contain **none** of them, so
whole‑function scalar register allocation is available exactly where it pays off. Where they *do* appear they are more
aliasing‑hostile than a first look suggests: ~18 % of `ADRL` targets are scalars, every `ADRL` uses a `*0` size hint, and
some programs deliberately `COPY` across a bank of contiguous named locals through one `ADRL` pointer — so the local
frame layout is part of the ABI and v2's caching must respect cross‑local aliasing (see §1.1).

### 1.1 `GETL` / `SETL` / `ADRL` — exact semantics, aliasing, and what it costs register allocation

These three are the only ways a GAZL function reaches stack memory *dynamically* or *by address*, so they are the whole
of the aliasing question. Precise semantics (interpreter `GAZL.cpp:1642`–`1645`, operator table `GAZL.cpp:359`/`469`/
`751`):

- **`GETL dst, arrayBase, index`** → `dst = (dsp + arrayBaseSlot)[index]`. `arrayBase` is a *local variable operand*
  encoded as a **constant slot offset** (the base of a `LOCA` array); `index` is a **dynamic** int read at runtime. It
  is a local‑array indexed load.
- **`SETL arrayBase, index, value`** → `(dsp + arrayBaseSlot)[index] = value`. Indexed store; `value` may be a var or a
  constant (`SETL_VVV`/`SETL_VVC`).
- **`ADRL dstPtr, var, *size`** → `dstPtr = &(dsp + varSlot) − memBase` (a VM pointer to a local). The `*size` hint
  drives the `LOCAL_BOUNDS` flag, which grows the reserved frame so `&var + size` stays in‑frame (`GAZL.cpp:1526`).
  **But in practice `*size` is *always* `*0`** — all 348 `ADRL` sites in the golden corpus emit `*0`, so it reserves
  no extra bytes and provides **no per‑slot aliasing bound**; the derived pointer is bounded only by the data stack.
  `ADRL` is how a local (scalar *or* array) is passed by reference to a function/native or accessed via
  `PEEK`/`POKE`/`COPY` — including bulk copies that deliberately span **several contiguous named locals** (see below).

> **Aside — why `ADRL` carries a `*size` at all (reconstructed from `GAZL.cpp`; no design doc exists).** `*size` is
> *not* an access bound; it is a **frame‑headroom reservation for GAZL's overlapping‑frame calling convention**, and it
> shares the `LOCAL_BOUNDS` flag with `CALL` (`GAZL.cpp:359`, `371`–`379`). The data stack uses zero‑copy overlapping
> frames with `dsp` on the boundary: locals (`LOCA`/`PARA`) are shifted to *negative* offsets below `dsp`
> (`v->i -= localsSize`, `GAZL.cpp:1162`); transients `%n` sit at *non‑negative* offsets above `dsp`. The CALL/FUNC `dsp`
> arithmetic (`dsp += firstTransient` at `1617`, then `dsp += localsSize` at `1607`) positions a callee's frame so **its
> params land exactly on the caller's transients** — caller `%(t+k)` *is* callee param `k`, same memory. The one‑line
> handler `paramsSize = max(paramsSize, slot + size)` (`GAZL.cpp:1527`) therefore does two mirror‑image jobs: for `CALL`
> it reserves the transient region for `n` outgoing args; for `ADRL` it reserves headroom when you take the address of a
> scratch buffer **in the transient region** that a callee will fill, so the overlapping callee frame can't stomp it.
> Because it feeds the frame‑size / `DATA_STACK_OVERFLOW` reservation only, there is **no runtime per‑pointer check** —
> `POKE`/`GETL`/`SETL`/`COPY` bounds‑check against the whole RW region / data stack, never against `&var + size`.
>
> This `ADRL` path is effectively **dead in practice**: it only bites for an address‑taken *transient* with nonzero
> size, but the front‑end always allocates buffers as `LOCA` locals (below `dsp`, where a nested callee — placed *above*
> `dsp` — can never overlap them, so no reservation is needed) and always emits `ADRL … *0`. For a local operand `slot`
> is already the negative shifted offset, so `slot + 0 ≤ 0` can't raise `paramsSize` anyway. Net effect for the JIT:
> `*size` conveys nothing exploitable for aliasing, and making it a *trusted per‑pointer bound* (§1.1 discussion) would
> be adding a genuinely new runtime concept, not tightening an existing one.

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

> **Measurement provenance (to harden).** These figures and the derived ones below (48 % of instructions reference a
> transient, def→use ≈ 1, ~18 % of `ADRL` targets scalar, 348/348 `ADRL` = `*0`, 15/57 escape‑free) came from ad‑hoc
> scripts over `tests/impala/golden/`, with two known imprecisions: (1) the executable‑instruction filter is a regex
> approximation, and (2) the scalar‑vs‑array `ADRL` split scoped declarations per *file*, not per `FUNC`, so a name
> reused with different types across functions adds noise to the 18 %. The order of magnitude and the concrete cases
> (`perfTest.main` bank, `calc.$f`, `update` out‑params) are solid, but a proper corpus‑analysis pass (per‑function
> scoping, exact opcode stream) should regenerate them as part of the benchmark tooling before they anchor v2 sizing.

- **`ADRL` targets are *mostly* local arrays, but a substantial minority are scalar working variables** — roughly 82 %
  of targets are arrays (`$buffer`, `$fftBuffer`, `$delays:8`, `$gains:8`, `$samples`, `$moves`, `$tos`, `$cells`,
  `$line:0`, …) taken into a transient that is immediately consumed by a call (`print(buffer)`, an FFT, etc.) or a
  `PEEK`/`POKE`. But **~18 % (67 of ~375 measured targets, in 14 of 57 programs) are `LOCi`/`LOCf` scalars whose address
  is taken** — three recurring idioms, not a handful of exceptions:
  - **By‑reference in/out parameters.** In `pongdev_code.gazl`'s `update`, `$maxGain1`, `$maxDelay1`, `$endGain`,
    `$endDelay` are `LOCf` scalars used in ordinary arithmetic (`MOVf $maxGain1 #0.00001`, `DIVf %7 #256.0 $maxGain1`)
    *and* passed by address to `calcGainsAndDelays`/`addTaps` so the callee updates them in place. `calc.gazl`'s `$f`
    is a `LOCf` accumulator worked on across ~40 instructions, then address‑taken so a callee writes the result back.
  - **The global↔local copy idiom.** `ADRL %0 $localParams *0` + `COPY %0 &params *PARAM_COUNT` copies a global array
    into a local (and the reverse copies back). Common across the corpus (~30 `&`‑COPY sites).
  - **Bulk load/store across a *bank of contiguous named scalars* — and this one crosses locals.** `perfTest.gazl`'s
    `main` declares 13 adjacent `LOCf` scalars (`$r, $k, $g0, $dxzL … $d3zR`), then does `ADRL %0 $r *0` +
    `COPY %0 &gGlobalState *13` to load **all thirteen at once** from a global state struct, works on them
    register‑style, and stores a 10‑word sub‑range back with `ADRL %0 $dxzL *0` + `COPY &gGlobalState:3 %0 *10`.
    `buffer.gazl`/`nobuffer.gazl` do the same with a scalar `$lpx` and `COPY *8`/`COPY *4`. The `ADRL` size hint is
    `*0` and the copy count (13) is far larger than the named target (1) — the write is bounds‑checked against the
    **data stack**, not the `$r` slot, so it legitimately spans the whole contiguous bank. **This is a normal,
    compiler‑generated technique and the programs depend on the exact frame layout.**

  Consequences for the JIT: *(a)* the slot whose address is taken becomes memory‑resident, which already pins hot
  scalars like `$f`/`$maxGain1`; and *(b)* — more importantly — an `ADRL`‑derived pointer with a `*0` size can read or
  write an **unbounded contiguous span of the frame**, so it can alias *neighboring* locals that are never themselves
  `ADRL` operands (`$k`, `$g0`, `$dxzL`, … in the `perfTest` bank). The local frame layout is therefore **part of the
  ABI** — the JIT/AOT must reproduce declaration order/offsets exactly — and escape analysis cannot key on "is this slot
  an `ADRL` operand?" alone (see the corrected rule below).
- **`GETL`/`SETL` are array subscripting with a runtime index** — `counts[color]`, `moves[capturesCount]`,
  `fftInput[idx]`, `mydata[i]`. They only appear alongside arrays that are already memory‑resident.
- **15 of 57 programs contain *zero* `ADRL`/`GETL`/`SETL`** — and they include the compute‑bound numeric kernels where a
  JIT wins most: `MLMoogFilter`, `perfTest1`, `perfTest2`, `BitMaskMod`, `ModTest`, `linsub`. In those, every local is a
  scalar with no address exposure → **fully register‑allocatable across the whole function.**

**Consequence for register allocation — the escape *floor*.** A one‑pass scan cannot mark escaping slots by "is this
slot an `ADRL` operand?" — that is unsound, because a `*0`‑size `ADRL` + `COPY`/pointer arithmetic writes a contiguous
span that reaches locals which are never `ADRL` operands themselves (the `perfTest` bank above). The sound and simple
rule, given the provenance‑bounded spec below: compute a per‑function **escape floor** — the minimum layout offset over
all `ADRL` targets and `GETL`/`SETL` bases. Every slot *at or above* the floor is **aliasable** (a defined pointer
access may reach it); every scalar *below* it, and every transient, is **private** (no defined access can touch it).
Then:

- **v1:** ignore all of this — every local is memory‑resident, so the JIT does exactly what the interpreter does and is
  bit‑identical by construction. `GETL`/`SETL`/`ADRL` lower to the same checked memory ops the interpreter runs.
- **v2:** registers become a **write‑back cache of the frame** (§5.7): every slot — aliasable or not — is cacheable
  within a basic block, with conservative flushes around pointer memory ops (v2.0, sound with no aliasing rule at all);
  **private** slots additionally get fixed whole‑function *bound* registers exempt from those flushes (v2.1 — this is
  what the escape floor and the §1.1 rule are for). The 15 zero‑escape kernels (`perfTest1`/`perfTest2`/
  `MLMoogFilter`/…) have no floor, so every scalar is bound‑eligible; even `perfTest.main` (the 13‑slot bank) keeps its
  14 pre‑bank scalars bound. The full allocator design is **§5.7**.

**Spec decision — two iterations.** A first draft proposed "distinct named locals never alias" (any cross‑local access
= unspecified). **Retracted as unsound**: `perfTest.gazl`, `buffer.gazl`, and `nobuffer.gazl` deliberately `ADRL` one
local and `COPY` a block spanning a whole run of adjacent named locals — the bulk load/store of a global‑state struct
into a bank of register‑like scalars is a normal, compiler‑emitted idiom, and the observed output *does* depend on that
cross‑local write. The second iteration (adopted below) keeps that idiom **defined** while drawing the unspecified zone
where no real program goes — which is also the weakest rule under which register caching is possible at all:

> **Local‑access rule (normative, provenance‑bounded).** A pointer derived from `ADRL var` in frame F (through any
> chain of `ADDp`/`SUBp`/`COPY`/argument passing) yields **defined** values exactly within `[&var, F.dsp)` — from the
> named target *upward through the end of F's locals*. A dynamic `GETL`/`SETL` index is likewise defined within
> `[base, F.dsp)`. A pointer derived from a global symbol is defined within that symbol's declared section. **Within
> the defined span, crossing into adjacent named locals is defined behavior**: frame layout (declaration order, sizes,
> offsets) is ABI, and every implementation must reproduce the interpreter's contiguous‑memory result bit‑for‑bit —
> this is what the bank/out‑param idioms rely on. **Outside the defined span** (below the derivation point, past the
> owning frame's `dsp`, into any *younger* frame, or from a global section into the data stack) an access is
> **memory‑safe but yields an unspecified value** — the sandbox bound (`BAD_PEEK`/`BAD_POKE` at the RW/data‑stack
> edges) and the fuel limit are unchanged, and the interpreter's current behavior is one conforming instance.

Why the rule has exactly this shape: *without* an unspecified zone, a variable‑pointer `POKE` may legally hit any slot
of any active frame (the interpreter checks only `rwMemorySize`), so **no slot could ever be cached in a register,
anywhere** — v2 would be dead on arrival. And the zone cannot be larger (the earlier "named locals never alias" rule)
because the bank‑`COPY` idiom is load‑bearing. `[derivation point, owning frame's dsp)` is the tightest span that
covers every observed idiom — banks, by‑ref out‑params, passed arrays, global↔local copies all stay inside it — while
leaving each frame's transients and every *other* function's private locals unreachable, which is precisely what the
v2 allocator (§5.7) needs. It changes nothing about the sandbox guarantee and nothing for v1; the interpreter needs no
modification. This is a **Phase 0 spec item**: it must be normative (with golden tests exercising the defined span)
before any caching JIT ships.

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
- **CET / shadow stacks (user‑mode):** the dispatcher (§5.4) owns the only native frame; segments are entered by
  `call` and leave by `ret` back to it — balanced, so CET is satisfied. Traps are ordinary returns‑with‑status, never
  `longjmp`/non‑local jumps that would unbalance the shadow stack. (The direct‑threaded `jmp` variant in §5.4 needs
  `ENDBR64` landing pads under IBT, not shadow‑stack changes.)
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

- **Tier 2 — register‑allocating JIT (v2, committed — §5.7).** Same lowering + registers treated as a write‑back
  cache of the frame, staged v2.0 (block‑local floating cache, no aliasing‑spec dependence) → v2.1 (bound registers for
  hot private scalars) → v2.2 (optional provenance‑scoped flushing). Not built first, but **designed up front**: v1 must
  satisfy the compatibility contract in §5.7.7 so v2 is an additive change. Still no speculation and no deopt — GAZL
  has no dynamic types to speculate on, so the machinery that causes most JIT CVEs never enters the design.

### 5.2 The invariant that makes everything safe and testable
> **At every safepoint, JIT program‑observable state is byte‑identical to the interpreter's at the same GAZL ip:** all
> frame slots written back to the data stack, `dsp`/`ipsp` in the `Processor`, and the GAZL ip materializable.

**`clockCyclesLeft` is deliberately *excluded* from that equality** — it is bookkeeping, not observable semantics.
Timeout in GAZL is a *suspend*, not a program event (the program computes the same result whether or not it is paused,
§5.5), so the exact fuel remaining at a suspend is implementation‑defined within block granularity. Two backends may
suspend at different points; that is legal precisely because no program can observe the difference. This is why the
interpreter is **not** modified to imitate the JIT's fuel granularity: there is no single canonical granularity to
imitate (it varies by backend, by v1‑vs‑v2 block sets, and by weight‑cap tuning), and coupling the reference semantics
to a mutable codegen artifact would stop the interpreter from being a fixed oracle. The interpreter stays exact and
per‑instruction; the JIT is free to be coarse (§5.5).

Safepoints = function entry, every call (GAZL and native), loop back‑edges (fuel checks), and returns; v2 additionally
syncs memory at every basic‑block boundary (§5.7), which makes them a superset. Between safepoints the JIT may hold
values in registers freely. This invariant delivers, for free:

- **Suspend/resume** (fuel timeout, native‑suspend): stop at a safepoint, state is already interpreter‑shaped, resume by
  re‑entering — into JIT *or* interpreter, interchangeably. Correctness is defined by *final result equals an
  uninterrupted run*, not by where the suspend landed.
- **Lockstep differential testing** (§8): align both engines at a chosen GAZL ip (driven by ip, never by cycle count)
  and `memcmp` the observable state (memory + `dsp` + `ipsp`), excluding `clockCyclesLeft`.
- **Mixed execution**: a JIT'd function can call an un‑JIT'd one (or vice versa) through the same call ABI.

### 5.3 Register & memory plan (v1)
Pinned registers (both ABIs have enough callee‑saved regs): `CTX` (`Processor*`), `DSP` (data‑stack pointer, mirrors
interpreter `dsp`), `MEMBASE` (precomputed `memoryBase − MEMORY_OFFSET` scaled to bytes), `FUEL` (clock cycles left), plus
scratch/temp regs and the FP scratch bank. The pinned set is deliberately minimal and enumerated: **every register not
pinned here is, by definition, the v2 allocator's pool** (§5.7.7 C5). Locals stay memory‑resident in v1 (§1, sharp edge
#1). An opcode like `ADDF_VVV %d,%a,%b` lowers to: `ldr s0,[DSP,#a*4]; ldr s1,[DSP,#b*4]; fadd s0,s0,s1;
str s0,[DSP,#d*4]` (AArch64) — three loads/stores that the CPU's store‑to‑load forwarding largely hides, and that v2
register caching removes (§5.7).

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

### 5.4 Calls, traps, and stack discipline (no signals, no longjmp, no nested host frames)

**Decision — GAZL calls are never host calls.** A GAZL call chain must be suspendable at any depth (fuel timeout,
native suspend) and resumable later — possibly by the *interpreter* (§5.2). A chain of nested host frames cannot be
reconstructed at resume time (after the unwind they are gone), so JIT'd code never nests host frames. One
**dispatcher** owns the single native frame; compiled functions are **segments** it threads between:

```
run / enterCall / resume:
    save host callee‑saved regs; load pinned regs (CTX, DSP, MEMBASE, FUEL, size regs)
    next = resolve(ctx)                      // entryTable[fn], or side table lookup when resuming mid‑function
    while next != EXIT:
        next = call next                     // segment executes; comes back with a continuation
    restore host regs; return ctx->status
```

- **Direct call** (`CALL_CVC`): `ipStack` overflow check; push interpreter‑shaped `{ip, dsp}` (`GAZL.cpp:1614`);
  `dsp += C1` for the arg window; transfer to the callee's entry. The **GAZL return ip** is what's pushed — not a
  native address — so the pushed state is exactly the interpreter's (§5.2) and either engine can continue from it. The
  native resume offset is recovered through the safepoint side table (per‑module array `code index → native offset`,
  O(1)).
- **Indirect call** (`CALL_VVC`): `idx = ptr − IP_OFFSET`; trap `BAD_CALL` unless `idx < codeSize` and
  `entryTable[idx] != 0` (nonzero only where `code[idx].opcode == FUNC_CC_` — mirrors the interpreter's two checks,
  `GAZL.cpp:1609`); then as direct.
- **Return** (`RETU`): pop `{ip, dsp}`; transfer to `resolve(ip)` — the dispatcher decides whether the continuation is
  a JIT segment or the interpreter, so **mixed execution falls out of the design** rather than being a feature.
- **Native call** (`CALL_NVC`): full safepoint write‑back, then a genuine host `call natives[idx](CTX)` — this one *is*
  a host call, because a native returns before GAZL continues, and if it suspends it does so by returning a `Status`.
  Nonzero `Status` → the exit path. Reentrancy (`enterCall` from inside a native) works because state is
  interpreter‑shaped at the boundary.
- **Traps** (bad peek/poke/call, div0, stack/ip overflow, fuel timeout): trap sites branch to a per‑function exit stub
  that stores `{status, GAZL ip of the faulting instruction, dsp, ipsp, cycles}` into the `Processor` and transfers to
  the dispatcher's EXIT — behaviorally identical to the interpreter's "leave `run()` with `ip` at the faulting
  instruction" (`GAZL.cpp:1762`). **No signals, no `longjmp`, no unwinding**; Hardened‑Runtime‑ and CET‑safe.

Segment‑to‑segment transfer has two viable encodings: **(a)** return‑to‑dispatcher (`ret` into the loop above —
simplest, trivially shadow‑stack‑balanced) and **(b)** direct‑threaded tail `jmp` between segments (one branch cheaper
per transfer; needs `ENDBR64`/`BTI` landing pads on IBT/BTI hosts). Start with (a); switching to (b) later is invisible
to everything else. Either way the cost is one indirect transfer per GAZL call/return — noise next to the interpreter's
per‑instruction dispatch, and irrelevant in the hot kernels, which don't call.

**Superseded alternative.** An earlier draft made GAZL call = native `call` with per‑frame status propagation, with the
dispatcher as the fallback. **Rejected**: nested host frames break the engine's async suspend/resume contract (resume
cannot re‑materialize a native call chain), and per‑frame propagation adds a status check to every call return. The
dispatcher model also simplifies v2 register allocation (§5.7): since no host register survives a segment transfer, the
caller‑saved/callee‑saved distinction disappears inside GAZL‑land.

### 5.5 Fuel

**The interpreter is the exact reference and stays untouched.** It charges 1/instruction, pre‑decrement
(`while (--clockCyclesLeft >= 0)`, `GAZL.cpp:1605`); `resetTimeOut(N)` executes exactly N instructions, then suspends
with `ip` at the next (un‑executed) instruction and `{ip, dsp, ipsp, clockCyclesLeft}` saved to the `Processor`
(`GAZL.cpp:1762`). `TIME_OUT` is the loop's fall‑through default (`GAZL.cpp:1599`), so exhaustion needs no explicit
branch. Resume = call `run()` again after `resetTimeOut(M)`.

`FUEL` is a pinned register (§5.3) mirroring `clockCyclesLeft`, flushed to the `Processor` at every safepoint. The JIT
charges **per basic block** — the hot path at a block leader of static weight `W` is one arithmetic op plus a
predicted‑not‑taken branch:

```
    subs  FUEL, FUEL, #W          ; AArch64 (x64: sub FUEL,W ; js …)
    b.mi  Ltimeout_<block>
Ltimeout_<block>:                 ; cold, shared with the §5.4 trap exit
    add   FUEL, #W                ; un‑charge: this block did not run → resume re‑enters it exactly
    ; store {block‑leader ip, dsp, ipsp, FUEL} → Processor ; status = TIME_OUT ; → dispatcher EXIT
```

Charge = the block's static instruction count. Checks go at **every block leader** (simple, bounds straight‑line
latency for free); the strictly‑necessary subset for termination is loop back‑edges + native‑call sites. This is the
standard baseline‑JIT placement (Liftoff/Winch check at back‑edges) and stays *cooperative and deterministic* — no
async preemption, so audio‑thread safe.

**Per‑instruction interpreter + block‑granular JIT is correct by construction, not a divergence to tolerate.** Because
timeout is a suspend and never a program event (§5.2), the two engines suspending at *different* points with *different*
`clockCyclesLeft` changes nothing a program can observe: give either enough fuel and it completes with identical
results; resume from any suspend and the final result equals an uninterrupted run. `clockCyclesLeft` is therefore
implementation‑defined within block granularity and excluded from the equivalence relation (§5.2), and — per the
Phase 0 spec — a native may depend only on `≤0` vs `>0` (stop or not), never on the exact remaining value.

**Weight cap — for latency *and* liveness.** Split long straight‑line runs so no block exceeds `maxBlockWeight`: (1)
worst‑case time‑to‑suspend is bounded by the largest block; (2) since a block runs all‑or‑nothing (it is not started
unless it can finish), a block whose weight exceeded the host's per‑`process()` fuel grant could never execute — so
`maxBlockWeight` must sit well below that grant, and the host must grant at least `maxBlockWeight` per resume.

### 5.6 Compilation pipeline

**What compiling a module produces.** At patch load, off the audio thread, the JIT walks the finalized `Instruction[]`
(§5.0) once per `FUNC` and emits native code. GAZL‑world (instruction indices, frame slots) and native‑world (machine
addresses, registers) are two coordinate systems; besides the code, the JIT emits two small tables that translate
between them at the only moments it matters — calls, suspends, and traps:

- **Entry table** — `code index → native entry address` (`0` where the index isn't a `FUNC`). Read by the dispatcher's
  `resolve()` to enter a function, and by indirect `CALL_VVC` as the runtime call‑target check (nonzero ⇔ a real
  `FUNC`, mirroring `GAZL.cpp:1609`).
- **Safepoint side table** — `GAZL ip ↔ native offset`, one entry per safepoint (block leaders, call sites). It is a
  *reverse* map, read only when **re‑entering** native code (see below). No live‑register mask is needed: memory is
  fully synced at every safepoint (§5.7.5), so resume is "reload the pinned registers, jump to the native offset."

**Two directions — do not conflate them.** Leaving JIT code and re‑entering it are opposite halves of a round trip, and
each needs the *other* coordinate. This is the source of the common confusion about per‑site exit stubs:

- **Outbound** (a block's cold timeout/trap stub, §5.4/§5.5): you hold a native PC and must record a GAZL ip to resume
  from, so you **materialize this block's GAZL ip as an immediate** into `Processor.ip`, then jump to the shared exit
  tail. That immediate is unavoidable — the outbound path is *producing* the resume key — and it is inherently per‑block
  (each block has its own leader ip and fuel weight). Only the tail (store fuel/dsp/ipsp, set status, return to the
  dispatcher) is shared per function.
- **Inbound** (`resolve()` in the dispatcher): you hold a GAZL ip (from `Processor.ip` on resume, or popped off
  `ipStack` on `RETU`) and **look it up in the side table to get the native offset** to jump to. This is where the table
  earns its keep — it consumes the ip the outbound path produced. It therefore does *not* remove the outbound immediate;
  the two operate in opposite directions.

**The passes.**
1. **Emit** — walk each `FUNC`'s instructions once (basic‑block edges are already explicit: relative branches,
   `FORi`/`FORp`, `SWCH`, fallthrough, `RETU`). Emit code; record each instruction's native offset, a safepoint entry
   per block leader / call site, and the function's entry address.
2. **Fixup** — patch forward branch displacements now that all native offsets are known. Branches are intra‑function and
   already relative (`GAZL.cpp:1176`), so this is purely local — no cross‑function linking.
3. **Fill the entry table**, then **publish** (§6.2): flip pages to executable and run the i‑cache/barrier sequence.
   Nothing executes the new code before this.

**Runtime.** `enterCall` / resume → dispatcher → `resolve()` → native segment (§5.4). Segments run until one hits the
shared EXIT with a `Status` (`OK`, `TIME_OUT`, a trap code); `run()` behaves exactly as today. `--no-jit` or a failed
executable‑memory probe feeds the same `Instruction[]` to the interpreter — no other difference.

### 5.7 v2 register allocation — registers as a write‑back cache of the frame (committed, staged)

v2 is not optional polish. Measured on the golden corpus: **48 % of executable instructions reference a transient**,
and transient def→use distance is almost always 1 (in `MLMoogFilter`, 44 of 51 transient uses consume the value on the
very next instruction) — so under v1, roughly half of all dataflow round‑trips through memory (§5.3's `ADDF` lowering
is three memory ops for one `fadd`). This section fixes the v2 design in near‑pseudo‑code so that every v1 structure it
relies on is a stated contract (§5.7.7), not a lucky accident.

**The model.** The allocator is a **write‑back cache of frame slots**, with host registers as the cache lines and the
data‑stack frame as backing store. Every design question becomes a cache‑coherence question against three observers:
*memory operations* (a pointer‑borne read must see pending writes; a pointer‑borne write makes cached copies stale),
*safepoints* (calls, back‑edge fuel checks, returns — memory must be interpreter‑identical, §5.2), and *control‑flow
joins* (a label's predecessors must agree on cache state). Two kinds of line:

- **Floating lines** — any slot (transient or local), filled on first read, allocated dirty on write (no store at the
  def), LRU‑evicted under pressure, **flushed‑dirty and cleared at every basic‑block boundary**. Because floating state
  never crosses a label, joins are trivially consistent, resume needs no register reconstruction, and the §8 machine‑
  code verifier can check each block in isolation.
- **Bound lines** (v2.1) — a few hot *private* scalars given one fixed register for the whole function. Because the
  binding is the same everywhere, a bound line is join‑consistent by construction and survives labels — which is
  exactly what floating lines give up. ("Bound", not "pinned", to avoid colliding with §5.3's pinned VM‑state
  registers.)

**The stages — each independently sound, each measured** (`benchmarks/jit/JitBenchA3`, hand‑written models of the
emitted code, Apple Silicon, integer kernels; speedups vs the interpreter):

| Stage | Mechanism | Needs the §1.1 aliasing spec? | Arith kernel | Seq‑memory kernel |
|---|---|---|--:|--:|
| v1 | all slots memory‑resident | no | 2.8× | ~3× (est.) |
| **v2.0** | floating lines only; **conservative flush around every pointer memory op** | **no — sound with no aliasing rule at all** | 3.9× | 5.2–5.5× |
| **v2.1** | + bound lines for hot private scalars (escape floor) | **yes — this is what the spec is for** | 6.1× | 12.4–13.6× |
| v2.2 | + provenance‑scoped flushing (taint frame‑born pointers) | yes (already required by v2.1) | — | 5.7–6.4× without bound lines; ~10–20 % over v2.0 |

The measured surprise that set this staging: **the block‑boundary clear is the expensive part, not the pointer
flushes.** Conservative flushing costs only ~10–20 % over provenance‑scoped flushing (store‑to‑load forwarding makes
the flush/reload traffic nearly free, at least on Apple Silicon — expect somewhat more on small x64 cores), while bound
lines roughly **double** throughput on both kernels. So the aliasing spec's real customer is v2.1's bound lines, and
v2.0 ships with no spec dependence whatsoever.

**Corpus facts the design builds on (§1.1):** locals sit at negative dsp‑relative offsets in declaration order,
transients at `[0, paramsSize)`; frame layout is ABI; every `ADRL` carries `*0`; ~18 % of `ADRL` targets are scalars;
the contiguous‑bank bulk `COPY` through one `ADRL` is load‑bearing; 15/57 programs contain no `ADRL`/`GETL`/`SETL` at
all; and the per‑function measurement (`benchmarks/jit/JitEscapeAnalysis`) shows the escape floor alone would leave
real firmware `process()` functions with **all** scalars aliasable purely because Impala declares buffers first — which
is why aliasable slots must be *cacheable between flushes* (v2.0 floating lines) rather than permanently
memory‑resident.

#### 5.7.1 v2.0 — the floating cache and its coherence events

All slots — transients and locals alike — are handled by one mechanism. Transients are **typeless** (`%1` legitimately
holds ptr, int, and float in successive defs), so a line's register class is chosen **per definition**; spills are raw
64‑bit word stores (bit‑pattern‑preserving, the same `Value`‑union semantics the interpreter has). Register class
matters for operations, never for the memory image.

```
cache : slot -> { reg, class, dirty }                // floating lines; meaningful only within one basic block
```

A def claims a register and records `dirty` — **no store at the def**. A use reads the mapped register — **no load on a
hit**. On pool exhaustion, evict the least‑recently‑used line (store if dirty, drop). The coherence events:

| Event | Action on floating lines | Why |
|---|---|---|
| read of a slot | hit → reg; miss → load, fill clean | |
| `PEEK`/`COPY`‑source via pointer, `GETL` | **flush dirty before** (lines stay valid) | the read must see pending writes |
| `POKE`/`COPY`‑dest via pointer, `SETL` | **flush dirty before + invalidate all after** | pending writes must land first (the pointer may miss them); after the write any cached copy may be stale |
| `PEEK`/`POKE` at a **constant address** | none | assembler‑validated global access; cannot touch the frame (§5.3) |
| label / block boundary | flush dirty, clear | predecessors need no agreement; resume/verifier stay block‑local |
| back‑edge | flush **before** the fuel check (§5.5) | a timeout suspends with memory interpreter‑identical |
| `CALL` (GAZL or native) | flush all | arg transients materialize here for free; no host reg survives the dispatcher (§5.4) |
| `RETU` | flush all | OUT params land in memory |

That is the whole of v2.0. Note what is *absent*: no aliasing analysis, no taint tracking, no escape floor, **no
dependence on the §1.1 local‑access rule** — a wild `SETL`, an `ADRL`‑derived pointer walking the frame, anything the
interpreter permits is handled correctly, because memory is current at every pointer op and every block boundary.
Correctness never rests on the clever part. Measured: 3.9× (arithmetic) / 5.2–5.5× (bounds‑checked memory loop) vs the
interpreter — already 1.4–1.9× over v1.

#### 5.7.2 v2.1 — bound lines for hot private scalars (the 2× lever)

The remaining cost in v2.0 is the block‑boundary clear: a single‑block loop (GAZL's dominant shape, thanks to fused
`FORi`) reloads every hot scalar at the top of each iteration and stores the dirty ones at the bottom. Bound lines
remove exactly that: a few long‑lived named values (`$acc`, loop counters) get one host register for the whole
function. No live ranges, no interference graph — the binding is a flat table, and because it is identical in every
block, a bound line **survives labels and back‑edges** (store‑when‑dirty at block ends, never reloaded except at call
returns and resume points).

Bound lines are **exempt from all pointer‑op flushes** — and that exemption is only sound for slots no defined pointer
access can reach. This is where the **provenance‑bounded local‑access rule** (§1.1, normative, Phase 0) enters, via the
escape floor:

```
escapeFloor = +INF                                   // a layout offset; locals < 0 < transients
for ins in func:
    if ins.op == ADRL:          escapeFloor = min(escapeFloor, layoutOff(base of ins.target))
    if ins.op in {GETL, SETL}:  escapeFloor = min(escapeFloor, layoutOff(ins.base))

private(s)   = s.layoutOff < escapeFloor             // unreachable by any defined pointer access
               or s.kind == TRANSIENT                 // above dsp: outside every defined span
aliasable(s) = otherwise                              // floating lines only (v2.0 treatment)

eligible  = { s : s.kind == LOCAL_SCALAR and private(s) }       // params (PARA scalars) included
weight(s) = Σ over references of s:  10 ^ loopDepth(block)
boundGPR  = top KG of eligible with class GPR (LOCi/LOCp), by weight
boundFPR  = top KF of eligible with class FPR (LOCf),      by weight
```

(`base of` — the floor anchors at the named object's **base**, not the `:offset` derivation point, so reverse iteration
through an interior/end pointer stays defined; measured on the corpus this changes no function's floor. §1.1's rule
text should be amended to match.) Register pools: because GAZL calls are dispatcher transfers (§5.4) and everything is
resynced around them, bound lines may use **any** host register not pinned by §5.3 — caller‑saved included. That is
≈20 GPR + ≈28 FP/SIMD on AArch64 and ≈9 GPR + ≈14 XMM on x64. Cap KG/KF (initially ~8/8) to keep scratch headroom;
eligible slots beyond K simply stay floating.

Measured effect: 6.1× (arithmetic) / 12.4–13.6× (memory loop) — bound lines roughly double v2.0, the single biggest
post‑v1 lever. Two notes from the per‑function corpus measurement: `perfTest.main`'s floor sits at the 13‑slot bank, so
its 14 pre‑bank scalars are bound‑eligible even though the function bulk‑copies the bank every iteration; and in the
`pong`‑pattern `process()` functions the floor is poisoned by a *first‑declared* buffer, so v2.1's value there depends
on the floor‑raising refinements in §5.7.6 (Impala declaring arrays last; `GETL`/`SETL` spans tightened to the named
array) — until then those functions still enjoy full v2.0 floating‑cache treatment.

#### 5.7.3 v2.2 — provenance‑scoped flushing (optional knob)

v2.0 flushes floating lines around *every* pointer memory op. The refinement: track a one‑bit taint — "derived from an
`ADRL` in this function" — through `MOVp`/`ADDp`/`SUBp`; only **tainted** pointer ops (plus `GETL`/`SETL`) flush, since
global‑born and param‑born pointers cannot reach this frame's slots (§1.1; a tainted pointer that round‑trips through
memory loses its guarantee, so the taint pass stays single‑pass). Measured: worth only ~10–20 % over conservative
flushing on Apple Silicon (store‑to‑load forwarding), likely more on small x64 cores. **Build only if profiling on x64
demands it** — it needs no new spec (v2.1 already requires §1.1) but adds the only genuinely subtle analysis in the
allocator, so it must earn its place with numbers.

#### 5.7.4 Codegen skeleton

One pass per basic block; the whole allocator state is the floating‑line map, a bound‑line dirty bitset (v2.1), and a
scratch free list. In v2.0 the `bound` table is simply empty — same code path.

```
emitBlock(b):
    cache = {}; dirtyB = {}
    for ins in b:
        case ARITH(op, dst, s1, s2):
            r1 = read(s1, classOf(op));  r2 = read(s2, classOf(op))
            emit op  defReg(dst, classOf(op)), r1, r2
        case PEEK/POKE, constant address:          // globals: direct MEMBASE+disp (§5.3)
            lower as v1                            // cannot touch the frame → no cache interaction
        case PEEK/COPY-src via pointer, GETL:      // pointer READ
            flushFloating(dirtyOnly: true)         //   memory must hold pending writes; lines stay valid
            lower as v1 (checked memory op)        //   (v2.2: only if the pointer is tainted)
        case POKE/COPY-dst via pointer, SETL:      // pointer WRITE
            flushFloating(dirtyOnly: true)
            lower as v1 (checked memory op)
            invalidateFloating()                   //   any cached copy may now be stale
        case CALL (GAZL or native):                // always terminates a block (contract C4)
            flushAll()                             //   arg transients are dirty floating lines → stored here
            <v1 call sequence, §5.4>
          resumePoint:                             // side-table entry
            emit reload of every bound reg         //   callee may have legally written aliasable/arg slots;
            cache = {}                             //   and no host reg survived the dispatcher anyway
        case backEdge:
            flushAll()                             // sync BEFORE the fuel check: a timeout suspends
            emit fuel check (§5.5)                 //   with memory already interpreter-identical
        case RETU:
            flushAll()                             // OUT params land in memory here
            <v1 return sequence, §5.4>
    flushAll()                                     // block boundary: memory synced, floating cleared,
                                                   //   bound regs stay VALID across the boundary

read(s, cls):
    if s in bound:   return bound[s]               // v2.1; join-safe: binding identical in every block
    if s in cache:   return cache[s].reg           // floating hit: load elided
    r = takeScratch(cls);  emit load r, [DSP + off(s)]
    cache[s] = {r, cls, dirty: false}
    return r

defReg(s, cls):
    if s in bound:   dirtyB += s;  return bound[s]
    r = takeScratch(cls);  cache[s] = {r, cls, dirty: true};  return r      // no store at the def
    // arrays: written through immediately, never cached (they are the backing store)

flushFloating(dirtyOnly):
    for s in cache if dirty:  emit store [DSP + off(s)], cache[s].reg      // raw 64-bit, class-agnostic
    mark all clean

invalidateFloating():  cache = {}

flushAll():
    flushFloating();  invalidateFloating()
    for s in dirtyB:  emit store [DSP + off(s)], bound[s];  dirtyB = {}
```

`takeScratch` never evicts a bound line: on pool exhaustion it evicts the least‑recently‑used floating line (store if
dirty, then drop). Straight‑line GAZL rarely has more than a handful of live values, so eviction is rare.

Two properties carry the correctness argument:

- **Stores are coalesced, never eliminated.** Within a block each slot is stored once with its *final* value instead of
  once per def — but every dirty slot **is** stored at every block end (dead slots included: no liveness analysis, and
  the measured cost of those extra stores is small). Since a safepoint can never occur mid‑block, the interpreter's
  memory at any block boundary also holds exactly each slot's last‑written value → the two engines are **byte‑identical
  in observable state at every safepoint**, and the §5.2 lockstep `memcmp` oracle stays exact. Dead‑store elimination is
  therefore *forbidden by design*: it would trade oracle precision for a few stores. Not worth it.
- **Loads are elided on every cache hit.** With def→use ≈ 1, transient memory traffic essentially vanishes even in
  v2.0; with v2.1's bound lines (only *dirty* is block‑local — the binding and the value survive), named hot scalars
  are read from memory once per function (plus once per call return).

Net effect on an `MLMoogFilter`‑style inner loop (one block, no calls): v1 spends ~3 memory ops per arithmetic
instruction; v2.0 spends one load + one store per *slot* per iteration (the block‑boundary clear); v2.1 spends **zero
loads** after first touch and one store per *modified* bound slot per iteration. Those steps are the measured
2.8× → 3.9× → 6.1× on the arithmetic kernel.

#### 5.7.5 Resume and the side table

Memory is fully synced at every block boundary and safepoint, so **resume never needs register state**: the resume stub
for any safepoint is "reload every bound register (none in v2.0), floating cache = `{}`, jump to the native offset."
The side table stays `GAZL ip ↔ native offset` (as in v1) — a per‑slot live mask is *not required*; the field stays
reserved for a future variant that syncs less. Suspend anywhere, resume in either engine, exactly as v1.

#### 5.7.6 Explicitly out of scope, and deferred refinements

No graph coloring, no linear scan, no live‑range splitting; no cross‑block floating state, no SSA/φ; no liveness / no
dead‑store elimination (see §5.7.4); no rematerialization; no instruction scheduling. Deferred refinements, in rough
value order — the first two raise the escape floor (more bound‑eligible scalars in `process()`‑pattern functions), the
rest shave flush traffic:

- **(a) Impala declares arrays after scalars.** The `pong` pattern (buffer declared first) poisons the floor for every
  scalar behind it; emitting `LOCA`/banks last fixes it in the front end at zero JIT cost (benefits recompiled
  firmwares only; frame layout stays per‑program ABI).
- **(b) Tighten `GETL`/`SETL` spans to the named array's declared size** (spec refinement): unlike `ADRL` pointers, the
  assembler knows the `LOCA` extent; corpus sites are genuine subscripts, and the cross‑local bank idiom uses
  `ADRL`+`COPY`, never `SETL`. Removes `GETL`/`SETL` from floor computation entirely.
- **(c) Dirty‑bit dataflow across blocks** to hoist bound‑line stores out of single‑block loops (closes most of the
  remaining gap to the C ceiling).
- **(d) Exact spans for constant‑count `COPY` through a pristine `ADRL`**, narrowing the floor to a range.
- **(e) Callee signature metadata** (§1.1 front‑end ideas) to keep bound values live across GAZL calls.

#### 5.7.7 The v1 compatibility contract (the point of this section)

v1 must be built so that v2 is **additive** — two new passes plus a new implementation of the slot accessors — never a
rewrite. Checkable in review:

- **C1 — dispatcher call model from day 1** (§5.4). "Nothing survives a GAZL call" is v2's central assumption;
  retrofitting host‑frame calls away later would rewrite the call/trap/resume machinery.
- **C2 — every operand access in the emitter goes through `read(slot)` / `defReg(slot)` helpers.** v1 implements them
  as naive `[DSP+off]` loads/stores; v2 swaps the implementation. No opcode lowering may hand‑roll a frame access.
- **C3 — the safepoint side table exists in v1** (`ip ↔ native offset`, plus a reserved live‑mask field), covering
  entry, call returns, and back‑edges.
- **C4 — basic‑block structure is first‑class** (§5.6 already splits blocks for fuel): a `CALL` terminates a block, and
  per‑block **loop depth** is computed and stored (v2's weights need it; v1 ignores it).
- **C5 — pinned registers stay minimal and enumerated** (§5.3). Everything unpinned is by definition v2's pool; v1
  lowerings must not grow informal fixed‑register habits beyond the declared scratch conventions.
- **C6 — frame layout is ABI** (§1.1): the emitter never reorders, pads, or re‑packs slots. The escape floor is a
  layout‑offset comparison; it dies if layout drifts from declaration order.
- **C7 — the equivalence relation excludes `clockCyclesLeft`** (§5.2, §5.5): the three‑way lockstep `memcmp` compares
  observable state (memory + `dsp` + `ipsp`) aligned by GAZL ip, never the fuel counter. The interpreter stays exact
  per‑instruction; v1/v2 may charge per block at whatever granularity their block sets imply. The only fuel
  requirements on the JIT are *liveness* (every back‑edge checked) and *progress* (weight cap) — not cycle‑identity
  with the interpreter.
- **C8 — a per‑function engine switch** in the test harness (interpret / v1 / v2), so v2 lands and bisects
  function‑by‑function against two oracles.
- **C9 — the provenance‑bounded local‑access rule ships in Phase 0** (§1.1, amended to anchor spans at the named
  object's *base*, §5.7.2), with golden tests that *rely* on defined‑span behavior (banks, out‑params, cross‑local
  `COPY`, reverse iteration) and a fuzzer policy: generated programs stay within defined spans; deliberately‑UB inputs
  are checked for memory‑safety only, not value equality. Retrofitting the spec after v1 ships would churn the corpus.
  Soft‑landing note: only **v2.1** (bound lines) depends on this rule — if the spec work slips, v2.0 still ships, since
  its conservative flushing is sound with no aliasing rule at all.

---

### 5.8 Worked example — one loop, compiled two ways

A tiny end‑to‑end illustration of the register/ABI convention (§5.3), fuel checks (§5.5), and the safepoint/trap model
(§5.4), plus the v1‑vs‑v2 register story (§1.1). The assembly is **schematic** — register choices and slot offsets are
made up for readability, not the output of a real assembler.

**GAZL source.** `sumTo(n)` → `0 + 1 + … + (n−1)`:

```gazl
FUNC                          ; sumTo
    INPi n                    ; input param
    OUTi result               ; output param (caller reads it back from the frame)
    LOCi i                    ; local
    MOVi result #0
    MOVi i      #0
@loop
    ADDi result result i      ; result += i
    FORi i n @loop            ; ++i ; if (i < n) goto @loop
    RETU
```

**What the JIT consumes** — the finalized `Instruction[]` (one 16‑byte record each; §5.0):

```
[0] FUNC_CC_   localsSize, paramsSize
[1] MOVi_VC_   result, #0
[2] MOVi_VC_   i,      #0
[3] ADDI_VVV   result, result, i        ← @loop  (basic-block head)
[4] FORi_VVB   i, n, →[3]               ← back-edge (fused ++, compare, branch)
[5] RETU_C__
```

The loop body is one basic block, instructions `[3]`+`[4]` → **block weight = 2**.

**Pinned‑register convention** (callee‑saved so calls don't clobber VM state):

| Role | ARM64 | x64 |
|---|---|---|
| `ctx` (`Processor*`) | `x19` | `rbx` |
| `dsp` (frame base) | `x20` | `r15` |
| `membase` (unused here — no `PEEK`/`POKE`) | `x21` | `r14` |
| `fuel` (`clockCyclesLeft`) | `w22` | `r13d` |

Frame slots (schematic byte offsets off `dsp`): `n=[+0]`, `result=[+4]`, `i=[+8]`. Omitted for focus: the `FUNC`
stack‑overflow check and a function‑entry fuel charge for the straight‑line prologue — the loop‑header check is the
interesting one.

#### v1 — locals memory‑resident (no register allocation)
Every operand is loaded from / stored to its frame slot, mirroring the interpreter exactly. Correct by construction.

ARM64:
```asm
sumTo:
        str     wzr, [x20, #4]         ; [1] result = 0
        str     wzr, [x20, #8]         ; [2] i = 0
.Lloop:                                ; [3] @loop
        subs    w22, w22, #2           ; fuel -= blockWeight(2)
        b.mi    .Ltimeout              ;   if fuel < 0 → safepoint
        ldr     w0, [x20, #4]          ; [3] ADDi: w0 = result
        ldr     w1, [x20, #8]          ;          w1 = i
        add     w0, w0, w1
        str     w0, [x20, #4]          ;          result = w0
        ldr     w0, [x20, #8]          ; [4] FORi: w0 = i
        add     w0, w0, #1             ;          ++i
        str     w0, [x20, #8]          ;          i = w0
        ldr     w1, [x20, #0]          ;          w1 = n
        cmp     w0, w1
        b.lt    .Lloop                 ;          if i < n → loop
        ret                            ; [5] RETU
.Ltimeout:
        mov     w0, #IP_loop           ; resume ip = &code[@loop]
        str     w0,  [x19, #IP_OFF]    ; Processor.ip
        str     w22, [x19, #FUEL_OFF]  ; Processor.clockCyclesLeft
        mov     w0, #-1                ; TIME_OUT
        ret
```

x64:
```asm
sumTo:
        mov     dword [r15+4], 0        ; [1] result = 0
        mov     dword [r15+8], 0        ; [2] i = 0
.Lloop:                                 ; [3] @loop
        sub     r13d, 2                 ; fuel -= 2
        js      .Ltimeout               ;   if negative → safepoint
        mov     eax, [r15+4]            ; [3] ADDi
        add     eax, [r15+8]
        mov     [r15+4], eax
        mov     eax, [r15+8]            ; [4] FORi
        inc     eax
        mov     [r15+8], eax
        cmp     eax, [r15+0]            ;   i vs n
        jl      .Lloop
        ret                             ; [5] RETU
.Ltimeout:
        mov     dword [rbx+IP_OFF], IP_loop
        mov     [rbx+FUEL_OFF], r13d
        mov     eax, -1                 ; TIME_OUT
        ret
```

Loop body ≈ **10 instructions** (6 memory ops).

#### v2 — locals in registers (v2.1 bound lines, §5.7.2; `result`/`i`/`n` are private per §1.1)
`result`, `i`, `n` are non‑escaping scalars (no `ADRL`/`GETL`/`SETL` touches them), so they live in registers for the
whole function. Load `n` once; keep `result`/`i` in regs; **spill back only at the epilogue and the timeout safepoint**
so the suspended state stays byte‑identical to the interpreter's. Caller‑saved scratch is fine — the loop makes no calls,
so no prologue save.

ARM64 (`w9=result`, `w10=i`, `w11=n`):
```asm
sumTo:
        ldr     w11, [x20, #0]         ; n loaded once
        mov     w9,  #0                ; result = 0
        mov     w10, #0                ; i = 0
.Lloop:
        subs    w22, w22, #2           ; fuel -= 2
        b.mi    .Ltimeout
        add     w9, w9, w10            ; [3] result += i
        add     w10, w10, #1           ; [4] ++i
        cmp     w10, w11               ;     i < n ?
        b.lt    .Lloop
        str     w9,  [x20, #4]         ; epilogue: write OUT param back
        str     w10, [x20, #8]         ;   (i: only if observable)
        ret
.Ltimeout:                             ; safepoint = spill live regs first
        str     w9,  [x20, #4]
        str     w10, [x20, #8]
        mov     w0, #IP_loop
        str     w0,  [x19, #IP_OFF]
        str     w22, [x19, #FUEL_OFF]
        mov     w0, #-1
        ret
```

x64 (`r8d=result`, `r9d=i`, `r10d=n`):
```asm
sumTo:
        mov     r10d, [r15+0]          ; n once
        xor     r8d, r8d               ; result = 0
        xor     r9d, r9d               ; i = 0
.Lloop:
        sub     r13d, 2                ; fuel -= 2
        js      .Ltimeout
        add     r8d, r9d               ; [3] result += i
        inc     r9d                    ; [4] ++i
        cmp     r9d, r10d              ;     i < n ?
        jl      .Lloop
        mov     [r15+4], r8d           ; epilogue: spill OUT
        mov     [r15+8], r9d
        ret
.Ltimeout:
        mov     [r15+4], r8d
        mov     [r15+8], r9d
        mov     dword [rbx+IP_OFF], IP_loop
        mov     [rbx+FUEL_OFF], r13d
        mov     eax, -1
        ret
```

Loop body = **4 instructions**, zero memory traffic.

#### What the machinery is doing
- **Fuel check** = one `subs`/`sub` + conditional branch at the loop head, charging the whole block at once (`−2`). The
  interpreter charged 1/instruction and could stop mid‑block; the JIT stops at block granularity. Because the check sits
  *before* the block's work, on timeout nothing in the block has executed yet, so resuming at `@loop` re‑runs it cleanly
  — no double `+= i`.
- **The timeout stub is a safepoint.** It records the resume ip (`&code[@loop]`) and fuel into the `Processor`, and in v2
  first **spills live registers** to their frame slots — that's what makes the suspended state identical to the
  interpreter's, so the host can resume in *either* engine. Any trap (bad `POKE`, div‑by‑zero) branches to a similar stub
  with a different status code — no signals, just a `ret` with a `Status` (§5.4).
- **v1 vs v2:** identical semantics, fuel accounting, and safepoint contract — only operand storage differs. The loop
  shrinks from ~10 instructions (6 memory ops) to 4 register ops. On real hardware v1 is not 2.5× slower (store‑to‑load
  forwarding hides most reloads), but v2 still wins by removing the memory µops and shortening the dependency chain — and
  this is exactly the case (tight scalar loop, no address‑taken locals) where the escape analysis lets v2 apply.

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
   interpreter‑result == JIT‑result (values *and* traps). Small, exhaustive‑ish, catches encoding bugs immediately.
   (Fuel is *not* asserted equal — it is excluded from the equivalence relation, §5.2/§5.5.)
3. **Grammar‑based program fuzzer + lockstep differential execution.** Generate random *valid* GAZL programs (reuse the
   assembler as the validity oracle; seed with the 57‑file Impala corpus under `tests/impala/sources`), run interpreter
   vs JIT to completion, `memcmp` the observable VM state at exit (memory + `dsp` + `ipsp`, **not** `clockCyclesLeft`).
   This is the wasmtime + `wasm-smith` playbook and V8's correctness‑fuzzing playbook, adapted. **Crucially, also fuzz
   the fuel schedule:** run with random `resetTimeOut` slices, suspend, resume (possibly switching engines mid‑run at
   safepoints), and assert the final state equals a single uninterrupted run — *this* is what pins down that block‑
   granular JIT fuel and per‑instruction interpreter fuel are observationally equivalent, without demanding they
   suspend at the same point. Exercises safepoints, resume, and interpreter↔JIT interchange in one test.
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
| **C3. Vertical slice: one fn, one arch, bit‑identical** | §5 — forces every ABI decision concrete | Hand‑emit one trivial fn (int loop + fuel check + return); run via dispatcher; `memcmp` final observable state (mem+`dsp`+`ipsp`, not `clockCyclesLeft`) vs interpreter | Identical observable state; pinned‑register/dispatcher/trap‑ABI/safepoint conventions proven |
| **C4. Suspend/resume + engine interchange + traps** | §5.2 — the bit‑identical‑state linchpin | Run C3 with tiny fuel → timeout mid‑way → resume in the *other* engine; fire an explicit‑check trap (bad poke, div0) under a CFG/CET Windows host | Suspend‑here/resume‑there gives identical results; traps propagate `Status` with no signal handler |

### Build phases (after Phase −1 gates pass)

| Phase | Deliverable | Gate |
|---|---|---|
| **0. Spec** | Pin down idiv/shift/`FTOI`/COPY semantics; **fuel = block‑granular in the JIT, `clockCyclesLeft` implementation‑defined within block granularity and excluded from the equivalence relation** (§5.2/§5.5; interpreter stays exact per‑instruction, unmodified); **+ the §1.1 provenance‑bounded local‑access rule** (defined access = `[derivation point, owning frame's dsp)`; cross‑local access *within* that span is defined — frame layout is ABI — beyond it memory‑safe‑but‑unspecified); add golden `.gazl` cases incl. cross‑local bulk `COPY` (banks, out‑params) | Both‑engine golden tests pass (interpreter‑only until JIT exists) |
| **1. Scaffolding** | Runtime capability probe (macOS `MAP_JIT`, Win `VirtualAlloc`, Linux `mprotect`) + W^X page manager + `--no-jit` + fallback plumbing | Probe correctly detects entitled/un‑entitled hosts; fallback never crashes |
| **2. Emitter** | Hand‑rolled x64 + AArch64 encoder behind one interface (decision: own encoders, no library); disassembler‑diff test of every encoded form | Round‑trip encode↔disassemble matches reference for all forms used |
| **3. Baseline JIT (arithmetic + memory + branches, no calls)** | Compile leaf functions; explicit bounds checks; safepoints; per‑opcode differential tests | #2 (per‑opcode) + #3 (leaf‑program fuzz) green on both arches |
| **4. Calls, indirect calls, natives, traps, fuel, suspend/resume** | Full ABI: dispatcher‑threaded calls + trap exit stubs (§5.4); block fuel; safepoint side table | Suspend/resume fuzzer (random fuel slices + engine switch) green |
| **5. Hardening** | Static verifier over emitted code; CI matrix (native ARM+x64, QEMU, Rosetta); extend `GAZLFuzz` to dual‑engine | Verifier passes on full corpus; sustained fuzzing finds nothing |
| **6. Ship opportunistically** | JIT on where probe succeeds; interpreter elsewhere; telemetry on JIT‑on rate across hosts | Real‑world A/B shows perf win + zero correctness regressions |
| **v2 (committed, staged)** | Registers as a write‑back frame cache per §5.7: **v2.0** block‑local floating cache with conservative pointer flushes (no aliasing‑spec dependence; measured ~4–5.5×), **v2.1** bound registers for hot private scalars (escape floor, §1.1; measured ~6–13.6×), **v2.2** optional provenance‑scoped flushing (~10–20 %); store coalescing, block‑boundary sync; requires the v1 contract §5.7.7 | Three‑way lockstep (interp/v1/v2) byte‑identical on corpus + fuzzer; each stage lands separately behind the C8 engine switch |
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
4. **`GETL`/`SETL`/`ADRL` aliasing** (§1.1): *investigated; model corrected.* Measured across the 57‑program golden
   corpus: `ADRL` 2.0 %, `GETL` 0.4 %, `SETL` 0.4 % of instructions; ~82 % of `ADRL` targets are local arrays and
   ~18 % are address‑taken scalars; **all 348 `ADRL` sites use a `*0` size hint**, so an `ADRL`‑derived pointer is
   bounded only by the data stack and can `COPY`/`POKE` across **adjacent named locals** — a real, load‑bearing idiom
   (`perfTest`/`buffer`/`nobuffer` bulk‑load a bank of contiguous scalars from a global struct). The earlier proposal to
   declare cross‑local access *unspecified* and assume named locals don't alias is **retracted as unsound** — it would
   miscompile those programs. Adopted model (normative in §1.1): the **provenance‑bounded rule** — defined access is
   `[derivation point, owning frame's dsp)`, cross‑local access *within* that span is defined (frame layout is ABI,
   bit‑for‑bit), beyond it memory‑safe‑but‑unspecified. That is simultaneously weak enough to keep the bank idiom
   defined and strong enough to make register caching possible at all. v1 keeps all locals memory‑resident (no analysis
   needed). v2 (§5.7, committed) register‑caches private slots via the escape floor; the 15 `ADRL`‑free programs (incl.
   the hot kernels) are fully cacheable, and even bank users like `perfTest.main` keep their pre‑floor scalars in
   registers.
5. **Call/trap mechanism** (§5.4): *decided — dispatcher‑threaded segments.* Nested host frames were rejected because
   they break async suspend/resume (a native call chain cannot be re‑materialized at resume). Remaining sub‑question is
   only the transfer encoding — return‑to‑dispatcher vs direct‑threaded `jmp` — which is a measured swap, invisible to
   the rest of the design.
6. **Fuel granularity** (§5.5): a *latency*, not correctness, question — block‑granular timeout is observationally
   equivalent to the interpreter (§5.2 excludes `clockCyclesLeft`). Confirm the worst‑case time‑to‑suspend is acceptable
   to the host and set `maxBlockWeight` below the host's per‑`process()` fuel grant (latency **and** liveness, §5.5).
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
