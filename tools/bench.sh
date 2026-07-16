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
	# -std=c++11 is explicit: GAZLCmd uses C++11 lambdas / <chrono> / unique_ptr, and BuildCpp does not set a standard,
	# so on a compiler whose default is pre-C++11 (older Apple clang) the build fails without it. GAZLCpp.cpp is always
	# linked (GAZLCmd references translateToCpp for --emit-cpp regardless of the JIT).
	(cd tools && CPP_OPTIONS="$1" bash BuildCpp.sh release native "../output/$2" -std=c++11 -I.. GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp) \
		>/dev/null 2>&1
}
build_o2() {  # -O2 macro binary, with the JIT compiled in on AArch64 (so one binary serves both interp and --jit)
	local jitargs=""
	if [ "$has_jit" = 1 ]; then
		local jitmem=../src/GAZLJitMemPosix.cpp
		[ "$(uname -s)" = Darwin ] && jitmem=../src/GAZLJitMemMacOS.cpp
		jitargs="-std=c++11 -DGAZL_JIT ../src/GAZLJit.cpp ../src/GAZLJitArm64.cpp $jitmem"	# JIT base + host backend (NativeJitCompiler)
	fi
	(cd tools && CPP_OPTIONS="-O2" bash BuildCpp.sh release native ../output/GAZLCmd_o2 \
		-I.. GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp $jitargs) >/dev/null 2>&1
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
jit_stats() {  # $1 = gazl, $2.. = extra flags; echoes "compile_ms code_bytes" (empty => fell back to interpreter)
	local gazl="$1"; shift
	./output/GAZLCmd_o2 --jit-stats "$@" "$gazl" main </dev/null 2>&1 >/dev/null \
		| sed -n 's/^jitstats compile_ms=\([^ ]*\) code_bytes=\([^ ]*\).*/\1 \2/p'
}
run_macro() {  # $1 = label, $2 = gazl, $3.. = extra flags (e.g. --no-libm)
	local label="$1" gazl="$2"; shift 2
	if [ ! -f "$gazl" ]; then printf '%-12s %10s\n' "$label" "MISSING"; return; fi
	local i; i=$(bench_min GAZLCmd_o2 "$gazl" "$@")
	if [ -z "$i" ]; then printf '%-12s %10s\n' "$label" "FAILED"; return; fi
	if [ "$has_jit" != 1 ]; then
		printf '%-12s %10.3f %10s %8s %10s %8s\n' "$label" "$i" "n/a" "-" "n/a" "n/a"; return
	fi
	local j st cms ckb spd
	j=$(bench_min GAZLCmd_o2 "$gazl" --jit "$@")
	st=$(jit_stats "$gazl" "$@")
	if [ -z "$st" ]; then					# no jitstats line -> the program fell back (full-JIT assertion failed)
		printf '%-12s %10.3f %10s %8s %10s %8s\n' "$label" "$i" "n/a" "FELLBACK" "-" "-"; return
	fi
	cms=$(echo "$st" | cut -d' ' -f1)
	ckb=$(awk "BEGIN{printf \"%.1f\", $(echo "$st" | cut -d' ' -f2)/1024}")
	spd=$([ -n "$j" ] && awk "BEGIN{printf \"%.2fx\", $i/$j}" || echo "-")
	printf '%-12s %10.3f %10.3f %8s %10s %8s\n' "$label" "$i" "${j:-n/a}" "$spd" "$cms" "$ckb"
}
printf '\n%-12s %10s %10s %8s %10s %8s\n' "macro -O2" "interp_ms" "jit_ms" "speedup" "compile_ms" "code_KB"
printf '%-12s %10s %10s %8s %10s %8s\n' "------------" "----------" "----------" "--------" "----------" "--------"
run_macro "perfTest"  "tests/impala/golden/perfTest.gazl"  --no-libm
run_macro "perfTest1" "tests/impala/golden/perfTest1.gazl"
run_macro "perfTest2" "tests/impala/golden/perfTest2.gazl"

# suite lane: the ported benchmark kernels (benchmarks/suite/), each self-checking. `check` verifies the printed result
# is identical interp-vs-jit AND matches the committed golden checksum; a fallback shows as FELLBACK in compile_ms.
run_suite() {  # $1 = gazl path
	local gazl="$1"; local name; name=$(basename "$gazl" .gazl)
	local ci cj exp chk="ok"
	ci=$(./output/GAZLCmd_o2 "$gazl" main </dev/null 2>/dev/null)
	exp=$(cat "benchmarks/suite/expected/$name.checksum" 2>/dev/null || true)
	[ -n "$exp" ] && [ "$ci" != "$exp" ] && chk="GOLDEN!"
	local i; i=$(bench_min GAZLCmd_o2 "$gazl")
	if [ "$has_jit" != 1 ]; then
		printf '%-12s %10.3f %10s %8s %10s %8s  %s\n' "$name" "$i" "n/a" "-" "n/a" "n/a" "$chk"; return
	fi
	cj=$(./output/GAZLCmd_o2 --jit "$gazl" main </dev/null 2>/dev/null)
	[ "$cj" != "$ci" ] && chk="JIT!=INT"
	local j st cms ckb spd
	j=$(bench_min GAZLCmd_o2 "$gazl" --jit)
	st=$(jit_stats "$gazl")
	if [ -z "$st" ]; then printf '%-12s %10.3f %10s %8s %10s %8s  %s\n' "$name" "$i" "n/a" "FELLBACK" "-" "-" "$chk"; return; fi
	cms=$(echo "$st" | cut -d' ' -f1)
	ckb=$(awk "BEGIN{printf \"%.1f\", $(echo "$st" | cut -d' ' -f2)/1024}")
	spd=$([ -n "$j" ] && awk "BEGIN{printf \"%.2fx\", $i/$j}" || echo "-")
	[ -n "$j" ] && SUITE_RATIOS="${SUITE_RATIOS:-} $(awk "BEGIN{print $i/$j}")"
	printf '%-12s %10.3f %10s %8s %10s %8s  %s\n' "$name" "$i" "${j:-n/a}" "$spd" "$cms" "$ckb" "$chk"
}
if ls benchmarks/suite/golden/*.gazl >/dev/null 2>&1; then
	printf '\n%-12s %10s %10s %8s %10s %8s  %s\n' "suite -O2" "interp_ms" "jit_ms" "speedup" "compile_ms" "code_KB" "check"
	printf '%-12s %10s %10s %8s %10s %8s  %s\n' "------------" "----------" "----------" "--------" "----------" "--------" "-----"
	SUITE_RATIOS=""
	for g in benchmarks/suite/golden/*.gazl; do run_suite "$g"; done
	if [ "$has_jit" = 1 ] && [ -n "$SUITE_RATIOS" ]; then
		gm=$(awk "BEGIN{m=split(\"$SUITE_RATIOS\",a,\" \");s=0;n=0;for(k=1;k<=m;k++)if(a[k]+0>0){s+=log(a[k]);n++};if(n>0)printf\"%.2fx\",exp(s/n)}")
		printf '%-12s %10s %10s %8s   (geomean speedup over %d kernels)\n' "geomean" "" "" "$gm" \
			"$(echo $SUITE_RATIOS | wc -w)"
	fi
fi

# x64 (Rosetta) suite lane, on Apple Silicon only: catches x64-backend regressions the arm64 lane cannot see. RATIOS
# only - Rosetta absolutes are unstable (translation cache, background load, code-layout sensitivity); confirm any
# suspicious delta with several fresh-process runs on an unloaded machine, or natively on real x64 hardware.
if [ "$(uname -s)" = Darwin ] && [ "$arch" = arm64 ] && arch -x86_64 true 2>/dev/null; then
	echo "Building GAZLCmd_o2x64 (-O2 +JIT, Rosetta suite lane)..." >&2
	jitmem=../src/GAZLJitMemMacOS.cpp
	(cd tools && clang++ -arch x86_64 -O2 -std=c++11 -DGAZL_JIT -I.. -o ../output/GAZLCmd_o2x64 \
			GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp ../src/GAZLJit.cpp ../src/GAZLJitX64.cpp "$jitmem") >/dev/null 2>&1
	if [ -x output/GAZLCmd_o2x64 ]; then
		x64min() {  # $1 = gazl, $2.. = extra flags; echoes min_ms
			local gazl="$1"; shift
			arch -x86_64 ./output/GAZLCmd_o2x64 "$@" "$gazl" main --bench="$iters" --warmup="$warmup" 2>/dev/null \
				| grep '^bench' | sed -n 's/.*min_ms=\([^	]*\).*/\1/p'
		}
		printf '\n%-12s %10s %10s %8s  %s\n' "x64 suite" "interp_ms" "jit_ms" "speedup" "(Rosetta: ratios only)"
		printf '%-12s %10s %10s %8s\n' "------------" "----------" "----------" "--------"
		X64_RATIOS=""
		for g in benchmarks/suite/golden/*.gazl; do
			name=$(basename "$g" .gazl)
			i=$(x64min "$g"); j=$(x64min "$g" --jit)
			if [ -n "$i" ] && [ -n "$j" ]; then
				spd=$(awk "BEGIN{printf \"%.2fx\", $i/$j}")
				X64_RATIOS="$X64_RATIOS $(awk "BEGIN{print $i/$j}")"
				printf '%-12s %10.3f %10.3f %8s\n' "$name" "$i" "$j" "$spd"
			else
				printf '%-12s %10s\n' "$name" "FAILED"
			fi
		done
		if [ -n "$X64_RATIOS" ]; then
			gm=$(awk "BEGIN{m=split(\"$X64_RATIOS\",a,\" \");s=0;n=0;for(k=1;k<=m;k++)if(a[k]+0>0){s+=log(a[k]);n++};if(n>0)printf\"%.2fx\",exp(s/n)}")
			printf '%-12s %10s %10s %8s   (geomean speedup over %d kernels)\n' "geomean" "" "" "$gm" "$(echo $X64_RATIOS | wc -w)"
		fi
	else
		echo "x64 suite lane skipped (Rosetta build failed)" >&2
	fi
fi
