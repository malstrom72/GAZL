#!/usr/bin/env bash
# Dispatch-layout search: benchmark seeded case orders of Processor::run's switch. See tools/layoutSearch.js.
# Usage: bash tools/layoutSearch.sh <first seed> <last seed> <out.csv> [--drift=20] [--pin=N]
set -e -o pipefail -u
export LAYOUT_SEARCH_CWD="$(pwd)"		# a relative <out.csv> is relative to the caller, not the repo root
cd "$(dirname "$0")"/..
node tools/layoutSearch.js "$@"
