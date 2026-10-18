# Measurements

Dated measurements of GAZL: timings, throughput, sizes. Add every number that a decision or a doc relies on,
newest first, and never edit an old entry; measure again and add a new one. A number without its commit, machine
and command cannot be reproduced, so every entry has all three.

    ## YYYY-MM-DD: what was measured
    - Commit: branch and hash
    - Machine: name, CPU, OS, compiler
    - Command: exactly what ran
    - Result: the numbers, and what they were compared with

Noise matters: on MSVC, unrelated edits move `Processor::run` kernels by -10% to +24%
([DispatchLayoutSearch](DispatchLayoutSearch.md)). Compare against a control run on the same machine, never a
number from another day. On MSVC, build both sides of an A/B without `/GL` (`tools/bench.cmd` does): with it,
even a change to the harness alone reshapes `run()` (2026-10-15 below).

## 2026-10-15: interpreter baseline, `main` without `/GL`

- Commit: `main` fb2f7f47
- Machine:
	- SILICON: AMD Ryzen 9 7950X, Windows 10, MSVC x64 `cl /O2 /MT /GS- /D NDEBUG`, no `/GL`
	- MacMini: Apple M4, macOS, Apple clang 21, `tools/buildGAZLCmd.sh release` (`-Os`, no LTO)
- Command: `GAZLCmd <kernel> main --bench=15 --warmup=3` (plus `--pin=2` on SILICON), median of 3 rounds of the
  cpu median. Kernels: `tests/bench/golden/op_*.gazl`, `tests/impala/golden/perfTest1/2.gazl`, and the DSP
  kernels biquad, osc, delay, fir16 and noise (Impala-compiled, kept outside the repo)
- Result (SILICON in Mcyc, MacMini in ms):

	| kernel | SILICON | MacMini | | kernel | SILICON | MacMini |
	|---|---|---|---|---|---|---|
	| op_add | 466 | 145.0 | | op_ftoi | 762 | 131.5 |
	| op_sub | 572 | 138.8 | | op_xor | 572 | 136.8 |
	| op_mul | 572 | 131.1 | | perfTest1 | 871 | 160.6 |
	| op_div | 986 | 238.8 | | perfTest2 | 1207 | 219.8 |
	| op_mod | 1088 | 328.9 | | biquad | 423 | 65.6 |
	| op_shl | 465 | 145.1 | | osc | 243 | 40.5 |
	| op_shr | 465 | 145.4 | | delay | 306 | 43.7 |
	| op_shru | 576 | 145.4 | | fir16 | 2813 | 463.8 |
	| op_abs | 570 | 115.6 | | noise | 281 | 46.1 |

## 2026-10-15: MSVC `/GL` lets the harness move every kernel

- Commit: `main` a63521fd vs f1baf175 (#122 changed `tools/GAZLCmd.cpp` only, not `src/`)
- Machine: SILICON, AMD Ryzen 9 7950X, Windows 10, MSVC x64 `/O2 /MT /GS-`
- Command: `GAZLCmd <kernel> main --bench=9 --warmup=2 --pin=2`, cpu median in Mcyc
- Result:
	- With `/GL` (`BuildCpp.cmd release`): op_xor 456 -> 582, osc 148 -> 265, biquad 301 -> 482
	- Without `/GL`: op_xor 666 -> 573, osc 262 -> 240, biquad 495 -> 424
	- So the fast `/GL` timings were a layout that #122 lost by changing how the harness calls `run()`. Even
	  without `/GL`, single-op kernels sit in two bands (about 465 or 572 Mcyc) by layout alone, so compare a
	  control kernel and the geomean, never one kernel

## 2026-10-11: fuzz corpus replay time

- Commit: `main` 6d49d975
- Machine:
	- SILICON: AMD Ryzen 9 7950X, Windows 10, MSVC x64 `beta`
	- GitHub Actions runners: `windows-latest`, `macos-latest`, `ubuntu-latest`
- Command: `tools/test-fuzz.sh` / `.cmd` (1377 inputs, `--budget=30`), standalone and inside `build.sh` / `build.cmd`
- Result:
	- SILICON: 5.9 s inside `build.cmd`, 1.4 s run on its own straight after
	- CI: Windows 2.9 s, macOS 2.2 s, Linux 1.1 s
	- All well inside the 30 s budget in [fuzzing](../fuzzing.md)
