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
  CALL node "%COMPILER%" compile "%%SRC%%" "%%OUT%%" %SEED% >NUL
  IF ERRORLEVEL 1 EXIT /b %ERRORLEVEL%
  ECHO Rebuilt %%OUT%%
  SET FOUND=1
)

IF %FOUND%==0 (
  ECHO No .impala sources found in %TESTDIR%
  EXIT /b 1
)

SET FILES=
FOR %%G IN ("%TESTDIR%\*.expected.gazl") DO (
  IF NOT DEFINED FILES (
    SET FILES="%%~fG"
  ) ELSE (
    SET FILES=!FILES! "%%~fG"
  )
)

IF NOT DEFINED FILES (
  ECHO No .expected.gazl outputs found in %TESTDIR%
  EXIT /b 1
)

CALL tools\gazl-validate.cmd !FILES!
IF ERRORLEVEL 1 EXIT /b %ERRORLEVEL%

EXIT /b 0
