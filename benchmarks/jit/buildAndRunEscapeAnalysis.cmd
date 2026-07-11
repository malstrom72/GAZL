@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
REM Builds and runs JitEscapeAnalysis over the golden Impala corpus. Measures how much the
REM "escape floor" register-allocation design (docs/JitCompilerResearch.md 1.1/5.7) pessimizes
REM hot scalar accesses, to decide escape-floor vs barrier caching. See benchmarks/jit/README.md.
CD /D "%~dp0\..\.."
IF NOT EXIST output MKDIR output
CALL tools\BuildCpp.cmd release x64 output\JitEscapeAnalysis.exe benchmarks\jit\JitEscapeAnalysis.cpp || GOTO error
output\JitEscapeAnalysis.exe tests\impala\golden || GOTO error
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
