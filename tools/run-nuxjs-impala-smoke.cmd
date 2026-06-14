@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CD /D "%~dp0\.."

IF DEFINED NUXJS (
  SET "NUXJS_EXE=%NUXJS%"
) ELSE IF EXIST "output\NuXJS.exe" (
  SET "NUXJS_EXE=output\NuXJS.exe"
) ELSE IF EXIST "output\NuXJS" (
  SET "NUXJS_EXE=output\NuXJS"
) ELSE (
  FOR /F "delims=" %%I IN ('WHERE NuXJS 2^>NUL') DO (
    IF NOT DEFINED NUXJS_EXE SET "NUXJS_EXE=%%I"
  )
)

IF NOT DEFINED NUXJS_EXE (
  ECHO Skipping NuXJS smoke test: set NUXJS or build/provide output\NuXJS.
  EXIT /b 0
)

SET "OUTPUT=%TEMP%\gazl-nuxjs-smoke-%RANDOM%-%RANDOM%.gazl"

CALL "%NUXJS_EXE%" -s "%CD%\impala\impala.nuxjs.js" "%CD%\impala\testdata\smoke.impala" 42 smoke.impala "%CD%\impala\impalaCompiler.js" > "%OUTPUT%"
SET "STATUS=%ERRORLEVEL%"
IF NOT "%STATUS%"=="0" (
  DEL "%OUTPUT%" >NUL 2>NUL
  EXIT /b %STATUS%
)

FOR %%A IN ("%OUTPUT%") DO SET "OUTPUT_SIZE=%%~zA"
DEL "%OUTPUT%" >NUL 2>NUL

IF "%OUTPUT_SIZE%"=="0" (
  ECHO NuXJS smoke test produced no output.
  EXIT /b 1
)

ECHO NuXJS Impala compiler smoke test passed.
EXIT /b 0
