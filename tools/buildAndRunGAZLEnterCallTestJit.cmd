@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0
REM The enterCall and pushCall tests (GAZLEnterCallTest.cpp) through the x64 JIT, built with MSVC. Twin of
REM buildAndRunGAZLEnterCallTestJit.sh; tools\test-jit.cmd runs it.
IF NOT EXIST ..\output MKDIR ..\output
CALL BuildCpp.cmd beta x64 ..\output\GAZLEnterCallTestJit.exe -DGAZL_JIT -I..\src GAZLEnterCallTest.cpp ..\src\GAZL.cpp ..\src\GAZLJit.cpp ..\src\GAZLJitX64.cpp ..\src\GAZLJitMemWindows.cpp
IF ERRORLEVEL 1 EXIT /B 1
..\output\GAZLEnterCallTestJit.exe
