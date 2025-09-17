@ECHO OFF
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

FOR %%F IN ("%TESTDIR%\*.impala") DO (
  SET SRC=%%~fF
  SET OUT=%%~dpnF.pika.gazl
  CALL "%CMD%" "%SCRIPT" compile "%%SRC%%" "%%OUT%%" %SEED% >NUL
  ECHO Rebuilt %%OUT%%
)

EXIT /b 0
