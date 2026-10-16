@ECHO OFF
REM Build and run the multiple-processor test (tools\GAZLMultiProcessorTest.cpp): several processors in one memory,
REM cooperative and on threads, each with its own part of the data stack. `beta` keeps asserts on. The .sh twin does
REM the same with the default POSIX compiler.
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0.."
IF NOT EXIST output MKDIR output

ECHO test-processors: building the test
PUSHD tools
CALL BuildCpp.cmd beta x64 ..\output\GAZLMultiProcessorTest.exe -I..\src GAZLMultiProcessorTest.cpp ..\src\GAZL.cpp || GOTO error
POPD
output\GAZLMultiProcessorTest.exe || GOTO error
EXIT /b 0

:error
ECHO test-processors FAILED
EXIT /b 1
