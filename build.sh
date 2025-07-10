#!/bin/sh
set -e

# Build PikaCmd (release native)
(cd tools/PikaCmd && ../BuildCpp.sh release native ../../PikaCmd -DPLATFORM_STRING=UNIX PikaCmdAmalgam.cpp)

# Build and test GAZLCmd beta
./tools/BuildCpp.sh beta native ./GAZLCmdBeta -I. GAZLCmd/GAZLCmd.cpp src/GAZL.cpp
./GAZLCmdBeta

# Build GAZLCmd release
./tools/BuildCpp.sh release native GAZLCmd/GAZLCmd -I. GAZLCmd/GAZLCmd.cpp src/GAZL.cpp

# Run Impala tests
cp PikaCmd impala/
(cd impala && ./PikaCmd runTests.pika)


