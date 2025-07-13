#!/bin/bash
set -e
cd "${0%/*}"

# Build PikaCmd
(cd ../externals/PikaCmd && ./BuildPikaCmd.sh)

# Copy PikaCmd to output so Impala can run from there
if [ -f ../externals/PikaCmd/PikaCmd ]; then
        cp ../externals/PikaCmd/PikaCmd ../output/PikaCmd
elif [ -f ../externals/PikaCmd/PikaCmd.exe ]; then
        cp ../externals/PikaCmd/PikaCmd.exe ../output/PikaCmd.exe
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

# Copy helper script
cp ../impala/impala.cmd "$outdir"/ 2>/dev/null || true


