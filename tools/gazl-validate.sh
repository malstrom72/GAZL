#!/usr/bin/env bash
set -e -o pipefail -u

cd "$(dirname "$0")"/..

if [ ! -x "output/NuXJS" ]; then
	echo "Missing output/NuXJS. Run bash tools/BuildNuXJS.sh first." >&2
	exit 1
fi

"output/NuXJS" "tools/gazl-validate.js" "$@"
