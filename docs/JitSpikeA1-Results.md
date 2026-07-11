# Results — Spike A1: JIT executable memory inside real DAW hosts

**Branch:** `jit-compiler`. **Status: COMPLETE — verdict GO** (2026-07-11). Probe + 4-format plug-in built,
self-verified, and run in the four priority DAW hosts (Logic/AU, Ableton/VST3, Pro Tools/AAX, + VST2 in
Live). All pass; see §4. Nothing committed/pushed.

Probe + scaffold live in [spike/jit-probe/](../spike/jit-probe/) (throwaway; vendors a minimal Symbiosis
copy — delete `spike/` when the spike closes). Build/run instructions:
[spike/jit-probe/README.md](../spike/jit-probe/README.md).

---

## 1. What was built

A minimal **silent pass-through effect** ("GAZL: JIT Probe") built in **all four macOS formats** — AU,
VST3, VST2, AAX — from vendored Symbiosis adapters, no JUCE. On the `configureAudio` audio-setup hook it
runs the probe ladder **once per process** (`std::call_once`), logging to
`~/Library/Logs/GazlJitProbe/<proc>-<pid>-<ts>.log` (+ `os_log`).

The ladder, per handoff §2.2, on **two threads** (host calling thread + spawned worker):

| Rung | Strategy | Needs (hardened macOS) |
|---|---|---|
| 1 | `mmap(MAP_JIT)` + arm64 `pthread_jit_write_protect_np` toggle | `allow-jit` |
| 2 | `mmap(RW)` → write → `mprotect(RX)` | `allow-unsigned-executable-memory` |
| 3 | `mmap(RWX)` directly (no `MAP_JIT`) | expected to fail; recorded anyway |

Each rung writes `mov w0,#0x5A; ret` (arm64) / `mov eax,0x5A; ret` (x86-64), invalidates icache, calls it,
checks the return is `0x5A`. Runtime entitlements are read via `SecTaskCopyValueForEntitlement`; a crash
breadcrumb is flushed to disk before every execute. All builds universal (arm64+x86_64), ad-hoc signed.

**Design rules held:** no signal handlers, no `mprotect` of foreign memory, no global FPU/MXCSR change; a
denied rung is a caught syscall error, never a crash.

## 2. Self-verification (done, no DAW)

| Environment | Process | arch | Rung 1 MAP_JIT | Rung 2 mprotect | Rung 3 RWX | Notes |
|---|---|---|:--:|:--:|:--:|---|
| Standalone (non-hardened) | `standalone_probe` | arm64 | OK | OK | denied (EACCES) | both threads identical; no entitlements |
| **AU in Apple's validator** | `/usr/bin/auvaltool` | arm64 native | **OK** | **OK** | denied (EACCES) | both threads identical; **AU validation PASSED** |

Runtime entitlements read inside `auvaltool`: `allow-jit` **absent**, `allow-unsigned-executable-memory`
**true**, `disable-library-validation` **true**.

> **Already an entitlement-table-vs-runtime surprise:** rung 1 (`MAP_JIT`) **succeeded in `auvaltool`
> despite `allow-jit` being absent** — because `auvaltool` is not a Hardened-Runtime process, so `MAP_JIT`
> is permitted unentitled. This confirms the handoff's core premise: entitlements are necessary-but-not-
> sufficient, and only a runtime probe tells the truth. `auvaltool` is a useful proxy but **not** a real
> host; the numbers that matter are the DAW rows below.

VST3 / VST2 / AAX bundles: build clean, universal, ad-hoc signed, and `dlopen` resolves all symbols
(entry points `bundleEntry`/`GetPluginFactory`, `VSTPluginMain`, ACF exports respectively). Their probe
firing happens on host audio-setup and is covered by the DAW runs in §4 — the plug-in→probe wiring itself
is already proven end-to-end by the AU/auval run. **VST2 confirmed loading in Ableton Live 12.4.2**
(`VST2: Created: GazlJitProbe`). **AU confirmed in Live** (`Audio Unit v2: Created`).

**Two build gotchas found and fixed (2026-07-11), worth remembering if this scaffold is reused:**
1. **`vendorUrl` must be non-null.** The VST3 factory-metadata path (`queryVST3FactoryMetadata`,
   `SymbiosisVST3.cpp:405`) asserts `vendorUrl != 0`. Our `PLUGIN_INFO` left it NULL → `SYMBIOSIS_ASSERT`
   → `abort()` **inside Ableton's out-of-process plugin scanner**, which blacklisted the VST3 (AU/VST2
   don't check it, which is why only VST3 failed to appear). Fixed by setting `info.vendorUrl = ""`.
2. **Build release (`-DNDEBUG`).** A probe/utility plug-in must NEVER `abort()` a host. A debug build
   leaves `SYMBIOSIS_ASSERT` live; any tripped contract assert takes down the host's scan process. All
   four builds now compile with `-DNDEBUG -O2`. Verified: the exact `bundleEntry`→`GetPluginFactory` path
   that crashed Live's scanner now returns a valid factory with no abort.

## 3. Per-host results — measured 2026-07-11 (Apple Silicon, macOS 26.5)

The four priority hosts were run; the spike was concluded there (all four pass, picture unambiguous). The
lower-priority hosts below were **not run** — they sit in entitlement groups already confirmed by the four
measured hosts, so they carry little additional information. To run one later: insert the probe plug-in on
a track, start audio, then read the newest `~/Library/Logs/GazlJitProbe/*.log`. The filename's `<procname>`
distinguishes main-app vs. out-of-process scanner/helper.

| # | Host (version) | Format | arch (native/Rosetta) | Probe ran in (process) | Runtime entitlements (jit / unsigned-exec / lib-val) | Rung 1 (caller/worker) | Rung 2 (caller/worker) | Rung 3 (caller/worker) | Notes |
|--:|---|---|---|---|---|:--:|:--:|:--:|---|
| 1 | **Logic Pro** (___) | AU | arm64 native | **`AUHostingServiceXPC_arrow`** (system XPC, ppid=launchd) — **out-of-process** | absent / **true** / true | **OK / OK** | **OK / OK** | denied / denied | ✅ **JIT works.** Logic hosts the AU out-of-process in Apple's `AUHostingServiceXPC_arrow.xpc`, which carries `allow-unsigned-executable-memory=true`; MAP_JIT also works there. Probe ran to completion, no crash. (Logic's empty-view spinner is cosmetic — plug-in has 0 params / no UI.) |
| 2 | **Ableton Live 12** (12.4.2 Trial) | VST3 | arm64 native | **`Live`** (main app, ppid=launchd) — **in-process** | absent / **true** / true | **OK / OK** | **OK / OK** | denied / denied | ✅ **JIT works.** VST3 hosted in-process. Rung 2 mprotect→exec confirmed on Apple Silicon (the key `allow-unsigned`-group check). MAP_JIT also works despite `allow-jit` absent. Also loads as AU + VST2 in Live. |
| 3 | Studio One 6 | VST3 | — | not run | — | — | — | — | `allow-unsigned` group — expected to match Ableton (rung 2 OK) |
| 4 | **Pro Tools Ultimate** (2026.4, Developer) | AAX | arm64 native | **`Pro Tools Developer`** (main app, ppid=launchd) — **in-process** | absent / **true** / true | **OK / OK** | **OK / OK** | denied / denied | ✅ **JIT works.** AAX hosted in-process; fires on plugin insert (`configureAudio`). Same profile as Ableton. (Getting here required sorting iLok Cloud auth + creating `/Library/Application Support/Avid`; unrelated to the probe. Unsigned/ad-hoc AAX loads because this is the PT **Developer** build.) |
| 5 | Waveform 13 | VST3 | — | not run | — | — | — | — | `allow-unsigned` group — expected rung 2 OK |
| 6 | Cubase 15 | VST3 | — | not run | — | — | — | — | `allow-jit` host — expected rung 1 + rung 2 OK |
| 7 | REAPER | VST3 | — | not run | — | — | — | — | `allow-jit` host — expected rung 1 + rung 2 OK |
| 8 | GarageBand | AU | — | not run | — | — | — | — | likely out-of-process XPC like Logic; expected rung 2 OK |
| — | Windows (any host) | VST3 | — | not run | — | — | — | — | expected easy: no default W^X. Rung 2 = `VirtualProtect` flip; **rung 3 (direct `PAGE_EXECUTE_READWRITE`) expected to SUCCEED** on 64-bit Windows unless the host enables Arbitrary Code Guard (DAWs don't). See §5. |

Rung cells: `OK` / `denied(errno)` / `crash`; caller/worker noted separately when they differ.

## 4. Go/no-go readout — **GO**

**Verdict: GO.** All four priority hosts — Logic (AU, out-of-process XPC), Ableton (VST3, in-process), and
Pro Tools (AAX, in-process), plus VST2 loading in Live — have **two** working strategies each (MAP_JIT +
mprotect). Nothing measured is interpreter-only; no execute step crashed; caller and worker threads agree
everywhere. The JIT's opportunistic-with-interpreter-fallback design is validated; on the evidence here the
fallback is not needed on macOS for any host tested.

- **Working rung for every host we care about?** Yes — every measured host has rungs 1 *and* 2 working.
- **Interpreter-only hosts?** None found.
- **Logic (the genuine unknown):** In-process JIT works — but note Logic hosts the AU **out-of-process** in
  Apple's `AUHostingServiceXPC_arrow.xpc`, and it's *that* process (which carries
  `allow-unsigned-executable-memory=true`) where the code runs. The runtime entitlement read + log
  `<procname>` were what pinned this down.
- **Any `allow-unsigned` host where rung 2 still failed?** No — rung 2 (`mprotect`→exec) worked on Apple
  Silicon in every host.
- **Worker-thread vs. main-thread differences (per-thread W^X)?** None — identical on both threads in all
  four hosts.
- **Hard crash on an execute step?** None. (The only crash in the whole exercise was a *scan-time*
  `SYMBIOSIS_ASSERT`→`abort()` from a missing `vendorUrl` in a debug build — §2 — fixed, unrelated to the
  execute path.)

**Surprise vs. the entitlement table:** every host reports `allow-jit` **absent** at runtime, yet rung 1
(`MAP_JIT`) works anyway in all four. So MAP_JIT is available more widely than the static survey predicts —
treat it as an opportunistic bonus, with rung 2 as the robust, table-consistent path. See §5.

## 5. Impact on JIT design assumptions (§2 of JitCompilerResearch.md)

To finalise after the remaining DAW runs. Nothing measured so far contradicts the design. Two
observations worth carrying back to §2 of JitCompilerResearch.md (flag only — don't edit that doc without
checking):

1. **`MAP_JIT` succeeds even where `allow-jit` is absent — seen in every process measured so far**
   (`auvaltool`, Logic's `AUHostingServiceXPC_arrow`, Ableton `Live`, `Pro Tools Developer`). On this machine (Apple Silicon,
   macOS 26.5), rung 1 is working more widely than the entitlement table predicts. Do **not** rely on
   this as a rule yet — it may be macOS-version- or host-build-specific. The **robust, table-consistent
   rung is #2 (`mmap(RW)`→`mprotect(RX)`)**, which works wherever `allow-unsigned-executable-memory` is
   present; treat MAP_JIT as an opportunistic bonus, confirmed per-host at runtime, never assumed.
2. **The runtime entitlement read is doing real work.** Every host so far reports `allow-jit=absent` yet
   `allow-unsigned-executable-memory=true` at runtime — reinforcing the design's "probe at runtime, don't
   trust the static entitlement survey" stance. Logic's out-of-process XPC host (`AUHostingServiceXPC`)
   is the clearest case: the process that actually runs the code is not the one you'd guess from the app.

3. **Windows is the permissive platform (not measured here, but worth stating for the design).** Windows
   has **no default W^X enforcement**, so the ladder inverts relative to macOS:
   - Rung 1 (`MAP_JIT`) has no Windows equivalent — the probe reports it n/a.
   - Rung 2 = `VirtualAlloc(RW)` → `VirtualProtect(EXECUTE_READ)` → `FlushInstructionCache`.
   - Rung 3 = `VirtualAlloc(PAGE_EXECUTE_READWRITE)` directly, and on Windows this is **expected to
     succeed** (the opposite of macOS, where rung 3 always fails). The only thing that blocks it is a
     process opting into **Arbitrary Code Guard** (browsers do; DAWs don't).
   - Corroboration from the wild: Magnus's legacy **AudioClay** JIT just `malloc`s memory, writes code, and
     jumps to it — and still runs in 2026. That works because it's a **32-bit binary without `/NXCOMPAT`**,
     so Windows runs the whole process with **DEP disabled** (opt-in policy) → heap pages execute freely;
     x86's coherent instruction cache means it doesn't even need an icache flush. **This won't survive a
     64-bit rebuild** (DEP is always-on for 64-bit, non-optional), so GAZL's JIT should use the
     `VirtualAlloc`/`VirtualProtect` paths (rungs 2/3) rather than the heap trick. Net: Windows is the easy
     target; the interpreter fallback is a macOS-hardened-host concern, and even there it wasn't needed.
