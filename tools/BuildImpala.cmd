@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

SET outdir=output
IF NOT EXIST %outdir% MKDIR %outdir%

CALL tools\BuildNuXJS.cmd release x64 "%outdir%\NuXJS.exe"
IF ERRORLEVEL 1 EXIT /B 1

REM impala.nuxjs.js load()s impalaImportClosure.js from its own directory - stage all three.
COPY /Y impala\impala.nuxjs.js %outdir%\ >NUL
COPY /Y impala\impalaImportClosure.js %outdir%\ >NUL
COPY /Y impala\impalaCompiler.js %outdir%\ >NUL
ECHO Impala staged in %outdir% using NuXJS.
EXIT /B 0
