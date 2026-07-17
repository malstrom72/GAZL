#!/usr/bin/env bash
# GAZL -> JIT -> disassembly, per function. Usage: jitDisasm.sh <file.gazl> [arm64|x64|both] [function-ordinal]
#
# Assembles + JIT-compiles the program (GAZLCmd --emit-jit), then disassembles each function's byte range separately
# (important on x64: variable-length decode must start at a function boundary) via llvm-mc. On an Apple Silicon Mac
# `x64` uses a Rosetta-built GAZLCmd, so BOTH backends can be inspected locally; `both` prints them back to back.
# The optional ordinal limits output to one function (ordinals follow declaration order in the .gazl).
set -e -o pipefail -u
cd "$(dirname "$0")/.."

gazl="$1"; mode="${2:-native}"; only="${3:-}"

llvmmc=""
for c in /opt/homebrew/opt/llvm/bin/llvm-mc /usr/local/opt/llvm/bin/llvm-mc llvm-mc; do
	command -v "$c" >/dev/null 2>&1 && { llvmmc="$c"; break; }
done
[ -n "$llvmmc" ] || { echo "jitDisasm: llvm-mc not found (brew install llvm)"; exit 1; }

hostArch=$(uname -m)
[ "$mode" = native ] && { [ "$hostArch" = arm64 ] && mode=arm64 || mode=x64; }

buildNative() {
	[ -x output/GAZLCmd ] || (cd tools && bash buildGAZLCmd.sh release) >/dev/null 2>&1
}
buildRosetta() {
	[ -x output/GAZLCmdX64Dump ] && return
	(cd tools && clang++ -arch x86_64 -O2 -std=c++11 -DGAZL_JIT -I.. -o ../output/GAZLCmdX64Dump \
			GAZLCmd.cpp ../src/GAZL.cpp ../src/GAZLCpp.cpp ../src/GAZLJit.cpp ../src/GAZLJitX64.cpp ../src/GAZLJitMemMacOS.cpp) >/dev/null 2>&1
}

disasmOne() {  # $1 = arm64|x64
	local want="$1" bin runner triple tmp
	tmp="${TMPDIR:-/tmp}/jitdisasm.$$.$want"
	if [ "$want" = "$hostArch" ] || { [ "$want" = x64 ] && [ "$hostArch" = x86_64 ]; }; then
		buildNative; bin=./output/GAZLCmd; runner=""
	elif [ "$want" = x64 ] && [ "$hostArch" = arm64 ] && arch -x86_64 true 2>/dev/null; then
		buildRosetta; bin=./output/GAZLCmdX64Dump; runner="arch -x86_64"
	elif [ "$want" = arm64 ] && [ "$hostArch" != arm64 ]; then
		echo "jitDisasm: cannot emit arm64 code on this host"; return 1
	else
		buildNative; bin=./output/GAZLCmd; runner=""
	fi
	triple=aarch64; [ "$want" = x64 ] && triple=x86_64
	if [ -n "$runner" ]; then $runner "$bin" "$gazl" main --emit-jit="$tmp.bin" >/dev/null
	else "$bin" "$gazl" main --emit-jit="$tmp.bin" >/dev/null; fi
	python3 - "$tmp.bin" "$triple" "$llvmmc" "$only" <<'PY'
import subprocess, sys

binPath, triple, llvmmc, only = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
data = open(binPath, "rb").read()
layout = {}
for line in open(binPath + ".txt"):
    parts = line.split()
    if parts[0] == "function": layout[int(parts[1])] = int(parts[2])
    elif parts[0] == "dispatch": dispatch = int(parts[1])
    elif parts[0] == "arch": arch = parts[1]
regions = sorted([(offset, "function %d" % ordinal) for ordinal, offset in layout.items()] + [(dispatch, "dispatcher")])
print("; %s, %d bytes, %d functions + dispatcher" % (arch, len(data), len(layout)))
for index, (offset, name) in enumerate(regions):
    if only != "" and name != "function " + only: continue
    end = regions[index + 1][0] if index + 1 < len(regions) else len(data)
    print("\n; ===== %s [0x%x..0x%x) =====" % (name, offset, end))
    chunk = data[offset:end]
    text = subprocess.run([llvmmc, "--disassemble", "--triple=" + triple],
            input=" ".join("0x%02x" % b for b in chunk), capture_output=True, text=True).stdout
    for line in text.split("\n"):
        line = line.strip()
        if line and not line.startswith(".") and not line.startswith("#"): print("\t" + line)
PY
	rm -f "$tmp.bin" "$tmp.bin.txt"
}

if [ "$mode" = both ]; then disasmOne arm64; disasmOne x64; else disasmOne "$mode"; fi
