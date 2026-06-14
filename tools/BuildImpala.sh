#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..

outdir=output
mkdir -p "$outdir"

# Build the NuXJS command-line runtime used to execute the generated compiler.
bash tools/BuildNuXJS.sh release native "$outdir/NuXJS"

# Copy the compiler sources needed to run Impala through NuXJS.
cp impala/impala.nuxjs.js impala/impalaCompiler.js "$outdir"

echo "Impala staged in $outdir using NuXJS."

