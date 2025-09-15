#!/usr/bin/env bash
set -e -o pipefail -u
cd "$(dirname "$0")"

if [ -e ./PikaCmd ]; then
       :
else
       # Use the portable build script from SourceDistribution to avoid
       # platform-specific flags such as `-target`
       bash ./SourceDistribution/BuildCpp.sh ./PikaCmd -DPLATFORM_STRING=UNIX PikaCmd.cpp BuiltIns.cpp ../../src/*.cpp
       if [ $? -ne 0 ]; then
               exit 1
       fi
fi

./PikaCmd UpdateBuiltIns.pika
if [ $? -ne 0 ]; then
	echo Failed updating built-in files
	exit 1
fi

cat ../../src/PikaScript.h ../../src/PikaScriptImpl.h ../../src/QStrings.h ../../src/QuickVars.h ../../src/PikaScript.cpp ../../src/QStrings.cpp BuiltIns.cpp PikaCmd.cpp >SourceDistribution/PikaCmdAmalgam.cpp
if [ $? -ne 0 ]; then
	echo Failed concatenating files
	exit 1
fi

cd SourceDistribution
cp -f ../../../tests/unittests.pika . \
		&& cp -f ../systools.pika . \
		&& cp -f ../../../tests/systoolsTests.pika . \
		&& cp -f ../../../LICENSE .
if [ $? -ne 0 ]; then
        echo Failed copying files
        exit 1
fi

rm -f ./PikaCmd
bash ./BuildPikaCmd.sh
if [ $? -ne 0 ]; then
	exit 1
fi

cp -f ./PikaCmd ../
cd ..

exit 0
