#!/usr/bin/env bash
#
# Regression check: run every Permut8 firmware with a committed golden (benchmarks/firmware/expected/) through
# the pure-GAZL host harness (tools/runPermut8Firmware.sh) and compare the output checksum. Extra arguments are
# passed through to GAZLCmd - in particular `--jit` turns this into an interp-vs-JIT DIFFERENTIAL suite over the
# real firmwares (the goldens are interpreter-produced). Exits non-zero on any mismatch.
#
# Usage: bash tools/checkPermut8Firmwares.sh [--jit ...]
set -u -o pipefail
cd "$(dirname "$0")/.."

fails=0; count=0
for exp in benchmarks/firmware/expected/*.checksum; do
	name=$(basename "$exp" .checksum)
	fw="benchmarks/firmware/golden/$name.gazl"			# the SHIPPED release builds (bank-verified) first,
	[ -f "$fw" ] || fw="tests/impala/golden/$name.gazl"	# then the dev/test firmwares
	if [ ! -f "$fw" ]; then printf '%-20s MISSING %s\n' "$name" "$fw"; fails=$((fails + 1)); continue; fi
	want=$(tr -d '\r' < "$exp")
	got=$(bash tools/runPermut8Firmware.sh "$fw" "$@" 2>/dev/null)
	count=$((count + 1))
	if [ "$got" = "$want" ]; then
		printf '%-20s %-13s ok\n' "$name" "$got"
	else
		printf '%-20s %-13s GOLDEN! (want %s)\n' "$name" "$got" "$want"
		fails=$((fails + 1))
	fi
done
echo
if [ "$fails" -eq 0 ]; then echo "All $count firmware checksums match."; else echo "$fails FAILURE(S)."; exit 1; fi
