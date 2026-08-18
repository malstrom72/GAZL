#!/usr/bin/env bash
set -o pipefail -u

# An exhaustive fuzzing round: walk the generative fuzzer's deterministic seed space under --vm until a
# time budget runs out. --vm is the point of a long round - it adds the four REFERENCE oracles (checked
# initializers vs stdout, dead-strip output-equivalence, --range-checks neutrality, import-split
# equivalence) on top of the crash/assert oracle the compile-only gate run (fuzzImpala.js 3000 1) uses.
#
# Seeds are contiguous and deterministic, so coverage is just "keep feeding the next range". Chunking
# across processes caps the RSS a single node run accumulates from its module cache (see the header in
# impala/fuzzImpala.js). Each chunk that reports a fault exits non-zero; we STOP on the first one so the
# failing seed is preserved in its log rather than buried under later chunks. Reproduce any fault with
#   node impala/fuzzImpala.js 1 <seed> --vm      # re-run just that seed
#   node impala/fuzzImpala.js --print <seed>     # dump the program that seed generates
#
# Usage: bash tools/fuzz48h.sh [hours] [chunk] [startSeed]
#   hours     wall-clock budget           (default 48)
#   chunk     seeds per node process      (default 5000; bump if RSS stays low)
#   startSeed first seed of the sweep     (default 1; give a worker its own band to parallelize)

cd "$(dirname "$0")"/..

HOURS="${1:-48}"
CHUNK="${2:-5000}"
start="${3:-1}"

if [ ! -x output/GAZLCmd.exe ] && [ ! -x output/GAZLCmd ]; then
	echo "output/GAZLCmd not found - --vm needs it. Build it first:" >&2
	echo "  cmd //c \"tools\\\\buildGAZLCmd.cmd release\"   (Windows)" >&2
	echo "  tools/buildGAZLCmd.sh                          (macOS/Linux)" >&2
	exit 1
fi

mkdir -p fuzzlogs
deadline=$(( $(date +%s) + HOURS * 3600 ))

echo "exhaustive fuzz: ${HOURS}h, ${CHUNK}/chunk, from seed ${start}, --vm; logs in fuzzlogs/"
while [ "$(date +%s)" -lt "$deadline" ]; do
	end=$(( start + CHUNK - 1 ))
	log="fuzzlogs/fuzz_${start}_${end}.log"
	echo "=== seeds ${start}..${end}  ($(date)) ==="
	node impala/fuzzImpala.js "$CHUNK" "$start" --vm 2>&1 | tee "$log"
	if [ "${PIPESTATUS[0]}" -ne 0 ]; then
		echo "!!! FAULT in seeds ${start}..${end} - full program in ${log}" >&2
		exit 1
	fi
	start=$(( end + 1 ))
done

echo "clean: no faults through seed $(( start - 1 )) in ${HOURS}h"
