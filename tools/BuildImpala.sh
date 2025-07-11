#!/bin/bash
set -e
cd "${0%/*}"

# Build PikaCmd
(cd PikaCmd && ./BuildPikaCmd.sh)

outdir=../output
mkdir -p "$outdir"

# Rebuild impala compiler using the local PikaCmd
(cd ../impala && ../tools/PikaCmd/PikaCmd impala.pika rebuild)

# Copy the compiler sources needed to run Impala
cp ../impala/impala.pika ../impala/impalaCompiler.pika \
    ../impala/initPPEG.pika ../impala/systools.pika "$outdir"


