#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
mode=${1:-release}
mkdir -p ../output

# update unit test include
bash UpdateUnitTest.sh

if [ "$mode" = "beta" ]; then
    out="../output/GAZLCmdBeta"
else
    out="../output/GAZLCmd"
fi

# On AArch64 hosts, compile in the native JIT so `GAZLCmd --jit` works. The interpreter stays the default; on other
# architectures the JIT sources are omitted and --jit transparently falls back (see GAZL_JIT guards in GAZLCmd.cpp).
arch=$(uname -m)
if [ "$arch" = "arm64" ] || [ "$arch" = "aarch64" ]; then
	jitmem=../src/GAZLJitMemPosix.cpp
	[ "$(uname -s)" = "Darwin" ] && jitmem=../src/GAZLJitMemMacOS.cpp
	bash BuildCpp.sh "$mode" native "$out" -std=c++11 -DGAZL_JIT -I.. \
		GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp ../src/GAZLJit.cpp "$jitmem"
else
	bash BuildCpp.sh "$mode" native "$out" -std=c++11 -I.. GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp
fi
chmod +x "$out" 2>/dev/null || true

