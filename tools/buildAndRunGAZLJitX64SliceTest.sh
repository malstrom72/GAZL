#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit X64 lowering vertical slice (tools/GAZLJitX64SliceTest.cpp) as x86-64. On Apple Silicon it
# cross-compiles with `-arch x86_64` and runs the JIT'd code under Rosetta; GAZL.cpp (interpreter/assembler) is compiled
# x64 too for the golden side. Standalone by design.

cd "$(dirname "$0")"/..

mode=${1:-release}
CPP=${CPP_COMPILER:-clang++}
opt="-O2"
[ "$mode" = "debug" ] && opt="-O0"

archflag=""
host=$(uname -m)
if [ "$host" = "arm64" ] || [ "$host" = "aarch64" ]; then
	if [ "$(uname -s)" = "Darwin" ] && arch -x86_64 true 2>/dev/null; then
		archflag="-arch x86_64"
	else
		echo "No x86-64 execution available on '$host'; skipping X64 slice run test."
		exit 0
	fi
fi

mkdir -p output
"$CPP" $opt $archflag -std=c++11 -I src \
	src/GAZLJitX64.cpp \
	src/GAZL.cpp \
	tools/GAZLJitX64SliceTest.cpp \
	-o output/GAZLJitX64SliceTest

echo "running output/GAZLJitX64SliceTest ..."
echo
output/GAZLJitX64SliceTest
