@ECHO OFF
REM Builds and runs the GAZLJit RESUME-continuation test (tools\GAZLJitResumeTest.cpp). Lowers GAZL with §5.7.5 suspend
REM stubs and checks suspend/resume against the interpreter (macOS / Linux AArch64 only); the Windows lowering is future
REM work. See docs\JitEmitterHandoff.md.

CD /D "%~dp0\.."

ECHO GAZLJit currently ships only a macOS/Linux AArch64 Emitter and is not built on Windows.
ECHO Run tools\buildAndRunGAZLJitResumeTest.sh on Apple Silicon or Linux arm64 instead.
EXIT /b 0
