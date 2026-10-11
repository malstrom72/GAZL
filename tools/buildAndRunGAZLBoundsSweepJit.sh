#!/usr/bin/env bash
set -e -o pipefail -u

# Builds the boundary sweep (tools/GAZLBoundsSweep.cpp) with the JIT and runs it: every memory instruction form at
# every edge of the memory arena, through the interpreter AND the JIT, each checked against the sweep's own oracle.
# The backend is picked by host arch, as in buildAndRunGAZLJitLowerTest.sh. tools/test-jit.sh runs it.
# Override the compiler with CPP_COMPILER. (Windows: use the .cmd.)

cd "$(dirname "$0")"/..
CPP=${CPP_COMPILER:-clang++}

case "$(uname -m)" in
	arm64 | aarch64) backend=src/GAZLJitArm64.cpp ;;
	x86_64) backend=src/GAZLJitX64.cpp ;;
	*) echo "GAZLJit has no backend for '$(uname -m)'; nothing to run. Skipping."; exit 0 ;;
esac
jitmem=src/GAZLJitMemPosix.cpp
[ "$(uname -s)" = "Darwin" ] && jitmem=src/GAZLJitMemMacOS.cpp

mkdir -p output
"$CPP" -O2 -std=c++11 -DGAZL_JIT -I src \
	src/GAZL.cpp \
	src/GAZLJit.cpp \
	"$backend" \
	"$jitmem" \
	tools/GAZLBoundsSweep.cpp \
	-o output/GAZLBoundsSweepJit
output/GAZLBoundsSweepJit
