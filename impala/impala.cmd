@ECHO OFF
SET "SCRIPT_DIR=%~dp0"
"%SCRIPT_DIR%..\tools\PikaCmd\Pika.cmd" "%SCRIPT_DIR%impala.pika" %*
