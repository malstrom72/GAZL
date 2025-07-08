#!/bin/bash

cd ${0%/*}

cd PikaCmd
chmod +x BuildPikaCmd.sh >/dev/null
./BuildPikaCmd.sh
if [ $? -ne 0 ]; then
	exit 1
fi
cd ..

PikaCmd/PikaCmd UpdateUnitTest.pika
if [ $? -ne 0 ]; then
	echo Failed updating unit test
	exit 1
fi

exit 0
