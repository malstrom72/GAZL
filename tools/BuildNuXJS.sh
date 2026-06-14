#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..

mode=${1:-release}
model=${2:-native}
out=${3:-output/NuXJS}

mkdir -p "$(dirname "$out")"

bash tools/BuildCpp.sh "$mode" "$model" "$out" \
	externals/NuXJS/tools/NuXJSREPL.cpp \
	externals/NuXJS/src/NuXJS.cpp \
	externals/NuXJS/src/stdlibJS.cpp
chmod +x "$out" 2>/dev/null || true
