# Fuzzing

Version: 2026-10-08

How these projects fuzz with libFuzzer. Every copy of this file is identical apart from the "Local additions" section
at the end, which holds a project's targets, scripts and exceptions.

## Machines

- The Mac (clang, AddressSanitizer plus UndefinedBehaviorSanitizer) is the main fuzzing machine. It is the fastest per
  worker and the only one with both sanitizers by default.
- Windows runs MSVC `/fsanitize=address /fsanitize=fuzzer` by default. It adds capacity and covers what only Windows
  has: 32-bit `long`, the MSVC compiler and runtime, and the Win32 backends.
- clang-cl on Windows is allowed only for a build that has passed the throw test under "clang-cl on Windows".
- Check the load before starting (`uptime` on the Mac) and keep to about 4 workers per campaign while other projects
  are running. Watch free disk space: nothing stops a run when the disk fills.

## Building

Build optimized with asserts on: the release target plus `/U NDEBUG` (MSVC, clang-cl) or `-UNDEBUG` (clang). Never
`-O0`, which is several times slower. Release is required on Windows anyway, because libFuzzer links against the
static release runtime.

- Mac clang: `-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -UNDEBUG`.
- MSVC: `/fsanitize=address /fsanitize=fuzzer /U NDEBUG`, and copy MSVC's `clang_rt.asan_dynamic-x86_64.dll` next to
  the executable. The two options must be separate: MSVC ignores clang's comma form with only a warning, and the link
  then fails with a misleading missing-entry-point error.

Link with an 8 MB stack on Windows (`/link /STACK:8388608`), since deep recursion otherwise overflows the 1 MB default
long before it would on the Mac.

### clang-cl on Windows

clang-cl's sanitizer instrumentation breaks MSVC C++ exception handling. A target that throws can crash inside
`__CxxFrameHandler3` (an access violation, often at `0xffffffffffffffff`), stop with exit code `0x80000003`, or
silently run the wrong code. A build needs all of the following:

- `/Ob0`: SanitizerCoverage puts callbacks in catch blocks without the funclet bundle, so the rest of the handler is
  replaced with unreachable code (llvm#212404). Without inlining, fewer comparisons land in catch blocks. This makes
  the bug rarer; it does not remove it.
- `-fsanitize-address-use-after-return=never`: ASan's fake stack breaks unwinding.
- `/D _DISABLE_STRING_ANNOTATION /D _DISABLE_VECTOR_ANNOTATION`: otherwise linking fails on `annotate_string`.
- The release target: libFuzzer needs the static release runtime (`/MT`).

Copy LLVM's `clang_rt.asan_dynamic-x86_64.dll` (from `lib\clang\<version>\lib\windows`), not MSVC's. With BuildCpp,
quote the compiler path because it contains a space:
`CPP_COMPILER="C:\Program Files\LLVM\bin\clang-cl.exe"`.

The throw test: before a clang-cl build is trusted, replay a corpus in which most inputs make the target throw through
both that build and a plain build of the same target without sanitizers, and require identical outcomes (status and
output) for every input. Zero crashes is not enough, since a handler cut short can run on to the wrong result without
crashing. The reference must be truly plain: with coverage instrumentation (`-fsanitize=fuzzer-no-link`) it breaks in
the same way, and the comparison proves nothing. Repeat the test whenever LLVM is updated.

## The harness

Remove every route to files, the console and the system from the target itself. Replacing a variable or a name is not
enough when the same function can still be reached another way.

A differential target, which compares implementations of the same thing, cannot find a bug they all share, such as a
range check that overflows the same way in each. It complements reading the bounds checks; it does not replace it.

Random inputs say little about correctness where only rare inputs fail. Number conversion is the clearest case: billions
of random decimals passed a parser that rounded constructed near-midpoint inputs wrong. Test such code with inputs
constructed to sit on the hard cases, checked against an exact oracle, and treat "random fuzzing found nothing" there
as uninformative.

Turn off CRT dialogs in `LLVMFuzzerInitialize`, or a failed assert hangs the worker on a message box:

```cpp
#if defined(_MSC_VER)
	_set_error_mode(_OUT_TO_STDERR);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
	_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
	_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
```

## Running

- Pass `-artifact_prefix=<folder>/`, or crash files land in the current directory, which is easy to commit by mistake.
- Pass a scratch folder as the first corpus directory, since libFuzzer writes new inputs there. Use forward slashes in
  `-dict` paths on Windows.
- Mac environment:
  ```bash
  symbolizer="$(brew --prefix llvm)/bin/llvm-symbolizer"
  export ASAN_OPTIONS="detect_container_overflow=0:external_symbolizer_path=$symbolizer"
  export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1:external_symbolizer_path=$symbolizer"
  ```
  `detect_container_overflow=0` avoids false reports from uninstrumented system libraries. The explicit symbolizer
  avoids a deadlock in `atos`.
- Start runs longer than half an hour with `nohup caffeinate -i ... & disown`, so that neither sleep nor the end of the
  session that started them stops them.
- Do not raise `-rss_limit_mb` to reproduce an out-of-memory input on a shared machine. Swap comes out of the same
  disk, and one 8 GB reproduction took 3 GB of a nearly full Mac disk.
- Keep `-jobs` small (a few thousand at most). A run that crashes while loading its corpus restarts job after job,
  looks busy and fuzzes nothing; a small cap ends that loop quickly and visibly. Watch the job logs as well.

## Corpus and regression

- `tests/fuzz/` holds a minimized corpus archive per target, a dictionary and hand-made seeds.
- Refresh an archive only after a substantial run: merge with `-merge=1` into an empty folder, then pack
  deterministically:
  ```bash
  tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='2000-01-01 00:00Z' -cf - corpus | gzip -9n
  ```
  GNU tar and bsdtar do not produce identical archives, so refresh with GNU tar. Use `xz -9` only where it saves a
  lot: the `tar.exe` that ships with Windows cannot read xz and hangs instead of failing.
- Inputs are replayed through a plain `main()` that reads files in binary mode and passes the number of bytes actually
  read to `LLVMFuzzerTestOneInput`; text mode on Windows folds CRLF and feeds the target the wrong bytes. It is built
  without fuzzer instrumentation, with every compiler the project uses, and never with clang-cl's sanitizers, which
  would bring back the exception handling bugs above. Unpack each archive into an empty folder, or inputs left from
  the previous archive are replayed too.
- The normal build replays every past crash input, always. It also replays the corpus and the seeds as long as that
  takes no more than about 30 seconds per build configuration. A corpus over that budget is replayed by a separate
  script, run in CI and before every release or freeze, so the normal build stays fast and nothing ships unreplayed.
- Commit the input of each fixed crash as a plain file in `tests/fuzz/<target>Crashes/`, outside the archives:
  `-merge=1` drops any input whose features others already cover, fixed crashes included. Mark these files `binary`
  in `.gitattributes`, so line-ending conversion cannot rewrite them.
- Only inputs for fixed defects belong in a corpus or the crash folder. Keep the input for a known, still unfixed
  defect in a separate folder until the fix lands, or it aborts a run that stops on the first error while the corpus
  loads.
- Keep a crash file from a Windows clang-cl build only if it also crashes with MSVC or on the Mac.

## Local additions

GAZL's lanes, and the one place it departs from the shared rules.

- **Three targets, not one.** The JIT-vs-interpreter differential (`tools/GAZLCmd.cpp` under `-DJITDIFF`), the
  text-assembler crash lane (the same file under `-DLIBFUZZ` without `JITDIFF`), and the Impala compiler fuzzer
  (`impala/fuzzImpala.js`, pure JavaScript, which runs in the JS gate and needs no C++ toolchain).
- **A seed is `(generator version, seed)`, never a bare integer.** The generated program is a pure function of a
  choice stream, and that stream has changed: float branches, NaN/Inf and negative in-range indices were added, and
  the per-statement `pick()` fix made the stream compiler-independent. A seed quoted before those changes names a
  different program now, so every recorded result must say which generator produced it.
- **The differential lane cannot find a bug both engines share.** It checks that the interpreter and the JIT AGREE,
  not that either is right. A `COPY` bounds check that wrapped identically in the interpreter and in both JIT
  backends survived 600,000 generated programs across two backends, and was found by reading the check. Treat a
  clean differential run as evidence of agreement only.
- **Throughput depends entirely on what fraction of inputs the assembler rejects.** A rejected program costs a parse;
  an accepted one assembles AND RUNS across a 128K-`Value` data arena and a 128K-`Instruction` code arena with a
  10M-instruction timeout. Measured on the Windows MSVC lane: **~1300 execs/s** from a 31-program seed corpus, where
  most mutations are invalid and rejected cheaply, against **about 1/s** on a corpus of a single valid program that
  executes every time. So a throughput figure means nothing without the corpus it was measured on, and a campaign
  slowing down as its corpus grows is expected, not a fault.
- **UBSan is Mac-only for now.** clang-cl's instrumentation breaks MSVC C++ exception handling, and this assembler
  throws to reject a malformed program as its normal path, so a Windows clang-cl build can silently run the wrong
  code. The Windows lane uses MSVC `/fsanitize=fuzzer /fsanitize=address` (separate flags; the comma form is clang
  syntax and MSVC drops it with only a warning), which is ASan-only.
- **The CRT dialog fix lives in `quietAsserts()`**, called from both `main()`s in `tools/GAZLCmd.cpp`, rather than in
  `LLVMFuzzerInitialize`. Same effect: an unattended run fails instead of blocking on a message box.
- **`buildGazlFuzz.cmd` and `buildGazlFuzz.sh` are deliberately NOT behaviour-identical**, the one documented
  exception to this repo's rule that a `.sh` and its `.cmd` twin behave the same. The `.sh` uses clang with
  `-fsanitize=fuzzer,address` (and `undefined` on request); the `.cmd` uses MSVC's libFuzzer and ASan, because
  clang-cl is unusable for this target and MSVC has no UBSan. Both build the same fuzz target from the same sources;
  only the engine and the available sanitizers differ. `seedTextCorpus.cmd` IS a true twin, down to matching
  `find -size -8k`'s rounding to whole KiB, which excludes a file the obvious `< 8192` test would include.
- **The MSVC build needs `clang_rt.asan_dynamic-x86_64.dll` beside the exe**, or it exits `0xC0000135` anywhere but a
  developer prompt. `buildGazlFuzz.cmd` copies it from the Visual Studio tree; that is why it searches for it.
