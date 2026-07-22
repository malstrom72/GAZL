#!/usr/bin/env bash
# Build and launch the Mac fuzz lanes in the background for HOURS (default 48).
#   lane 1  arm64 code-gen, coverage-guided libFuzzer JIT-vs-interp differential  -> output/GAZLFuzz
#   lane 2  x86_64/Rosetta code-gen, --gen seed-walk differential (no libFuzzer)  -> output/GAZLFuzzX64
#   lane 6  arm64 text-assembler, coverage-guided libFuzzer (assembler+interp)    -> output/GAZLFuzzText
#
# libFuzzer lanes use libFuzzer's own -jobs/-workers: each job writes a live fuzz-N.log in its lane dir and
# merges into the shared corpus/. ALL run output lives under output/fuzz/ (gitignored) and is WIPED at each
# start, so nothing accumulates across runs.
#   tools/runFuzzMac.sh [hours]      stop everything:  pkill -f 'GAZLFuzz'
set -e -o pipefail -u
cd "$(dirname "$0")"
HOURS=${1:-48}
SECS=$(( HOURS * 3600 ))

echo "building lanes 1, 2, 6 ..."
./buildGazlFuzz.sh
./buildGazlFuzz.sh text
./buildGazlFuzz.sh x64

OUT="$(cd ../output && pwd)"
FUZZ="$OUT/fuzz"
rm -rf "$FUZZ"; mkdir -p "$FUZZ"			# clean slate every run - no stale lane dirs / logs / corpora piling up

launch_libfuzzer() {   # <lane-dir-name> <binary-name> <job-count> [extra libFuzzer flags]
	local dir="$FUZZ/$1" bin="$OUT/$2" n="$3" extra="${4:-}"
	mkdir -p "$dir/corpus"
	( cd "$dir" && nohup "$bin" -jobs="$n" -workers="$n" -max_total_time=$SECS -artifact_prefix=./ $extra corpus > jobs.log 2>&1 & )
	echo "  $1: $n jobs -> live in output/fuzz/$1/fuzz-*.log   (corpus: output/fuzz/$1/corpus)"
}

echo "launching (${HOURS}h) ..."
launch_libfuzzer lane1_arm64_diff GAZLFuzz     6
mkdir -p "$FUZZ/lane6_text/corpus"; ./seedTextCorpus.sh "$FUZZ/lane6_text/corpus"		# real GAZL source so the text->JIT diff mutates valid programs, not garbage
# -max_len caps mutated program size; -timeout drops a unit whose interp/JIT run doesn't finish in 25s (non-terminating
# or pathologically slow program) so it can't stall the lane. A dropped unit becomes a timeout-* artifact.
launch_libfuzzer lane6_text       GAZLFuzzText 4 "-max_len=8192 -timeout=25"

# lane 2: --gen has no timer/corpus; loop disjoint seed bands until the deadline (prints live to stderr).
( cd "$FUZZ" && nohup bash -c '
	end=$(( $(date +%s) + '"$SECS"' )); seed=0
	while [ "$(date +%s)" -lt "$end" ]; do
		arch -x86_64 '"$OUT"'/GAZLFuzzX64 --gen 50000000 "$seed" deep || exit 1
		seed=$(( seed + 50000000 ))
	done' > lane2_x64_diff.log 2>&1 & )
echo "  lane2_x64_diff: --gen seed walk -> output/fuzz/lane2_x64_diff.log"

echo
echo "watch:            tail -f $FUZZ/lane1_arm64_diff/fuzz-*.log $FUZZ/lane6_text/fuzz-*.log $FUZZ/lane2_x64_diff.log"
echo "found a bug:      grep -rl 'JIT/interp divergence' $FUZZ  ;  crash inputs: $FUZZ/lane*/crash-*"
echo "stop everything:  pkill -f 'GAZLFuzz'"
