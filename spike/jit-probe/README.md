# GAZL JIT Spike A1 — executable-memory host probe

**Status: COMPLETE (2026-07-11) — verdict GO.** Ran in Logic (AU), Ableton (VST3), Pro Tools (AAX) and
loads as VST2 in Live; all pass (rungs 1+2 work, rung 3 denied). Full results:
[docs/JitSpikeA1-Results.md](../../docs/JitSpikeA1-Results.md). This tree is throwaway — `rm -rf spike/`
when you're done with it.

A minimal audio plug-in that, when a host instantiates it and sets up audio, runs the GAZL JIT
executable-memory **probe ladder** once, records the result to a per-process log, and otherwise behaves
as a silent stereo pass-through. It answers one empirical question per host: *which strategy for
allocating and executing machine code actually works inside this DAW's process?*

See [docs/JitSpikeA1-Results.md](../../docs/JitSpikeA1-Results.md) for the measured results, and
[docs/JitCompilerResearch.md](../../docs/JitCompilerResearch.md) for the wider JIT research effort.

> **This whole directory is a throwaway spike.** It vendors a minimal copy of Sonic Charge's Symbiosis
> adapter framework purely to get a loadable plug-in in every format without pulling in JUCE. Delete
> `spike/` when the spike is done. Nothing else in GAZL depends on it.

## What it does

On the Symbiosis `configureAudio` hook (the audio-setup path — not the realtime thread) the plug-in calls
`gazlJitProbeRunOnce()` exactly once per process. The probe:

1. Writes a per-process log header with **runtime-read** identity + code-signing entitlements
   (`SecTaskCopyValueForEntitlement` — ground truth for *which* process we are really in, e.g. a host's
   out-of-process plug-in scanner vs. its main app).
2. Runs the fallback **ladder** on the calling thread and again on a spawned worker thread (Apple-Silicon
   W^X is per-thread, so results can differ):
   - **Rung 1 — `MAP_JIT`**: `mmap(...PROT_READ|WRITE|EXEC, MAP_JIT)` + (arm64) `pthread_jit_write_protect_np` toggle. Needs `allow-jit`.
   - **Rung 2 — `mprotect` flip**: `mmap(RW)` → write → `mprotect(RX)`. Needs `allow-unsigned-executable-memory` on hardened macOS.
   - **Rung 3 — `RWX`**: `mmap(...RWX)` directly, no `MAP_JIT`. Expected to fail under Hardened Runtime; recorded anyway.
   Each rung writes a tiny function returning `0x5A`, invalidates icache, calls it, checks the result.
3. Before every *execute* step it flushes a `"about to EXECUTE rung N on thread T"` breadcrumb to disk, so
   if the host hard-crashes on the call, the last log line names the rung and thread that killed it.

Design rules held (see handoff §2.3): **no** signal handlers, **no** `mprotect` of memory it didn't
allocate, **no** global FPU/MXCSR changes. A *denied* rung is a caught syscall error, logged, stepped
past — never a crash.

## Where the log goes

```
~/Library/Logs/GazlJitProbe/<procname>-<pid>-<timestamp>.log
```
Appended, never truncated; also mirrored to `os_log` (view in Console.app, subsystem = default,
search "GazlJitProbe").

## Build

Requires the 3rd-party SDKs (default `/Users/magnus/projects/3rdparty`; override with `THIRDPARTY=` or the
per-SDK `VST2_SDK_ROOT` / `VST3_SDK_ROOT` / `AAX_SDK_ROOT` env vars). All builds are universal
(arm64 + x86_64) and ad-hoc signed.

```sh
./build_au.sh   --install   # AU  → ~/Library/Audio/Plug-Ins/Components   (Logic, GarageBand, MainStage)
./build_vst3.sh --install   # VST3→ ~/Library/Audio/Plug-Ins/VST3         (Ableton, Studio One, Cubase, REAPER, Bitwig, Waveform, ...)
./build_vst2.sh --install   # VST2→ ~/Library/Audio/Plug-Ins/VST          (older host versions)
./build_aax.sh  --install   # AAX → /Library/Application Support/Avid/Audio/Plug-Ins  (Pro Tools Developer; may need sudo)
```

Omit `--install` to build into `.build/` only. AU needs no external SDK (system frameworks only).

## Quick self-test (no DAW)

```sh
# Standalone: runs the probe in a plain (non-hardened) process.
c++ -std=c++17 -arch arm64 probe/GazlJitProbe.cpp probe/standalone_probe_main.cpp \
    -framework CoreFoundation -framework Security -o .build/standalone_probe
./.build/standalone_probe && cat ~/Library/Logs/GazlJitProbe/*.log

# AU inside Apple's auvaltool host process (exercises configureAudio → fires the probe):
./build_au.sh --install
killall -9 AudioComponentRegistrar 2>/dev/null
auval -v aufx GJbP GAZL
cat ~/Library/Logs/GazlJitProbe/auvaltool-*.log
```

## Running in a real host

1. Build+install the format(s) the host loads.
2. In the DAW, add **GAZL: JIT Probe** to a track and start audio (the probe fires on audio setup).
3. Read the newest file in `~/Library/Logs/GazlJitProbe/`. The filename's `<procname>` tells you whether
   it ran in the main app or an out-of-process scanner/helper.
4. Record the per-host outcome in `docs/JitSpikeA1-Results.md`.

## Plug-in identity

- AU: type `aufx`, subtype `GJbP`, manufacturer `GAZL`
- Symbiosis `plugInId` `'GJbP'` (0x474A6250), `vendorId` `'GAZL'` (0x47415A4C)
