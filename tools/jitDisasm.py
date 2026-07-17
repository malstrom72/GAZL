#!/usr/bin/env python3
# Windows sibling of tools/jitDisasm.sh: disassemble a GAZLCmd --emit-jit dump with capstone instead of llvm-mc.
# Usage: python tools/jitDisasm.py <file.jit> [function-ordinal]
# The sidecar <file.jit>.txt supplies arch, per-function byte offsets, and the dispatcher offset; each function is
# sliced at its boundary (essential on x64: variable-length decode must start at an instruction boundary).
import sys

import capstone


def main():
	path = sys.argv[1]
	want = int(sys.argv[2]) if len(sys.argv) > 2 else None
	code = open(path, 'rb').read()
	arch = None
	functions = []
	dispatch = None
	for line in open(path + '.txt'):
		parts = line.split()
		if not parts:
			continue
		if parts[0] == 'arch':
			arch = parts[1]
		elif parts[0] == 'function':
			functions.append((int(parts[1]), int(parts[2])))
		elif parts[0] == 'dispatch':
			dispatch = int(parts[1])
	if arch == 'x64':
		md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
	else:
		md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
	bounds = sorted([off for _, off in functions] + [dispatch, len(code)])
	for ordinal, offset in functions:
		if want is not None and ordinal != want:
			continue
		end = min(b for b in bounds if b > offset)
		print('; --- function %d @ %d..%d ---' % (ordinal, offset, end))
		for insn in md.disasm(code[offset:end], offset):
			print('%5x:  %-24s %s %s' % (insn.address, insn.bytes.hex(' '), insn.mnemonic, insn.op_str))
	if want is None and dispatch is not None:
		print('; --- dispatcher @ %d..%d ---' % (dispatch, len(code)))
		for insn in md.disasm(code[dispatch:], dispatch):
			print('%5x:  %-24s %s %s' % (insn.address, insn.bytes.hex(' '), insn.mnemonic, insn.op_str))


if __name__ == '__main__':
	main()
