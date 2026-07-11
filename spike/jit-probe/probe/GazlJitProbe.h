//
//  GazlJitProbe.h
//
//  Spike A1 — executable-memory probe for the proposed GAZL native (JIT) compiler.
//
//  This is NOT part of the JIT. It answers one empirical question: inside a given DAW host process,
//  which strategy for allocating and executing machine code actually works at runtime? It allocates a
//  few pages, writes a tiny function that returns a known constant, and calls it — trying each strategy
//  in a fallback ladder, on both the caller's thread and a spawned worker thread.
//
//  Design rules (non-negotiable, see docs/JitSpikeA1-Handoff.md §2.3):
//    - No process-global state: no signal handlers, no mprotect of memory we did not allocate, no
//      changes to global FPU/MXCSR state.
//    - Never crash or abort the host on a *denied* rung: a syscall failure (MAP_FAILED / mprotect==-1)
//      is caught and treated as "rung unavailable". Only a fault on the execute step can kill us; that
//      is itself a data point, and we flush a breadcrumb line before every execute so the log names the
//      rung and thread that died.
//
//  The single entry point is idempotent: call it from a plug-in's audio-configure hook; it runs the
//  whole sequence exactly once per process regardless of how many instances call it.
//

#ifndef GAZL_JIT_PROBE_H
#define GAZL_JIT_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

// Runs the full probe sequence (both threads, all rungs) exactly once for the lifetime of this process.
// Safe to call repeatedly and from multiple instances/threads. `triggerContext` is a short free-text
// tag recorded in the log identifying what caused the run (e.g. "AU configureAudio", "VST3 setActive").
// Never throws; never aborts on a denied rung.
void gazlJitProbeRunOnce(const char* triggerContext);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GAZL_JIT_PROBE_H
