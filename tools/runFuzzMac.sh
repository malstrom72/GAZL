#!/usr/bin/env bash
# Build and launch the Mac fuzz lanes in the background for HOURS (default 48).
#   lane 1  arm64 code-gen, coverage-guided libFuzzer JIT-vs-interp differential  -> output/GAZLFuzz
#   lane 2  x86_64/Rosetta code-gen, --gen seed-walk differential (no libFuzzer)  -> output/GAZLFuzzX64
#   lane 6  arm64 text-assembler, coverage-guided libFuzzer (assembler+interp)    -> output/GAZLFuzzText
#
# libFuzzer lanes use libFuzzer's own -jobs/-workers (NOT -fork): each job writes a live fuzz-N.log in its lane
# dir (libFuzzer stats go to stderr, unbuffered) and merges into the shared corpus/. Everything stays under
# output/ (gitignored): output/lane*/ holds corpus/ + fuzz-*.log + crash-* artifacts.
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

launch_libfuzzer() {   # <lane-dir-name> <binary-name> <job-count>
	local dir="$OUT/$1" bin="$OUT/$2" n="$3"
	mkdir -p "$dir/corpus"
	( cd "$dir" && nohup "$bin" -jobs="$n" -workers="$n" -max_total_time=$SECS -artifact_prefix=./ corpus > jobs.log 2>&1 & )
	echo "  $1: $n jobs -> live in $dir/fuzz-*.log   (corpus: $dir/corpus)"
}

echo "launching (${HOURS}h) ..."
launch_libfuzzer lane1_arm64_diff GAZLFuzz     6
launch_libfuzzer lane6_text       GAZLFuzzText 4

# lane 2: --gen has no timer/corpus; loop disjoint seed bands until the deadline (prints live to stderr).
( cd "$OUT" && nohup bash -c '
	end=$(( $(date +%s) + '"$SECS"' )); seed=0
	while [ "$(date +%s)" -lt "$end" ]; do
		arch -x86_64 ./GAZLFuzzX64 --gen 50000000 "$seed" deep || exit 1
		seed=$(( seed + 50000000 ))
	done' > lane2_x64_diff.log 2>&1 & )
echo "  lane2_x64_diff: --gen seed walk -> $OUT/lane2_x64_diff.log"

echo
echo "watch:            tail -f $OUT/lane1_arm64_diff/fuzz-*.log $OUT/lane6_text/fuzz-*.log $OUT/lane2_x64_diff.log"
echo "found a bug:      grep -rl 'JIT/interp divergence' $OUT/lane*  ;  crash inputs: $OUT/lane*/crash-*"
echo "stop everything:  pkill -f 'GAZLFuzz'"
