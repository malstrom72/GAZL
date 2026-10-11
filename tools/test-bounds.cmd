@ECHO OFF
REM Build and run the deterministic boundary sweep of every memory instruction form (tools\GAZLBoundsSweep.cpp).
REM `beta` keeps asserts on. The .sh twin does the same with the default POSIX compiler.
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0.."
IF NOT EXIST output MKDIR output

ECHO test-bounds: building the sweep
PUSHD tools
CALL BuildCpp.cmd beta x64 ..\output\GAZLBoundsSweep.exe -I..\src GAZLBoundsSweep.cpp ..\src\GAZL.cpp || GOTO error
POPD
output\GAZLBoundsSweep.exe || GOTO error
EXIT /b 0

:error
ECHO test-bounds FAILED
EXIT /b 1
