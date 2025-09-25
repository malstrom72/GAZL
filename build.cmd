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

REM Optionally validate emitted .gazl metadata when requested
IF NOT "%GAZL_VALIDATE%"=="" IF NOT "%GAZL_VALIDATE%"=="0" (
  SET VALIDATOR_FILES=
  IF NOT "%GAZL_VALIDATE_FILES%"=="" (
    SET VALIDATOR_FILES=%GAZL_VALIDATE_FILES%
  ) ELSE (
    FOR %%F IN (impala\jspeg\testdata\*.gazl) DO (
      SET VALIDATOR_FILES=!VALIDATOR_FILES! "%%F"
    )
  )
  IF NOT "!VALIDATOR_FILES!"=="" (
    CALL tools\gazl-validate.cmd !VALIDATOR_FILES!
    IF ERRORLEVEL 1 EXIT /B 1
  ) ELSE (
    ECHO GAZL_VALIDATE enabled but no .gazl files were found for validation.
  )
)

REM Verify the copied files by running the demo from the output directory
PUSHD output
PikaCmd impala.pika run ..\impala\ImpalaDemo.impala
IF ERRORLEVEL 1 EXIT /B 1
POPD

