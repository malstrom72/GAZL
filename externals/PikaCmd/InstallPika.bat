@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SETLOCAL

IF "%~1"=="" (
	ECHO.
	ECHO InstallPika ^<target path^>
	ECHO.
	ECHO To run as administrator and install into C:\WINDOWS, type:
	ECHO.
	ECHO runas.exe /savecred /user:administrator "cmd /c cd %CD%&&InstallPika.bat C:\WINDOWS"
	EXIT /B 1
)
COPY Pika.cmd %1\ || GOTO error
COPY PikaCmd.exe %1\ || GOTO error
COPY systools.pika %1\ || GOTO error
ASSOC .pika=PikaScript
FTYPE PikaScript=%1\Pika.cmd %%1 %%*
EXIT /B 0

:error
ECHO Error %ERRORLEVEL%
EXIT /B %ERRORLEVEL%
