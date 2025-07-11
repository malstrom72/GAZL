#!/bin/sh
set -e

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && ./buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && ./buildGAZLCmd.sh release)
cp output/GAZLCmd impala/GAZLCmd 2>/dev/null || cp output/GAZLCmd.exe impala/GAZLCmd.exe

# Build Impala
(cd tools && ./BuildImpala.sh)

# Run demo and tests from the impala directory
(cd impala && ../output/PikaCmd impala.pika run ImpalaDemo.impala)
(cd impala && ../output/PikaCmd runTests.pika)

