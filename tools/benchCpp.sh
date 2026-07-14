#!/bin/bash
# Four-way speed comparison for a GAZL program: interpreter vs arm64 JIT vs the C++ backend at -O0 and -O2.
# Usage: bash benchCpp.sh <file.gazl> [function=main]
# Emits a standalone C++ translation (GAZLCmd --emit-cpp), compiles it at -O0/-O2, and times all four engines.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gazl="$1"; fn="${2:-main}"
cmd="$here/../output/GAZLCmd"
tmp="${TMPDIR:-/tmp}/benchcpp.$$"; mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT

[ -x "$cmd" ] || bash "$here/buildGAZLCmd.sh" release >/dev/null 2>&1

# C++ translation → compile → run (min of 3, parse the "[cpp] time_ms=" the generated main prints to stderr).
"$cmd" "$gazl" "$fn" --emit-cpp="$tmp/k.cpp" >/dev/null 2>"$tmp/emit.log" || { cat "$tmp/emit.log"; exit 1; }
cppmin() {
	local opt="$1" best=""
	clang++ -std=c++11 "$opt" -o "$tmp/k_$opt" "$tmp/k.cpp" 2>/dev/null || { echo "n/a"; return; }
	for i in 1 2 3; do
		local ms; ms=$("$tmp/k_$opt" 2>&1 >/dev/null | sed -n 's/.*time_ms=\([0-9.]*\).*/\1/p')
		[ -z "$best" ] && best="$ms"
		awk "BEGIN{exit !($ms<$best)}" && best="$ms"
	done
	echo "$best"
}
o0=$(cppmin -O0); o2=$(cppmin -O2)

# Interpreter + JIT via GAZLCmd --bench (min_ms).
gazlmin() { "$cmd" $1 --bench=5 "$gazl" "$fn" 2>&1 | sed -n 's/.*min_ms=\([0-9.]*\).*/\1/p'; }
interp=$(gazlmin "");  jit=$(gazlmin "--jit")

printf '%-28s  %10s %10s %10s %10s\n' "$(basename "$gazl")" interp jit "cpp-O0" "cpp-O2"
printf '%-28s  %10s %10s %10s %10s  (ms, min)\n' "" "$interp" "$jit" "$o0" "$o2"
