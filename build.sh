#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && bash buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && bash buildGAZLCmd.sh release)

# Every node-only gate, shared with build.cmd so the two cannot run different subsets.
bash tools/test-js.sh

# Build Impala
bash tools/BuildImpala.sh

# Verify the staged Impala compiler by compiling with NuXJS and running with GAZLCmd.
./output/NuXJS output/impala.nuxjs.js \
	impala/ImpalaDemo.impala output/ImpalaDemo.gazl 0x4d2 impala/ImpalaDemo.impala
./output/GAZLCmd output/ImpalaDemo.gazl main

# ImpalaDemo imports nothing, so it cannot tell whether the closure walk survived staging.
bash tools/run-nuxjs-impala-smoke.sh
