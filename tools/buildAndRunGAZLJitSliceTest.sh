#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit C3 vertical-slice test (tools/GAZLJitSliceTest.cpp): assembles a GAZL sum-loop, runs it in
# the real interpreter for the golden result, hand-lowers the same kernel through the Emitter, executes it from W^X
# memory under a JIT calling convention (frame/dsp slots + per-block fuel check), and compares. AArch64 only.
#
# This links the shipped VM (src/GAZL.cpp) READ-ONLY — it is compiled, not modified. Standalone by design (not wired
# into build.sh). clang++ direct, like the benchmarks/jit/ scripts. Override the compiler with CPP_COMPILER.

cd "$(dirname "$0")"/..

mode=${1:-release}
arch=$(uname -m)
if [ "$arch" != "arm64" ] && [ "$arch" != "aarch64" ]; then
	echo "GAZLJit ships only an AArch64 Emitter; nothing to run on '$arch'. Skipping."
	exit 0
fi

CPP=${CPP_COMPILER:-clang++}
opt="-O2"
[ "$mode" = "debug" ] && opt="-O0"

mkdir -p output
"$CPP" $opt -std=c++17 -I src \
	src/GAZL.cpp \
	src/GAZLJit.cpp \
	tools/GAZLJitSliceTest.cpp \
	-o output/GAZLJitSliceTest

echo "running output/GAZLJitSliceTest ..."
echo
output/GAZLJitSliceTest
