@ECHO OFF
REM Builds and runs the GAZLJit C3 vertical-slice test (tools\GAZLJitSliceTest.cpp). Compares Emitter-produced native
REM code executed from W^X memory against the GAZL interpreter (macOS / Linux AArch64 only); the Windows lowering and
REM its executable-memory path are future work, so this test is not built on Windows yet. See docs\JitEmitterHandoff.md.

CD /D "%~dp0\.."

ECHO GAZLJit currently ships only a macOS/Linux AArch64 Emitter and is not built on Windows.
ECHO Run tools\buildAndRunGAZLJitSliceTest.sh on Apple Silicon or Linux arm64 instead.
EXIT /b 0
