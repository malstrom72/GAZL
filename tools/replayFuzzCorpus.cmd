@ECHO OFF
REM Replay the committed text-lane corpus, the seeds and the fixed-crash inputs through the assembler and
REM interpreter, checking each run against the reference executor, and require that none of them crashes or
REM disagrees. The corpus replay takes longer than the normal build's 30 s budget, so it runs here instead of in
REM test-fuzz: in CI, and before every release or freeze (design/fuzzing.md). The .sh twin does the same work with
REM the same driver.
REM
REM Usage: replayFuzzCorpus.cmd (the .sh twin's `valgrind` option is Linux only)
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0.."
IF "%~1"=="valgrind" (
	ECHO Valgrind runs on Linux only: use replayFuzzCorpus.sh there.
	EXIT /B 1
)
IF NOT "%~1"=="" (
	ECHO Usage: replayFuzzCorpus.cmd
	EXIT /B 1
)
IF NOT EXIST output MKDIR output

ECHO replayFuzzCorpus: building the replay driver
PUSHD tools
CALL BuildCpp.cmd beta x64 ..\output\GAZLReplay.exe -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_CANONICAL_NAN -I.. GAZLCmd.cpp GAZLReference.cpp ..\src\GAZL.cpp || GOTO error
POPD

REM Unpack into an EMPTY folder, or inputs left from a previous run are replayed too (design/fuzzing.md).
SET "WORK=output\fuzz\corpusReplay"
IF EXIST "%WORK%" RMDIR /S /Q "%WORK%"
MKDIR "%WORK%" || GOTO error
tar -xzf tests\fuzz\textCorpus.tar.gz -C "%WORK%" || GOTO error

REM One path per line, because a thousand-odd paths do not fit in a command line.
SET "LIST=%WORK%\inputs.txt"
IF EXIST "%LIST%" DEL /Q "%LIST%"
FOR /F "delims=" %%F IN ('DIR /S /B "%WORK%\corpus\*" 2^>NUL') DO ECHO %%F>>"%LIST%"
FOR /F "delims=" %%F IN ('DIR /S /B "tests\fuzz\textCrashes\*.gazl" 2^>NUL') DO ECHO %%F>>"%LIST%"
FOR /F "delims=" %%F IN ('DIR /S /B "tests\fuzz\textSeeds\*.gazl" 2^>NUL') DO ECHO %%F>>"%LIST%"
FOR /F %%C IN ('FIND /C /V "" ^< "%LIST%"') DO SET "N=%%C"

ECHO replayFuzzCorpus: replaying %N% inputs
output\GAZLReplay.exe "@%LIST%" >NUL || GOTO error
ECHO replayFuzzCorpus: %N% inputs replayed, no crashes
EXIT /b 0

:error
ECHO replayFuzzCorpus FAILED
EXIT /b %ERRORLEVEL%
