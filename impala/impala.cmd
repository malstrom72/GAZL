@ECHO OFF
SET "SCRIPT_DIR=%~dp0"
"%SCRIPT_DIR%..\externals\PikaCmd\PikaCmd.exe" "%SCRIPT_DIR%impala.pika" %*
