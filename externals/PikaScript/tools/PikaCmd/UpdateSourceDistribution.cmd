@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0"

IF NOT EXIST PikaCmd.exe (
        REM Use the portable build script from SourceDistribution to avoid
        REM platform-specific flags not supported by all compilers
        CALL SourceDistribution\BuildCpp PikaCmd.exe -DPLATFORM_STRING=WINDOWS PikaCmd.cpp BuiltIns.cpp ..\..\src\*.cpp
        IF ERRORLEVEL 1 EXIT /B 1
)

PikaCmd.exe UpdateBuiltIns.pika
IF ERRORLEVEL 1 (
	ECHO Failed updating built-in files
	EXIT /B 1
)

COPY /Y /B ..\..\src\PikaScript.h + ..\..\src\PikaScriptImpl.h + ..\..\src\QStrings.h + ..\..\src\QuickVars.h + ..\..\src\PikaScript.cpp + ..\..\src\QStrings.cpp + BuiltIns.cpp + PikaCmd.cpp SourceDistribution\PikaCmdAmalgam.cpp >NUL
IF ERRORLEVEL 1 (
	ECHO Failed concatenating files
	EXIT /B 1
)

CD SourceDistribution
COPY /Y ..\..\..\tests\unittests.pika . >NUL ^
		&& COPY /Y ..\systools.pika . >NUL ^
		&& COPY /Y ..\..\..\tests\systoolsTests.pika . >NUL ^
		&& COPY /Y ..\..\..\LICENSE . >NUL
IF ERRORLEVEL 1 (
	ECHO Failed copying files
	EXIT /B 1
)

DEL /Q PikaCmd.exe >NUL 2>NUL
CALL BuildPikaCmd.cmd
IF ERRORLEVEL 1 EXIT /B 1

COPY /Y PikaCmd.exe ..\PikaCmd.exe >NUL
CD ..

EXIT /B 0
