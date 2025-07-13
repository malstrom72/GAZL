@ECHO OFF
SET "SCRIPT_DIR=%~dp0"
CD /D "%SCRIPT_DIR%"
"%SCRIPT_DIR%..\externals\PikaCmd\PikaCmd.exe" "%SCRIPT_DIR%impala.pika" %*
