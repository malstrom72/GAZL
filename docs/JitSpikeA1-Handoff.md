# Handoff — Spike A1: can we allocate & execute JIT memory inside real DAW hosts?

**Branch:** `jit-compiler`. **Do not commit or push** unless Magnus explicitly says so (standing rule). Leave work in
the tree and report back.

You are picking up **one** de-risking spike from a larger research effort on adding a native (JIT) compiler to GAZL.
You do **not** need to read the whole research doc to do this task, but the two files worth skimming are
[docs/JitCompilerResearch.md](JitCompilerResearch.md) §2 (executable-memory constraints) and this file. Everything you
need is restated below.

---

## 1. The question you are answering

The proposed GAZL JIT will, at plugin load time, write machine code into memory and execute it. Inside an audio plugin,
that memory must be made executable **within the host DAW's process**, whose sandbox/entitlements the plugin does not
control. macOS (Hardened Runtime) is the hard case; Windows and Linux are expected easy but should be sanity-checked.

**Go/no-go decision this spike feeds:** *for each target host, which executable-memory strategy actually works at
runtime, and is there at least one that works everywhere we care about?* If the answer is "JIT works in hosts X, Y, Z
and falls back to the interpreter in W," that is a fine and expected outcome — the JIT is designed to be opportunistic
with an interpreter fallback. The point is to replace assumptions with measured per-host facts.

This is **empirical**: entitlements being *present* is necessary but not sufficient. The only real test is to allocate
W^X/executable memory inside the actual host process and run a byte of code from it.

---

## 2. What we already know (don't re-derive)

### 2.1 macOS entitlement survey (already measured on Magnus's machine, 2026-07)

Run with `codesign -d --entitlements - --xml "/Applications/<App>.app"`. Findings:

| Host (versions on this machine) | Hardened RT | `allow-jit` | `allow-unsigned-executable-memory` | `disable-library-validation` |
|---|:--:|:--:|:--:|:--:|
| Cubase 11/14/15, Nuendo 11/12 | yes | **yes** | yes | yes |
| REAPER | yes | **yes** | yes | yes |
| Bitwig 3.3 / 5.3 | yes | **yes** | yes | yes |
| FL Studio 21 / 2025 | yes | **yes** | yes | yes |
| Reason 11 / 12 | yes | **yes** | yes | yes |
| Studio One 4/5/6 | yes | no | **yes** | yes |
| Ableton Live 10/11/12 | yes | no | **yes** | yes |
| Pro Tools (+ Developer) | yes | no | **yes** | yes |
| Waveform 12/13 | yes | no | **yes** | yes |
| GarageBand | yes | no | **yes** | yes |
| **Logic Pro** | **no (flags=0x0)** | no | no | no |
| Studio One 2, Bitwig 1.x/2.x | no | — | — | — |

Interpretation already established:
- **`allow-jit`** gates `mmap(MAP_JIT)` + the Apple-Silicon per-thread W^X toggle `pthread_jit_write_protect_np()`.
- **`allow-unsigned-executable-memory`** permits the classic `mmap(RW)` → write → `mprotect(RX)` path *without*
  `MAP_JIT`. Present on **every** hardened third-party host measured.
- **Logic Pro** is the real unknown: not hardened-runtime, none of these entitlements, and it hosts many AU components
  out-of-process — so an in-process plugin JIT there must be tested, not assumed.
- Entitlements belong to the **host** process; a dylib inherits them. The plugin cannot add its own.

### 2.2 The probe ladder the JIT will use (this is what you are validating)

The runtime should try, in order, and fall back on failure — **never crash, never abort the host**:

1. **`MAP_JIT`**: `mmap(NULL, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON|MAP_JIT, -1, 0)`, then per
   Apple Silicon: `pthread_jit_write_protect_np(false)` → write code → `pthread_jit_write_protect_np(true)` →
   `sys_icache_invalidate(ptr, size)` → call it. (Needs `allow-jit`.)
2. **`mprotect` flip**: `mmap(... PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON ...)` → write code →
   `mprotect(ptr, size, PROT_READ|PROT_EXEC)` → `sys_icache_invalidate` → call it. (Needs
   `allow-unsigned-executable-memory` on hardened macOS; works unentitled on non-hardened.)
3. **RWX without `MAP_JIT`**: `mmap(... PROT_READ|PROT_WRITE|PROT_EXEC, ...)` directly (no `MAP_JIT`). Expected to fail
   under Hardened Runtime; record the outcome anyway — it's a cheap extra data point.
4. **interpreter fallback** (out of scope for this spike — just report that the JIT paths failed).

Expected failure mode for a denied rung is a **syscall error** (`mmap`/`mprotect` returning `MAP_FAILED`/`-1` with
`EPERM`/`EACCES`/`ENOTSUP`), which is detectable and safe — treat it as "rung unavailable" and move to the next. A fault
on the *execute* step is the dangerous case; see the crash-forensics logging in §3.

### 2.3 Constraints that are non-negotiable (design rules, respect them in the probe)

- **No process-global state.** The probe must **not** install signal handlers, must not `mprotect` memory it did not
  allocate, must not change global FPU/MXCSR state. (This is why the real design uses explicit bounds checks, not guard
  pages — irrelevant to the probe, but don't be tempted to test SIGSEGV tricks.)
- On Apple Silicon the W^X toggle `pthread_jit_write_protect_np` is **per-thread**; within a single rung, the thread
  that writes the code and the thread that flips protection must be the same. **But run the whole ladder twice — once on
  the host's calling thread and once on a spawned `pthread`** — because the real JIT compiles on a worker thread, and
  per-thread W^X means a worker-thread result can differ from the main-thread one. Report both.
- iOS is out of scope (no JIT there at all; interpreter-only by design).

---

## 3. What to build

A **minimal audio plugin** that, when instantiated by a host, runs the probe ladder **once** (e.g. on the audio-
processing setup or first process call — see caveat below) and records the result somewhere retrievable, then behaves
as a trivial pass-through/silent plugin so it loads cleanly in every host.

- **Plugin format:** the most universally loadable format across the target hosts. On macOS that is **AU (v2
  component)** and **VST3**; most of the listed hosts load VST3, but Logic/GarageBand need AU. Pick VST3 for the widest
  coverage and add AU if Logic testing needs it. A thin wrapper via **JUCE** or the **CLAP**/VST3 SDK is fine — smallest
  thing that loads. If a scaffold already exists in Magnus's other projects (Permut8 firmwares are built on similar
  tooling), reuse it; ask.
- **The probe** (a small dependency-free C/C++ function, arch-aware) attempts the rungs in §2.2 order. For each, it
  writes a tiny function that returns a known constant (e.g. AArch64 `mov w0, #0x5A; ret` = bytes
  `40 0B 80 52  C0 03 5F D6`; x86-64 `mov eax, 0x5A; ret` = `B8 5A 00 00 00 C3`), invalidates icache, calls it, and
  checks the return value equals `0x5A`. Also record `pthread_jit_write_protect_supported_np()`.
- **Log the process's *own* entitlements read at runtime — this is the ground truth for "which process am I really
  in."** Don't infer from the process name: use `SecTaskCreateFromSelf(NULL)` + `SecTaskCopyValueForEntitlement(...)`
  for `com.apple.security.cs.allow-jit`, `...allow-unsigned-executable-memory`, and
  `...disable-executable-page-protection` (link `Security.framework`). If the plugin is running in a host's out-of-
  process scanner/helper, these values (and the probe results) will differ from the main app — which is exactly the
  distinction we need for Logic.
- **Rich process identity** in every record: executable path (`_NSGetExecutablePath` or `proc_pidpath(getpid())`),
  process name, pid, parent pid + parent path if obtainable, architecture (arm64 vs x86_64 — check
  `sysctlbyname("sysctl.proc_translated")` for Rosetta), and macOS version.
- **Result recording — must survive a host that may hard-crash on failure.** Do **not** rely on returning a value.
  Append one self-contained report per run to a per-process log file, e.g.
  `~/Library/Logs/GazlJitProbe/<procname>-<pid>-<timestamp>.log`, and mirror to `os_log`/`NSLog`. Append, never
  truncate, and write the process-identity + entitlement header *first* so even a partial file identifies its host.
- Wrap each rung so a syscall failure (`MAP_FAILED`, `mprotect` = `-1`) is caught and logged, then move on. A hard crash
  on the *execute* step is itself a data point: **log "about to execute rung N on thread T" and flush the file *before*
  the call**, so if the process dies, the last line names the rung and thread that killed it.

### Caveats to handle
- **Codesigning:** the test plugin itself must be signed/loadable by the hosts (ad-hoc signing may suffice for local
  testing; some hosts validate). If a host refuses to load an unsigned/ad-hoc plugin, note it and sign appropriately.
  `disable-library-validation` (present on all hardened hosts above) *should* let third-party dylibs load.
- **When to run the probe:** some hosts scan/validate plugins in a **separate sandboxed process** (e.g. Logic's
  `AUHostingService`, Cubase's plugin-sanity-check) with *different* entitlements than the main app. Run the probe on
  actual instantiation in a project, not just at scan time, and ideally log the process name so scan-process vs main-app
  results are distinguishable. This distinction may matter for Logic especially.
- **Apple Silicon vs Rosetta:** if any host runs under Rosetta 2 (x86-64 translated), the arch and JIT behavior differ.
  Log `sysctl.proc_translated` / the running arch.

---

## 4. Hosts to test (in priority order)

Priority is driven by the entitlement table — test the *uncertain* cases first, since the `allow-jit` hosts are
low-risk:

1. **Logic Pro** — the genuine unknown (no entitlements, not hardened, out-of-process AU hosting). Highest information.
2. **Ableton Live 12**, **Studio One 6**, **Pro Tools**, **Waveform 13** — the `allow-unsigned`-only group: confirms
   the `mprotect` rung actually works on Apple Silicon in these (the necessary-but-not-sufficient check). Ableton is the
   most important commercially.
3. **One `allow-jit` host** (Cubase 15 or REAPER) — confirm the preferred `MAP_JIT` rung works end-to-end.
4. GarageBand (AU, `allow-unsigned`), any others available.

All of the above are installed on Magnus's machine (see the table). Testing needs macOS on **Apple Silicon** primarily;
if an Intel Mac is available, spot-check one host there too (the `mprotect` path differs — no per-thread toggle).

Windows and Linux: lower priority for this spike (expected to work via `VirtualAlloc`+`VirtualProtect` /
`mmap`+`mprotect`), but if easy, run the same probe in one Windows host (e.g. REAPER or Cubase) to confirm.

---

## 5. Deliverable

A short results file (e.g. `docs/JitSpikeA1-Results.md`) containing:

1. **A per-host table**: host + version, arch (native/Rosetta), the process the probe actually ran in and its
   runtime-read entitlements, which rung succeeded (`MAP_JIT` / `mprotect` / `RWX` / none) **on each of the two threads**,
   and any notes (crash — and which rung/thread killed it, refused to load, scan-process vs main-app difference).
2. **The go/no-go readout**: is there a working rung for every host we care about? Which hosts are interpreter-only?
   Any surprises vs the entitlement table (e.g. an `allow-unsigned` host where `mprotect`→exec still fails, or a
   worker-thread result differing from the main thread)?
3. **The probe source + plugin scaffold**, committed to the branch **only if Magnus asks** — otherwise left in the tree
   with a note on how to build/run it.
4. Anything that changes the JIT design assumptions in §2 of [JitCompilerResearch.md](JitCompilerResearch.md) (flag it;
   don't edit that doc yourself without checking).

Keep it factual and per-host. A negative result (some host blocks JIT) is a **useful** result, not a failure of the
spike — it just confirms the interpreter fallback earns its place there.

---

## 6. What NOT to do

- Don't build any part of the actual JIT/codegen — this is purely an executable-memory probe.
- Don't install signal handlers or touch process-global state (§2.3).
- Don't commit/push without explicit say-so.
- Don't test on iOS (JIT is out of scope there by design).
- Don't edit [JitCompilerResearch.md](JitCompilerResearch.md) or the `benchmarks/jit/` spike A3 files; those are a
  different, completed spike.
