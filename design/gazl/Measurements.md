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
number from another day.

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
