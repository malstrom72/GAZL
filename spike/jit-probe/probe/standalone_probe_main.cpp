//
//  standalone_probe_main.cpp — runs the probe in a plain process (no host, no Hardened Runtime).
//  Useful as a smoke test: confirms the probe compiles, doesn't crash, and produces a log. Under a
//  normal ad-hoc/unsigned process every rung is expected to succeed; the interesting denials only show
//  up inside real hardened DAW hosts.
//
#include "GazlJitProbe.h"
#include <cstdio>

int main() {
	std::printf("running GAZL JIT probe (standalone)...\n");
	gazlJitProbeRunOnce("standalone smoke test");
	std::printf("done. See ~/Library/Logs/GazlJitProbe/ (macOS) or the platform temp dir.\n");
	return 0;
}
