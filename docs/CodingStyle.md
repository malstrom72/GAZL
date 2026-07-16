# GAZL - Coding Style and Design Principles

Guidance for anyone (human or agent) writing code in this repository. `AGENTS.md` covers the mechanical formatting
rules (tabs width 4, brace placement, 120-column lines, script portability, the `build.sh` regression gate); this file
covers the deeper design principles and conventions that reviews are held to. When the two ever disagree, raise it -
do not guess.

## 1. Error handling, RAII, design by contract (PRIO 1)

These are the most important principles in the codebase. Get them wrong and the change will be rejected.

- **RAII means resource acquisition IS initialization.** A constructor either produces a fully valid object or throws.
  No two-phase construction. No `ok()` / `isValid()` / `init()` methods to check after the fact. No friend class that
  reaches in and fills the fields. A resource-owning class exposes no public data members.
- **Design by contract.** State preconditions and `assert` them. A broken precondition is a programmer error, not a
  runtime condition to paper over.
- **Exceptions are for errors.** Never silently swallow an error. Never return a half-filled output or a success code
  on a path that did not actually succeed. If a function cannot do its job, it throws.
- **assert loudly, then degrade safely.** `assert` catches bugs in development; asserts vanish in release, so where
  running past a broken invariant would corrupt state, ALSO throw (or take the safe branch) so a release build degrades
  predictably instead of emitting wrong results. Example: a JIT backend that meets an opcode it cannot lower asserts AND
  throws, so release falls back to the interpreter rather than running past the gap.

## 2. Naming

- **No abbreviations.** Full words: `functionCount` not `fnCount`, `memory` not `mem`, `natives` not `nat`,
  `registerClass` not `regClass`. Established short domain terms that read as words are fine (`dsp`, `ip`).
- **Boolean queries are prefixed `isX()`**: `isCompiled()`, `isResident()`.
- Wrap variable, parameter, class, and function names in back-ticks inside comment text: "`weight` is the block span".

## 3. Class layout and headers

- **Grouped access-specifier sections, public first.** Write `public:` / `protected:` / `private:` as section headers
  on their own line (one tab in), with members indented one further tab beneath them - the NuXJS style. Do NOT prefix
  every member with its access specifier (`public:  method()` on each line): that per-declaration form is an old GAZL
  habit being phased out. New code uses grouped sections; when editing an existing file, match whatever that file
  already uses.
- **No heavy headers.** Big function bodies live in a `.cpp`; only small or hot inlines belong in a header. A large
  method defined inline in a header will be moved out in review.
- **Keep the client surface minimal.** Internal helpers are not public API - make them protected members of the class
  that uses them, or namespace-internal, not part of what a client sees when they include the header.
- **Shipped headers stay C++03-clean.** The distributed library headers (and their `.cpp`) compile under strict
  `-std=c++03` (`0` not `nullptr`, `const` not `constexpr`, `<stdint.h>` not `<cstdint>`). Tools and tests may use
  C++11.

## 4. Comments

- **Multi-line block comments** use `/*` on its own line, the body indented one tab, and `*/` on its own line - single
  asterisk, tab-indented body:
  ```
  /*
  	One or more sentences. Wrap `names` in back-ticks.
  */
  ```
  Do NOT write a paragraph as a stack of `//` lines, and do NOT use decorative empty `//` banner lines.
- **Short inline comments** use a single end-of-line `//`.
- **No Doxygen.** No `///`, no `///<`, no `/** */`, no `@param`/`@return` tags. Plain `//` and `/* */` only. (NuXJS still
  carries the old Doxygen style; it is abandoned - do not copy it.)
- **One declaration per line.**
- **Never use en or em dashes** (the `-` and `--` characters) anywhere - in code, comments, docs, or commit messages.
  Plain ASCII hyphen only. Long dashes read as an AI giveaway; normal coders do not type them.

## 5. Commits and git

- **Short, concise, one-line commit subjects.** No essay in the subject.
- **Never add a `Co-Authored-By` trailer** (or any Claude / Anthropic attribution) to a commit.
- **Commit and push only when explicitly asked.** Do not commit proactively.
- **Run the regression gate before committing**: `timeout 180 ./build.sh` (see `AGENTS.md`).
- Do not proliferate files. Reuse an existing `.cpp` / `.h`; a new file has to earn its place. When something belongs
  in an existing translation unit, put it there.

## 6. Working style

Observed from how changes are built and reviewed here:

- **Stage large changes into small, independently-sound increments**, and keep the test suite green at every step. A big
  refactor lands as a sequence of commits, each of which builds and passes, not one giant drop.
- **Differential testing against a reference oracle.** New execution engines are validated by running them in lockstep
  against the interpreter (the ground truth) and comparing the full result byte-for-byte, across many inputs and edge
  conditions (including forced suspend/resume). Validate on every target platform (arm64, x86-64, Windows), not just the
  development host.
- **Match the surrounding code** - its comment density, naming, and idiom. New code should read like the code next to
  it.
- **Be decisive.** Recommend a direction rather than surveying every option; act when the path is clear; surface a real
  fork for a decision rather than guessing on something that changes the outcome.
