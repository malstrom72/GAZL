#!/bin/bash
set -e
cd "${0%/*}"

# Build PikaCmd
(cd PikaCmd && ./BuildPikaCmd.sh)

# Copy PikaCmd to output so Impala can run from there
if [ -f PikaCmd/PikaCmd ]; then
	cp PikaCmd/PikaCmd ../output/PikaCmd
elif [ -f PikaCmd/PikaCmd.exe ]; then
	cp PikaCmd/PikaCmd.exe ../output/PikaCmd.exe
fi

outdir=../output
mkdir -p "$outdir"

# Rebuild impala compiler using the local PikaCmd
if [ -f ../output/PikaCmd ]; then
	pkcmd=../output/PikaCmd
else
	pkcmd=../output/PikaCmd.exe
fi
(cd ../impala && "$pkcmd" impala.pika rebuild)

# Copy the compiler sources needed to run Impala
	cp ../impala/impala.pika ../impala/impalaCompiler.pika \
    ../impala/initPPEG.pika ../impala/systools.pika "$outdir"


