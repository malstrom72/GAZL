#!/bin/sh
set -e

mkdir -p output

	# Build PikaCmd (release native)
	(cd tools/PikaCmd && ./BuildPikaCmd.sh)

	# Build and test GAZLCmd beta
	./tools/BuildCpp.sh beta native output/GAZLCmdBeta -I. GAZLCmd/GAZLCmd.cpp src/GAZL.cpp
	./output/GAZLCmdBeta

	# Build GAZLCmd release
	./tools/BuildCpp.sh release native output/GAZLCmd -I. GAZLCmd/GAZLCmd.cpp src/GAZL.cpp

	# Run Impala tests
	(cd impala && ../tools/PikaCmd/PikaCmd runTests.pika)


