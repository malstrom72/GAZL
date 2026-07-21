#!/usr/bin/env bash
# Build and launch the Mac fuzz lanes in the background for HOURS (default 48). Three lanes:
#   lane 1  arm64 code-gen, coverage-guided libFuzzer JIT-vs-interp differential  -> output/GAZLFuzz
#   lane 2  x86_64/Rosetta code-gen, --gen seed-walk differential (no libFuzzer)  -> output/GAZLFuzzX64
#   lane 6  arm64 text-assembler, coverage-guided libFuzzer (assembler+interp)    -> output/GAZLFuzzText
# libFuzzer lanes use -fork (live progress goes to the lane log; corpus is merged and grows continuously).
# EVERYTHING stays under output/ (gitignored): each lane has its own dir output/lane*/ holding corpus/ +
# crash-*/oom-*/timeout-* artifacts; the lane's console log is output/lane*.log. Stop with: pkill -f 'GAZLFuzz'
#   tools/runFuzzMac.sh [hours]
set -e -o pipefail -u
cd "$(dirname "$0")"
HOURS=${1:-48}
SECS=$(( HOURS * 3600 ))

echo "building lanes 1, 2, 6 ..."
./buildGazlFuzz.sh
./buildGazlFuzz.sh text
./buildGazlFuzz.sh x64

OUT="$(cd ../output && pwd)"
mkdir -p "$OUT/lane1_arm64_diff/corpus" "$OUT/lane6_text/corpus"

echo "launching (${HOURS}h) ..."
# Each -fork lane runs in its OWN dir (fork writes per-job files into cwd; separate dirs => no collision).
( cd "$OUT/lane1_arm64_diff" && nohup "$OUT/GAZLFuzz" \
	-max_total_time=$SECS -fork=6 -ignore_crashes=1 -artifact_prefix=./ corpus > lane.log 2>&1 & )
echo "  lane 1  arm64 diff   -> output/lane1_arm64_diff/lane.log   (corpus: output/lane1_arm64_diff/corpus)"
( cd "$OUT/lane6_text" && nohup "$OUT/GAZLFuzzText" \
	-max_total_time=$SECS -fork=4 -ignore_crashes=1 -artifact_prefix=./ corpus > lane.log 2>&1 & )
echo "  lane 6  text         -> output/lane6_text/lane.log         (corpus: output/lane6_text/corpus)"
# lane 2: --gen has no timer and no corpus; loop disjoint seed bands until the deadline.
( cd "$OUT" && nohup bash -c '
	end=$(( $(date +%s) + '"$SECS"' )); seed=0
	while [ "$(date +%s)" -lt "$end" ]; do
		arch -x86_64 ./GAZLFuzzX64 --gen 50000000 "$seed" deep || exit 1
		seed=$(( seed + 50000000 ))
	done' > lane2_x64_diff.log 2>&1 & )
echo "  lane 2  x64 diff     -> output/lane2_x64_diff.log          (seed-walk, no corpus)"

echo
echo "watch progress:   tail -f $OUT/lane1_arm64_diff/lane.log $OUT/lane6_text/lane.log $OUT/lane2_x64_diff.log"
echo "a JIT bug prints 'JIT/interp divergence'; a libFuzzer crash drops output/lane*/crash-*"
echo "stop all:         pkill -f 'GAZLFuzz'"
