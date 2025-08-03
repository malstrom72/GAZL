@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D %~dp0
IF NOT EXIST ..\output MKDIR ..\output
IF "%CPP_COMPILER%"=="" SET CPP_COMPILER=clang++
IF "%CPP_OPTIONS%"=="" SET CPP_OPTIONS=-fsanitize=fuzzer,address -DLIBFUZZ
%CPP_COMPILER% -pipe -fvisibility=hidden -fvisibility-inlines-hidden ^
		-Wno-trigraphs -Wreturn-type -Wunused-variable -Os -DNDEBUG %CPP_OPTIONS% ^
		-I.. GAZLCmd.cpp ..\src\GAZL.cpp -o ..\output\GAZLFuzz.exe || GOTO error
ATTRIB +x ..\output\GAZLFuzz.exe >NUL 2>&1
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
