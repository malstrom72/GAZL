@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
REM Regenerate impala\testdata\*.expected.gazl from their .impala sources
REM using the JSPEG Impala compiler.

CD /D "%~dp0\.."

SET COMPILER=impala\impala.node.js
IF NOT EXIST "%COMPILER%" (
  ECHO Missing %COMPILER%. Run "node impala\updateJSPEG.js" first.
  EXIT /b 1
)

SET TESTDIR=impala\testdata
SET SEED=42
SET FOUND=0

FOR %%F IN ("%TESTDIR%\*.impala") DO (
  SET SRC=%%~fF
  SET OUT=%%~dpnF.expected.gazl
  node "%COMPILER%" compile "!SRC!" "!OUT!" %SEED% >NUL
  IF ERRORLEVEL 1 EXIT /b %ERRORLEVEL%
  ECHO Rebuilt !OUT!
  SET FOUND=1
)

IF %FOUND%==0 (
  ECHO No .impala sources found in %TESTDIR%
  EXIT /b 1
)

REM One at a time, as build.cmd does: these are independent programs, not a link set - handed to the
REM validator together, every `main` after the first is reported as a conflicting redefinition and the
REM whole regeneration exits non-zero after having already rewritten the fixtures. The caller/provider
REM pair is the one set that IS meant to link together.
FOR %%G IN ("%TESTDIR%\*.expected.gazl") DO (
  IF /I NOT "%%~nxG"=="externAssignment.expected.gazl" IF /I NOT "%%~nxG"=="returnContractCaller.expected.gazl" (
    CALL tools\gazl-validate.cmd "%%~fG"
    IF ERRORLEVEL 1 EXIT /b %ERRORLEVEL%
  )
)
CALL tools\gazl-validate.cmd "%TESTDIR%\returnContractCaller.expected.gazl" "%TESTDIR%\returnContractProviderFloat.expected.gazl"
IF ERRORLEVEL 1 EXIT /b %ERRORLEVEL%

EXIT /b 0
