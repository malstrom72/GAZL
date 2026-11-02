#!/usr/bin/env bash
# Replay the committed text-lane corpus and the fixed-crash inputs through the assembler and interpreter, and
# require that none of them crashes. This is a REGRESSION gate, not a fuzz run: it finds nothing new, it stops the
# defects already found from coming back. The .cmd twin does the same work with the same driver.
#
# Build with the ordinary toolchain, no sanitizers and no libFuzzer: this must run everywhere the normal build runs.
# `beta` keeps asserts on, so a broken internal contract fails here too.
#
# Usage: test-fuzz.sh [valgrind]. `valgrind` runs the replay under Valgrind's memcheck, which reports reads of
# uninitialized memory that the sanitizers miss (Linux only, CI). It is about 25 times slower, so the budget is off.
set -e -o pipefail -u
cd "$(dirname "$0")/.."
mkdir -p output

RUNNER=""
BUDGET="--budget=30"		# warns past the normal build's 30 s budget
if [[ "${1:-}" == "valgrind" ]]; then
	RUNNER="valgrind -q --error-exitcode=1"
	BUDGET=""
elif [[ $# -gt 0 ]]; then
	echo "Usage: test-fuzz.sh [valgrind]"
	exit 1
fi

echo "test-fuzz: building the replay driver"
( cd tools && bash BuildCpp.sh beta native ../output/GAZLReplay \
		-DLIBFUZZ -DLIBFUZZ_STANDALONE -I.. GAZLCmd.cpp ../src/GAZL.cpp )
chmod +x output/GAZLReplay 2>/dev/null || true

# Unpack into an EMPTY folder, or inputs left from a previous run are replayed too (design/fuzzing.md).
WORK=output/fuzz/replay
rm -rf "$WORK"
mkdir -p "$WORK"
tar -xzf tests/fuzz/textCorpus.tar.gz -C "$WORK"

# One path per line, so the .cmd twin can do the same thing without a command-line length limit.
LIST="$WORK/inputs.txt"
find "$WORK/corpus" -type f > "$LIST"
find tests/fuzz/textCrashes -type f -name '*.gazl' >> "$LIST"
N=$(wc -l < "$LIST" | tr -d ' ')

echo "test-fuzz: replaying $N inputs${RUNNER:+ under Valgrind}"
$RUNNER ./output/GAZLReplay $BUDGET "@$LIST" > /dev/null
echo "test-fuzz: $N inputs replayed, no crashes"
