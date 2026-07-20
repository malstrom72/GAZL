@ECHO OFF
SETLOCAL ENABLEEXTENSIONS
CD /D %~dp0
IF NOT EXIST ..\output MKDIR ..\output
REM Windows fuzz build: the standalone --gen JIT-vs-interpreter differential fuzzer (MSVC ships no libFuzzer, so the
REM coverage-guided modes of buildGazlFuzz.sh are unavailable here; --gen walks a seed stream instead). 'beta' = /O2
REM but asserts ON (/D DEBUG, no NDEBUG) - the internal RegisterCache / finalize / contract asserts MUST fire while
REM fuzzing. x64 backend + Windows W^X memory backend; GAZLCpp is omitted (the standalone main doesn't transpile).
CALL BuildCpp.cmd beta x64 ..\output\GAZLFuzz.exe -DLIBFUZZ -DLIBFUZZ_STANDALONE -DGAZL_JIT -DJITDIFF -I.. GAZLCmd.cpp ..\src\GAZL.cpp ..\src\GAZLJit.cpp ..\src\GAZLJitX64.cpp ..\src\GAZLJitMemWindows.cpp
IF EXIST ..\output\GAZLFuzz.exe ECHO Built output\GAZLFuzz.exe
