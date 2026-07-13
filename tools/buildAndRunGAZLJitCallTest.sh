#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the GAZLJit calls test (tools/GAZLJitCallTest.cpp): lowers a two-function GAZL program with GAZL->GAZL
# calls under the §5.4 dispatcher/segment model and checks it against the interpreter (whole memory image). AArch64
# only. Links the shipped VM READ-ONLY. Standalone; clang++ direct. Override the compiler with CPP_COMPILER.

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
	tools/GAZLJitCallTest.cpp \
	-o output/GAZLJitCallTest

echo "running output/GAZLJitCallTest ..."
echo
output/GAZLJitCallTest
