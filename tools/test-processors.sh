#!/usr/bin/env bash
# Build and run the multiple-processor test (tools/GAZLMultiProcessorTest.cpp): several processors in one memory,
# cooperative and on threads, each with its own part of the data stack. `beta` keeps asserts on. The .cmd twin does
# the same with MSVC.
set -e -o pipefail -u
cd "$(dirname "$0")/.."
mkdir -p output

echo "test-processors: building the test"
( cd tools && bash BuildCpp.sh beta native ../output/GAZLMultiProcessorTest -pthread -I../src GAZLMultiProcessorTest.cpp ../src/GAZL.cpp )
./output/GAZLMultiProcessorTest
