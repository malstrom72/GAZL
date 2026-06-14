@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

SET outdir=output
IF NOT EXIST %outdir% MKDIR %outdir%

CALL tools\BuildNuXJS.cmd release x64 "%outdir%\NuXJS.exe"
IF ERRORLEVEL 1 EXIT /B 1

COPY /Y impala\jspeg\impala.nuxjs.js %outdir%\ >NUL
COPY /Y impala\jspeg\impalaCompiler.js %outdir%\ >NUL
ECHO Impala staged in %outdir% using NuXJS.
EXIT /B 0
