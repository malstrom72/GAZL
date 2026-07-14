#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit x86-64 engine test (tools/GAZLJitX64EngineTest.cpp): assembles a set of GAZL kernels,
# compiles each through JitCompiler::compile, runs them via JitProcessor, and diffs status + whole memory image against
# the interpreter. Built as an x86-64 binary; on Apple Silicon it cross-compiles with `-arch x86_64` and runs under
# Rosetta, on a native x64 host it just builds native. Links the shipped VM (src/GAZL.cpp) READ-ONLY. Standalone by
# design (not wired into build.sh). Override the compiler with CPP_COMPILER.

cd "$(dirname "$0")"/..

mode=${1:-release}
CPP=${CPP_COMPILER:-clang++}
opt="-O2"
[ "$mode" = "debug" ] && opt="-O0"

archflag=""
host=$(uname -m)
if [ "$host" = "arm64" ] || [ "$host" = "aarch64" ]; then
	if [ "$(uname -s)" = "Darwin" ] && arch -x86_64 true 2>/dev/null; then
		archflag="-arch x86_64"		# cross-build to x64, run under Rosetta
	else
		echo "No x86-64 execution available on '$host'; skipping GAZLJit X64 engine test."
		exit 0
	fi
fi

mkdir -p output
"$CPP" $opt $archflag -std=c++11 -I src \
	src/GAZLJitX64.cpp \
	src/GAZLJit.cpp \
	src/GAZL.cpp \
	src/GAZLJitMemPosix.cpp \
	tools/GAZLJitX64EngineTest.cpp \
	-o output/GAZLJitX64EngineTest

echo "running output/GAZLJitX64EngineTest ..."
echo
output/GAZLJitX64EngineTest
