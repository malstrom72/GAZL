#!/bin/sh
set -e

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && ./buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && ./buildGAZLCmd.sh release)

# Build Impala and run demo
(cd tools && ./BuildImpala.sh)

# Run Impala tests
(cd output && ./PikaCmd runTests.pika)

