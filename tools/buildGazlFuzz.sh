#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
mkdir -p ../output
CPP_COMPILER=${CPP_COMPILER:-clang++}
CPP_OPTIONS=${CPP_OPTIONS:-"-fsanitize=fuzzer,address -DLIBFUZZ -DGAZL_JIT -DJITDIFF"}
case "$(uname -m)" in
	arm64 | aarch64) backend=../src/GAZLJitArm64.cpp ;;
	x86_64) backend=../src/GAZLJitX64.cpp ;;
	*) echo "GAZLFuzz JIT-diff has no backend for '$(uname -m)'."; exit 1 ;;
esac
jitmem=../src/GAZLJitMemPosix.cpp
[ "$(uname -s)" = "Darwin" ] && jitmem=../src/GAZLJitMemMacOS.cpp
CPP_COMPILER="$CPP_COMPILER" CPP_OPTIONS="$CPP_OPTIONS" \
		bash BuildCpp.sh release native ../output/GAZLFuzz \
		-I.. GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLJit.cpp "$backend" "$jitmem"
chmod +x ../output/GAZLFuzz 2>/dev/null || true
