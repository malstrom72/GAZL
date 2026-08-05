#!/usr/bin/env bash
set -e -o pipefail -u

# Every gate that needs nothing but node. Around a minute and a half, most of it the 3000-program fuzz
# run at the end; no C++ toolchain, so this is what you run before committing a compiler-only change.
# build.sh and build.cmd both call it, which is the only reason they cannot drift apart again - they
# used to run different subsets of this list.
#
# These degrade rather than fail when output/GAZLCmd is absent: runJspegTests, jspegCompilerTests and
# checkDocProofs skip their assemble+run checks and say so. Metadata validation is NOT here, because
# gazl-validate runs under NuXJS and so needs the build.

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
node importBuildTests.js
node fuzzImpala.js 3000 1
