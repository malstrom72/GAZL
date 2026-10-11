@ECHO OFF
REM Wrapper for tools\cascade.js; see there for what it does and its options.
CD /D "%~dp0\.."
node tools\cascade.js %*
EXIT /B %ERRORLEVEL%
