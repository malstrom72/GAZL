@ECHO OFF
REM Builds and runs the Phase -1 spike A3 benchmark (interpreter vs hand-written baseline
REM JIT). The hand-written machine code is AArch64 (macOS / Linux) only; the Windows
REM lowering is future work, so this spike is not supported on Windows yet.

CD /D "%~dp0\..\.."

ECHO JitBenchA3 currently ships only a macOS/Linux AArch64 hand-JIT and is not built on
ECHO Windows. Run benchmarks\jit\buildAndRunJitBenchA3.sh on Apple Silicon or Linux arm64 instead.
EXIT /b 0
