#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs the Phase -1 spike A3 benchmark (interpreter vs hand-written baseline
# JIT). AArch64 only; see benchmarks/jit/README.md.

cd "$(dirname "$0")"/../..

mode=${1:-release}
arch=$(uname -m)
if [ "$arch" != "arm64" ] && [ "$arch" != "aarch64" ]; then
	echo "JitBenchA3 provides only an AArch64 hand-JIT; nothing to run on '$arch'. Skipping."
	exit 0
fi

CPP=${CPP_COMPILER:-clang++}
opt="-O2"
[ "$mode" = "debug" ] && opt="-O0"

mkdir -p output
"$CPP" $opt -DNDEBUG -std=c++17 -I src \
	src/GAZL.cpp \
	benchmarks/jit/JitBenchA3.cpp \
	benchmarks/jit/JitBenchA3.arm64.S \
	-o output/JitBenchA3

echo "running output/JitBenchA3 ..."
echo
output/JitBenchA3
