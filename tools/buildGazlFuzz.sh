#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
mkdir -p ../output

# Apple's clang ships NO libFuzzer, so prefer Homebrew LLVM when the caller named no compiler; a plain `clang++` on
# macOS fails to link -fsanitize=fuzzer. That LLVM's fuzzer runtime also references newer std::__1 symbols than Apple's
# system libc++ exports, so link against its own libc++ when the directory is there.
libcxxflags=""
if [ -z "${CPP_COMPILER:-}" ]; then
	for c in /opt/homebrew/opt/llvm/bin/clang++ /usr/local/opt/llvm/bin/clang++; do
		[ -x "$c" ] && { CPP_COMPILER=$c; break; }
	done
fi
: "${CPP_COMPILER:=clang++}"
libcxx="$(dirname "$(dirname "$CPP_COMPILER")")/lib/c++"
[ -d "$libcxx" ] && libcxxflags="-L$libcxx -Wl,-rpath,$libcxx"

CPP_OPTIONS=${CPP_OPTIONS:-"-fsanitize=fuzzer,address -DLIBFUZZ $libcxxflags"}
CPP_COMPILER="$CPP_COMPILER" CPP_OPTIONS="$CPP_OPTIONS" \
		bash BuildCpp.sh release native ../output/GAZLFuzz \
		-I.. GAZLCmd.cpp ../src/GAZL.cpp
chmod +x ../output/GAZLFuzz 2>/dev/null || true
