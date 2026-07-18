@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."
REM Run one Permut8 firmware through the pure-GAZL host harness (see runPermut8Firmware.sh for details).
REM Usage: tools\runPermut8Firmware.cmd <firmware.gazl> [extra GAZLCmd args]
IF "%~1"=="" ( ECHO Usage: tools\runPermut8Firmware.cmd ^<firmware.gazl^> & EXIT /B 1 )
SET FW=%~1
SET NAME=%~n1
IF NOT EXIST output\p8 MKDIR output\p8
node tools\permut8Host.js "%FW%" "output\p8\%NAME%.gazl" || EXIT /B 1

SET EXTRA=
FINDSTR /R /C:"^sqrt:" /C:"^log:" /C:"^atan2:" "%FW%" >NUL 2>&1 && SET EXTRA=--no-libm
FOR %%N IN (input print printInt printFloat printLF exit) DO (
  FINDSTR /R /C:"^%%N:" "%FW%" >NUL 2>&1 && SET EXTRA=!EXTRA! --no-native=%%N
)

REM GAZLCMD overrides the binary (e.g. an A-B baseline build).
IF NOT DEFINED GAZLCMD SET GAZLCMD=output\GAZLCmd.exe
%GAZLCMD% "output\p8\%NAME%.gazl" hostMain --forward=yield:yield_,read:read_,write:write_,trace:trace_ !EXTRA! %2 %3 %4
