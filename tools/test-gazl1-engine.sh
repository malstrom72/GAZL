#!/usr/bin/env bash
# Build GAZLCmd as a GAZL 1.0-compatible engine (`GAZL_2=0`) and run it, so its built-in unit test runs. `beta` keeps
# asserts on. The .cmd twin does the same with MSVC.
set -e -o pipefail -u
cd "$(dirname "$0")/.."
mkdir -p output

echo "test-gazl1-engine: building GAZLCmd with GAZL_2=0"
( cd tools && bash BuildCpp.sh beta native ../output/GAZLCmdGazl1Beta -DGAZL_2=0 -I.. GAZLCmd.cpp ../src/GAZL.cpp \
		../src/GAZLCpp.cpp )
./output/GAZLCmdGazl1Beta
