@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0
IF "%~1"=="" (
    SET mode=release
) ELSE (
    SET mode=%1
)
IF NOT EXIST ..\output MKDIR ..\output
CALL UpdateUnitTest.cmd
IF "%mode%"=="beta" (
    SET out=..\output\GAZLCmdBeta.exe
) ELSE (
    SET out=..\output\GAZLCmd.exe
)

REM Pick the JIT backend by host architecture. GAZLCmd.cpp is arch-neutral (it drives JitCompiler / JitProcessor); only
REM the backend .cpp differs (GAZLJitX64.cpp vs GAZLJitArm64.cpp), plus the shared GAZLJit.cpp and the Windows W^X
REM memory backend GAZLJitMemWindows.cpp. On an unrecognized architecture the JIT sources are omitted and --jit
REM transparently falls back to the interpreter (see the GAZL_JIT guards in GAZLCmd.cpp). Mirrors buildGAZLCmd.sh.
SET jitarch=other
IF /I "%PROCESSOR_ARCHITEW6432%"=="ARM64" ( SET jitarch=arm64
) ELSE IF /I "%PROCESSOR_ARCHITECTURE%"=="ARM64" ( SET jitarch=arm64
) ELSE IF /I "%PROCESSOR_ARCHITEW6432%"=="AMD64" ( SET jitarch=x64
) ELSE IF /I "%PROCESSOR_ARCHITECTURE%"=="AMD64" ( SET jitarch=x64
)

IF "%jitarch%"=="x64" (
    CALL BuildCpp.cmd %mode% x64 %out% -DGAZL_JIT -I.. GAZLCmd.cpp ..\src\GAZL.cpp ..\src\GAZLCpp.cpp ..\src\GAZLJit.cpp ..\src\GAZLJitX64.cpp ..\src\GAZLJitMemWindows.cpp
) ELSE IF "%jitarch%"=="arm64" (
    CALL BuildCpp.cmd %mode% arm64 %out% -DGAZL_JIT -I.. GAZLCmd.cpp ..\src\GAZL.cpp ..\src\GAZLCpp.cpp ..\src\GAZLJit.cpp ..\src\GAZLJitArm64.cpp ..\src\GAZLJitMemWindows.cpp
) ELSE (
    CALL BuildCpp.cmd %mode% native %out% -I.. GAZLCmd.cpp ..\src\GAZL.cpp ..\src\GAZLCpp.cpp
)
IF EXIST %out% ATTRIB +x %out% >NUL 2>&1
