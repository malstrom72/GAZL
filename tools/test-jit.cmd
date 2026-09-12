@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

REM Every gate that exercises the JIT compiler on the HOST's backend. build.sh and build.cmd both call it, which is the
REM only reason the two lanes cannot run different subsets - the drift test-js.sh/.cmd suffered for months.
REM THIS FILE AND test-jit.sh MUST STAY IN LOCKSTEP.
REM
REM Around twenty seconds, and deliberately NOT the whole safety net (design\jit\JitTechnologyMap.md section 5). Left
REM manual because they cost minutes rather than seconds, or need a toolchain this lane has not got:
REM   - the byte-golden emitter tests (buildAndRunGAZLJitX64Test.sh) diff against a clang-assembled oracle .s, so there
REM     is no MSVC lane for them at all;
REM   - the 300k-deep fuzz soak per backend (fuzzSoak.ps1), the standing gate for a codegen change.
REM
REM SKIPS LOUDLY rather than failing where the host forbids executable memory. It does NOT skip when the JIT is merely
REM absent from the binary: --jit falls back to the interpreter without complaint, so a GAZLCmd built without GAZL_JIT
REM would sail through the differential below comparing the interpreter against its own goldens.

REM Host arch with no backend: buildGAZLCmd.cmd then omits the JIT sources entirely, so there is nothing to gate. Same
REM test it makes, so the two cannot disagree about what "has a backend" means (test-jit.sh asks uname -m).
SET jitarch=other
IF /I "%PROCESSOR_ARCHITEW6432%"=="ARM64" ( SET jitarch=arm64
) ELSE IF /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" ( SET jitarch=arm64
) ELSE IF /I "%PROCESSOR_ARCHITEW6432%"=="AMD64" ( SET jitarch=x64
) ELSE IF /I "%PROCESSOR_ARCHITECTURE%"=="AMD64" ( SET jitarch=x64
)
IF "%jitarch%"=="other" (
	ECHO test-jit: GAZLJit has no backend for '%PROCESSOR_ARCHITECTURE%'; skipping the JIT gates.
	EXIT /B 0
)

REM Prove the JIT is reachable BEFORE running anything whose result only differs when it is. --jit-stats prints its
REM `jitstats` line only after JitCompiler::compile has actually produced native code.
output\GAZLCmd.exe --jit-stats benchmarks\suite\golden\leibniz.gazl main >NUL 2>output\jitprobe.log
FINDSTR /C:"jitstats " output\jitprobe.log >NUL
IF NOT ERRORLEVEL 1 GOTO jitok
FINDSTR /C:"does not permit executable memory" output\jitprobe.log >NUL
IF NOT ERRORLEVEL 1 (
	ECHO test-jit: this host forbids executable memory ^(ACG^); skipping the JIT gates.
	EXIT /B 0
)
ECHO test-jit: output\GAZLCmd.exe compiled nothing to native code - was it built with -DGAZL_JIT? It said:
TYPE output\jitprobe.log
EXIT /B 1
:jitok

REM The lockstep kernel test: ~35 GAZL kernels through the real JitCompiler::compile, JIT vs interpreter over the whole
REM memory image + Status, at full fuel AND at tiny fuel (which forces every leader through suspend/resume).
CALL tools\buildAndRunGAZLJitLowerTest.cmd
IF ERRORLEVEL 1 EXIT /B 1

REM The 28 shipped Permut8 firmwares against interpreter-produced goldens: a differential over real products.
CALL tools\checkPermut8Firmwares.cmd --jit
IF ERRORLEVEL 1 EXIT /B 1

REM A short lap of the generative differential fuzzer - the thing that caught DIVf/0, the v2.3a-lite scalar deref and
REM the cross-class preload miscompile, two of them after every other test passed. 2000 programs is a smoke test only.
REM buildGazlFuzz.cmd reports a failed compile only by not producing the exe, so a stale one must not survive it.
DEL /Q output\GAZLFuzz.exe >NUL 2>&1
CALL tools\buildGazlFuzz.cmd
IF NOT EXIST output\GAZLFuzz.exe (
	ECHO test-jit: tools\buildGazlFuzz.cmd produced no output\GAZLFuzz.exe.
	EXIT /B 1
)
output\GAZLFuzz.exe --gen 2000 1 deep
IF ERRORLEVEL 1 EXIT /B 1
