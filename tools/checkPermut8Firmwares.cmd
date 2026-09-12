@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

REM Windows lane of checkPermut8Firmwares.sh - read that file for what this proves. Runs every Permut8 firmware with a
REM committed golden (benchmarks\firmware\expected\) through the pure-GAZL host harness (tools\runPermut8Firmware.cmd)
REM and compares the output checksum. Extra arguments are passed through to GAZLCmd - in particular `--jit` turns this
REM into an interp-vs-JIT DIFFERENTIAL suite over the real firmwares, since the goldens are interpreter-produced.
REM Exits non-zero on any mismatch. THIS FILE AND checkPermut8Firmwares.sh MUST STAY IN LOCKSTEP.
REM
REM Usage: tools\checkPermut8Firmwares.cmd [--jit ...]

SET /A fails=0
SET /A count=0
FOR %%E IN (benchmarks\firmware\expected\*.checksum) DO (
	SET "NAME=%%~nE"
	SET "FW=benchmarks\firmware\golden\!NAME!.gazl"
	REM the SHIPPED release builds (bank-verified) first, then the dev/test firmwares
	IF NOT EXIST "!FW!" SET "FW=tests\impala\golden\!NAME!.gazl"
	SET "PAD=!NAME!                    "
	SET "PAD=!PAD:~0,20!"
	IF NOT EXIST "!FW!" (
		ECHO !PAD! MISSING !FW!
		SET /A fails+=1
	) ELSE (
		SET /P WANT=<"%%E"
		SET "GOT="
		FOR /F "usebackq delims=" %%O IN (`tools\runPermut8Firmware.cmd "!FW!" %* 2^>NUL`) DO SET "GOT=%%O"
		SET /A count+=1
		SET "GOTPAD=!GOT!             "
		SET "GOTPAD=!GOTPAD:~0,13!"
		IF "!GOT!"=="!WANT!" (
			ECHO !PAD! !GOTPAD! ok
		) ELSE (
			ECHO !PAD! !GOTPAD! GOLDEN^^! ^(want !WANT!^)
			SET /A fails+=1
		)
	)
)

ECHO.
IF !fails! EQU 0 (
	ECHO All !count! firmware checksums match.
) ELSE (
	ECHO !fails! FAILURE^(S^).
	EXIT /B 1
)
