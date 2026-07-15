#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the unified GAZLJit lowering test (tools/GAZLJitLowerTest.cpp): assembles a spread of GAZL kernels,
# compiles each through the one shared JitCompiler::compile for the HOST's backend, and checks every JIT run against the
# interpreter (whole memory image + Status) at full fuel AND at tiny fuel (forcing repeated suspend/resume, §5.7.5).
# Runs on arm64 and x86-64 — the backend is picked by host arch. Links the shipped VM (src/GAZL.cpp) READ-ONLY.
# Standalone by design (not wired into build.sh). Override the compiler with CPP_COMPILER. (Windows: use the .cmd.)

cd "$(dirname "$0")"/..
mode=${1:-release}
CPP=${CPP_COMPILER:-clang++}
opt="-O2"
[ "$mode" = "debug" ] && opt="-O0"

case "$(uname -m)" in
	arm64 | aarch64) backend=src/GAZLJitArm64.cpp ;;
	x86_64) backend=src/GAZLJitX64.cpp ;;
	*) echo "GAZLJit has no backend for '$(uname -m)'; nothing to run. Skipping."; exit 0 ;;
esac
jitmem=src/GAZLJitMemPosix.cpp
[ "$(uname -s)" = "Darwin" ] && jitmem=src/GAZLJitMemMacOS.cpp

mkdir -p output
"$CPP" $opt -std=c++11 -I src \
	src/GAZL.cpp \
	src/GAZLJit.cpp \
	"$backend" \
	"$jitmem" \
	tools/GAZLJitLowerTest.cpp \
	-o output/GAZLJitLowerTest

echo "running output/GAZLJitLowerTest ..."
echo
output/GAZLJitLowerTest
