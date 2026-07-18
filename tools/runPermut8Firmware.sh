#!/usr/bin/env bash
#
# Run one Permut8 firmware (an UNMODIFIED tests/impala/golden/*.gazl) through the pure-GAZL host harness:
# tools/permut8Host.js wraps it (host constants + delay line + yield_/read_/write_/trace_ + driver), and GAZLCmd
# executes it with --forward mapping the firmware's ^yield/^read/^write/^trace native calls onto the GAZL
# implementations (pushCall). Prints the deterministic output checksum. Interpreter only for now (pushCall is
# not yet supported under the JIT).
#
# Usage: bash tools/runPermut8Firmware.sh <firmware.gazl> [extra GAZLCmd args, e.g. --bench=8]
set -e -o pipefail -u
cd "$(dirname "$0")/.."

fw="$1"; shift || true
name=$(basename "$fw" .gazl)
mkdir -p output/p8
node tools/permut8Host.js "$fw" "output/p8/$name.gazl" 1>&2

CMD=output/GAZLCmd; [ -x output/GAZLCmd.exe ] && CMD=output/GAZLCmd.exe

# Auto-flags: self-contained libm and/or globals colliding with a built-in native name.
extra=""
grep -qE '^\s*(sqrt|log|atan2):\s+FUNC' "$fw" && extra="--no-libm"
for n in input print printInt printFloat printLF exit; do
	grep -qE "^$n:" "$fw" && extra="$extra --no-native=$n"
done

"$CMD" "output/p8/$name.gazl" hostMain --forward=yield:yield_,read:read_,write:write_,trace:trace_ $extra "$@"
