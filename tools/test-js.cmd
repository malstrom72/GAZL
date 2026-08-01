@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

REM Every gate that needs nothing but node. Under 15 seconds, no C++ toolchain, so this is what you run
REM before committing a compiler-only change. build.sh and build.cmd both call it, which is the only reason
REM they cannot drift apart again - they used to run different subsets of this list.
REM
REM These degrade rather than fail when output\GAZLCmd is absent: runJspegTests and jspegCompilerTests skip
REM their assemble+run checks and say so. Metadata validation is NOT here, because gazl-validate runs under
REM NuXJS and so needs the build.

REM The checked-in compiler must match the grammar it is generated from, or the playground goes stale
REM (impala\playground.html loads impala\impalaCompiler.js directly).
node impala\updateJSPEG.js --check
IF ERRORLEVEL 1 EXIT /B 1

PUSHD impala
node jspegCompilerTests.js
IF ERRORLEVEL 1 EXIT /B 1
node runJspegTests.js
IF ERRORLEVEL 1 EXIT /B 1
node importBuildTests.js
IF ERRORLEVEL 1 EXIT /B 1
node fuzzImpala.js 3000 1
IF ERRORLEVEL 1 EXIT /B 1
POPD
