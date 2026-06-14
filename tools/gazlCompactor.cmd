@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

IF "%~2"=="" (
	ECHO Usage: tools\gazlCompactor.cmd input.gazl output.gazl
	EXIT /B 1
)

CALL tools\BuildNuXJS.cmd release x64 output\NuXJS.exe
IF ERRORLEVEL 1 EXIT /B 1

output\NuXJS.exe tools\gazlCompactor.nuxjs.js "%~1" "%~2"
IF ERRORLEVEL 1 EXIT /B 1

EXIT /B 0
