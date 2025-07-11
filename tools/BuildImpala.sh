#!/bin/bash
set -e
cd "${0%/*}"

# Build PikaCmd
(cd PikaCmd && ./BuildPikaCmd.sh)

outdir=../output
mkdir -p "$outdir"
cp PikaCmd/PikaCmd* "$outdir"/ 2>/dev/null || true

# Rebuild impala compiler in its folder
(cd ../impala && ../output/PikaCmd impala.pika rebuild)

# Copy compiler files and tests to output
cp ../impala/impala.pika ../impala/impalaCompiler.pika \
../impala/initPPEG.pika ../impala/systools.pika \
../impala/runTests.pika ../impala/ImpalaDemo.impala "$outdir"
cp -r ../impala/tests "$outdir"/


