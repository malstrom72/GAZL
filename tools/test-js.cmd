@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0\.."

REM Every gate that needs nothing but node. Around a minute and a half, most of it the 3000-program fuzz
REM run at the end; no C++ toolchain, so this is what you run before committing a compiler-only change.
REM build.sh and build.cmd both call it, which is the only reason they cannot drift apart again - they
REM used to run different subsets of this list. THIS FILE AND test-js.sh MUST STAY IN LOCKSTEP: they had
REM drifted anyway (2026-08-05 - docSamples.js ran only on the .sh side, so the doc-sample gate had never
REM run on Windows), which is the one thing the sentence above cannot protect against.
REM
REM These degrade rather than fail when output\GAZLCmd is absent: runJspegTests, jspegCompilerTests and

REM The checked-in compiler must match the grammar it is generated from, or the playground goes stale
REM (impala\playground.html loads impala\impalaCompiler.js directly). Its hand-copied native prelude
REM must match natives.impala for the same reason.
node impala\updateJSPEG.js --check
IF ERRORLEVEL 1 EXIT /B 1
node tools\checkPlaygroundPrelude.js
IF ERRORLEVEL 1 EXIT /B 1

REM The hand-written .gazl proofs under docs\ still prove what their docs claim.
node tools\checkDocProofs.js
IF ERRORLEVEL 1 EXIT /B 1

PUSHD impala
node jspegCompilerTests.js
IF ERRORLEVEL 1 EXIT /B 1
node docSamples.js
IF ERRORLEVEL 1 EXIT /B 1
node runJspegTests.js
IF ERRORLEVEL 1 EXIT /B 1
REM The compiler must run under NuXJS too, and everything above this line only ever exercised node. A
REM shaped-array bug (dims.map, which NuXJS has no method for) survived from 1aae39a to 2026-08-07
REM because the only NuXJS gate was a four-program smoke test that declares no shapes. SKIPS LOUDLY when
REM no NuXJS binary is present, so it does not fail a node-only checkout.
node nuxjsParityTests.js
IF ERRORLEVEL 1 EXIT /B 1
node importBuildTests.js
IF ERRORLEVEL 1 EXIT /B 1
node fuzzImpala.js 3000 1
IF ERRORLEVEL 1 EXIT /B 1
POPD
