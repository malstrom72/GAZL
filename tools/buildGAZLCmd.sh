#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
mode=${1:-release}
mkdir -p ../output

# update unit test include
bash UpdateUnitTest.sh

if [ "$mode" = "beta" ]; then
    out="../output/GAZLCmdBeta"
else
    out="../output/GAZLCmd"
fi

bash BuildCpp.sh "$mode" native "$out" -I.. GAZLCmd.cpp ../src/GAZL.cpp
chmod +x "$out" 2>/dev/null || true

