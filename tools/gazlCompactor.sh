#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..

if [ "$#" -ne 2 ]; then
	echo "Usage: bash tools/gazlCompactor.sh input.gazl output.gazl" >&2
	exit 1
fi

bash tools/BuildNuXJS.sh release native output/NuXJS
output/NuXJS tools/gazlCompactor.nuxjs.js "$1" "$2"
