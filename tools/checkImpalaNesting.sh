#!/usr/bin/env bash
# Fail when the generated Impala compiler's compile-time NESTING need grows toward NuXJS's limit.
#
# Why this exists: the Impala compiler is a generated JSPEG parser, so its nesting comes from the grammar and moves
# when the grammar does. NuXJS bounds compile recursion with MAX_NESTED_COMPILE_DEPTH; when it was lowered to 48,
# GAZL stopped building entirely, with no warning beforehand. Nothing measured the need until this script.
#
# How it measures without touching NuXJS: each nested `{ }` block costs exactly one level (the NestGuard in
# Compiler::statement), so wrapping the compiler in K blocks leaves LIMIT - K levels for the compiler itself. If it
# still compiles at K, its peak need is at most LIMIT - K.
set -e -o pipefail -u
cd "$(dirname "$0")/.."

MAX_NEED=${MAX_NEED:-80}				# fail above this; measured need was 55 on 2026-10-09, so it trips on ~45% growth
NUXJS=output/NuXJS
[ -x "$NUXJS" ] || NUXJS=output/NuXJS.exe
SRC=output/impalaCompiler.js
WORK=output/nestProbe
measure=0
[ "${1:-}" = "--measure" ] && measure=1

[ -x "$NUXJS" ] || { echo "check-nesting: $NUXJS not built; run the build first" >&2; exit 1; }
[ -f "$SRC" ] || { echo "check-nesting: $SRC not generated; run BuildImpala first" >&2; exit 1; }

# The limit is NuXJS's, so read it from the vendored source rather than restating it here.
LIMIT=$(sed 's/\r$//' externals/NuXJS/src/NuXJS.cpp \
		| sed -n 's/^const Int32 MAX_NESTED_COMPILE_DEPTH = \([0-9]*\);.*$/\1/p' | head -1)
case "$LIMIT" in
	''|*[!0-9]*) echo "check-nesting: could not read MAX_NESTED_COMPILE_DEPTH from the vendored NuXJS" >&2; exit 1;;
esac

rm -rf "$WORK"; mkdir -p "$WORK"

compiles() {						# $1 = K wrapper blocks; 0 = compiled, 1 = hit the nesting limit
	local k=$1 f="$WORK/wrapped.js" i=0
	: > "$f"
	while [ "$i" -lt "$k" ]; do printf '{\n' >> "$f"; i=$((i + 1)); done
	cat "$SRC" >> "$f"
	i=0
	while [ "$i" -lt "$k" ]; do printf '}\n' >> "$f"; i=$((i + 1)); done
	local out
	out=$("./$NUXJS" "$f" 2>&1 || true)			# NOT piped into grep: grep -q exits early, SIGPIPEs NuXJS, and
	case "$out" in									# pipefail then reports the match as a failure to match
		*"Internal compiler limitations reached"*) return 1;;
	esac
	return 0
}

if [ "$measure" = 1 ]; then
	compiles 0 || { echo "check-nesting: the compiler does not compile even unwrapped" >&2; exit 1; }
	lo=0; hi=$LIMIT
	compiles "$hi" && { echo "check-nesting: wrapper consumed no depth, probe invalid" >&2; exit 1; }
	while [ $((hi - lo)) -gt 1 ]; do
		mid=$(((lo + hi) / 2))
		if compiles "$mid"; then lo=$mid; else hi=$mid; fi
	done
	echo "check-nesting: peak need $((LIMIT - lo)) of $LIMIT levels, headroom $lo"
	exit 0
fi

if [ "$MAX_NEED" -ge "$LIMIT" ]; then
	echo "check-nesting: NuXJS's MAX_NESTED_COMPILE_DEPTH is $LIMIT, at or below the $MAX_NEED this project allows" >&2
	exit 1
fi

if compiles $((LIMIT - MAX_NEED)); then
	echo "check-nesting: Impala compiler needs at most $MAX_NEED of $LIMIT levels"
else
	echo "check-nesting: FAILED - the Impala compiler now needs more than $MAX_NEED of NuXJS's $LIMIT levels." >&2
	echo "  Its nesting comes from the generated grammar, so a grammar change is the likely cause." >&2
	echo "  Run 'bash tools/checkImpalaNesting.sh --measure' for the exact figure, then either reduce the" >&2
	echo "  grammar's nesting or raise MAX_NEED here deliberately, recording the new number." >&2
	exit 1
fi
