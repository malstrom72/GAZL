#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit v1 lowering-pass test (tools/GAZLJitLowerTest.cpp): compiles GAZL functions straight from
# their finalized Instruction[] to arm64 via the Emitter, runs them through JitEngine, and checks each against the
# interpreter (whole memory image). AArch64 only. Links the shipped VM (src/GAZL.cpp) READ-ONLY.
#
# Standalone by design (not wired into build.sh). clang++ direct, like the benchmarks/jit/ scripts. Override the
# compiler with CPP_COMPILER.

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
	tools/GAZLJitLowerTest.cpp \
	-o output/GAZLJitLowerTest

echo "running output/GAZLJitLowerTest ..."
echo
output/GAZLJitLowerTest
