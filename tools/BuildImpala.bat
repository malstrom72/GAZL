@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0

PUSHD PikaCmd
CALL BuildPikaCmd
IF ERRORLEVEL 1 EXIT /B 1
POPD
IF NOT EXIST ..\output MKDIR ..\output
COPY /Y PikaCmd\PikaCmd* ..\output\ >NUL

SET outdir=..\output
IF NOT EXIST %outdir% MKDIR %outdir%

PUSHD ..\impala
..\output\PikaCmd impala.pika rebuild
IF ERRORLEVEL 1 EXIT /B 1
POPD

COPY /Y ..\impala\impala.pika %outdir%\ >NUL
COPY /Y ..\impala\impalaCompiler.pika %outdir%\ >NUL
COPY /Y ..\impala\initPPEG.pika %outdir%\ >NUL
COPY /Y ..\impala\systools.pika %outdir%\ >NUL
COPY /Y ..\impala\ImpalaDemo.impala %outdir%\ >NUL
COPY /Y ..\impala\runTests.pika %outdir%\ >NUL
XCOPY /E /I /Y ..\impala\tests %outdir%\tests >NUL
EXIT /B 0


