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

REM Build Impala
CALL tools\BuildImpala.cmd
IF ERRORLEVEL 1 EXIT /B 1

REM Run the Impala test suite from the source directory
PUSHD impala
node jspegCompilerTests.js
IF ERRORLEVEL 1 EXIT /B 1
node runJspegTests.js
IF ERRORLEVEL 1 EXIT /B 1
POPD

REM Validate generated .gazl metadata for the JSPEG fixtures.
FOR %%F IN (impala\testdata\*.expected.gazl) DO (
  IF /I NOT "%%~nxF"=="externAssignment.expected.gazl" IF /I NOT "%%~nxF"=="returnContractCaller.expected.gazl" (
    CALL tools\gazl-validate.cmd "%%F"
    IF ERRORLEVEL 1 EXIT /B 1
  )
)
CALL tools\gazl-validate.cmd impala\testdata\returnContractCaller.expected.gazl impala\testdata\returnContractProviderFloat.expected.gazl
IF ERRORLEVEL 1 EXIT /B 1

REM Verify the staged Impala compiler by compiling with NuXJS and running with GAZLCmd.
output\NuXJS.exe output\impala.nuxjs.js ^
	impala\ImpalaDemo.impala 0x4d2 impala\ImpalaDemo.impala > output\ImpalaDemo.gazl
IF ERRORLEVEL 1 EXIT /B 1
output\GAZLCmd.exe output\ImpalaDemo.gazl main
IF ERRORLEVEL 1 EXIT /B 1
