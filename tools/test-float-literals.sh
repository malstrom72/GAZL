#!/usr/bin/env bash
# Build and run a sampled check of float literal rounding (tools/GAZLFloatLiteralTest.cpp): every 16411th float. Pass
# `0 0x7F800000` to check all of them (about 35 minutes on 32 threads, split into ranges). `beta` keeps asserts on.
# The .cmd twin does the same with MSVC.
set -e -o pipefail -u
cd "$(dirname "$0")/.."
mkdir -p output

echo "test-float-literals: building the checker"
( cd tools && bash BuildCpp.sh beta native ../output/GAZLFloatLiteralTest -I../src GAZLFloatLiteralTest.cpp ../src/GAZL.cpp )
if [ $# -ge 2 ]; then
	./output/GAZLFloatLiteralTest "$@"
else
	./output/GAZLFloatLiteralTest 0 0x7F800000 16411
fi
