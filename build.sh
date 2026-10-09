#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && bash buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && bash buildGAZLCmd.sh release)

# Every JIT gate, shared with build.cmd. Runs here, straight after the binary that carries the JIT, so a miscompile
# surfaces before the slower node suite rather than after it.
bash tools/test-jit.sh

# Replay the committed fuzz corpus and the fixed-crash inputs, shared with build.cmd. A regression gate, not a fuzz
# run: it cannot find anything new, it stops what has been found from coming back.
bash tools/test-fuzz.sh

# Every node-only gate, shared with build.cmd so the two cannot run different subsets.
bash tools/test-js.sh

# Build Impala
bash tools/BuildImpala.sh

# Verify the staged Impala compiler by compiling with NuXJS and running with GAZLCmd.
./output/NuXJS output/impala.nuxjs.js \
	impala/ImpalaDemo.impala output/ImpalaDemo.gazl 0x4d2 impala/ImpalaDemo.impala
./output/GAZLCmd output/ImpalaDemo.gazl main

# The staging above only proves the compiler fits TODAY's nesting limit. This fails while there is still headroom,
# because the limit is NuXJS's to change: when it dropped to 48, GAZL stopped building with no prior warning.
bash tools/checkImpalaNesting.sh

# ImpalaDemo imports nothing, so it cannot tell whether the closure walk survived staging.
bash tools/run-nuxjs-impala-smoke.sh
