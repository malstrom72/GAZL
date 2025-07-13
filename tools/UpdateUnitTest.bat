@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

PUSHD ..\externals\PikaCmd
CALL BuildPikaCmd
IF ERRORLEVEL 1 EXIT /B 1
POPD

..\externals\PikaCmd\PikaCmd UpdateUnitTest.pika
IF ERRORLEVEL 1 (
	ECHO Failed updating unit test
	EXIT /B 1
)

EXIT /B 0
