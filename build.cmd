@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

REM Build PikaCmd (release native)
	PUSHD tools\PikaCmd
	CALL ..\BuildCpp.cmd release x64 ..\PikaCmd.exe /D "PLATFORM_STRING=WINDOWS" PikaCmdAmalgam.cpp
	IF ERRORLEVEL 1 EXIT /B 1
	POPD

REM Build and test GAZLCmd beta
        CALL tools\BuildCpp.cmd beta x64 GAZLCmdBeta.exe -I. GAZLCmd\GAZLCmd.cpp src\GAZL.cpp
        IF ERRORLEVEL 1 EXIT /B 1
        GAZLCmdBeta.exe
        IF ERRORLEVEL 1 EXIT /B 1

        REM Build GAZLCmd release
        CALL tools\BuildCpp.cmd release x64 GAZLCmd.exe -I. GAZLCmd\GAZLCmd.cpp src\GAZL.cpp
        IF ERRORLEVEL 1 EXIT /B 1

REM Run Impala tests
        COPY /Y PikaCmd.exe impala\ >NUL
        PUSHD impala
        PikaCmd.exe runTests.pika
        IF ERRORLEVEL 1 EXIT /B 1
        POPD

