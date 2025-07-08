@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION

CD PikaCmd
CALL BuildPikaCmd
IF ERRORLEVEL 1 EXIT /B 1
CD ..

IF NOT EXIST GAZLCmd.exe (
	CALL UpdateUnitTest
	IF ERRORLEVEL 1 EXIT /B 1

	CALL BuildCpp GAZLCmd.exe ..\GAZLCmd\GAZLCmd.cpp ..\src\GAZL.cpp
	IF ERRORLEVEL 1 EXIT /B 1
)

COPY /Y GAZLCmd.exe ..\impala\ >NUL
COPY /Y PikaCmd\PikaCmd.exe ..\impala\ >NUL
COPY /Y PikaCmd\systools.pika ..\impala\ >NUL
CD ..\impala\
PikaCmd impala.pika rebuild
impala run ImpalaDemo.impala
EXIT /B 0
