#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && bash buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && bash buildGAZLCmd.sh release)

# Replay the committed fuzz corpus and the fixed-crash inputs, shared with build.cmd. Runs here, straight after the
# engine it exercises, so a regression surfaces before the slower node suite rather than after it.
bash tools/test-fuzz.sh

# Every memory instruction form at every edge of the memory arena, against an oracle (tools/GAZLBoundsSweep.cpp).
bash tools/test-bounds.sh

# Float literals round correctly, sampled across every float (tools/GAZLFloatLiteralTest.cpp).
bash tools/test-float-literals.sh

# Several processors in one memory, cooperative and on threads (tools/GAZLMultiProcessorTest.cpp).
bash tools/test-processors.sh

# Every node-only gate, shared with build.cmd so the two cannot run different subsets.
bash tools/test-js.sh

# Build Impala
bash tools/BuildImpala.sh

# Verify the staged Impala compiler by compiling with NuXJS and running with GAZLCmd.
./output/NuXJS output/impala.nuxjs.js \
	impala/ImpalaDemo.impala output/ImpalaDemo.gazl 0x4d2 impala/ImpalaDemo.impala
./output/GAZLCmd output/ImpalaDemo.gazl main

# Impala keeps named constants in its output, so a golden that only matches text can still fail to assemble.
./output/GAZLCmd tests/impala/golden/pointerConstants.gazl main

# The staging above only proves the compiler fits TODAY's nesting limit. This fails while there is still headroom,
# because the limit is NuXJS's to change: when it dropped to 48, GAZL stopped building with no prior warning.
bash tools/checkImpalaNesting.sh

# ImpalaDemo imports nothing, so it cannot tell whether the closure walk survived staging.
bash tools/run-nuxjs-impala-smoke.sh
