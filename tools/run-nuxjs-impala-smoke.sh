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
invalid_source="$(mktemp "${TMPDIR:-/tmp}/gazl-nuxjs-invalid.XXXXXX.impala")"
error_log="$(mktemp "${TMPDIR:-/tmp}/gazl-nuxjs-error.XXXXXX")"
trap 'rm -f "$output_file" "$invalid_source" "$error_log"' EXIT

"$nuxjs" \
	"$(pwd)/impala/impala.nuxjs.js" \
	"$(pwd)/impala/testdata/smoke.impala" \
	"$output_file" \
	42 \
	smoke.impala \
	"$(pwd)/impala/impalaCompiler.js"

if [ ! -s "$output_file" ]; then
	echo "NuXJS smoke test produced no output." >&2
	exit 1
fi

printf 'function main()\nlocals int x\n{\n\tx();\n}\n' > "$invalid_source"
if "$nuxjs" "$(pwd)/impala/impala.nuxjs.js" "$invalid_source" - >"$error_log" 2>&1; then
	echo "NuXJS smoke test expected invalid Impala source to fail." >&2
	exit 1
fi
if ! grep -q "Invalid type for function call" "$error_log"; then
	echo "NuXJS smoke test did not preserve the Impala diagnostic." >&2
	cat "$error_log" >&2
	exit 1
fi
if grep -q "isFinite is not a function" "$error_log"; then
	echo "NuXJS smoke test hit an incompatible Number.isFinite diagnostic path." >&2
	cat "$error_log" >&2
	exit 1
fi

echo "NuXJS Impala compiler smoke test passed."
