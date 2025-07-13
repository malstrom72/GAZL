@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0

PUSHD ..\externals\PikaCmd
CALL BuildPikaCmd.cmd
IF ERRORLEVEL 1 EXIT /B 1
POPD
IF EXIST ..\externals\PikaCmd\PikaCmd (COPY /Y ..\externals\PikaCmd\PikaCmd ..\output\PikaCmd >NUL)
IF EXIST ..\externals\PikaCmd\PikaCmd.exe (COPY /Y ..\externals\PikaCmd\PikaCmd.exe ..\output\PikaCmd.exe >NUL)
IF NOT EXIST ..\output MKDIR ..\output

SET outdir=..\output
IF NOT EXIST %outdir% MKDIR %outdir%

PUSHD ..\impala
IF EXIST ..\output\PikaCmd (SET pkcmd=..\output\PikaCmd) ELSE (SET pkcmd=..\output\PikaCmd.exe)
%pkcmd% impala.pika rebuild
IF ERRORLEVEL 1 EXIT /B 1
POPD

COPY /Y ..\impala\impala.pika %outdir%\ >NUL
COPY /Y ..\impala\impalaCompiler.pika %outdir%\ >NUL
COPY /Y ..\impala\initPPEG.pika %outdir%\ >NUL
COPY /Y ..\impala\systools.pika %outdir%\ >NUL
COPY /Y ..\impala\impala.cmd %outdir%\ >NUL
EXIT /B 0
