@ECHO OFF
REM Dispatch-layout search: benchmark seeded case orders of Processor::run's switch. See tools\layoutSearch.js.
REM Usage: tools\layoutSearch.cmd <first seed> <last seed> <out.csv> [--drift=20] [--pin=N]
SETLOCAL ENABLEEXTENSIONS
REM a relative <out.csv> is relative to the caller, not the repo root
SET "LAYOUT_SEARCH_CWD=%CD%"
CD /D "%~dp0\.."
node tools\layoutSearch.js %* || GOTO error
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
