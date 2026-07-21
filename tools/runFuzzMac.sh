#!/usr/bin/env bash
# Build and launch the Mac fuzz lanes in the background for HOURS (default 48). Three lanes:
#   lane 1  arm64 code-gen, coverage-guided libFuzzer JIT-vs-interp differential  -> output/GAZLFuzz
#   lane 2  x86_64/Rosetta code-gen, --gen seed-walk differential (no libFuzzer)  -> output/GAZLFuzzX64
#   lane 6  arm64 text-assembler, coverage-guided libFuzzer (assembler+interp)    -> output/GAZLFuzzText
#
# The libFuzzer lanes run N independent workers sharing one corpus dir (the standard parallel pattern) - NOT
# libFuzzer -fork, whose merged progress is block-buffered to stdout and never appears live in a file. Each
# worker logs to its own w<k>.log; libFuzzer writes stats to stderr (unbuffered), so those logs update live.
# EVERYTHING stays under output/ (gitignored): output/lane*/ holds corpus/ + per-worker logs + crash-* artifacts.
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

# launch_libfuzzer <lane-dir-name> <binary-name> <worker-count>
launch_libfuzzer() {
	local dir="$OUT/$1" bin="$OUT/$2" n="$3" k
	mkdir -p "$dir/corpus"
	for (( k = 0; k < n; k++ )); do
		# own cwd + own log per worker; shared corpus dir. stderr (libFuzzer stats) is unbuffered => live log.
		( cd "$dir" && nohup "$bin" -max_total_time=$SECS -artifact_prefix=./ corpus > "w$k.log" 2>&1 & )
	done
	echo "  $1: $n workers -> $dir/w*.log   (corpus: $dir/corpus)"
}

echo "launching (${HOURS}h) ..."
launch_libfuzzer lane1_arm64_diff GAZLFuzz     6
launch_libfuzzer lane6_text       GAZLFuzzText 4

# lane 2: --gen has no timer and no corpus; loop disjoint seed bands until the deadline (prints to stderr = live).
( cd "$OUT" && nohup bash -c '
	end=$(( $(date +%s) + '"$SECS"' )); seed=0
	while [ "$(date +%s)" -lt "$end" ]; do
		arch -x86_64 ./GAZLFuzzX64 --gen 50000000 "$seed" deep || exit 1
		seed=$(( seed + 50000000 ))
	done' > lane2_x64_diff.log 2>&1 & )
echo "  lane2_x64_diff: --gen seed walk -> $OUT/lane2_x64_diff.log"

echo
echo "watch:            tail -f $OUT/lane1_arm64_diff/w*.log $OUT/lane6_text/w*.log $OUT/lane2_x64_diff.log"
echo "found a bug:      grep -rl 'JIT/interp divergence' $OUT/lane*  ;  crash inputs: $OUT/lane*/crash-*"
echo "stop everything:  pkill -f 'GAZLFuzz'"
