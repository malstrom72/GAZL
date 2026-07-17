#!/usr/bin/env bash
# Usage: buildGazlFuzz.sh [standalone] [x64] [text]
#   (no args)   coverage-guided libFuzzer, JIT-vs-interp differential (generator-driven) -> ../output/GAZLFuzz
#   standalone  self-contained --gen driver for the differential fuzzer (no libFuzzer runtime) -> ../output/GAZLFuzz
#   x64         the differential fuzzer on the x86_64 backend, run under Rosetta on Apple Silicon -> ../output/GAZLFuzzX64
#   text        coverage-guided libFuzzer over GAZL SOURCE TEXT (mutates the assembler's input; no JIT) -> ../output/GAZLFuzzText
# The differential modes decode the input bytes as a structured-generator choice stream (every input a valid program);
# `text` is the original pre-JIT harness that feeds the raw bytes to the assembler - it fuzzes the assembler/interpreter,
# not the JIT. libFuzzer needs a clang that ships the fuzzer runtime; Apple clang lacks it, so we prefer Homebrew LLVM.
set -e -o pipefail -u
cd "$(dirname "$0")"
mkdir -p ../output

standalone=0; cross=0; text=0
for a in "$@"; do
	case "$a" in
		standalone) standalone=1 ;;
		x64) cross=1 ;;
		text) text=1 ;;
		*) echo "buildGazlFuzz.sh: unknown option '$a' (expected standalone / x64 / text)"; exit 1 ;;
	esac
done

# Resolve a libFuzzer-capable clang (Homebrew LLVM) and the flags to link ITS libc++ - the fuzzer runtime references
# newer std::__1 symbols (__hash_memory) absent from Apple's system libc++, so the link needs <prefix>/lib/c++.
libcxxflags=""
pick_libfuzzer_clang() {
	if [ -z "${CPP_COMPILER:-}" ]; then
		for c in /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++; do
			[ -x "$c" ] && { CPP_COMPILER=$c; break; }
		done
	fi
	: "${CPP_COMPILER:=clang++}"
	local libcxx; libcxx="$(dirname "$(dirname "$CPP_COMPILER")")/lib/c++"
	[ -d "$libcxx" ] && libcxxflags="-L$libcxx -Wl,-rpath,$libcxx"
}

if [ "$text" = 1 ]; then
	# GAZL source-text fuzzer: no JIT, no JITDIFF (the pre-JIT harness at GAZLCmd.cpp's `#else`); libFuzzer supplies main.
	pick_libfuzzer_clang
	CPP_OPTIONS=${CPP_OPTIONS:-"-fsanitize=fuzzer,address -DLIBFUZZ $libcxxflags"}
	out=../output/GAZLFuzzText
	"$CPP_COMPILER" -std=c++11 -O1 -g $CPP_OPTIONS -I.. -o "$out" GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp
	chmod +x "$out" 2>/dev/null || true
	exit 0
fi

if [ "$standalone" = 1 ]; then
	# Self-contained --gen driver: no libFuzzer runtime needed, so the default clang++ (incl. Apple's) is fine.
	defopts="-O1 -g -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_JIT -DJITDIFF"
	: "${CPP_COMPILER:=clang++}"
else
	# Coverage-guided libFuzzer, JIT differential (fat runtime: x86_64 + arm64).
	pick_libfuzzer_clang
	defopts="-fsanitize=fuzzer,address -DLIBFUZZ -DGAZL_JIT -DJITDIFF $libcxxflags"
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
