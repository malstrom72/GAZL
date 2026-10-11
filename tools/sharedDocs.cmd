@ECHO OFF
REM Wrapper for tools\sharedDocs.js; see there for what it does and its options.
CD /D "%~dp0\.."
node tools\sharedDocs.js %*
EXIT /B %ERRORLEVEL%
