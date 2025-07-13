@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
SETLOCAL

IF "%~1"=="" (
	ECHO.
	ECHO UninstallPika ^<installation path^>
	ECHO.
	ECHO To run as administrator and uninstall from C:\WINDOWS, type:
	ECHO.
	ECHO runas.exe /savecred /user:administrator "cmd /c cd %CD%&&UninstallPika.bat C:\WINDOWS"
	EXIT /B 1
)
DEL %1\Pika.cmd
DEL %1\PikaCmd.exe
DEL %1\systools.pika
