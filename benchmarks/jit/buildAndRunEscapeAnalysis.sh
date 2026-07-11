#!/usr/bin/env bash
set -e -o pipefail -u

# Builds and runs JitEscapeAnalysis over the golden Impala corpus. Measures how much the
# "escape floor" register-allocation design (docs/JitCompilerResearch.md 1.1/5.7) pessimizes
# hot scalar accesses, to decide escape-floor vs barrier caching. See benchmarks/jit/README.md.

cd "$(dirname "$0")"/../..

CPP=${CPP_COMPILER:-clang++}
mkdir -p output
"$CPP" -O2 -std=c++17 benchmarks/jit/JitEscapeAnalysis.cpp -o output/JitEscapeAnalysis
output/JitEscapeAnalysis tests/impala/golden
