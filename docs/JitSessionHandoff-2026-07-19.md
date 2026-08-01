# Session handoff (2026-07-19): JIT session complete; NEXT UP = assembler-level inlining

> Session handoff from 2026-07-19, moved here 2026-08-01 from an untracked `temp/` directory. A
> snapshot of that day's state - read it as history, and verify anything you intend to rely on.

Branch `jit-compiler` at `74d0199`, pushed, clean tree. SILICON (Windows box, 192.168.72.183, ClaudeRunner,
ssh key auth from this Mac) synced to the same commit, clean tree, milestone exes `output/GAZLCmd_h_*.exe`
(pre/pool/div/fix/realm/maps1/maps2) kept for future matrix runs. Mac has the same set as `output/GAZLCmd_h_*`.

## What shipped today (all validated: lower test arm64/Rosetta/native-Win64, 28 firmwares x both engines,
## slice tests, 300k-deep differential fuzz per backend per change)

- `6f0ad7a` x64 RIP-relative float literal pool + 16B function-entry alignment (alignment fixed a
  deterministic miditest +15% layout regression the pool alone caused)
- `ea068ba` div/mod zero-divisor trap arms -> cold section (arm64 leibniz -34%, spectralnorm -24%)
- `b09affe` **SOUNDNESS FIX**: v2.3a-lite skipped required flushes for scalar/offset pointer deref
  (`PEEK $x $p` finalizes to PEEK_VCV with a non-address const base; gate on `p >= MEMORY_OFFSET`)
- `b4eb687` fuzzer generates the scalar-deref form that had hidden that bug
- `0cfe34a` v2.3a-full pointer-realm stamping (`buildPointerRealms`): NONFRAME base PEEK/POKE_VVV skip the
  frame flush. Native x64 firmware geomean -7.9%; the vortex reverb -6% arm64 / -10% x64 at this step
- `d744c13` buildLiveIn (global backward liveness)
- `1f1b689` v2.2 multi-block residency, SHARED maps - measured a WASH (commit msg has the full story);
  landed for infra (side-entry scan, ColdEdge exit stubs, per-class pressure gate)
- `114fb44` **v2.2 varying maps** (liveness-sized per-leader maps + PRELOAD): native x64 geomean -3.2%,
  leibniz -64% (44.7->16.0ms); arm64 montecarlo -8.6%. Fuzzer caught a cross-class stale-preload miscompile
  on its FIRST seed (fix: capture spills ALL classes before any preload; isolation test N locks it)
- `74d0199` runPermut8Firmware.cmd: FINDSTR patterns accept indented labels (shipped bank builds indent
  `sqrt:` etc.; specular_code failed on Windows only)

Full 7-milestone x 2-box benchmark matrix: `temp/SessionBenchMatrix.md`. End-to-end vs session start:
native x64 geomean -3.6% (leibniz -64%, crossfade -10%, specular -10%, FFTTest -9%, vortex -6%);
arm64 ~-1% geomean but leibniz -29%, spectralnorm -21%, FFTTest -26%, vortex -7%. CAVEAT: pre/pool/div
columns for verber8/pong/miditest are miscompile-fast (pre-`fix`); their step up at `fix` is correctness.

## Decisions made (do not relitigate without new data)
- **v2.3b within-realm precision is DEAD as a JIT tier** (Magnus). Vortex debunked as its motivating case
  (its hot loop is pure v2.3a). If ever revived it is a LANGUAGE initiative: co-enforced slice bounds,
  `&a[lo:hi]`, index-less sliced deref ops - full design record in `docs/SliceBoundsDesign.md` (committed).
- **JIT sound-aliasing terminates at v2.3a.** Regalloc seam considered mined out; remaining ceiling gap is
  call-bound and native-boundary-bound.
- Known open perf oddity: bitztest_code +6% x64 deterministic since varying maps (diagnosis pending, jitDisasm).

## NEXT TASK: assembler-level function inlining (Magnus's pick; helps interpreter AND JIT)
Investigation DONE - read `docs/InliningInvestigation.md` first (committed). Highlights:
- Per-call overhead 2.69ns interp / 1.51ns JIT (arm64; probe kernels `temp/callbench_{call,inline}.gazl`).
- The SUITE has ZERO GAZL calls (blind to inlining); firmwares are the target (vortex 100 sites / 6 calls
  per frame dynamic, reciter 46, specular 24). Prize ~5-15% of --jit firmware time, ~2x that interpreted,
  PLUS second-order: call-containing loops currently barrier + are residency-disqualified.
- Design: finalize-time CALL_CVC pass in the assembler (GAZL.cpp). Window slots already overlap caller
  transients -> remap directly; callee locals append to caller frame (fix FUNC C0/C1); RETU -> GOTO (also
  fixes the known lowerFunction multi-RETU gap); non-recursive, size-budgeted, direct sites only
  (address-taken functions keep the out-of-line copy); pushCall/native forwards NOT inlinable.
- Police: fuzzer G4 inline-vs-not differential vs interpreter oracle + 28-firmware checksums
  (`tools/checkPermut8Firmwares.sh --jit`, works on both boxes; GAZLCMD env overrides the binary).

## Process lessons that cost time today (respect them)
- NEVER pipe a fuzz soak through `tail`/`findstr` - it eats the abort and the divergence dump; redirect to a
  file, check the file AND the exit code. A 7-byte output ending "RETU" was a swallowed counterexample.
- cmd.exe: CALLing vcvarsall-based batch files inside a parenthesized FOR body silently no-ops - UNROLL
  generated batch files. Verify milestone builds differ (certutil MD5) before trusting a matrix.
- After changing a shared signature (e.g. RegisterCache::capture), re-scp TEST files to SILICON too and run
  its lower test un-piped; "Compiled GAZLCmd" alone does not mean the test built.
- Benchmark discipline: min of N fresh processes on an idle box; arm64 deltas need code_bytes equality checks
  (P/E bimodality); Rosetta firmware deltas are pure noise (interp control swung -38% on identical binaries).
- Standing rules: commit/push only when Magnus says; short subjects; no Co-Authored-By; no em dashes;
  memory files in ~/.claude/projects/-Users-magnus-git-GAZL/memory/ have the full history (jit-roadmap-status).
