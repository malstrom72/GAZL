#!/usr/bin/env bash
set -e -o pipefail -u

# Every gate that needs nothing but node. Under 15 seconds, no C++ toolchain, so this is what you run
# before committing a compiler-only change. build.sh and build.cmd both call it, which is the only reason
# they cannot drift apart again - they used to run different subsets of this list.
#
# These degrade rather than fail when output/GAZLCmd is absent: runJspegTests and jspegCompilerTests skip
# their assemble+run checks and say so. Metadata validation is NOT here, because gazl-validate runs under
# NuXJS and so needs the build.

cd "$(dirname "$0")"/..

# The checked-in compiler must match the grammar it is generated from, or the playground goes stale
# (impala/playground.html loads impala/impalaCompiler.js directly).
node impala/updateJSPEG.js --check

cd impala
node jspegCompilerTests.js
node runJspegTests.js
node importBuildTests.js
node fuzzImpala.js 3000 1
