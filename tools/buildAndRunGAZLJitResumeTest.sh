#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit RESUME-continuation test (tools/GAZLJitResumeTest.cpp): lowers GAZL functions with a
# §5.7.5 per-safepoint suspend stub (adr continuation + RESUME field), drives a one-field dispatcher loop, and checks
# suspend/resume against the interpreter (whole memory image). AArch64 only. Links the shipped VM READ-ONLY.
#
# Standalone by design (not wired into build.sh). clang++ direct. Override the compiler with CPP_COMPILER.

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
	tools/GAZLJitResumeTest.cpp \
	-o output/GAZLJitResumeTest

echo "running output/GAZLJitResumeTest ..."
echo
output/GAZLJitResumeTest
