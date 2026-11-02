@ECHO OFF
REM Replay the seeds and the fixed-crash inputs through the assembler and interpreter, checking each run against the
REM reference executor, and require that none of them crashes or disagrees. This is a REGRESSION gate, not a fuzz
REM run: it finds nothing new, it stops the defects already found from coming back. The .sh twin does the same work
REM with the same driver. The corpus is over the normal build's 30 s budget, so replayFuzzCorpus.cmd replays it
REM instead.
REM
REM Build with the ordinary toolchain, no sanitizers and no libFuzzer: this must run everywhere the normal build
REM runs. `beta` keeps asserts on, so a broken internal contract fails here too.
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0.."
IF NOT EXIST output MKDIR output

ECHO test-fuzz: building the replay driver
PUSHD tools
CALL BuildCpp.cmd beta x64 ..\output\GAZLReplay.exe -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_CANONICAL_NAN -I.. GAZLCmd.cpp GAZLReference.cpp ..\src\GAZL.cpp || GOTO error
POPD

SET "WORK=output\fuzz\replay"
IF EXIST "%WORK%" RMDIR /S /Q "%WORK%"
MKDIR "%WORK%" || GOTO error

REM One path per line, like the .sh twin, so the list never meets a command-line length limit.
SET "LIST=%WORK%\inputs.txt"
IF EXIST "%LIST%" DEL /Q "%LIST%"
FOR /F "delims=" %%F IN ('DIR /S /B "tests\fuzz\textCrashes\*.gazl" 2^>NUL') DO ECHO %%F>>"%LIST%"
FOR /F "delims=" %%F IN ('DIR /S /B "tests\fuzz\textSeeds\*.gazl" 2^>NUL') DO ECHO %%F>>"%LIST%"
FOR /F %%C IN ('FIND /C /V "" ^< "%LIST%"') DO SET "N=%%C"

ECHO test-fuzz: replaying %N% inputs
REM --budget warns past the normal build's 30 s budget
output\GAZLReplay.exe --budget=30 "@%LIST%" >NUL || GOTO error
ECHO test-fuzz: %N% inputs replayed, no crashes
EXIT /b 0

:error
ECHO test-fuzz FAILED
EXIT /b %ERRORLEVEL%
