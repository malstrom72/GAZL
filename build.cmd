@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

IF NOT EXIST output MKDIR output

REM Build and test GAZLCmd beta
PUSHD tools
CALL buildGAZLCmd.bat beta
IF ERRORLEVEL 1 EXIT /B 1
POPD
output\GAZLCmdBeta.exe
IF ERRORLEVEL 1 EXIT /B 1

REM Build GAZLCmd release
PUSHD tools
CALL buildGAZLCmd.bat release
IF ERRORLEVEL 1 EXIT /B 1
POPD
COPY /Y output\GAZLCmd.exe impala\GAZLCmd.exe >NUL

REM Build Impala
PUSHD tools
CALL BuildImpala.bat
IF ERRORLEVEL 1 EXIT /B 1
POPD

REM Run demo and tests from the impala directory
PUSHD impala
..\output\PikaCmd impala.pika run ImpalaDemo.impala
IF ERRORLEVEL 1 EXIT /B 1
..\output\PikaCmd runTests.pika
IF ERRORLEVEL 1 EXIT /B 1
POPD

