@ECHO OFF
SET "SCRIPT_DIR=%~dp0"
"%SCRIPT_DIR%..\tools\PikaCmd\PikaCmd.exe" "%SCRIPT_DIR%impala.pika" %*
