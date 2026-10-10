@ECHO OFF
SETLOCAL ENABLEEXTENSIONS
CD /D %~dp0
IF NOT EXIST ..\output MKDIR ..\output
IF /I "%~1"=="text" GOTO text
REM Windows fuzz build: the standalone --gen JIT-vs-interpreter differential fuzzer (MSVC ships no libFuzzer, so the
REM coverage-guided modes of buildGazlFuzz.sh are unavailable here; --gen walks a seed stream instead). 'beta' = /O2
REM but asserts ON (/D DEBUG, no NDEBUG) - the internal RegisterCache / finalize / contract asserts MUST fire while
REM fuzzing. x64 backend + Windows W^X memory backend; GAZLCpp is omitted (the standalone main doesn't transpile).
CALL BuildCpp.cmd beta x64 ..\output\GAZLFuzz.exe -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_JIT -DJITDIFF -DGAZL_CANONICAL_NAN -I.. GAZLCmd.cpp ..\src\GAZL.cpp ..\src\GAZLJit.cpp ..\src\GAZLJitX64.cpp ..\src\GAZLJitMemWindows.cpp
IF EXIST ..\output\GAZLFuzz.exe ECHO Built output\GAZLFuzz.exe
EXIT /B 0

:text
REM The text lane as a plain replay binary, the twin of "buildGazlFuzz.sh standalone text": every file or @listfile
REM argument is assembled as GAZL source and run through both engines. Beta, so asserts fire.
CALL BuildCpp.cmd beta x64 ..\output\GAZLFuzzText.exe -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_JIT -DJITDIFF -DFUZZ_TEXT_INPUT -DGAZL_CANONICAL_NAN -I.. GAZLCmd.cpp ..\src\GAZL.cpp ..\src\GAZLJit.cpp ..\src\GAZLJitX64.cpp ..\src\GAZLJitMemWindows.cpp
IF EXIST ..\output\GAZLFuzzText.exe ECHO Built output\GAZLFuzzText.exe
EXIT /B 0
