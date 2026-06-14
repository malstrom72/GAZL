@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

SET mode=%~1
SET model=%~2
SET out=%~3
IF "%mode%"=="" SET mode=release
IF "%model%"=="" SET model=x64
IF "%out%"=="" SET out=output\NuXJS.exe

IF NOT EXIST output MKDIR output

CALL tools\BuildCpp.cmd %mode% %model% "%out%" ^
	externals\NuXJS\tools\NuXJSREPL.cpp ^
	externals\NuXJS\src\NuXJS.cpp ^
	externals\NuXJS\src\stdlibJS.cpp
IF ERRORLEVEL 1 EXIT /B 1
EXIT /B 0
