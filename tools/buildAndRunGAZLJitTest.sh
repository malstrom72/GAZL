#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit arm64 Emitter assemble-diff test (tools/GAZLJitTest.cpp). AArch64 only; the Emitter and
# its clang-assembled reference (tools/GAZLJitTestRef.arm64.S) both target AArch64. See docs/JitEmitterHandoff.md.
#
# Standalone by design: not wired into build.sh (that suite is hardcoded). Compiles with clang++ directly, like the
# benchmarks/jit/ scripts. Override the compiler with CPP_COMPILER.

cd "$(dirname "$0")"/..

mode=${1:-release}
arch=$(uname -m)
if [ "$arch" != "arm64" ] && [ "$arch" != "aarch64" ]; then
	echo "GAZLJit ships only an AArch64 Emitter; nothing to test on '$arch'. Skipping."
	exit 0
fi

CPP=${CPP_COMPILER:-clang++}
opt="-O2"
[ "$mode" = "debug" ] && opt="-O0"

mkdir -p output
"$CPP" $opt -std=c++17 -I src \
	src/GAZLJit.cpp \
	tools/GAZLJitTest.cpp \
	tools/GAZLJitTestRef.arm64.S \
	-o output/GAZLJitTest

echo "running output/GAZLJitTest ..."
echo
output/GAZLJitTest
