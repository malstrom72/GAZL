#!/usr/bin/env bash
# Build and run the deterministic boundary sweep of every memory instruction form (tools/GAZLBoundsSweep.cpp). `beta`
# keeps asserts on. The .cmd twin does the same with MSVC.
set -e -o pipefail -u
cd "$(dirname "$0")/.."
mkdir -p output

echo "test-bounds: building the sweep"
( cd tools && bash BuildCpp.sh beta native ../output/GAZLBoundsSweep -I../src GAZLBoundsSweep.cpp ../src/GAZL.cpp )
./output/GAZLBoundsSweep
