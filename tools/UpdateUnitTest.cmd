@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0

PUSHD ..\externals\PikaCmd
CALL BuildPikaCmd.cmd
IF ERRORLEVEL 1 EXIT /B 1
POPD

..\externals\PikaCmd\PikaCmd UpdateUnitTest.pika
IF ERRORLEVEL 1 (
    ECHO Failed updating unit test
    EXIT /B 1
)

EXIT /B 0
