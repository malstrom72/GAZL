@ECHO OFF
REM Builds and runs the GAZLJit C3-full/C4 engine prototype (tools\GAZLJitEngineTest.cpp). A JitEngine subclass drives
REM Emitter-produced native code over shared VM state and is checked against the interpreter (macOS / Linux AArch64
REM only); the Windows lowering and its executable-memory path are future work. See docs\JitEmitterHandoff.md.

CD /D "%~dp0\.."

ECHO GAZLJit currently ships only a macOS/Linux AArch64 Emitter and is not built on Windows.
ECHO Run tools\buildAndRunGAZLJitEngineTest.sh on Apple Silicon or Linux arm64 instead.
EXIT /b 0
