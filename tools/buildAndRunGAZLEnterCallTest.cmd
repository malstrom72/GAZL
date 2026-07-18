@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0
REM enterCall-from-native tests (blocking + non-blocking forward), interpreter only, built with MSVC.
IF NOT EXIST ..\output MKDIR ..\output
CALL BuildCpp.cmd release x64 ..\output\GAZLEnterCallTest.exe -I..\src GAZLEnterCallTest.cpp ..\src\GAZL.cpp
IF ERRORLEVEL 1 EXIT /B 1
..\output\GAZLEnterCallTest.exe
