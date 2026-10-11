@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0
REM The boundary sweep (GAZLBoundsSweep.cpp) through the interpreter AND the x64 JIT, built with MSVC. Twin of
REM buildAndRunGAZLBoundsSweepJit.sh; tools\test-jit.cmd runs it.
IF NOT EXIST ..\output MKDIR ..\output
CALL BuildCpp.cmd beta x64 ..\output\GAZLBoundsSweepJit.exe -DGAZL_JIT -I..\src GAZLBoundsSweep.cpp ..\src\GAZL.cpp ..\src\GAZLJit.cpp ..\src\GAZLJitX64.cpp ..\src\GAZLJitMemWindows.cpp
IF ERRORLEVEL 1 EXIT /B 1
..\output\GAZLBoundsSweepJit.exe
