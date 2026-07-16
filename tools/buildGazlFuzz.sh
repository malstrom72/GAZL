#!/usr/bin/env bash
# Usage: buildGazlFuzz.sh [standalone]
# Default links libFuzzer (needs a toolchain with the fuzzer runtime; Apple clang lacks it). `standalone` builds the
# self-contained generative driver instead - no libFuzzer needed - driven by `../output/GAZLFuzz --gen COUNT [SEED] [deep]`.
set -e -o pipefail -u
cd "$(dirname "$0")"
mkdir -p ../output
CPP_COMPILER=${CPP_COMPILER:-clang++}
if [ "${1:-}" = "standalone" ]; then
	CPP_OPTIONS=${CPP_OPTIONS:-"-O1 -g -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_JIT -DJITDIFF"}
else
	CPP_OPTIONS=${CPP_OPTIONS:-"-fsanitize=fuzzer,address -DLIBFUZZ -DGAZL_JIT -DJITDIFF"}
fi
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
