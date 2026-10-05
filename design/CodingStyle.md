# Coding Style and Design Principles

Version: 2026-10-05

The coding style and design principles shared across these projects: the basis for both humans and agents, and held to
in review. Each project adds its own operational notes (directory layout, build and test gates, dependencies) in that
project's `AGENTS.md` and points here for code style, so there is one source of truth to keep from drifting. Every copy
of this file is identical apart from the "Local additions" section at the end, so copies can be compared by their
version line. Examples below are drawn from specific projects (GAZL, NuXJS); they illustrate a rule, they are not
project-scoped exceptions to it.

The rules apply to new and edited code. Settled code that predates them is left alone unless you are already changing
it.

## 1. Error handling, RAII, design by contract (PRIO 1)

These are the most important principles in the codebase. Get them wrong and the change will be rejected.

- **RAII means resource acquisition IS initialization.** A constructor either produces a fully valid object or throws.
  No two-phase construction. No `ok()` / `isValid()` / `init()` methods to check after the fact. No friend class that
  reaches in and fills the fields. A resource-owning class exposes no public data members.
- **Assert liberally, a lot of them.** Assertions are the primary tool for programmer errors: anything that cannot
  happen with correct code and valid inputs (a broken invariant, a precondition, an impossible case) gets an `assert`.
  Prefer the `assert(condition && "why this must hold")` form so a failure reads as an explanation. Include it as
  `#include "assert.h"` (with quotes, not `<cassert>`) so a project can override the handler with a local `assert.h`.
  Asserts are how programmer errors are handled: you never reach for `abort()`.
- **Silence an assert-only variable with `(void)name;`.** Not `static_cast<void>(name)`, `[[maybe_unused]]` or a
  macro. A library ships no `assert.h` of its own; the host product supplies it.
- **Trust the contract inward; validate only at the boundary.** A function states its preconditions and then relies on
  them. It does not re-check what a caller is contractually obligated to provide, and it does not defensively null-,
  range-, or enum-check a value the contract already pins down. It accesses it directly. Untrusted data (external
  input, a call arriving across a format/API boundary, bytes off a file, socket, or OS call) is validated exactly once,
  at the perimeter where it enters; past that line the value is known-good and code uses it as given. A guard duplicated
  inside the perimeter is not extra safety, it is dead code that hides where the real contract lives and drifts out of
  sync with it. This split (assumption-driven inside, defensive at the edges) is not optional: a defensive check on a
  contract path is a review-blocking defect, not a nicety. An assert is not validation: it vanishes in release builds
  (`NDEBUG`), so input from outside always gets a real check.
- **Exceptions are for runtime conditions, not for bugs.** Throw when a failure CAN happen with correct code because of
  the environment or input (the OS refuses an executable page, allocation fails, malformed source). Never silently
  swallow such an error and never return a half-filled output or a success code on a path that did not succeed. If a
  function cannot do its job, it throws. But do NOT throw a catchable exception for a programmer error you have proven
  cannot happen: that invites the caller to build recovery around a non-condition. Assert it instead.
- **No recovery or fallback for a state that cannot happen.** Do not add an `else` that handles a case the contract
  excludes, or a fallback branch around a condition correct code cannot produce. Assert the invariant and continue as
  if it holds, because it does. A recovery path for a non-condition is untested by construction, tempts callers into
  depending on the non-condition, and launders a real bug into a silent success. This is the previous rule (never
  `throw` for a proven-impossible error) applied to branches instead of exceptions.
- **`assert` + `throw` together is transitional scaffolding, not a default.** Use it only for a bug you have not yet
  PROVEN impossible, where running past it in release would corrupt state and a real safe fallback exists, e.g. a JIT
  backend that meets an opcode it does not yet lower and falls back to the interpreter while coverage is being built.
  The throw is the release net precisely because `assert` vanishes there. Once the invariant is proven (an exhaustive
  test plus fuzzing, ideally a compile-time exhaustiveness check), remove the throw and drop to assert-only. A permanent
  hybrid advertises doubt in your own invariant.

## 2. Compactness and no duplicated functionality (PRIO 1)

- **No duplicated functionality.** Two functions or branches that do substantially the same thing are a
  defect, not a convenience: they drift apart and double every future fix. Before writing a function,
  branch, or helper, find the existing one that already does most of the job and generalize it. If you
  catch yourself writing a near-copy (a `fooArray` beside a `fooStruct`, a third size helper beside two
  others), unify them instead.
- **Compact by default.** Say it once, with the fewest moving parts. Prefer generalizing an existing path
  over adding a parallel one, and a data-driven table over repeated branches. Delete dead and superseded
  paths as you go; a change that adds a capability should still look for what it lets you remove.
- **A refactor must REDUCE size.** Its success metric is fewer lines (or fewer functions, fields,
  branches) with behavior unchanged and tests green. If a "refactor" grows the file it was the wrong
  change: reconsider the shape rather than bolting more on.
- **Do not let the data model accrete.** When a record keeps gaining a field to carry one more case,
  reshape it; a struct that has doubled its members is telling you the abstraction moved.
- **Minimizing the number of code paths is the single most important thing for correctness.** Every
  extra branch is a combination that must be reasoned about and tested; two paths that could be one are
  where bugs hide. Prefer one path that handles all cases over a special-case branch, and collapse
  branches that differ only in a value into a table or a computed operand.
- **Rewrite in three passes.** One: a prototype, thrown away. Two: the first real implementation, 100%
  working. Three: rewrite it to the fewest lines and strongest structure. Do not ship stage two: the
  experiments and the paths they left behind are the debt this section is about.
- **Optimize only for a PROVEN win.** An optimization that adds lines or code paths is allowed only when
  it buys a measured performance gain that matters. A speculative optimization is a net loss: it costs
  the one thing (code paths) most worth protecting for a benefit you have not shown.

## 3. Naming

- **No abbreviations.** Full words: `functionCount` not `fnCount`, `memory` not `mem`, `natives` not `nat`,
  `registerClass` not `regClass`. Established short domain terms that read as words are fine (`dsp`, `ip`).
- **Boolean queries are prefixed `isX()`**: `isCompiled()`, `isResident()`.
- Wrap variable, parameter, class, and function names in back-ticks inside comment text: "`weight` is the block span".

## 4. Class layout and headers

- **Hard encapsulation.** A class owns its representation and hides it completely. Data members are private; state
  changes only through methods that preserve the class invariant, which the constructor establishes (see RAII, section
  1). No client, subclass, or friend reaches past the interface to read or write internals, and there is no backdoor
  that fills the fields from outside. Expose behavior, not state: a getter/setter pair over what is really a public
  field is still a leak if it lets a caller drive the object into an invalid state. Keep implementation detail (helper
  types, buffers, bookkeeping) out of the public header so the client surface shows only what a client must call. Hard
  encapsulation is what makes RAII and design-by-contract enforceable: if the representation can only change through
  vetted methods, an inconsistent or half-built object is simply unobservable.
- **Grouped access-specifier sections, public first.** Write `public:` / `protected:` / `private:` as section headers
  on their own line (one tab in), with members indented one further tab beneath them, as NuXJS does. Do NOT prefix
  every member with its access specifier (`public:  method()` on each line): that per-declaration form is an older
  style being phased out. New code uses grouped sections; when editing an existing file, match whatever that file
  already uses.
- **No heavy headers.** Big function bodies live in a `.cpp`; only small or hot inlines belong in a header. A large
  method defined inline in a header will be moved out in review.
- **No `inline` keyword in a `.cpp`.** A file-scope helper is `static`, never `static inline`: the compiler sees the
  whole translation unit and decides better than the author can. In a header `inline` means something else: it is
  what makes a definition legal in every translation unit that includes it, so header definitions keep it.
- **Keep the client surface minimal.** Internal helpers are not public API: make them protected members of the class
  that uses them, or namespace-internal, not part of what a client sees when they include the header.
- **C++ standard is per-repo, not a universal rule.** Match whatever standard the target repo requires. Application and
  product code is typically C++11 with some C++14; reusable libraries lean C++03 for maximum portability and stability,
  but pragmatically go to C++11 where it clearly pays (e.g. `shared_ptr`); it is a judgement call, not dogma. (GAZL, for
  example, keeps its shipped headers and `.cpp` strict `-std=c++03`-clean, with `0` not `nullptr` and `<stdint.h>` not
  `<cstdint>`, while its tools and tests use C++11.)

## 5. Comments

- **Comment sparingly: few and short.** Every comment is a maintenance liability that drifts as the code moves out
  from under it, so minimize the surface that can go stale. A comment must earn its place: the non-obvious *why*, an
  invariant, a gotcha; never the *what* (the code says that). Default to no comment. Lean on a reference to the design
  doc (by name and section) instead of re-explaining the design inline, and when editing prefer deleting a stale
  comment to updating it.
- **Write for the reader of the file you are in, not for yourself.** A public header is read by someone USING the
  interface, so it states the contract and nothing else. Rationale aimed at whoever implements the other side is not
  contract: why an enum starts at 1, what a `memset` mistake would look like, which alternatives were rejected, what a
  host was measured doing, how a decision was reached. That belongs in `docs/`, referenced by name from the
  declaration. After a design is argued out, what lands in the code is the CONCLUSION, never the argument.
- **The FIRST SENTENCE is the brief.** Whatever form the comment takes, its opening sentence states what the
  signature cannot: allocates or not, thread rules, ownership, the constraint that decides whether to call it. Never
  a paraphrase of the name. Everything after it is detail, and a reader who stops at the first sentence must not be
  misled.
- **Keep a block comment adjacent to what it describes.** Two blocks stacked with a declaration below them silently
  orphan the first, and a reader drops it.
- **Multi-line block comments** use `/*` on its own line, the body indented one tab, and `*/` on its own line (single
  asterisk, tab-indented body):
  ```
  /*
  	One or more sentences. Wrap `names` in back-ticks.
  */
  ```
  Do NOT write a paragraph as a stack of `//` lines, and do NOT use decorative empty `//` banner lines.
- **Struct members take an end-of-line comment, never a preceding block comment.** However long the text runs, it stays
  on the member's own line. Block comments in a public header belong to the file as a whole (`/* # Title ... */`) or to
  a GROUP of declarations (an enum), never to one member. If a member seems to need more than its line, the excess is
  rationale (see above).
- **Short inline comments** use a single end-of-line `//`, sitting at column 120 (the wrap column) padded with tabs.
  That is the general rule, with exceptions. A run of short related declarations may align to a common local column
  instead.
- **No Doxygen.** No `///`, no `///<`, no `/** */`, no `@param`/`@return` tags in new code. Plain `//` and `/* */`
  only. Legacy code that still carries the old Doxygen style keeps it: do not copy it into new code, and do not convert
  a file wholesale as a side effect of an unrelated change.
- **One declaration per line.**
- **No dashes as punctuation, and plain ASCII only.** Never use en or em dashes (U+2013, U+2014) or any other non-ASCII
  lookalike (U+2011 non-breaking hyphen, curly quotes, U+00A0), and do not fake a dash with a spaced hyphen either
  (`this - like - that`). Rewrite the sentence with a comma, a colon, parentheses or two sentences. The ASCII hyphen is
  only for hyphenated words, ranges, options and code. Plain ASCII everywhere: code, comments, docs and commit
  messages, so write "section 5" rather than a section sign. The one exception is people's names in copyright and
  license lines. Stick to characters that are on every keyboard and survive every encoding, so text stays greppable
  and diffs stay clean.

## 6. Formatting

- **Tabs for indentation, width 4.**
- **Opening brace on the same line** as the control statement; closing brace on its own line.
- **Control-flow bodies are always braced and never inlined.** `if`, `else`, `for`, `while`, `do`, and `switch`
  always use `{ }`, even for a single statement, and the body goes on its own line(s), never on the control
  statement's line. The opening brace ends the control line, each body statement sits on its own indented line,
  and the closing brace is on its own line:
  ```
  if (x) {
  	blahblah;
  	duhduh;
  }
  ```
  Both a braceless body (`if (x) blahblah;`) and a one-line braced body (`if (x) { blahblah; duhduh; }`) are wrong.
  There are no exceptions: a flat run of `if (cond) return X;` rows is braced too.
- **Short inline function bodies may be a single line.** A function or method body of one or two simple statements
  may sit on one line when that reads more elegantly (`int size() const { return count; }`). This is about function
  bodies only, not the control-flow rule above: a control statement inside an inline body still follows that rule.
- **Maximum line width 120 columns.** (A trailing `//` comment may start at column 120 and run past it; see section 5.)
- **`#if` / `#endif` sit one tab LEFT** of the surrounding code's indentation.
- **Break long lines by leading with the operator**, indented two tabs from the original line: the double tab marks a
  continuation, distinct from a nested block:
  ```
  someCall(veryLongFirstArgument, secondArgument, thirdArgument
  		, fourthArgument, fifthArgument)
  ```

## 7. Markdown

- **Pad table cells so the pipes line up.** Markdown renders either way; we read the source far more often than the
  rendering, so a table must be legible as plain ASCII. Pad every cell with trailing spaces to the width of its widest
  cell, and make the separator row that same width in dashes. Every row then has identical length:
  ```
  | access                          | bound                     | enforced                         |
  | ------------------------------- | ------------------------- | -------------------------------- |
  | `%N`, `$x` (fixed offset)       | `localsSize + paramsSize` | once, at `FUNC` entry (cpp:1273) |
  | `SETL` / `GETL` (dynamic index) | `dataStackEnd`            | every access (cpp:1312-1313)     |
  ```
  Tables obey the 120-column limit like everything else. If padding pushes past it, shorten the cells (abbreviate,
  or move an aside into prose under the table); do not let the table sprawl.
- **ASCII diagrams must actually align.** Count the columns; a caret or arrow that misses its target by one is worse
  than no diagram. Generate the marker lines rather than eyeballing them.

## 8. Commit messages

- **Short imperative subject, little or no body.** "Fix pen joint documentation", not a paragraph restating what the
  diff already shows. Add a body only when the *why* is not visible in the change itself.
- **No attribution trailers.** No `Co-Authored-By` for tools or agents, no generated-with footers.
- The dash and ASCII rule in section 5 applies to commit messages too.

## Local additions

GAZL takes two deliberate exceptions to the rules above. Do not "fix" either one, and do not convert existing code to
match the shared rule; both apply to new and edited code the same way they apply to what is already here.

- **Section 6, braces: a flat dispatch or lookup table stays unbraced.** Where braces clearly hurt, a one-per-line run
  of `if (cond) return X;` rows reads better than dozens of braced blocks, and bracing it only bloats the line count
  for no clarity. The assembler and opcode dispatch in `src/GAZL.cpp` are what this exists for. Outside such a table
  the shared rule holds: when in doubt, brace it, and confirm the exception in review.
- **Section 4, the `inline` keyword in a `.cpp`: GAZL keeps it.** The file-scope helpers near the top of
  `src/GAZL.cpp` (`absolute`, `canonicalNaN`, `idiv`, `imod`, `iadd`, `isub`, `imul`, `ishl`, `ashr`, `lshr`, `ftoi`)
  are marked `inline` on purpose. They sit on the interpreter's hottest arithmetic path, and they are the
  defined-semantics helpers that replaced undefined behaviour, so the interpreter, both JIT backends and the emitted
  C++ must all agree on them. Removing the keyword is a performance change, not a formatting change, and would need a
  benchmark rather than a sweep.
