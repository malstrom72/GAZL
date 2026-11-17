#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"
# enterCall and pushCall tests: calls from natives (blocking + non-blocking forward) and interrupts on a suspended
# processor. On the interpreter; buildAndRunGAZLEnterCallTestJit.sh runs them on the JIT. `beta` keeps asserts on.
mkdir -p ../output
bash BuildCpp.sh beta native ../output/GAZLEnterCallTest -I../src GAZLEnterCallTest.cpp ../src/GAZL.cpp
../output/GAZLEnterCallTest
