#!/bin/sh
set -e
cd "$(dirname "$0")"
mode=${1:-release}
mkdir -p ../output

# update unit test include
../tools/UpdateUnitTest.sh

if [ "$mode" = "beta" ]; then
    out="../output/GAZLCmdBeta"
else
    out="../output/GAZLCmd"
fi

../tools/BuildCpp.sh "$mode" native "$out" -I.. GAZLCmd.cpp ../src/GAZL.cpp
chmod +x "$out" 2>/dev/null || true

