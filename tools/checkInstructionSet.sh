#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..

bash tools/BuildNuXJS.sh release native output/NuXJS
output/NuXJS tools/checkInstructionSet.nuxjs.js
