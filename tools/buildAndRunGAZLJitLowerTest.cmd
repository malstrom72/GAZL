@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0
REM Unified GAZLJit lowering test (JitCompiler vs interpreter, full + tiny fuel) for the x64 backend, built with MSVC.
IF NOT EXIST ..\output MKDIR ..\output
CALL BuildCpp.cmd release x64 ..\output\GAZLJitLowerTest.exe -I..\src -I.. GAZLJitLowerTest.cpp ..\src\GAZL.cpp ..\src\GAZLJit.cpp ..\src\GAZLJitX64.cpp ..\src\GAZLJitMemWindows.cpp
IF ERRORLEVEL 1 EXIT /B 1
..\output\GAZLJitLowerTest.exe
