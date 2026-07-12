@ECHO OFF
REM Builds and runs the GAZLJit arm64 Emitter assemble-diff test (tools\GAZLJitTest.cpp). The Emitter and its
REM clang-assembled reference target AArch64 (macOS / Linux) only; the Windows lowering is future work, so this test is
REM not built on Windows yet. See docs\JitEmitterHandoff.md.

CD /D "%~dp0\.."

ECHO GAZLJit currently ships only a macOS/Linux AArch64 Emitter and is not built on Windows.
ECHO Run tools\buildAndRunGAZLJitTest.sh on Apple Silicon or Linux arm64 instead.
EXIT /b 0
