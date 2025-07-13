@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0
IF "%~1"=="" (
    SET mode=release
) ELSE (
    SET mode=%1
)
IF NOT EXIST ..\output MKDIR ..\output
CALL UpdateUnitTest.cmd
IF "%mode%"=="beta" (
    SET out=..\output\GAZLCmdBeta.exe
) ELSE (
    SET out=..\output\GAZLCmd.exe
)
CALL BuildCpp.cmd %mode% x64 %out% -I.. GAZLCmd.cpp ..\src\GAZL.cpp
IF EXIST %out% ATTRIB +x %out% >NUL 2>&1
