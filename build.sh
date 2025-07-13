#!/bin/sh
set -e
cd "$(dirname "$0")"

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && ./buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && ./buildGAZLCmd.sh release)
cp output/GAZLCmd impala/GAZLCmd 2>/dev/null || cp output/GAZLCmd.exe impala/GAZLCmd.exe

# Build Impala
(cd tools && ./BuildImpala.sh)

# Run the Impala test suite from the source directory
(cd impala && ../output/PikaCmd runTests.pika)

# Verify the copied files by running the demo from the output directory
(cd output && ./PikaCmd impala.pika run ../impala/ImpalaDemo.impala)

