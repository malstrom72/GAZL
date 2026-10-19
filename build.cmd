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

REM Replay the committed fuzz corpus and the fixed-crash inputs, shared with build.sh. Runs here, straight after the
REM engine it exercises, so a regression surfaces before the slower node suite rather than after it.
CALL tools\test-fuzz.cmd
IF ERRORLEVEL 1 EXIT /B 1

REM Every memory instruction form at every edge of the memory arena, against an oracle (tools\GAZLBoundsSweep.cpp).
CALL tools\test-bounds.cmd
IF ERRORLEVEL 1 EXIT /B 1

REM Float literals round correctly, sampled across every float (tools\GAZLFloatLiteralTest.cpp).
CALL tools\test-float-literals.cmd
IF ERRORLEVEL 1 EXIT /B 1

REM Several processors in one memory, cooperative and on threads (tools\GAZLMultiProcessorTest.cpp).
CALL tools\test-processors.cmd
IF ERRORLEVEL 1 EXIT /B 1

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
	impala\ImpalaDemo.impala output\ImpalaDemo.gazl 0x4d2 impala\ImpalaDemo.impala
IF ERRORLEVEL 1 EXIT /B 1
output\GAZLCmd.exe output\ImpalaDemo.gazl main
IF ERRORLEVEL 1 EXIT /B 1

REM Impala keeps named constants in its output, so a golden that only matches text can still fail to assemble.
output\GAZLCmd.exe tests\impala\golden\pointerConstants.gazl main
IF ERRORLEVEL 1 EXIT /B 1

REM The staging above only proves the compiler fits TODAY's nesting limit. This fails while there is still headroom,
REM because the limit is NuXJS's to change: when it dropped to 48, GAZL stopped building with no prior warning.
CALL tools\checkImpalaNesting.cmd
IF ERRORLEVEL 1 EXIT /B 1
