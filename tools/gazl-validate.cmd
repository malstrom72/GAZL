@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."
IF EXIST output\NuXJS.exe (
  output\NuXJS.exe tools\gazl-validate.js %*
) ELSE IF EXIST output\NuXJS (
  output\NuXJS tools\gazl-validate.js %*
) ELSE (
  ECHO Missing output\NuXJS. Run tools\BuildNuXJS.cmd first.
  EXIT /b 1
)
EXIT /b %ERRORLEVEL%
