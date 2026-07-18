# Permut8 firmware execution tests (pure-GAZL host)

This lane RUNS the real Permut8 firmwares in `tests/impala/golden/` - unmodified - and checks their output
against committed checksums. The host that a firmware needs (constants, delay-line memory, per-sample driver,
and the `yield`/`read`/`write`/`trace` callbacks) is supplied as **concatenated GAZL**, not C++:

```
bash tools/runPermut8Firmware.sh tests/impala/golden/ringmod_code.gazl     # run one (prints its checksum)
bash tools/checkPermut8Firmwares.sh                                        # check all against expected/
tools\runPermut8Firmware.cmd tests\impala\golden\ringmod_code.gazl         # Windows
```

## How it works

`tools/permut8Host.js` (node) wraps a firmware into one self-checking program:

1. **Host constants** (`PARAM_COUNT`, operator enums, switch masks, ...) as `DEFi` lines - the firmware's
   symbolic references resolve at assembly, exactly as in the real host.
2. **Host core** (compiled Impala): a wrapped stereo delay line (8192 frames), a deterministic pseudo-audio
   LCG, and GAZL implementations `read_`, `write_`, `trace_`.
3. **`yield_`** - the per-sample boundary: folds the firmware's output `signal` into a running checksum,
   feeds the next input sample, advances `clock`, and prints the checksum + `exit()`s after 100000 frames.
   (Mod patches are driven per sample by the generated driver loop instead.)
4. **The firmware, verbatim.**
5. **`hostMain`** - a generated driver: fills `params` (and `config`, e.g. sam's speech phrase), calls
   `init()`/`update()`/`reset()` when the firmware defines them, then drives `process()` (full patches) or
   `operate1()`/`operate2()` (mod patches).

The firmware's `^yield`/`^read`/`^write`/`^trace` **native** calls are bound to the GAZL implementations with
GAZLCmd's `--forward=native:function,...` option, which uses `Processor::pushCall()` - a native pushing a call
onto the current GAZL continuation so the `^native` call behaves exactly like a `&function` call (see
`src/GAZL.h`; tested in `tools/GAZLEnterCallTest.cpp`).

## Status and limitations

- **Both engines**: `bash tools/checkPermut8Firmwares.sh` verifies the interpreter against the goldens, and
  `bash tools/checkPermut8Firmwares.sh --jit` runs the whole set as an **interp-vs-JIT differential** over
  real product code (the goldens are interpreter-produced). Firmware JIT speedups are real - e.g. ringmod
  4.5x, sam 5.8x (`runPermut8Firmware.sh <fw> [--jit] --bench=5`).
- The checksums are harness-defined regression values (fixed params, pseudo-noise input), not musical output.
- Not covered: `buffer`/`nobuffer`/`floatVerber8` (experimental firmwares with non-standard APIs - no
  `signal` global / different `read` signature) and `startupcrash_code` (traps by design; its trap IS the
  expected behavior).
- Firmwares with a self-contained libm or with globals shadowing a built-in native name are handled by the
  runner's auto-flags (`--no-libm`, `--no-native=name`).

## Regenerating a golden

After an intentional change, run the firmware and store the printed checksum:

```
bash tools/runPermut8Firmware.sh tests/impala/golden/<name>.gazl > benchmarks/firmware/expected/<name>.checksum
```
