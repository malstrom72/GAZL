#!/bin/sh

if [ -e ./PikaCmd ]; then
	chmod +x ./PikaCmd >/dev/null 2>&1
else
	./BuildCpp.sh ./PikaCmd -DPLATFORM_STRING=UNIX PikaCmdAmalgam.cpp
	if [ $? -ne 0 ]; then
		exit 1
	fi
	echo Testing...
	if [ -e ./unittests.pika ]; then
		./PikaCmd unittests.pika >/dev/null
		if [ $? -ne 0 ]; then
			echo Unit tests failed
			exit 1
		fi
	fi
	if [ -e ./systoolsTests.pika ]; then
		./PikaCmd systoolsTests.pika
		if [ $? -ne 0 ]; then
			echo Systools tests failed
			exit 1
		fi
	fi
	echo Success
fi

exit 0
