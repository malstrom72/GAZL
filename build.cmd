@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0"

IF NOT EXIST output MKDIR output

REM Build and test GAZLCmd beta
PUSHD tools
CALL buildGAZLCmd.cmd beta
IF ERRORLEVEL 1 EXIT /B 1
POPD
output\GAZLCmdBeta.exe
IF ERRORLEVEL 1 EXIT /B 1

REM Build GAZLCmd release
PUSHD tools
CALL buildGAZLCmd.cmd release
IF ERRORLEVEL 1 EXIT /B 1
POPD

REM Every node-only gate, shared with build.sh so the two cannot run different subsets.
CALL tools\test-js.cmd
IF ERRORLEVEL 1 EXIT /B 1

REM Build Impala
CALL tools\BuildImpala.cmd
IF ERRORLEVEL 1 EXIT /B 1

REM Verify the staged Impala compiler by compiling with NuXJS and running with GAZLCmd.
output\NuXJS.exe output\impala.nuxjs.js ^
	impala\ImpalaDemo.impala output\ImpalaDemo.gazl 0x4d2 impala\ImpalaDemo.impala
IF ERRORLEVEL 1 EXIT /B 1
output\GAZLCmd.exe output\ImpalaDemo.gazl main
IF ERRORLEVEL 1 EXIT /B 1

REM ImpalaDemo imports nothing, so it cannot tell whether the closure walk survived staging.
CALL tools\run-nuxjs-impala-smoke.cmd
IF ERRORLEVEL 1 EXIT /B 1
