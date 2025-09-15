#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"/..

mkdir -p output

CPP_COMPILER="${CPP_COMPILER:-clang++}"
CPP_OPTIONS="-std=c++14 -fsanitize=fuzzer,address"
export CPP_COMPILER CPP_OPTIONS

bash tools/PikaCmd/SourceDistribution/BuildCpp.sh debug native output/PikaCmdFuzz \
                -DLIBFUZZ -DPLATFORM_STRING=UNIX -I src \
                tools/PikaCmd/PikaCmd.cpp tools/PikaCmd/BuiltIns.cpp src/PikaScript.cpp src/QStrings.cpp

