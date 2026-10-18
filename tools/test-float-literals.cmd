@ECHO OFF
REM Build and run a sampled check of float literal rounding (tools\GAZLFloatLiteralTest.cpp): every 16411th float.
REM Pass `0 0x7F800000` to check all of them. `beta` keeps asserts on. The .sh twin does the same with the default
REM POSIX compiler.
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0.."
IF NOT EXIST output MKDIR output

ECHO test-float-literals: building the checker
PUSHD tools
CALL BuildCpp.cmd beta x64 ..\output\GAZLFloatLiteralTest.exe -I..\src GAZLFloatLiteralTest.cpp ..\src\GAZL.cpp || GOTO error
POPD
IF "%~2"=="" (
	output\GAZLFloatLiteralTest.exe 0 0x7F800000 16411 || GOTO error
) ELSE (
	output\GAZLFloatLiteralTest.exe %* || GOTO error
)
EXIT /b 0

:error
ECHO test-float-literals FAILED
EXIT /b 1
