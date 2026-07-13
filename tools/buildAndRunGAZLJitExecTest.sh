#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit arm64 Emitter execution test (tools/GAZLJitExecTest.cpp): emits kernels through the
# Emitter, runs them from W^X executable memory, and checks their return values against C references. AArch64 only.
# Reuses the allocation/flush strategy proven GO by JIT spike A1 (spike/jit-probe/). See docs/JitEmitterHandoff.md.
#
# Standalone by design (not wired into build.sh). clang++ direct, like the benchmarks/jit/ scripts. Override the
# compiler with CPP_COMPILER.
#
# On Apple Silicon the MAP_JIT rung needs the JIT entitlement; clang's default ad-hoc signing grants a plain process
# `com.apple.security.cs.allow-jit` implicitly, so no extra codesign step is required for this smoke test.

cd "$(dirname "$0")"/..

mode=${1:-release}
arch=$(uname -m)
if [ "$arch" != "arm64" ] && [ "$arch" != "aarch64" ]; then
	echo "GAZLJit ships only an AArch64 Emitter; nothing to execute on '$arch'. Skipping."
	exit 0
fi

CPP=${CPP_COMPILER:-clang++}
opt="-O2"
[ "$mode" = "debug" ] && opt="-O0"

mkdir -p output
"$CPP" $opt -std=c++11 -I src \
	src/GAZLJit.cpp \
	src/GAZLJitMemPosix.cpp \
	tools/GAZLJitExecTest.cpp \
	-o output/GAZLJitExecTest

echo "running output/GAZLJitExecTest ..."
echo
output/GAZLJitExecTest
