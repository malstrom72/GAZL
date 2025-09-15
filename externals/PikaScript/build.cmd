@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0"

PUSHD tools\PikaCmd\SourceDistribution
SET CPP_TARGET=beta
DEL /Q PikaCmd.exe 2>NUL
CALL BuildPikaCmd.cmd || GOTO error
POPD
tools\PikaCmd\SourceDistribution\PikaCmd tests\ppegTest.pika || GOTO error

tools\PikaCmd\SourceDistribution\PikaCmd examples\ppegDocExample.pika || GOTO error

tools\PikaCmd\SourceDistribution\PikaCmd tests\htmlifyTests.pika || GOTO error

PUSHD tools\PikaCmd\SourceDistribution
DEL /Q PikaCmd.exe 2>NUL
SET CPP_TARGET=release
CALL BuildPikaCmd.cmd || GOTO error
POPD
IF NOT EXIST output MD output
COPY /Y tools\PikaCmd\SourceDistribution\PikaCmd.exe output\PikaCmd.exe || GOTO error
COPY /Y tools\PikaCmd\SourceDistribution\systools.pika output\systools.pika || GOTO error
output\PikaCmd.exe tests\ppegTest.pika || GOTO error

output\PikaCmd.exe examples\ppegDocExample.pika || GOTO error

output\PikaCmd.exe tests\htmlifyTests.pika || GOTO error

EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
