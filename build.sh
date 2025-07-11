#!/bin/sh
set -e

mkdir -p output

# Build and test GAZLCmd beta
./tools/BuildCpp.sh beta native output/GAZLCmdBeta -I. GAZLCmd/GAZLCmd.cpp src/GAZL.cpp
./output/GAZLCmdBeta

# Build Impala (release tools and demo)
(cd tools && ./BuildImpala.sh)

# Copy release binary to output directory
cp tools/GAZLCmd output/GAZLCmd

# Run Impala tests
(cd impala && ./PikaCmd runTests.pika)


