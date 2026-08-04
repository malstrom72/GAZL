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
REM Propagate the compile result BEFORE anything else runs. A script's exit status is its LAST command,
REM and the `IF EXIST`/`ATTRIB` below succeeds whatever happened - so a FAILED build reported success and
REM `build.cmd` sailed straight past its own `IF ERRORLEVEL 1`. That is how a stale GAZLCmd.exe shipped
REM while the build called itself green: the linker could not overwrite a copy still held open by an
REM earlier run (LNK1104), printed the error, and nothing downstream noticed.
IF ERRORLEVEL 1 EXIT /B 1
IF EXIST %out% ATTRIB +x %out% >NUL 2>&1
EXIT /B 0
