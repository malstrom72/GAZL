@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

IF NOT EXIST output MKDIR output

REM Build and test GAZLCmd beta
PUSHD GAZLCmd
CALL buildGAZLCmd.bat beta
IF ERRORLEVEL 1 EXIT /B 1
POPD
output\GAZLCmdBeta.exe
IF ERRORLEVEL 1 EXIT /B 1

REM Build GAZLCmd release
PUSHD GAZLCmd
CALL buildGAZLCmd.bat release
IF ERRORLEVEL 1 EXIT /B 1
POPD

REM Build Impala and run demo
PUSHD tools
CALL BuildImpala.bat
IF ERRORLEVEL 1 EXIT /B 1
POPD

REM Run Impala tests
PUSHD output\impala
PikaCmd runTests.pika
IF ERRORLEVEL 1 EXIT /B 1
POPD

