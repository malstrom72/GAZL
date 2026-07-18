#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
# enterCall-from-native tests (blocking + non-blocking forward), interpreter only.
mkdir -p ../output
bash BuildCpp.sh release native ../output/GAZLEnterCallTest -I../src GAZLEnterCallTest.cpp ../src/GAZL.cpp
../output/GAZLEnterCallTest
