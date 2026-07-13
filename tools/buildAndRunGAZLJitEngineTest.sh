#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit C3-full/C4 engine prototype (tools/GAZLJitEngineTest.cpp): a ProtoEngine subclass of the VM
# Processor drives Emitter-produced native code over shared machine state, and the test checks full-run equivalence,
# suspend-in-JIT/resume-in-interpreter, and trap-as-status against the interpreter. AArch64 only.
#
# Links the shipped VM (src/GAZL.cpp) READ-ONLY — compiled, not modified. Standalone by design (not wired into
# build.sh). clang++ direct, like the benchmarks/jit/ scripts. Override the compiler with CPP_COMPILER.

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
"$CPP" $opt -std=c++11 -I src \
	src/GAZL.cpp \
	src/GAZLJit.cpp \
	src/GAZLJitMemPosix.cpp \
	tools/GAZLJitEngineTest.cpp \
	-o output/GAZLJitEngineTest

echo "running output/GAZLJitEngineTest ..."
echo
output/GAZLJitEngineTest
