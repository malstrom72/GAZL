@ECHO OFF
REM Builds and runs the GAZLJit calls test (tools\GAZLJitCallTest.cpp). GAZL->GAZL calls under the §5.4 dispatcher model,
REM checked against the interpreter (macOS / Linux AArch64 only); the Windows lowering is future work.

CD /D "%~dp0\.."

ECHO GAZLJit currently ships only a macOS/Linux AArch64 Emitter and is not built on Windows.
ECHO Run tools\buildAndRunGAZLJitCallTest.sh on Apple Silicon or Linux arm64 instead.
EXIT /b 0
