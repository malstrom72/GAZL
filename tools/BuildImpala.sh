#!/bin/bash
set -e
cd "${0%/*}"

# Build PikaCmd
(cd PikaCmd && ./BuildPikaCmd.sh)

outdir=../output
mkdir -p "$outdir"

# Copy required source files
cp ../impala/impala.ppeg ../impala/impala.pika ../impala/initPPEG.pika "$outdir"
cp ../impala/runTests.pika ../impala/systools.pika "$outdir"
cp ../impala/ImpalaDemo.impala "$outdir"
rsync -a --delete ../impala/tests "$outdir"/

# Copy tools
cp PikaCmd/PikaCmd "$outdir"/
cp PikaCmd/systools.pika "$outdir"/

# Build impala compiler
(cd "$outdir" && ./PikaCmd impala.pika rebuild)

# Run demo
(cd "$outdir" && ./PikaCmd impala.pika run ImpalaDemo.impala)


