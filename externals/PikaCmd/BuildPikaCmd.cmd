@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

IF NOT EXIST PikaCmd.exe (
	CALL BuildCpp.cmd PikaCmd.exe /D "PLATFORM_STRING=WINDOWS" PikaCmdAmalgam.cpp
	IF ERRORLEVEL 1 EXIT /B 1
	ECHO Testing...
	IF EXIST unittests.pika (
		.\PikaCmd unittests.pika >NUL
		IF ERRORLEVEL 1 (
			ECHO Unit tests failed
			EXIT /B 1
		)
	)
	IF EXIST systoolsTests.pika (
		.\PikaCmd systoolsTests.pika
		IF ERRORLEVEL 1 (
			ECHO Systools tests failed
			EXIT /B 1
		)
	)
	ECHO Success
)

EXIT /B 0
