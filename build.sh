#!/bin/sh
set -e

mkdir -p output

# Build and test GAZLCmd beta
(cd GAZLCmd && ./buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd GAZLCmd && ./buildGAZLCmd.sh release)

# Build Impala and run demo
(cd tools && ./BuildImpala.sh)

# Run Impala tests
(cd output/impala && ./PikaCmd runTests.pika)

