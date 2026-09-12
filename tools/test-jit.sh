#!/usr/bin/env bash
set -e -o pipefail -u

# Every gate that exercises the JIT compiler on the HOST's backend. build.sh and build.cmd both call it, which is the
# only reason the two lanes cannot run different subsets - the drift test-js.sh/.cmd suffered for months.
#
# Around twenty seconds, and deliberately NOT the whole safety net (design/jit/JitTechnologyMap.md section 5). Left
# manual because they cost minutes rather than seconds, or need a toolchain the .cmd lane has not got:
#   - the byte-golden emitter tests (buildAndRunGAZLJitX64Test.sh, buildAndRunGAZLJitTest.sh) diff against a
#     clang-assembled oracle .s, so there is no MSVC lane for them;
#   - the 300k-deep fuzz soak per backend (fuzzSoak.ps1 / fuzz48h.sh), the standing gate for a codegen change.
#
# SKIPS LOUDLY rather than failing where the host has no backend or forbids executable memory. It does NOT skip when
# the JIT is merely absent from the binary: `--jit` falls back to the interpreter without complaint, so a GAZLCmd built
# without GAZL_JIT would sail through the differential below comparing the interpreter against its own goldens.

cd "$(dirname "$0")"/..

case "$(uname -m)" in
	arm64 | aarch64 | x86_64) ;;
	*) echo "test-jit: GAZLJit has no backend for '$(uname -m)'; skipping the JIT gates."; exit 0 ;;
esac

CMD=output/GAZLCmd; [ -x output/GAZLCmd.exe ] && CMD=output/GAZLCmd.exe

# Prove the JIT is reachable BEFORE running anything whose result only differs when it is. `--jit-stats` prints its
# `jitstats` line only after JitCompiler::compile has actually produced native code.
probe=$("$CMD" --jit-stats benchmarks/suite/golden/leibniz.gazl main 2>&1 >/dev/null) || true
case "$probe" in
	*jitstats*) ;;
	*"does not permit executable memory"*)
		echo "test-jit: this host forbids executable memory (entitlement / ACG); skipping the JIT gates."; exit 0 ;;
	*)	echo "test-jit: $CMD compiled nothing to native code - was it built with -DGAZL_JIT? It said:"
		echo "$probe"; exit 1 ;;
esac

# The lockstep kernel test: ~35 GAZL kernels through the real JitCompiler::compile, JIT vs interpreter over the whole
# memory image + Status, at full fuel AND at tiny fuel (which forces every leader through suspend/resume).
bash tools/buildAndRunGAZLJitLowerTest.sh

# The 28 shipped Permut8 firmwares against interpreter-produced goldens: a differential over real products.
bash tools/checkPermut8Firmwares.sh --jit

# A short lap of the generative differential fuzzer - the thing that caught DIVf/0, the v2.3a-lite scalar deref and the
# cross-class preload miscompile, two of them after every other test passed. 2000 programs is a smoke test only.
bash tools/buildGazlFuzz.sh standalone
./output/GAZLFuzz --gen 2000 1 deep
