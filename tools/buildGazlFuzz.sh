#!/usr/bin/env bash
# Usage: buildGazlFuzz.sh [standalone] [x64]
#   (no args)   coverage-guided libFuzzer for the host backend      -> ../output/GAZLFuzz
#   standalone  self-contained --gen driver (no libFuzzer runtime)  -> ../output/GAZLFuzz
#   x64         the x86_64 backend, to run under Rosetta on Apple Silicon -> ../output/GAZLFuzzX64
# libFuzzer needs a clang that ships the fuzzer runtime; Apple clang lacks it, so we prefer Homebrew LLVM. The generator
# grammar is arch-neutral, so ONE corpus seeds both backends - only the JIT under test differs.
set -e -o pipefail -u
cd "$(dirname "$0")"
mkdir -p ../output

standalone=0; cross=0
for a in "$@"; do
	case "$a" in
		standalone) standalone=1 ;;
		x64) cross=1 ;;
		*) echo "buildGazlFuzz.sh: unknown option '$a' (expected 'standalone' and/or 'x64')"; exit 1 ;;
	esac
done

if [ "$standalone" = 1 ]; then
	# Self-contained --gen driver: no libFuzzer runtime needed, so the default clang++ (incl. Apple's) is fine.
	defopts="-O1 -g -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_JIT -DJITDIFF"
	: "${CPP_COMPILER:=clang++}"
else
	# Coverage-guided libFuzzer needs the fuzzer runtime; prefer Homebrew LLVM (its runtime is fat: x86_64 + arm64).
	defopts="-fsanitize=fuzzer,address -DLIBFUZZ -DGAZL_JIT -DJITDIFF"
	if [ -z "${CPP_COMPILER:-}" ]; then
		for c in /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++; do
			[ -x "$c" ] && { CPP_COMPILER=$c; break; }
		done
	fi
	: "${CPP_COMPILER:=clang++}"
fi
CPP_OPTIONS=${CPP_OPTIONS:-$defopts}

jitmem=../src/GAZLJitMemPosix.cpp
[ "$(uname -s)" = "Darwin" ] && jitmem=../src/GAZLJitMemMacOS.cpp

if [ "$cross" = 1 ]; then
	# Cross build for x86_64 (Rosetta). Direct compile: BuildCpp.sh's "native" tuning would fight -arch x86_64.
	out=../output/GAZLFuzzX64
	"$CPP_COMPILER" -arch x86_64 -std=c++11 $CPP_OPTIONS -I.. \
			-o "$out" GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLJit.cpp ../src/GAZLJitX64.cpp "$jitmem"
else
	case "$(uname -m)" in
		arm64 | aarch64) backend=../src/GAZLJitArm64.cpp ;;
		x86_64) backend=../src/GAZLJitX64.cpp ;;
		*) echo "GAZLFuzz JIT-diff has no backend for '$(uname -m)'."; exit 1 ;;
	esac
	out=../output/GAZLFuzz
	CPP_COMPILER="$CPP_COMPILER" CPP_OPTIONS="$CPP_OPTIONS" \
			bash BuildCpp.sh release native "$out" \
			-I.. GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLJit.cpp "$backend" "$jitmem"
fi
chmod +x "$out" 2>/dev/null || true
