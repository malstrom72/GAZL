#!/usr/bin/env sh
# Shared configuration for the JIT-probe plug-in builds. Sourced by build_{au,vst3,vst2,aax}.sh.
# Override any SDK path via environment if your 3rd-party SDKs live elsewhere.

# Directory of this file = spike/jit-probe.
PROBE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SYMB_DIR="$PROBE_DIR/symbiosis"

# 3rd-party plug-in SDKs (defaults match Magnus's machine, 2026-07).
THIRDPARTY="${THIRDPARTY:-/Users/magnus/projects/3rdparty}"
VST2_SDK_ROOT="${VST2_SDK_ROOT:-$THIRDPARTY/vstsdk}"
VST3_SDK_ROOT="${VST3_SDK_ROOT:-$THIRDPARTY/vst3sdk}"
AAX_SDK_ROOT="${AAX_SDK_ROOT:-$THIRDPARTY/aaxsdk}"

OUT_DIR="$PROBE_DIR/.build"
mkdir -p "$OUT_DIR"

# Universal binary so the same bundle loads in both native-arm64 and Rosetta-x86_64 hosts.
ARCHES="-arch arm64 -arch x86_64"

# Release config: compile out Symbiosis contract asserts. A probe plug-in must NEVER abort a host
# process (a stray SYMBIOSIS_ASSERT -> abort() during a host's out-of-process scan will blacklist us).
# This is also the normal shipping config for Symbiosis. -O2 keeps the probe's generated code sane.
RELEASE_FLAGS="-DNDEBUG -O2"

# Symbiosis core + our probe plug-in + the probe itself. The plug-in .cpp pulls in the vendored
# headers via "symbiosis/src/..." and "probe/..." paths (resolved from PROBE_DIR).
CORE_SOURCES="$SYMB_DIR/src/SymbiosisCpp.cpp $SYMB_DIR/src/SymbiosisTests.cpp $PROBE_DIR/JitProbePlugin.cpp $PROBE_DIR/probe/GazlJitProbe.cpp"

# Every adapter needs these include dirs and (for the probe) Security/CoreFoundation.
COMMON_INCLUDES="-I$PROBE_DIR -I$SYMB_DIR/src"
PROBE_FRAMEWORKS="-framework CoreFoundation -framework Security"
