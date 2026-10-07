@ECHO OFF
REM Windows build of the text-assembler fuzz target: libFuzzer + AddressSanitizer over the assembler and the
REM interpreter. The .cmd twin of buildGazlFuzz.sh, and DELIBERATELY NOT behaviour-identical to it.
REM
REM The .sh uses clang with -fsanitize=fuzzer,address (and `undefined` when the caller asks). This uses MSVC's
REM libFuzzer and ASan instead, because clang-cl is not usable for this target: its sanitizer instrumentation
REM breaks MSVC C++ exception handling (llvm#212404, fix not in LLVM 23), and this assembler THROWS to reject a
REM malformed program, which is its normal path. A clang-cl build can therefore silently run the wrong code
REM rather than crash, so a clean run there proves nothing. The cost of using MSVC is that there is no UBSan on
REM Windows; UBSan coverage comes from the POSIX lane. See design/fuzzing.md, which records both.
REM
REM Three things that are easy to get wrong here:
REM   - the two /fsanitize options must be SEPARATE. MSVC drops `/fsanitize=fuzzer,address` with only a
REM     "D9002 ignoring unknown option" warning, and the build then fails at link with "LNK1561: entry point
REM     must be defined", which looks like a linker problem and is not.
REM   - the RELEASE target, because libFuzzer wants the static release CRT. `/U NDEBUG` keeps asserts on.
REM   - both _DISABLE_*_ANNOTATION defines, or linking fails on `annotate_string`.
SETLOCAL ENABLEEXTENSIONS
CD /D %~dp0
IF NOT EXIST ..\output MKDIR ..\output
SET "CPP_OPTIONS=/fsanitize=fuzzer /fsanitize=address /Zi /U NDEBUG /D _DISABLE_STRING_ANNOTATION /D _DISABLE_VECTOR_ANNOTATION /D LIBFUZZ"
CALL BuildCpp.cmd release x64 ..\output\GAZLFuzz.exe -I.. GAZLCmd.cpp ..\src\GAZL.cpp || GOTO error

REM ASan links against a DLL that lives in the MSVC toolchain, not beside the exe, so the fuzzer exits
REM 0xC0000135 (DLL not found) anywhere but a developer prompt. Copy it next to the binary so the lane runs from
REM a plain shell, a script or a scheduler.
REM BuildCpp.cmd calls vcvarsall in its own scope, so %VCToolsInstallDir% is not set here. Search the Visual
REM Studio tree for the DLL instead, and take the host-x64 copy.
FOR %%R IN ("%ProgramFiles%\Microsoft Visual Studio" "%ProgramFiles(x86)%\Microsoft Visual Studio") DO (
	IF NOT DEFINED ASANDLL IF EXIST "%%~R" (
		REM FINDSTR /C: does not take backslashes literally, so match on Hostx64 alone (the only other host is x86).
		FOR /F "delims=" %%A IN ('DIR /S /B "%%~R\clang_rt.asan_dynamic-x86_64.dll" 2^>NUL ^| FINDSTR /I /C:Hostx64') DO (
			IF NOT DEFINED ASANDLL SET "ASANDLL=%%A"
		)
	)
)
IF DEFINED ASANDLL (
	COPY /Y "%ASANDLL%" ..\output\ >NUL
) ELSE (
	ECHO buildGazlFuzz: WARNING - clang_rt.asan_dynamic-x86_64.dll not found; run the fuzzer from a developer prompt
)

IF EXIST ..\output\GAZLFuzz.exe ECHO Built output\GAZLFuzz.exe
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
