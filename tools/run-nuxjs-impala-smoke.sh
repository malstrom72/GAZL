#!/usr/bin/env bash
set -e -o pipefail -u

cd "$(dirname "$0")"/..

if [ "${NUXJS:-}" != "" ]; then
	nuxjs="$NUXJS"
elif [ -x "output/NuXJS" ]; then
	nuxjs="output/NuXJS"
elif command -v NuXJS >/dev/null 2>&1; then
	nuxjs="$(command -v NuXJS)"
else
	echo "Skipping NuXJS smoke test: set NUXJS or build/provide output/NuXJS."
	exit 0
fi

output_file="$(mktemp "${TMPDIR:-/tmp}/gazl-nuxjs-smoke.XXXXXX")"
trap 'rm -f "$output_file"' EXIT

"$nuxjs" \
	"$(pwd)/impala/impala.nuxjs.js" \
	"$(pwd)/impala/testdata/smoke.impala" \
	42 \
	smoke.impala \
	"$(pwd)/impala/impalaCompiler.js" > "$output_file"

if [ ! -s "$output_file" ]; then
	echo "NuXJS smoke test produced no output." >&2
	exit 1
fi

echo "NuXJS Impala compiler smoke test passed."
