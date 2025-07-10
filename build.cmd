@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

	IF NOT EXIST output MKDIR output

	REM Build PikaCmd (release native)
	PUSHD tools\PikaCmd
	CALL BuildPikaCmd.cmd
	IF ERRORLEVEL 1 EXIT /B 1
	POPD

	REM Build and test GAZLCmd beta
	CALL tools\BuildCpp.cmd beta x64 output\GAZLCmdBeta.exe -I. GAZLCmd\GAZLCmd.cpp src\GAZL.cpp
	IF ERRORLEVEL 1 EXIT /B 1
	output\GAZLCmdBeta.exe
	IF ERRORLEVEL 1 EXIT /B 1

	REM Build GAZLCmd release
CALL tools\BuildCpp.cmd release x64 output\GAZLCmd.exe -I. GAZLCmd\GAZLCmd.cpp src\GAZL.cpp
IF ERRORLEVEL 1 EXIT /B 1

REM Copy release binary to the Impala folder
COPY /Y output\GAZLCmd.exe impala\GAZLCmd.exe >NUL

	REM Run Impala tests
	PUSHD impala
	..\tools\PikaCmd\PikaCmd.exe runTests.pika
	IF ERRORLEVEL 1 EXIT /B 1
	POPD

