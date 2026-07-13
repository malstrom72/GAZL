@ECHO OFF
REM Builds and runs the GAZLJit v1 lowering-pass test (tools\GAZLJitLowerTest.cpp). Compiles GAZL functions from their
REM Instruction[] to arm64 and checks them against the interpreter (macOS / Linux AArch64 only); the Windows lowering is
REM future work. See docs\JitEmitterHandoff.md.

CD /D "%~dp0\.."

ECHO GAZLJit currently ships only a macOS/Linux AArch64 Emitter and is not built on Windows.
ECHO Run tools\buildAndRunGAZLJitLowerTest.sh on Apple Silicon or Linux arm64 instead.
EXIT /b 0
