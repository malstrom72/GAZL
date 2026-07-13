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

# The native JIT is AArch64-only; on other hosts the macro lane runs interpreter-only and the JIT column reads n/a.
arch=$(uname -m)
has_jit=0; { [ "$arch" = arm64 ] || [ "$arch" = aarch64 ]; } && has_jit=1

build_bin() {  # $1 = CPP_OPTIONS, $2 = output basename
	(cd tools && CPP_OPTIONS="$1" bash BuildCpp.sh release native "../output/$2" -I.. GAZLCmd.cpp ../src/GAZL.cpp) \
		>/dev/null 2>&1
}
build_o2() {  # -O2 macro binary, with the JIT compiled in on AArch64 (so one binary serves both interp and --jit)
	local jitargs=""
	if [ "$has_jit" = 1 ]; then
		local jitmem=../src/GAZLJitMemPosix.cpp
		[ "$(uname -s)" = Darwin ] && jitmem=../src/GAZLJitMemMacOS.cpp
		jitargs="-std=c++17 -DGAZL_JIT ../src/GAZLJit.cpp ../src/GAZLJitCompile.cpp $jitmem"
	fi
	(cd tools && CPP_OPTIONS="-O2" bash BuildCpp.sh release native ../output/GAZLCmd_o2 \
		-I.. GAZLCmd.cpp ../src/GAZL.cpp $jitargs) >/dev/null 2>&1
}
echo "Building GAZLCmd_os (-Os, micro) + GAZLCmd_o2 (-O2$( [ "$has_jit" = 1 ] && echo ' +JIT' ), macro)..." >&2
build_bin "-Os" GAZLCmd_os
build_o2

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

# macro lane: whole-program workloads, interpreter (-O2, the optimally-built reference) vs the native JIT in the SAME
# binary (toggled by --jit). perfTest is a self-contained libm (defines log/sqrt/...) so it needs --no-libm.
bench_min() {  # $1 = binary basename, $2 = gazl, $3.. = extra flags; echoes min_ms (empty on failure)
	local bin="./output/$1" gazl="$2"; shift 2
	"$bin" "$@" "$gazl" main --bench="$iters" --warmup="$warmup" 2>/dev/null \
		| grep '^bench' | sed -n 's/.*min_ms=\([^	]*\).*/\1/p'
}
run_macro() {  # $1 = label, $2 = gazl, $3.. = extra flags (e.g. --no-libm)
	local label="$1" gazl="$2"; shift 2
	if [ ! -f "$gazl" ]; then printf '%-16s %12s\n' "$label" "MISSING"; return; fi
	local i j spd
	i=$(bench_min GAZLCmd_o2 "$gazl" "$@")
	if [ -z "$i" ]; then printf '%-16s %12s\n' "$label" "FAILED"; return; fi
	if [ "$has_jit" = 1 ]; then j=$(bench_min GAZLCmd_o2 "$gazl" --jit "$@"); fi
	if [ "$has_jit" = 1 ] && [ -n "$j" ]; then
		spd=$(awk "BEGIN{printf \"%.2fx\", $i/$j}")
		printf '%-16s %12.3f %12.3f %10s\n' "$label" "$i" "$j" "$spd"
	else
		printf '%-16s %12.3f %12s %10s\n' "$label" "$i" "n/a" "-"
	fi
}
printf '\n%-16s %12s %12s %10s\n' "macro -O2" "interp_ms" "jit_ms" "speedup"
printf '%-16s %12s %12s %10s\n' "----------------" "------------" "------------" "----------"
run_macro "perfTest"  "tests/impala/golden/perfTest.gazl"  --no-libm
run_macro "perfTest1" "tests/impala/golden/perfTest1.gazl"
run_macro "perfTest2" "tests/impala/golden/perfTest2.gazl"
