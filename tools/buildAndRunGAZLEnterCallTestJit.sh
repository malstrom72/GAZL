#!/usr/bin/env bash
set -e -o pipefail -u

# Builds the enterCall and pushCall tests (tools/GAZLEnterCallTest.cpp) with the JIT and runs them through the JIT:
# calls from natives, and interrupts on a suspended processor. The backend is picked by host arch, as in
# buildAndRunGAZLJitLowerTest.sh. tools/test-jit.sh runs it. Override the compiler with CPP_COMPILER. (Windows: use the
# .cmd.)

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
	tools/GAZLEnterCallTest.cpp \
	-o output/GAZLEnterCallTestJit
output/GAZLEnterCallTestJit
