@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0.."

REM An exhaustive fuzzing round: walk the generative fuzzer's deterministic seed space under --vm until a
REM time budget runs out. --vm is the point of a long round - it adds the four REFERENCE oracles (checked
REM initializers vs stdout, dead-strip output-equivalence, --range-checks neutrality, import-split
REM equivalence) on top of the crash/assert oracle the compile-only gate run (fuzzImpala.js 3000 1) uses.
REM
REM Seeds are contiguous and deterministic, so coverage is just "keep feeding the next range". Chunking
REM across processes caps the RSS a single node run accumulates from its module cache (see the header in
REM impala\fuzzImpala.js). The first chunk that reports a fault exits non-zero; we STOP on it so the
REM failing seed is preserved in its log. Reproduce any fault with
REM   node impala\fuzzImpala.js 1 <seed> --vm      (re-run just that seed)
REM   node impala\fuzzImpala.js --print <seed>     (dump the program that seed generates)
REM
REM Usage: tools\fuzz48h.cmd [hours] [chunk] [startSeed]
REM   hours     wall-clock budget           (default 48; fractional ok, e.g. 0.5)
REM   chunk     seeds per node process      (default 5000; bump if RSS stays low)
REM   startSeed first seed of the sweep     (default 1; give a worker its own band to parallelize)
REM
REM Mirrors tools\fuzz48h.sh. Output is buffered to the chunk log and echoed when the chunk finishes
REM (no live stream, so the exit code stays node's own) - use a smaller chunk for more frequent progress.

IF "%~1"=="" (SET "HOURS=48") ELSE (SET "HOURS=%~1")
IF "%~2"=="" (SET "CHUNK=5000") ELSE (SET "CHUNK=%~2")
IF "%~3"=="" (SET "start=1") ELSE (SET "start=%~3")

IF NOT EXIST "output\GAZLCmd.exe" (
    ECHO output\GAZLCmd.exe not found - --vm needs it. Build it first:
    ECHO   tools\buildGAZLCmd.cmd release
    EXIT /B 1
)
IF NOT EXIST fuzzlogs MKDIR fuzzlogs

FOR /F %%D IN ('powershell -NoProfile -Command "(Get-Date).AddHours(%HOURS%).Ticks"') DO SET "deadline=%%D"

ECHO exhaustive fuzz: %HOURS%h, %CHUNK%/chunk, from seed %start%, --vm; logs in fuzzlogs\

:loop
FOR /F %%N IN ('powershell -NoProfile -Command "if ((Get-Date).Ticks -lt !deadline!) {1} else {0}"') DO SET "go=%%N"
IF "!go!"=="0" GOTO done

SET /A end=start + CHUNK - 1
SET "log=fuzzlogs\fuzz_!start!_!end!.log"
ECHO === seeds !start!..!end!  (%DATE% %TIME%) ===
node impala\fuzzImpala.js %CHUNK% !start! --vm > "!log!" 2>&1
SET "rc=!ERRORLEVEL!"
TYPE "!log!"
IF NOT "!rc!"=="0" (
    ECHO !!! FAULT in seeds !start!..!end! - full program in !log!
    EXIT /B 1
)
SET /A start=end + 1
GOTO loop

:done
SET /A lastseed=start - 1
ECHO clean: no faults through seed !lastseed! in %HOURS%h
EXIT /B 0
