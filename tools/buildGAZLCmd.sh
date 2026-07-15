#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
mode=${1:-release}
target=${2:-native}		# native (host) | x64 - x64 cross-builds -arch x86_64 (runs under Rosetta on Apple Silicon)
mkdir -p ../output

# update unit test include
bash UpdateUnitTest.sh

base="../output/GAZLCmd"
[ "$mode" = "beta" ] && base="../output/GAZLCmdBeta"

# Pick the JIT backend by the TARGET architecture. `GAZLCmd.cpp` is arch-neutral (it uses JitCompiler / JitProcessor);
# only the backend .cpp differs (GAZLJitArm64.cpp vs GAZLJitX64.cpp) plus the W^X memory backend. On other targets the
# JIT sources are omitted and --jit transparently falls back to the interpreter (see the GAZL_JIT guards in GAZLCmd.cpp).
buildarch="$target"
if [ "$target" = "native" ]; then
	case "$(uname -m)" in
		arm64 | aarch64) buildarch=arm64 ;;
		x86_64) buildarch=x64 ;;
		*) buildarch=other ;;
	esac
fi

if [ "$buildarch" = "arm64" ]; then
	out="$base"
	jitmem=../src/GAZLJitMemPosix.cpp
	[ "$(uname -s)" = "Darwin" ] && jitmem=../src/GAZLJitMemMacOS.cpp
	bash BuildCpp.sh "$mode" native "$out" -std=c++11 -DGAZL_JIT -I.. \
		GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp ../src/GAZLJit.cpp ../src/GAZLJitArm64.cpp "$jitmem"
elif [ "$buildarch" = "x64" ]; then
	# distinct name so an x64 (Rosetta) build sits next to the native arm64 one for side-by-side runs. Rosetta's W^X is
	# plain mmap+mprotect, so the Posix memory backend is the right one here.
	out="${base}_x64"
	buildtarget=native
	[ "$(uname -m)" != "x86_64" ] && buildtarget=x64		# cross-build to x64 on a non-x64 host (Apple Silicon)
	bash BuildCpp.sh "$mode" "$buildtarget" "$out" -std=c++11 -DGAZL_JIT -I.. \
		GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp ../src/GAZLJit.cpp ../src/GAZLJitX64.cpp ../src/GAZLJitMemPosix.cpp
else
	out="$base"
	bash BuildCpp.sh "$mode" native "$out" -std=c++11 -I.. GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp
fi
chmod +x "$out" 2>/dev/null || true
