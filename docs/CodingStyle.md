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
- **Assert liberally - a lot of them.** Assertions are the primary tool for programmer errors: anything that cannot
  happen with correct code and valid inputs (a broken invariant, a precondition, an impossible case) gets an `assert`.
  Prefer the `assert(condition && "why this must hold")` form so a failure reads as an explanation. Include it as
  `#include "assert.h"` (with quotes, not `<cassert>`) so a project can override the handler with a local `assert.h`
  (see `GAZL.h`). Asserts are how programmer errors are handled - you never reach for `abort()`.
- **Exceptions are for runtime conditions, not for bugs.** Throw when a failure CAN happen with correct code because of
  the environment or input (the OS refuses an executable page, allocation fails, malformed source). Never silently
  swallow such an error and never return a half-filled output or a success code on a path that did not succeed - if a
  function cannot do its job, it throws. But do NOT throw a catchable exception for a programmer error you have proven
  cannot happen: that invites the caller to build recovery around a non-condition. Assert it instead.
- **`assert` + `throw` together is transitional scaffolding, not a default.** Use it only for a bug you have not yet
  PROVEN impossible, where running past it in release would corrupt state and a real safe fallback exists - e.g. a JIT
  backend that meets an opcode it does not yet lower and falls back to the interpreter while coverage is being built.
  The throw is the release net precisely because `assert` vanishes there. Once the invariant is proven (an exhaustive
  test plus fuzzing, ideally a compile-time exhaustiveness check), remove the throw and drop to assert-only. A permanent
  hybrid advertises doubt in your own invariant.

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
- **C++ standard is per-repo, not a universal rule.** Match whatever standard the target repo requires. Magnus's
  product code is C++11 with some C++14; reusable libraries lean C++03 for maximum portability and stability, but
  pragmatically go to C++11 where it clearly pays (e.g. `shared_ptr`) - it is a judgement call, not dogma. **For GAZL
  specifically:** the distributed library headers and their `.cpp` compile under strict `-std=c++03` (`0` not
  `nullptr`, `const` not `constexpr`, `<stdint.h>` not `<cstdint>`); the tools and tests may use C++11.

## 4. Comments

- **Comment sparingly - few and short.** Every comment is a maintenance liability that drifts as the code moves out
  from under it, so minimize the surface that can go stale. A comment must earn its place: the non-obvious *why*, an
  invariant, a gotcha - never the *what* (the code says that). Default to no comment. Lean on a doc `§` reference
  instead of re-explaining the design inline, and when editing prefer deleting a stale comment to updating it.
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
