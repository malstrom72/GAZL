#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit X64Emitter assemble-diff test (tools/GAZLJitX64Test.cpp) as an x86-64 binary. On Apple
# Silicon it cross-compiles with `-arch x86_64` and runs under Rosetta; on a native x64 host it just builds native. The
# emitter and its clang-assembled reference (tools/GAZLJitX64TestRef.x64.s) both target x86-64. Standalone by design.

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
		echo "No x86-64 execution available on '$host'; skipping X64Emitter run test."
		exit 0
	fi
fi

mkdir -p output
# GAZLJitX64.cpp now hosts JitCompiler::compile, so it pulls in GAZLJit.h / GAZL.h and needs the VM + JIT-mem backend
# linked in (mirrors how the arm64 emitter test links GAZLJit.cpp). GAZLJitMemPosix.cpp is enough for these tests.
"$CPP" $opt $archflag -std=c++11 -I src \
	src/GAZLJitX64.cpp \
	src/GAZLJit.cpp \
	src/GAZL.cpp \
	src/GAZLJitMemPosix.cpp \
	tools/GAZLJitX64Test.cpp \
	tools/GAZLJitX64TestRef.x64.s \
	-o output/GAZLJitX64Test

echo "running output/GAZLJitX64Test ..."
echo
output/GAZLJitX64Test
