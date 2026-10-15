@ECHO OFF
REM Interpreter benchmark driver (Windows / MSVC) -- parity with bench.sh.
REM Builds release GAZLCmdBench.exe (/O2 WITHOUT /GL), then runs the micro
REM (per-instruction) and macro workloads under --bench mode, printing one 'bench' line per
REM workload with min/median/mean/stddev (ms).
REM
REM Unlike bench.sh, this uses ONE /O2 binary for BOTH groups: MSVC /O2 does NOT show the
REM Apple-Silicon-clang dispatch-alignment artifact that forces micro-benches to -Os there
REM (measured MSVC /O2 micro cluster is sane: add~=xor~=102ms, sub~=94ms), so /O2 is both
REM stable AND representative on Windows. Do not edit BuildCpp.cmd (shared/synced).
REM
REM /GL is turned off (/GL-) because whole-program optimisation lets the HARNESS reshape
REM Processor::run: a change to GAZLCmd.cpp alone moved every kernel by about 25%
REM (design/gazl/Measurements.md, 2026-10-15). Without /GL only src/ decides the layout.
REM
REM Usage:  tools\bench.cmd [iters] [warmup]     (defaults: 10 measured, 3 warmup)
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

SET ITERS=%1
IF "%ITERS%"=="" SET ITERS=10
SET WARMUP=%2
IF "%WARMUP%"=="" SET WARMUP=3

ECHO Building release GAZLCmdBench.exe without /GL...
PUSHD tools
SET CPP_OPTIONS=/GL-
CALL BuildCpp.cmd release x64 ..\output\GAZLCmdBench.exe -I.. GAZLCmd.cpp ..\src\GAZL.cpp >NUL
IF ERRORLEVEL 1 ( ECHO Build failed & POPD & EXIT /B 1 )
POPD

ECHO.
ECHO === micro (128M ops; xor = control, must stay flat) ===
FOR %%O IN (add sub mul div mod shl shr shru abs ftoi xor) DO (
  output\GAZLCmdBench.exe tests\bench\golden\op_%%O.gazl main --bench=%ITERS% --warmup=%WARMUP% 2>NUL | findstr /B bench
)
ECHO.
ECHO === macro ===
FOR %%O IN (perfTest1 perfTest2) DO (
  output\GAZLCmdBench.exe tests\impala\golden\%%O.gazl main --bench=%ITERS% --warmup=%WARMUP% 2>NUL | findstr /B bench
)
