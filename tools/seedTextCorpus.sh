#!/usr/bin/env bash
# Seed the text-assembler fuzz corpus (lane 6) with real GAZL source, so libFuzzer mutates valid programs instead
# of starting from pure garbage the assembler rejects. Copies every .gazl in the repo into the corpus dir.
# Idempotent; safe to re-run. runFuzzMac.sh calls this automatically (it wipes output/fuzz/ each launch).
#   tools/seedTextCorpus.sh [corpus-dir]
set -e -o pipefail -u
cd "$(dirname "$0")/.."									# repo root
CORPUS="${1:-output/fuzz/lane6_text/corpus}"
mkdir -p "$CORPUS"
n=0
while IFS= read -r f; do
	cp -f "$f" "$CORPUS/seed_$(printf '%03d' "$n")_$(basename "$f")"
	n=$((n + 1))
done < <(find tests src docs temp -type f -name '*.gazl' 2>/dev/null | grep -v '/output/')
echo "seeded $n GAZL programs into $CORPUS"
