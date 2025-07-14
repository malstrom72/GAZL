#!/usr/bin/env python3
import re
from pathlib import Path

docs_file = Path('docs/InstructionSet.md')
source_file = Path('src/UnitTest.gazl')

docs_instructions = set()
for line in docs_file.read_text().splitlines():
    m = re.match(r"##\s+([A-Za-z0-9_]+)", line)
    if m:
        docs_instructions.add(m.group(1))

source_instructions = set()
for line in source_file.read_text().splitlines():
    line = line.rstrip()
    m = re.match(r"^;\s*!?\s*([A-Za-z0-9_]+)(?:\s{2,}|$)", line)
    if m:
        name = m.group(1)
        if name.isupper():
            source_instructions.add(name)

missing = sorted(i for i in source_instructions if i not in docs_instructions)
if missing:
    print("Missing instructions in docs:", ', '.join(missing))
    raise SystemExit(1)
print("All instructions accounted for.")
