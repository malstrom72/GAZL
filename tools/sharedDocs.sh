#!/usr/bin/env bash
# Wrapper for tools/sharedDocs.js; see there for what it does and its options.
set -e -o pipefail -u
cd "$(dirname "$0")/.."
node tools/sharedDocs.js "$@"
