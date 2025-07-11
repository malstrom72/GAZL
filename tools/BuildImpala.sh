#!/bin/bash
set -e
cd "${0%/*}"

# Build PikaCmd

(cd PikaCmd && ./BuildPikaCmd.sh)

outdir=../output
mkdir -p "$outdir"
cp PikaCmd/PikaCmd* "$outdir"/ 2>/dev/null || true

# Copy required source files
cp ../impala/impala.ppeg ../impala/impala.pika ../impala/initPPEG.pika ../impala/systools.pika "$outdir"

# Build impala compiler
(cd "$outdir" && ./PikaCmd impala.pika rebuild)


