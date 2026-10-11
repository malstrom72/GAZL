# Dispatch layout search (2026-10-10)

`Processor::run` is very sensitive to code layout: on MSVC x64 an unrelated three-line edit moved kernels that
never ran the edited cases by -10% and +24%, and on Apple M4 a change to `GAZLCmd.cpp` alone moved one kernel by
6%. The order of the `case`s in the dispatch switch decides much of that layout, and it was hand-tuned for MSVC
long ago. This search tested whether a better order exists.

## Method

- Each candidate is a seed. `permuteDispatch.js` shuffles the switch's cases with a seeded PRNG, keeping
  together cases that share code (the COPY group with `copy:`, the CALLs with `call:`). Seed 0 is today's order.
- `layoutSearch` builds each seed in release, checks that all 28 Permut8 firmwares give seed 0's checksums,
  then times the `op_*` kernels and `perfTest1`/`perfTest2` in thread CPU time, pinned to one core on Windows.
- Seed 0 runs again after every 20 seeds; each candidate is scored against the seed 0 runs on either side of
  it, so machine drift cancels out.
- Seeds 1 to 200 ran on both Windows x64 (MSVC) and Apple M4 (clang); the best ran again 5 times, interleaved
  with seed 0.

## Result

- On Windows, 198 of 200 random orders were slower than the hand-tuned one, some by about 16%.
- Seed 109 was confirmed at +4% on Windows (+3.7% to +4.4% over 5 repeats; noise about 1.8%) and neutral on
  the M4. Seed 166's first-round +4.9% did not hold up.
- On the M4, no order mattered beyond the noise.

**Not adopted.** A random order for a few percent is not worth losing a case order that means something; the
current hand-tuned grouping stays. A future layout change should keep cases in a meaningful order.

## Where it is

The tools and every CSV are on the tag `layout-search-2026-10`: `tools/permuteDispatch.js`,
`tools/layoutSearch.{js,sh,cmd}`, `tools/layoutScore.js` and `tools/layoutSearchResults/`. They apply to this
`Processor::run` as of `main` 8136bbd; the switch on `GAZL2` and `jit-compiler` has more cases.
