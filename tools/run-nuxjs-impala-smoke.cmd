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
SET "LABEL_SOURCE=%TEMP%\gazl-nuxjs-label-%RANDOM%-%RANDOM%.impala"
SET "INVALID_SOURCE=%TEMP%\gazl-nuxjs-invalid-%RANDOM%-%RANDOM%.impala"
SET "ERROR_LOG=%TEMP%\gazl-nuxjs-error-%RANDOM%-%RANDOM%.log"

CALL "%NUXJS_EXE%" "%CD%\impala\impala.nuxjs.js" "%CD%\impala\testdata\smoke.impala" "%OUTPUT%" 42 smoke.impala "%CD%\impala\impalaCompiler.js"
SET "STATUS=%ERRORLEVEL%"
IF NOT "%STATUS%"=="0" (
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b %STATUS%
)

FOR %%A IN ("%OUTPUT%") DO SET "OUTPUT_SIZE=%%~zA"
DEL "%OUTPUT%" >NUL 2>NUL

IF "%OUTPUT_SIZE%"=="0" (
  ECHO NuXJS smoke test produced no output.
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b 1
)

(
  ECHO readonly array panelTextRows[1] = {
  ECHO 	"GRBLEN"
  ECHO }
  ECHO.
  ECHO function main^(^)
  ECHO {
  ECHO }
) > "%LABEL_SOURCE%"

CALL "%NUXJS_EXE%" "%CD%\impala\impala.nuxjs.js" "%LABEL_SOURCE%" "%OUTPUT%" -845775591 evighet_code.impala "%CD%\impala\impalaCompiler.js"
SET "STATUS=%ERRORLEVEL%"
IF NOT "%STATUS%"=="0" (
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b %STATUS%
)

FINDSTR /C:".s_GRBLEN_cd967d19" "%OUTPUT%" >NUL
IF ERRORLEVEL 1 (
  ECHO NuXJS smoke test did not emit an unsigned hex string label.
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b 1
)
REM Anchored to the label token itself - a bare `.*` runs to the end of the line and matches any
REM dash anywhere on it, which made this fire on every clean build.
FINDSTR /R /C:"\.s_GRBLEN[^ :]*-" "%OUTPUT%" >NUL
IF NOT ERRORLEVEL 1 (
  ECHO NuXJS smoke test emitted a string label containing '-' ^(invalid GAZL identifier character^).
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b 1
)

REM The import closure must be walked on the NuXJS path too, not just under Node - impala.nuxjs.js
REM load()s impalaImportClosure.js, so a missing staged copy shows up right here.
CALL "%NUXJS_EXE%" "%CD%\impala\impala.nuxjs.js" "%CD%\tests\impala\sources\import\main.impala" "%OUTPUT%" 1234 main.impala "%CD%\impala\impalaCompiler.js"
SET "STATUS=%ERRORLEVEL%"
IF NOT "%STATUS%"=="0" (
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b %STATUS%
)
REM makeVec and addVec are defined only in mathlib.impala, so their presence proves the imported
REM unit was walked, concatenated and compiled - not just the root.
FINDSTR /C:"makeVec:" "%OUTPUT%" >NUL
IF ERRORLEVEL 1 (
  ECHO NuXJS smoke test did not link the import closure - missing makeVec.
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b 1
)
FINDSTR /C:"addVec:" "%OUTPUT%" >NUL
IF ERRORLEVEL 1 (
  ECHO NuXJS smoke test did not link the import closure - missing addVec.
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b 1
)

(
  ECHO function main^(^)
  ECHO locals int x
  ECHO {
  ECHO 	x^(^);
  ECHO }
) > "%INVALID_SOURCE%"

CALL "%NUXJS_EXE%" "%CD%\impala\impala.nuxjs.js" "%INVALID_SOURCE%" - > "%ERROR_LOG%" 2>&1
IF "%ERRORLEVEL%"=="0" (
  ECHO NuXJS smoke test expected invalid Impala source to fail.
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b 1
)

FINDSTR /C:"Invalid type for function call" "%ERROR_LOG%" >NUL
IF ERRORLEVEL 1 (
  ECHO NuXJS smoke test did not preserve the Impala diagnostic.
  TYPE "%ERROR_LOG%"
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b 1
)
FINDSTR /C:"isFinite is not a function" "%ERROR_LOG%" >NUL
IF NOT ERRORLEVEL 1 (
  ECHO NuXJS smoke test hit an incompatible Number.isFinite diagnostic path.
  TYPE "%ERROR_LOG%"
  DEL "%OUTPUT%" >NUL 2>NUL
  DEL "%LABEL_SOURCE%" >NUL 2>NUL
  DEL "%INVALID_SOURCE%" >NUL 2>NUL
  DEL "%ERROR_LOG%" >NUL 2>NUL
  EXIT /b 1
)

DEL "%OUTPUT%" >NUL 2>NUL
DEL "%LABEL_SOURCE%" >NUL 2>NUL
DEL "%INVALID_SOURCE%" >NUL 2>NUL
DEL "%ERROR_LOG%" >NUL 2>NUL

ECHO NuXJS Impala compiler smoke test passed.
EXIT /b 0
