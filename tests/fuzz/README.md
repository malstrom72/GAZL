# JIT differential fuzzer

`tools/GAZLCmd.cpp` (under `-DJITDIFF`) assembles a generated GAZL program, runs the interpreter and the JIT from an
identical memory image at full AND tiny fuel, and requires byte-identical final memory + status. Any divergence is a
miscompile: it dumps the program and aborts. See [docs/JitFuzzPlan.md](../../docs/JitFuzzPlan.md) for the design and the
G1-G4 generator stages.

The generated program is a pure function of a **choice stream** (`generateProgram`): every input decodes to a valid
program, so the assembler never has to reject anything. That stream is either an integer seed (reproducible) or the raw
libFuzzer input bytes (coverage-guided) - the two entry points below.

## Standalone driver (reproducible, no libFuzzer runtime)

Works with any compiler, including Apple clang:

```
cd tools && bash buildGazlFuzz.sh standalone
../output/GAZLFuzz --gen COUNT [SEED0] [deep]   # diff COUNT programs from seeds SEED0..; `deep` allows ipStack overflow
../output/GAZLFuzz --gen1 SEED                  # dump one generated program (repro / inspection)
```

A non-zero exit means a divergence was found; re-run `--gen1 <seed>` to see the offending program.

## Coverage-guided (libFuzzer)

Needs a clang that ships the fuzzer runtime; Apple clang does not, so `buildGazlFuzz.sh` prefers Homebrew LLVM
(`brew install llvm`) automatically:

```
cd tools && bash buildGazlFuzz.sh
../output/GAZLFuzz -max_len=2048 tests/fuzz/corpus/            # resume from the seed corpus
../output/GAZLFuzz -max_len=2048 -jobs=8 -workers=8 <livedir>  # parallel soak into a scratch dir
```

libFuzzer's input bytes ARE the generator's choice stream, so coverage feedback evolves the bytes toward uncovered JIT
paths. Point soak runs at a scratch directory (e.g. `tests/fuzz/live/`, git-ignored), not `corpus/`.

## Both backends

Each binary links ONE JIT backend - the host's - and diffs it against the (arch-neutral) interpreter, so a single run
tests one backend. To cover both on Apple Silicon, also build the x86_64 backend and run it under Rosetta:

```
cd tools && bash buildGazlFuzz.sh x64            # -> ../output/GAZLFuzzX64 (add `standalone` for the --gen driver)
arch -x86_64 ../output/GAZLFuzzX64 -max_len=2048 tests/fuzz/corpus/
```

The generator grammar is identical across backends, so the SAME corpus seeds both - a corpus grown on arm64 is a valid
warm start for x64. Native x64 (no Rosetta) runs on the Windows/Linux x64 box, where clang has its own fuzzer runtime.

## The seed corpus

`corpus/` is a small, minimized seed set - just enough accumulated coverage to warm-start a run. Inputs are tiny binary
choice-streams, not readable GAZL. After a soak, fold new coverage back in and re-minimize:

```
../output/GAZLFuzz -merge=1 tests/fuzz/corpus/ tests/fuzz/live/
```

Only `corpus/` is tracked; live run dirs and crash artifacts are git-ignored. Because the corpus's meaning is tied to
the generator grammar, it lives in this repo alongside the code that defines it - if the grammar changes, re-minimize.
