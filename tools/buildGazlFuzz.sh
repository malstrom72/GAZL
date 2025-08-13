#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
mkdir -p ../output
CPP_COMPILER=${CPP_COMPILER:-clang++}
CPP_OPTIONS=${CPP_OPTIONS:-"-fsanitize=fuzzer,address -DLIBFUZZ"}
CPP_COMPILER="$CPP_COMPILER" CPP_OPTIONS="$CPP_OPTIONS" \
		bash BuildCpp.sh release native ../output/GAZLFuzz \
		-I.. GAZLCmd.cpp ../src/GAZL.cpp
chmod +x ../output/GAZLFuzz 2>/dev/null || true
