#!/usr/bin/env bash
# Usage: buildGazlFuzz.sh [standalone] [x64] [text]
#   (no args)   coverage-guided libFuzzer, JIT-vs-interp differential (generator-driven) -> ../output/GAZLFuzz
#   standalone  self-contained --gen driver for the differential fuzzer (no libFuzzer runtime) -> ../output/GAZLFuzz
#   x64         the differential fuzzer on the x86_64 backend, run under Rosetta on Apple Silicon -> ../output/GAZLFuzzX64
#   text        coverage-guided libFuzzer over GAZL SOURCE TEXT, assembled then diffed interp-vs-JIT -> ../output/GAZLFuzzText
# The (no-arg)/standalone/x64 modes decode the input bytes as a structured-generator choice stream (every input a valid
# program); `text` instead feeds the raw bytes to the assembler as source and runs the SAME interp-vs-JIT diff, so it
# reaches arbitrary/real programs the generator can't. libFuzzer needs a clang that ships the fuzzer runtime; Apple
# clang lacks it, so we prefer Homebrew LLVM.
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

[ "$cross" = 1 ] && standalone=1		# x64 libFuzzer can't link here (arm64-only libc++), so the cross build is always the --gen standalone driver

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
	# text->JIT: raw fuzz bytes are fed to the assembler as GAZL SOURCE, then run interp + JIT (FUZZ_TEXT_INPUT). Arbitrary
	# source can legally break the aliasing contract, so this lane does NOT diff - it is crash/assert coverage of the
	# assembler + JIT compiler + both engines on real/arbitrary programs. Inputs that don't assemble are skipped.
	if [ "$cross" = 1 ]; then
		# Rosetta x86_64 text lane. libFuzzer can't link x86_64 here (see the standalone note above), so this is a
		# STANDALONE corpus-REPLAY binary: point it at the arm64 text lane's corpus dir and it runs every program that
		# lane discovered through the x64 backend. No mutation of its own - arm64 mutates + grows the corpus, x64 replays
		# it for x64-backend-specific crashes (the arm64 text lane's blind spot). Default clang++ is fine (no fuzzer rt).
		: "${CPP_COMPILER:=clang++}"
		out=../output/GAZLFuzzTextX64
		"$CPP_COMPILER" -arch x86_64 -std=c++11 -O1 -g \
				-DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_JIT -DJITDIFF -DFUZZ_TEXT_INPUT -DGAZL_CANONICAL_NAN -I.. \
				-o "$out" GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLJit.cpp ../src/GAZLJitX64.cpp ../src/GAZLJitMemPosix.cpp
		chmod +x "$out" 2>/dev/null || true
		exit 0
	fi
	pick_libfuzzer_clang
	CPP_OPTIONS=${CPP_OPTIONS:-"-fsanitize=fuzzer -DLIBFUZZ -DGAZL_JIT -DJITDIFF -DFUZZ_TEXT_INPUT -DGAZL_CANONICAL_NAN $libcxxflags"}
	case "$(uname -m)" in
		arm64 | aarch64) backend=../src/GAZLJitArm64.cpp ;;
		x86_64) backend=../src/GAZLJitX64.cpp ;;
		*) echo "GAZLFuzzText: no JIT backend for '$(uname -m)'."; exit 1 ;;
	esac
	jitmem=../src/GAZLJitMemPosix.cpp
	[ "$(uname -s)" = "Darwin" ] && jitmem=../src/GAZLJitMemMacOS.cpp
	out=../output/GAZLFuzzText
	"$CPP_COMPILER" -std=c++11 -O1 -g $CPP_OPTIONS -I.. -o "$out" GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLJit.cpp "$backend" "$jitmem"
	chmod +x "$out" 2>/dev/null || true
	exit 0
fi

if [ "$standalone" = 1 ]; then
	# Self-contained --gen driver: no libFuzzer runtime needed, so the default clang++ (incl. Apple's) is fine.
	defopts="-O1 -g -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_JIT -DJITDIFF -DGAZL_CANONICAL_NAN"		# canonical NaN so interp/JIT agree bit-for-bit (unspecified NaN sign otherwise diverges on MSVC)
	: "${CPP_COMPILER:=clang++}"
else
	# Coverage-guided libFuzzer, JIT differential (fat runtime: x86_64 + arm64).
	pick_libfuzzer_clang
	defopts="-fsanitize=fuzzer -DLIBFUZZ -DGAZL_JIT -DJITDIFF -DGAZL_CANONICAL_NAN $libcxxflags"		# canonical NaN (see standalone); no ,address - ASan's macOS re-exec silently no-ops the binary on macOS 26 + Homebrew clang 21.

fi
CPP_OPTIONS=${CPP_OPTIONS:-$defopts}

jitmem=../src/GAZLJitMemPosix.cpp
[ "$(uname -s)" = "Darwin" ] && jitmem=../src/GAZLJitMemMacOS.cpp

if [ "$cross" = 1 ]; then
	# Cross build for x86_64 (Rosetta). Direct compile: BuildCpp.sh's "native" tuning would fight -arch x86_64.
	out=../output/GAZLFuzzX64
	"$CPP_COMPILER" -arch x86_64 -std=c++11 -O2 $CPP_OPTIONS -I.. \
			-o "$out" GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLJit.cpp ../src/GAZLJitX64.cpp ../src/GAZLJitMemPosix.cpp
else
	case "$(uname -m)" in
		arm64 | aarch64) backend=../src/GAZLJitArm64.cpp ;;
		x86_64) backend=../src/GAZLJitX64.cpp ;;
		*) echo "GAZLFuzz JIT-diff has no backend for '$(uname -m)'."; exit 1 ;;
	esac
	out=../output/GAZLFuzz
	CPP_COMPILER="$CPP_COMPILER" CPP_OPTIONS="$CPP_OPTIONS" \
			bash BuildCpp.sh beta native "$out" \
			-I.. GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLJit.cpp "$backend" "$jitmem"
fi
chmod +x "$out" 2>/dev/null || true
