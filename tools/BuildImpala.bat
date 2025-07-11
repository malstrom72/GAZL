@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0

PUSHD PikaCmd
CALL BuildPikaCmd
IF ERRORLEVEL 1 EXIT /B 1
POPD

SET outdir=..\output
IF NOT EXIST %outdir% MKDIR %outdir%

COPY /Y ..\impala\impala.ppeg %outdir%\ >NUL
COPY /Y ..\impala\impala.pika %outdir%\ >NUL
COPY /Y ..\impala\initPPEG.pika %outdir%\ >NUL
COPY /Y ..\impala\systools.pika %outdir%\ >NUL

PUSHD %outdir%
..\tools\PikaCmd\PikaCmd impala.pika rebuild
IF ERRORLEVEL 1 EXIT /B 1
POPD
EXIT /B 0


