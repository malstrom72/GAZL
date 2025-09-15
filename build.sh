#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

mkdir -p output

# Build and test GAZLCmd beta
(cd tools && bash buildGAZLCmd.sh beta)
./output/GAZLCmdBeta

# Build GAZLCmd release
(cd tools && bash buildGAZLCmd.sh release)
cp output/GAZLCmd impala/GAZLCmd 2>/dev/null || cp output/GAZLCmd.exe impala/GAZLCmd.exe

# Build Impala
(cd tools && bash BuildImpala.sh)

# Run the Impala test suite from the source directory
 (cd impala && ../output/PikaCmd runTests.pika && (cd jspeg && node jspegCompilerTests.js))

# Verify the copied files by running the demo from the output directory
(cd output && ./PikaCmd impala.pika run ../impala/ImpalaDemo.impala)

