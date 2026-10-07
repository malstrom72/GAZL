@ECHO OFF
REM Replay the committed text-lane corpus and the fixed-crash inputs through the assembler and interpreter, and
REM require that none of them crashes. This is a REGRESSION gate, not a fuzz run: it finds nothing new, it stops the
REM defects already found from coming back. The .sh twin does the same work with the same driver.
REM
REM Build with the ordinary toolchain, no sanitizers and no libFuzzer: this must run everywhere the normal build
REM runs. `beta` keeps asserts on, so a broken internal contract fails here too.
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0.."
IF NOT EXIST output MKDIR output

ECHO test-fuzz: building the replay driver
PUSHD tools
CALL BuildCpp.cmd beta x64 ..\output\GAZLReplay.exe -DLIBFUZZ -DLIBFUZZ_STANDALONE -I.. GAZLCmd.cpp ..\src\GAZL.cpp || GOTO error
POPD

REM Unpack into an EMPTY folder, or inputs left from a previous run are replayed too (design/fuzzing.md).
SET "WORK=output\fuzz\replay"
IF EXIST "%WORK%" RMDIR /S /Q "%WORK%"
MKDIR "%WORK%" || GOTO error
tar -xzf tests\fuzz\textCorpus.tar.gz -C "%WORK%" || GOTO error

REM One path per line, because a thousand-odd paths do not fit in a command line.
SET "LIST=%WORK%\inputs.txt"
IF EXIST "%LIST%" DEL /Q "%LIST%"
FOR /F "delims=" %%F IN ('DIR /S /B "%WORK%\corpus\*" 2^>NUL') DO ECHO %%F>>"%LIST%"
FOR /F "delims=" %%F IN ('DIR /S /B "tests\fuzz\textCrashes\*.gazl" 2^>NUL') DO ECHO %%F>>"%LIST%"
FOR /F %%C IN ('FIND /C /V "" ^< "%LIST%"') DO SET "N=%%C"

ECHO test-fuzz: replaying %N% inputs
output\GAZLReplay.exe "@%LIST%" >NUL || GOTO error
ECHO test-fuzz: %N% inputs replayed, no crashes
EXIT /b 0

:error
ECHO test-fuzz FAILED
EXIT /b %ERRORLEVEL%
