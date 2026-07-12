@ECHO OFF
REM Builds and runs the GAZLJit arm64 Emitter execution test (tools\GAZLJitExecTest.cpp). Emits kernels through the
REM Emitter and runs them from W^X memory (macOS / Linux AArch64 only); the Windows lowering and its executable-memory
REM path are future work, so this test is not built on Windows yet. See docs\JitEmitterHandoff.md.

CD /D "%~dp0\.."

ECHO GAZLJit currently ships only a macOS/Linux AArch64 Emitter and is not built on Windows.
ECHO Run tools\buildAndRunGAZLJitExecTest.sh on Apple Silicon or Linux arm64 instead.
EXIT /b 0
