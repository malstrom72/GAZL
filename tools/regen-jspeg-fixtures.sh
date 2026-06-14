#!/usr/bin/env bash
set -e -o pipefail -u

# Regenerate impala/testdata/*.expected.gazl from their .impala sources
# using the JSPEG Impala compiler.

# Move to repo root regardless of invocation directory
cd "$(dirname "$0")"/..

compiler_script="impala/impala.node.js"
testdir="impala/testdata"
seed=42 # match jspegCompilerTests.js

if [ ! -f "$compiler_script" ]; then
    echo "Missing $compiler_script. Run 'node impala/updateJSPEG.js' first." >&2
    exit 1
fi

shopt -s nullglob
changed=0
for src in "$testdir"/*.impala; do
    out="${src%.impala}.expected.gazl"
    node "$compiler_script" compile "$src" "$out" "$seed" >/dev/null
    echo "Rebuilt $out"
    changed=1
done

if [ $changed -eq 0 ]; then
    echo "No .impala sources found in $testdir" >&2
    exit 1
fi

gazl_files=("$testdir"/*.expected.gazl)
if [ ${#gazl_files[@]} -eq 0 ]; then
    echo "No .expected.gazl outputs found in $testdir" >&2
    exit 1
fi

node ./tools/gazl-validate.js "${gazl_files[@]}"

exit 0
