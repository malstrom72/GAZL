# Assembler-level function inlining: opportunity probe (2026-07-19)

Goal: inline CALL_CVC to small non-recursive GAZL functions at assembly/finalization, benefiting BOTH engines
(interpreter drops FUNC/CALL/RETU dispatch + ipStack traffic; JIT additionally drops its CALL BARRIER, which
today kills register residency in any loop containing a call).

## Measured opportunity (native arm64)

**Per-call overhead** (synthetic pair, 20M iterations, 2-arg int callee, temp/callbench_{call,inline}.gazl):
- interpreter: 130.8 vs 77.0 ms -> **2.69 ns/call = 41% of this loop**
- JIT:          52.9 vs 22.7 ms -> **1.51 ns/call = 57% of this loop** (ipStack push/pop + tail-branches + barrier)

**Where the calls are** (static CALL& sites): the SUITE has ZERO GAZL calls in all 9 kernels - it structurally
cannot show inlining gains (explains why call overhead never appeared in suite geomeans). The shipped firmwares
are the real target: vortex 100 sites, reciter 46, specular 24, js80rmx 22, ringmod 14, mozaik 11.

**Dynamic counts** (counter-patched interpreter, 100k frames, includes the concatenated GAZL host wrapper):
most firmwares ~200k GAZL calls (2/frame - largely the wrapper's driver path), beatrick 400k (4/frame),
vortex 604k (6/frame). Note pushCall-forwarded natives (^yield->yield_) do NOT go through the CALL opcode and
are NOT inlinable; the CALL_CVC portion (driver->process(), firmware-internal helpers) IS.

**Prize estimate**: gazl_calls x per-call cost => ~0.3-1.2 ms per 100k frames = roughly **5-15% of --jit runtime
on call-heavy firmwares** (vortex, reciter-class where fixed calls dominate a small process()), and about DOUBLE
that fraction on the interpreter (2.69 vs 1.51 ns). PLUS a second-order JIT win that may exceed the direct one:
loops containing calls currently barrier every iteration and are disqualified from v2.2 residency; inlining makes
them plain loops (montecarlo-style wins become reachable for firmware code).

## Design sketch (assembler finalize-time pass)

Candidates: CALL_CVC to a function that is non-recursive (no path back to itself through CALL_CVC), contains no
CALL_VVC/SWCH complications (start strict), and is small (body <= N instructions) or called-once.
Transform per call site:
- Remap callee frame slots: the callee's OUT/INP window ALREADY overlaps the caller's transients at the call's
  window base (%N) - those references rewrite to caller-relative slots directly; callee locals/transients get
  fresh caller frame slots appended (caller FUNC frame grows; fix its C0/C1 and the stack-overflow check).
- RETU -> GOTO to the end of the inlined body (handles multi-RETU callees, which the JIT lowerFunction cannot
  even digest today - inlining REMOVES that class).
- Rebase internal branch offsets; SWCH tables in callees excluded initially.
- Remove the CALL/ipStack round trip entirely.
Semantics: both engines see the same finalized code, so engine-vs-engine differential holds unchanged. Fuel spend
changes (fewer instructions) - fine, it is a different program. IP_STACK_OVERFLOW can only get rarer.
DATA_STACK caveat: caller frames grow by inlined locals even on unexecuted paths -> bound the growth (budget per
function) so a program near the data-stack limit does not newly trap.
Realms (section 1.1): inlining merges callee frame realms into the caller's - only LOOSENS what is defined;
conforming programs stay conforming, and the JIT realm analysis is per-function so it stays sound.

Where: a finalize-time pass in the assembler (Magnus's call: benefits every producer - hand-written GAZL, Impala,
future compilers - with no source changes). Alternative placements (Impala, a GAZL->GAZL tool like gazlCompactor,
which is text-only today) rejected for coverage.

Policing: the differential fuzzer's G4 stage already generates call-heavy programs; an --inline toggle in GAZLCmd
lets the fuzzer diff inlined-vs-not against the interpreter oracle, and the 28-firmware checksum lane gates real
code. Add fuzzer arms for multi-RETU callees and window-aliasing idioms (out-params INTO the shared window).

## Open questions
- Inline budget / size heuristic (bench-driven; start "leaf <= 16 instructions", measure).
- Interaction with function pointers: a function that is BOTH direct-called and address-taken keeps its
  out-of-line copy (inline at direct sites only).
- Whether the assembler (shipped, C++03-clean, trusted gatekeeper) is the right home vs a post-finalize
  AssembledProgram->AssembledProgram transform inside the same TU (identical coverage, less parser churn).
