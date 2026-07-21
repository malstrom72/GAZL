#!/usr/bin/env bash
# Build and launch the Mac fuzz lanes in the background for HOURS (default 48). Three lanes:
#   lane 1  arm64 code-gen, coverage-guided libFuzzer JIT-vs-interp differential  -> output/GAZLFuzz
#   lane 2  x86_64/Rosetta code-gen, --gen seed-walk differential (no libFuzzer)  -> output/GAZLFuzzX64
#   lane 6  arm64 text-assembler, coverage-guided libFuzzer (assembler+interp)    -> output/GAZLFuzzText
# Logs + libFuzzer corpora live under output/ (gitignored). Stop everything with: pkill -f 'GAZLFuzz'
#   tools/runFuzzMac.sh [hours]
set -e -o pipefail -u
cd "$(dirname "$0")"
HOURS=${1:-48}
SECS=$(( HOURS * 3600 ))
logs=../output/fuzzlogs; mkdir -p "$logs"
diffcorpus=../output/diffcorpus; mkdir -p "$diffcorpus"
textcorpus=../output/textcorpus; mkdir -p "$textcorpus"

echo "building lanes 1, 2, 6 ..."
./buildGazlFuzz.sh
./buildGazlFuzz.sh text
./buildGazlFuzz.sh x64

echo "launching (${HOURS}h) ..."
nohup ../output/GAZLFuzz     -max_total_time=$SECS -jobs=6 -workers=6 "$diffcorpus" > "$logs/lane1_arm64_diff.log" 2>&1 &
echo "  lane 1  arm64 diff   pid $!  -> $logs/lane1_arm64_diff.log"
nohup ../output/GAZLFuzzText -max_total_time=$SECS -jobs=4 -workers=4 "$textcorpus" > "$logs/lane6_text.log" 2>&1 &
echo "  lane 6  text         pid $!  -> $logs/lane6_text.log"
# lane 2: --gen has no timer, so loop disjoint seed bands until the deadline.
nohup bash -c '
	end=$(( $(date +%s) + '"$SECS"' )); seed=0
	while [ "$(date +%s)" -lt "$end" ]; do
		arch -x86_64 ../output/GAZLFuzzX64 --gen 50000000 "$seed" deep || exit 1
		seed=$(( seed + 50000000 ))
	done' > "$logs/lane2_x64_diff.log" 2>&1 &
echo "  lane 2  x64 diff     pid $!  -> $logs/lane2_x64_diff.log"

echo
echo "running. watch:   tail -f $logs/*.log"
echo "a JIT bug prints 'JIT/interp divergence' and stops that lane; the text lane drops a crash-* file in tools/."
echo "stop all:         pkill -f 'GAZLFuzz'"
