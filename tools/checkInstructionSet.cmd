@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

CALL tools\BuildNuXJS.cmd release x64 output\NuXJS.exe
IF ERRORLEVEL 1 EXIT /B 1

output\NuXJS.exe tools\checkInstructionSet.nuxjs.js
IF ERRORLEVEL 1 EXIT /B 1

EXIT /B 0
