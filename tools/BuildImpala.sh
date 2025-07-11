#!/bin/bash
set -e
cd "${0%/*}"

# Build PikaCmd
(cd PikaCmd && ./BuildPikaCmd.sh)

mkdir -p ../output/impala

# Copy source scripts
rsync -a --delete ../impala/ ../output/impala/

# Copy tools
cp PikaCmd/PikaCmd ../output/impala/
cp PikaCmd/systools.pika ../output/impala/
cp ../output/GAZLCmd ../output/impala/GAZLCmd

# Build impala compiler
(cd ../output/impala && ./PikaCmd impala.pika rebuild)

# Run demo
(cd ../output/impala && ./PikaCmd impala.pika run ImpalaDemo.impala)


