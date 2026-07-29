#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..

outdir=output
mkdir -p "$outdir"

# Build the NuXJS command-line runtime used to execute the generated compiler.
bash tools/BuildNuXJS.sh release native "$outdir/NuXJS"

# Copy the compiler sources needed to run Impala through NuXJS. impala.nuxjs.js load()s
# impalaImportClosure.js from its own directory, so all three stage together.
cp impala/impala.nuxjs.js impala/impalaImportClosure.js impala/impalaCompiler.js "$outdir"

echo "Impala staged in $outdir using NuXJS."

