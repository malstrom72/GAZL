#!/usr/bin/env bash
set -e -o pipefail -u

# Every gate that needs nothing but node. Around a minute and a half, most of it the 3000-program fuzz
# run at the end; no C++ toolchain, so this is what you run before committing a compiler-only change.
# build.sh and build.cmd both call it, which is the only reason they cannot drift apart again - they
# used to run different subsets of this list.
#
# These degrade rather than fail when output/GAZLCmd is absent: runJspegTests, jspegCompilerTests and

cd "$(dirname "$0")"/..

# The checked-in compiler must match the grammar it is generated from, or the playground goes stale
# (impala/playground.html loads impala/impalaCompiler.js directly).
node impala/updateJSPEG.js --check

# The hand-written .gazl proofs under docs/ still prove what their docs claim. Cheap, and it is the only
# thing that runs them - docSamples covers the .impala samples, nothing covered these.
node tools/checkDocProofs.js

cd impala
node jspegCompilerTests.js
node docSamples.js
node runJspegTests.js
# The compiler must run under NuXJS too, and everything above this line only ever exercised node. A
# shaped-array bug (dims.map, which NuXJS has no method for) survived from 1aae39a to 2026-08-07 because
# the only NuXJS gate was a four-program smoke test that declares no shapes. SKIPS LOUDLY when no NuXJS
# binary is present, so it does not fail a node-only checkout.
node nuxjsParityTests.js
node importBuildTests.js
node fuzzImpala.js 3000 1
