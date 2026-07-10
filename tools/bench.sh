#!/usr/bin/env bash
#
# Interpreter benchmark driver (dual optimization mode).
#
# Builds TWO release GAZLCmd binaries and routes each workload group to the right one:
#
#   micro/ -> -Os : one tight loop per instruction the UB fix touches (add/sub/mul/div/mod/
#                   shl/shr/shru/abs/ftoi) plus `xor` as a CONTROL. Built -Os on purpose:
#                   at -O2, clang on Apple Silicon lets dispatch-loop alignment swing a
#                   single-opcode loop's time by ~100% for reasons unrelated to the opcode
#                   (measured: add 190ms vs sub 66ms on M4 at -O2 -- two identical-cost ALU
#                   ops), which would swamp the few-cycle cost of the UB helpers. -Os keeps
#                   the dispatch compact and uniform, so per-op deltas are trustworthy.
#   macro/ -> -O2 : whole-program workloads (perfTest1/2). -O2 is ~1.5x faster than -Os on
#                   real code and represents an optimally-built interpreter; averaging over
#                   many opcodes makes it robust to the alignment noise that wrecks micro@-O2.
#
# All optimization is set via CPP_OPTIONS on the command line -- BuildCpp.sh is shared/synced
# across projects and must NOT be edited here.
#
# Measure an interpreter change by running this before and after on the same machine and
# comparing min_ms. `xor` (micro control) must stay flat; movement there means the
# measurement drifted. Regenerate the micro set with tools/bench/genbench.sh.
#
# Usage:  bash tools/bench.sh [iters] [warmup]        (defaults: 10 measured, 3 warmup)
#
set -e -o pipefail -u
cd "$(dirname "$0")/.."

iters=${1:-10}
warmup=${2:-3}

build_bin() {  # $1 = CPP_OPTIONS, $2 = output basename
	(cd tools && CPP_OPTIONS="$1" bash BuildCpp.sh release native "../output/$2" -I.. GAZLCmd.cpp ../src/GAZL.cpp) \
		>/dev/null 2>&1
}
echo "Building GAZLCmd_os (-Os, micro) + GAZLCmd_o2 (-O2, macro)..." >&2
build_bin "-Os" GAZLCmd_os
build_bin "-O2" GAZLCmd_o2

run_row() {  # $1 = label, $2 = .gazl path, $3 = binary basename
	local label="$1" gazl="$2" bin="./output/$3" line min median mean stddev
	if [ ! -f "$gazl" ]; then printf '%-16s %10s\n' "$label" "MISSING"; return; fi
	line=$("$bin" "$gazl" main --bench="$iters" --warmup="$warmup" 2>/dev/null | grep '^bench' || true)
	if [ -z "$line" ]; then printf '%-16s %10s\n' "$label" "FAILED"; return; fi
	min=$(echo "$line"    | sed -n 's/.*min_ms=\([^	]*\).*/\1/p')
	median=$(echo "$line" | sed -n 's/.*median_ms=\([^	]*\).*/\1/p')
	mean=$(echo "$line"   | sed -n 's/.*mean_ms=\([^	]*\).*/\1/p')
	stddev=$(echo "$line" | sed -n 's/.*stddev_ms=\([^	]*\).*/\1/p')
	printf '%-16s %10.3f %10.3f %10.3f %10.3f\n' "$label" "$min" "$median" "$mean" "$stddev"
}

header() {
	printf '\n%-16s %10s %10s %10s %10s\n' "$1" "min_ms" "median_ms" "mean_ms" "stddev_ms"
	printf '%-16s %10s %10s %10s %10s\n' "----------------" "----------" "----------" "----------" "----------"
}

header "micro -Os (128M ops)"
for op in add sub mul div mod shl shr shru abs ftoi; do
	run_row "$op" "tests/bench/golden/op_$op.gazl" GAZLCmd_os
done
run_row "xor [control]" "tests/bench/golden/op_xor.gazl" GAZLCmd_os

# perfTest (float DSP) is excluded: it defines functions named log/sqrt that collide with the
# native functions of the same name in GAZLCmd's table.
header "macro -O2"
run_row "perfTest1" "tests/impala/golden/perfTest1.gazl" GAZLCmd_o2
run_row "perfTest2" "tests/impala/golden/perfTest2.gazl" GAZLCmd_o2
