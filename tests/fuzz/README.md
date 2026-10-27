# Text-lane fuzz inputs

The text lane mutates GAZL **source text**, assembles it and runs it, and requires that nothing crashes. Build it
with [`tools/buildGazlFuzz.sh`](../../tools/buildGazlFuzz.sh) or its `.cmd`, and seed a fresh corpus with
[`tools/seedTextCorpus.sh`](../../tools/seedTextCorpus.sh). How to build and run the lane, on which machine, and
with which sanitizers, is in [`design/fuzzing.md`](../../design/fuzzing.md).

`textCorpus.tar.gz` is a corpus grown by that lane, minimized with `-merge=1` to 2076 inputs and packed
deterministically as `design/fuzzing.md` describes. **Unpack it into an EMPTY folder** - inputs left behind from a
previous unpack would be replayed as well.

`textCrashes/` holds the input of each FIXED text-lane defect, as a plain file rather than inside the archive,
because `-merge=1` drops any input whose coverage others already cover and would throw these away. They are marked
`binary` in `.gitattributes`: they are byte-exact, and line-ending conversion would corrupt them. Each one runs
clean on a current engine and reproduces its defect on the commit before the fix:

| input | defect |
| ----------------------------- | ------------------------------------------------------------------------ |
| `copyCountWrapsPastArena`     | `COPY` summed index + count, so a count of `MEMORY_OFFSET` wrapped past the bounds check and wrote out of bounds |
| `copyInRangeControl`          | the in-range control for the above; must stay clean, and is not a defect |
| `declaratorOutsideFunc`       | a declarator before any `FUNC` computed `nullptr + 1`                    |
| `foriConstLimitAtIntMax`      | `FORi` incremented with `++` on a counter already at `INT_MAX`           |
| `foriVarLimitAtIntMax`        | the same, with a variable limit                                          |
| `peekIndexOverflowsInt`       | `PEEK`'s bounds check summed a biased address in `Int`, overflowing it   |
| `pokeIndexOverflowsInt`       | the same, for `POKE`                                                     |
| `swchSizeOverflowsInt`        | `SWCH *2147483647` computed its table size as `Int + 1`, a signed overflow (UBSan catches it) |
| `setlNegativeFrameOffset`     | a negative `local:k` on `SETL`'s base reached below the frame, which run time does not check |
| `defpOffsetPastSection`       | a `! DEFp` name took any offset, and `PEEK`/`POKE` through it checked only the name, never the section |
| `defpOfCompileTimeAddress`    | the same, for a `! DEFp` name of an assembly-time address                |
| `compileTimeAddressAccess`    | `PEEK`/`POKE` through an assembly-time address, which names no label to check against |
| `defpOfPrefixedCompileTimeAddress` | `! DEFp #<P>` took the label path, looked up the name `<P>` and read the map's end node (ASan stack-buffer-overflow) |

Only inputs for FIXED defects belong here. An input for a defect that is still open goes somewhere else until the
fix lands, or it aborts a strict run while the corpus is still loading.
