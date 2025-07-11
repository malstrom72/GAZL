@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0

PUSHD PikaCmd
CALL BuildPikaCmd
IF ERRORLEVEL 1 EXIT /B 1
POPD

IF NOT EXIST ..\output\impala MKDIR ..\output\impala

xcopy ..\impala ..\output\impala /E /Y >NUL

COPY /Y PikaCmd\PikaCmd.exe ..\output\impala\ >NUL
COPY /Y PikaCmd\systools.pika ..\output\impala\ >NUL
COPY /Y ..\output\GAZLCmd.exe ..\output\impala\GAZLCmd.exe >NUL

PUSHD ..\output\impala
PikaCmd impala.pika rebuild
IF ERRORLEVEL 1 EXIT /B 1
PikaCmd impala.pika run ImpalaDemo.impala
IF ERRORLEVEL 1 EXIT /B 1
POPD
EXIT /B 0


