#!/bin/bash

cd ${0%/*}

cd PikaCmd
chmod +x BuildPikaCmd.sh >/dev/null 2>&1
./BuildPikaCmd.sh
if [ $? -ne 0 ]; then
	exit 1
fi
cd ..

if [ -e ./GAZLCmd ]; then
	chmod +x ./GAZLCmd >/dev/null 2>&1
else
	chmod +x ./UpdateUnitTest.sh >/dev/null 2>&1
	./UpdateUnitTest.sh
	if [ $? -ne 0 ]; then
		exit 1
	fi
	chmod +x ./BuildCpp.sh >/dev/null 2>&1
	./BuildCpp.sh ./GAZLCmd ../GAZLCmd/GAZLCmd.cpp ../src/GAZL.cpp
	if [ $? -ne 0 ]; then
		exit 1
	fi
fi

cp -f ./GAZLCmd ../impala/ >/dev/null
cp -f PikaCmd/PikaCmd ../impala/ >/dev/null
cp -f PikaCmd/systools.pika ../impala/ >/dev/null
cd ../impala/
./PikaCmd impala.pika rebuild
./PikaCmd impala.pika run ImpalaDemo.impala
exit 0
