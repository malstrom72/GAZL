@ECHO OFF
REM Twin of checkImpalaNesting.sh: fail when the generated Impala compiler's compile-time NESTING need grows toward
REM NuXJS's limit. See the .sh for why this exists and how it measures without touching NuXJS.
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

IF "%MAX_NEED%"=="" SET MAX_NEED=80
SET NUXJS=output\NuXJS.exe
SET SRC=output\impalaCompiler.js
SET WORK=output\nestProbe
SET MEASURE=0
IF /I "%~1"=="--measure" SET MEASURE=1

IF NOT EXIST "%NUXJS%" ECHO check-nesting: %NUXJS% not built; run the build first& EXIT /B 1
IF NOT EXIST "%SRC%" ECHO check-nesting: %SRC% not generated; run BuildImpala first& EXIT /B 1

RD /S /Q "%WORK%" 2>NUL
MKDIR "%WORK%" 2>NUL

REM The limit is NuXJS's, so read it from the vendored source rather than restating it here. Through a temp file: a
REM FOR /F over a FINDSTR command line carrying quotes does not survive cmd's parser.
SET LIMIT=
FINDSTR /C:"const Int32 MAX_NESTED_COMPILE_DEPTH" externals\NuXJS\src\NuXJS.cpp > "%WORK%\limit.txt"
FOR /F "tokens=5 delims=; " %%A IN (%WORK%\limit.txt) DO IF NOT DEFINED LIMIT SET LIMIT=%%A
IF NOT DEFINED LIMIT ECHO check-nesting: could not read MAX_NESTED_COMPILE_DEPTH from the vendored NuXJS& EXIT /B 1

IF "%MEASURE%"=="1" GOTO measure

IF %MAX_NEED% GEQ %LIMIT% (
	ECHO check-nesting: NuXJS's MAX_NESTED_COMPILE_DEPTH is %LIMIT%, at or below the %MAX_NEED% this project allows
	EXIT /B 1
)
SET /A K=%LIMIT% - %MAX_NEED%
CALL :compiles %K%
IF "%COMPILED%"=="1" (
	ECHO check-nesting: Impala compiler needs at most %MAX_NEED% of %LIMIT% levels
	EXIT /B 0
)
ECHO check-nesting: FAILED - the Impala compiler now needs more than %MAX_NEED% of NuXJS's %LIMIT% levels.
ECHO   Its nesting comes from the generated grammar, so a grammar change is the likely cause.
ECHO   Run "tools\checkImpalaNesting.cmd --measure" for the exact figure, then either reduce the
ECHO   grammar's nesting or raise MAX_NEED here deliberately, recording the new number.
EXIT /B 1

:measure
CALL :compiles 0
IF NOT "%COMPILED%"=="1" ECHO check-nesting: the compiler does not compile even unwrapped& EXIT /B 1
CALL :compiles %LIMIT%
IF "%COMPILED%"=="1" ECHO check-nesting: wrapper consumed no depth, probe invalid& EXIT /B 1
SET /A LO=0
SET /A HI=%LIMIT%
:bisect
SET /A SPAN=%HI% - %LO%
IF %SPAN% LEQ 1 GOTO bisectDone
SET /A MID=(%LO% + %HI%) / 2
CALL :compiles %MID%
IF "%COMPILED%"=="1" (SET /A LO=%MID%) ELSE (SET /A HI=%MID%)
GOTO bisect
:bisectDone
SET /A NEED=%LIMIT% - %LO%
ECHO check-nesting: peak need %NEED% of %LIMIT% levels, headroom %LO%
EXIT /B 0

REM %1 = K wrapper blocks. Sets COMPILED to 1 when it compiled, 0 when it hit the nesting limit.
:compiles
SET F=%WORK%\wrapped.js
TYPE NUL > "%F%"
IF %1 GTR 0 FOR /L %%I IN (1,1,%1) DO >>"%F%" ECHO {
TYPE "%SRC%" >> "%F%"
IF %1 GTR 0 FOR /L %%I IN (1,1,%1) DO >>"%F%" ECHO }
"%NUXJS%" "%F%" > "%WORK%\out.txt" 2>&1
SET COMPILED=1
FINDSTR /C:"Internal compiler limitations reached" "%WORK%\out.txt" >NUL && SET COMPILED=0
EXIT /B 0
