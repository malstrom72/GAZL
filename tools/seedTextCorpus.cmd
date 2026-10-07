@ECHO OFF
REM Seed the text-assembler fuzz corpus with real GAZL source, so libFuzzer mutates valid programs instead of
REM starting from pure garbage the assembler rejects. Copies every .gazl in the repo into the corpus dir.
REM Idempotent; safe to re-run. The .cmd twin of seedTextCorpus.sh, and it must seed the same files.
REM   tools\seedTextCorpus.cmd [corpus-dir]
REM
REM Only SMALL programs (< 8 KiB): a seed is assembled AND RUN, so a huge program (some impala goldens are
REM 200 KB) takes tens of seconds per unit and starves fuzzing. Small programs still give broad, diverse valid
REM syntax; mutations grow coverage from there.
REM
REM Enumerate with DIR /S /B rather than FOR /R: a FOR /R root taken from an enclosing FOR variable does not
REM substitute reliably, and the first version of this script silently seeded nothing because of it.
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0.."
SET "CORPUS=%~1"
IF "%CORPUS%"=="" SET "CORPUS=output\fuzz\lane6_text\corpus"
IF NOT EXIST "%CORPUS%" MKDIR "%CORPUS%"
SET /A N=0
FOR %%D IN (tests src docs temp) DO (
	IF EXIST "%%D\" (
		FOR /F "delims=" %%F IN ('DIR /S /B "%%D\*.gazl" 2^>NUL') DO (
			ECHO %%F| FINDSTR /I /C:"\output\" >NUL || (
				REM 7168, not 8192: the .sh uses `find -size -8k`, which ROUNDS SIZE UP to whole KiB and so
				REM matches only files of 7 KiB or less. Matching that exactly is what makes this a twin - at
				REM 8192 the two scripts disagreed by one file (floatVerber8.gazl, 7786 bytes).
				IF %%~zF LEQ 7168 (
					SET "IDX=00!N!"
					COPY /Y "%%F" "%CORPUS%\seed_!IDX:~-3!_%%~nxF" >NUL
					SET /A N+=1
				)
			)
		)
	)
)
ECHO seeded %N% small GAZL programs ^(^<8KiB^) into %CORPUS%
EXIT /b 0
