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
COPY /Y output\GAZLCmd.exe impala\GAZLCmd.exe >NUL

REM Build Impala
PUSHD tools
CALL BuildImpala.cmd
IF ERRORLEVEL 1 EXIT /B 1
POPD

REM Run the Impala test suite from the source directory
PUSHD impala\jspeg
node jspegCompilerTests.js
IF ERRORLEVEL 1 EXIT /B 1
node runJspegTests.js
IF ERRORLEVEL 1 EXIT /B 1
POPD

REM Validate generated .gazl metadata for the JSPEG fixtures.
FOR %%F IN (impala\jspeg\testdata\*.expected.gazl) DO (
  IF /I NOT "%%~nxF"=="externAssignment.expected.gazl" IF /I NOT "%%~nxF"=="returnContractCaller.expected.gazl" (
    CALL tools\gazl-validate.cmd "%%F"
    IF ERRORLEVEL 1 EXIT /B 1
  )
)
CALL tools\gazl-validate.cmd impala\jspeg\testdata\returnContractCaller.expected.gazl impala\jspeg\testdata\returnContractProviderFloat.expected.gazl
IF ERRORLEVEL 1 EXIT /B 1

REM Verify the copied files by running the demo from the output directory
PUSHD output
PikaCmd impala.pika run ..\impala\ImpalaDemo.impala
IF ERRORLEVEL 1 EXIT /B 1
POPD
