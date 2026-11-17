@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0
REM enterCall and pushCall tests: calls from natives (blocking + non-blocking forward) and interrupts on a suspended
REM processor. On the interpreter, built with MSVC; buildAndRunGAZLEnterCallTestJit.cmd runs them on the JIT. `beta`
REM keeps asserts on.
IF NOT EXIST ..\output MKDIR ..\output
CALL BuildCpp.cmd beta x64 ..\output\GAZLEnterCallTest.exe -I..\src GAZLEnterCallTest.cpp ..\src\GAZL.cpp
IF ERRORLEVEL 1 EXIT /B 1
..\output\GAZLEnterCallTest.exe
