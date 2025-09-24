#!/usr/bin/env bash
set -e -o pipefail -u

# Regenerate impala/jspeg/testdata/*.pika.gazl from their .impala sources
# using the PPEG PikaScript-based Impala compiler (via output/PikaCmd).

# Move to repo root regardless of invocation directory
cd "$(dirname "$0")"/..

cmd="./output/PikaCmd"
script="impala.pika"
testdir="impala/jspeg/testdata"
seed=42 # match jspegCompilerTests.js

if [ ! -x "$cmd" ]; then
    echo "Missing $cmd. Run 'bash build.sh' first." >&2
    exit 1
fi

shopt -s nullglob
changed=0
for src in "$testdir"/*.impala; do
    out="${src%.impala}.pika.gazl"
    "$cmd" "$script" compile "$src" "$out" "$seed" >/dev/null
    echo "Rebuilt $out"
    changed=1
done

if [ $changed -eq 0 ]; then
    echo "No .impala sources found in $testdir" >&2
    exit 1
fi

gazl_files=("$testdir"/*.pika.gazl)
if [ ${#gazl_files[@]} -eq 0 ]; then
    echo "No .pika.gazl outputs found in $testdir" >&2
    exit 1
fi

node ./tools/gazl-validate.js "${gazl_files[@]}"

exit 0
