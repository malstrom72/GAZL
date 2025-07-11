#!/bin/bash
set -e
cd "${0%/*}"

# Build PikaCmd
(cd PikaCmd && ./BuildPikaCmd.sh)

outdir=../output
mkdir -p "$outdir"

# Copy required source files
cp ../impala/impala.ppeg ../impala/impala.pika ../impala/initPPEG.pika ../impala/systools.pika "$outdir"

# Build impala compiler
(cd "$outdir" && ../tools/PikaCmd/PikaCmd impala.pika rebuild)


