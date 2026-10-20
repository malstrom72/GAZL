@ECHO OFF
REM Build GAZLCmd as a GAZL 1.0-compatible engine (`GAZL_2=0`) and run it, so its built-in unit test runs. `beta`
REM keeps asserts on. The .sh twin does the same with the default POSIX compiler. `#` stands for `=` in the define,
REM which cmd would otherwise split into two arguments.
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0.."
IF NOT EXIST output MKDIR output

ECHO test-gazl1-engine: building GAZLCmd with GAZL_2=0
PUSHD tools
CALL BuildCpp.cmd beta x64 ..\output\GAZLCmdGazl1Beta.exe -DGAZL_2#0 -I.. GAZLCmd.cpp ..\src\GAZL.cpp ..\src\GAZLCpp.cpp || GOTO error
POPD
output\GAZLCmdGazl1Beta.exe || GOTO error
EXIT /b 0

:error
ECHO test-gazl1-engine FAILED
EXIT /b 1
