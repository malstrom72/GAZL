@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
REM Regenerate impala\jspeg\testdata\*.pika.gazl from their .impala sources
REM using the PPEG PikaScript-based Impala compiler (via output\PikaCmd).

CD /D "%~dp0\.."

SET CMD=output\PikaCmd.exe
IF NOT EXIST "%CMD%" SET CMD=output\PikaCmd
IF NOT EXIST "%CMD%" (
  ECHO Missing output\PikaCmd(.exe). Run build.cmd first.
  EXIT /b 1
)

SET SCRIPT=impala.pika
SET TESTDIR=impala\jspeg\testdata
SET SEED=42
SET FOUND=0

FOR %%F IN ("%TESTDIR%\*.impala") DO (
  SET SRC=%%~fF
  SET OUT=%%~dpnF.pika.gazl
  CALL "%CMD%" "%SCRIPT" compile "%%SRC%%" "%%OUT%%" %SEED% >NUL
  IF ERRORLEVEL 1 EXIT /b %ERRORLEVEL%
  ECHO Rebuilt %%OUT%%
  SET FOUND=1
)

IF %FOUND%==0 (
  ECHO No .impala sources found in %TESTDIR%
  EXIT /b 1
)

SET FILES=
FOR %%G IN ("%TESTDIR%\*.pika.gazl") DO (
  IF NOT DEFINED FILES (
    SET FILES="%%~fG"
  ) ELSE (
    SET FILES=!FILES! "%%~fG"
  )
)

IF NOT DEFINED FILES (
  ECHO No .pika.gazl outputs found in %TESTDIR%
  EXIT /b 1
)

CALL tools\gazl-validate.cmd !FILES!
IF ERRORLEVEL 1 EXIT /b %ERRORLEVEL%

EXIT /b 0
