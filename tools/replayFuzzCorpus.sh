#!/usr/bin/env bash
# Replay the committed text-lane corpus, the seeds and the fixed-crash inputs through the assembler and interpreter,
# checking each run against the reference executor, and require that none of them crashes or disagrees. The corpus
# replay takes longer than the normal build's 30 s budget, so it runs here instead of in test-fuzz: in CI, and
# before every release or freeze (design/fuzzing.md). The .cmd twin does the same work with the same driver.
#
# Usage: replayFuzzCorpus.sh [valgrind]. `valgrind` runs the replay under Valgrind's memcheck, which reports reads of
# uninitialized memory that the sanitizers miss (Linux only, CI).
set -e -o pipefail -u
cd "$(dirname "$0")/.."
mkdir -p output

RUNNER=""
if [[ "${1:-}" == "valgrind" ]]; then
	RUNNER="valgrind -q --error-exitcode=1"
elif [[ $# -gt 0 ]]; then
	echo "Usage: replayFuzzCorpus.sh [valgrind]"
	exit 1
fi

echo "replayFuzzCorpus: building the replay driver"
( cd tools && bash BuildCpp.sh beta native ../output/GAZLReplay \
		-DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_CANONICAL_NAN -I.. GAZLCmd.cpp GAZLReference.cpp ../src/GAZL.cpp )
chmod +x output/GAZLReplay 2>/dev/null || true

# Unpack into an EMPTY folder, or inputs left from a previous run are replayed too (design/fuzzing.md).
WORK=output/fuzz/corpusReplay
rm -rf "$WORK"
mkdir -p "$WORK"
tar -xzf tests/fuzz/textCorpus.tar.gz -C "$WORK"

# One path per line, so the .cmd twin can do the same thing without a command-line length limit.
LIST="$WORK/inputs.txt"
find "$WORK/corpus" -type f > "$LIST"
find tests/fuzz/textCrashes -type f -name '*.gazl' >> "$LIST"
find tests/fuzz/textSeeds -type f -name '*.gazl' >> "$LIST"
N=$(wc -l < "$LIST" | tr -d ' ')

echo "replayFuzzCorpus: replaying $N inputs${RUNNER:+ under Valgrind}"
$RUNNER ./output/GAZLReplay "@$LIST" > /dev/null
echo "replayFuzzCorpus: $N inputs replayed, no crashes"
