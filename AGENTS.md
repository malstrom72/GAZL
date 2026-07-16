# Repository Guidelines

All code style and design principles (RAII / design-by-contract, naming, class layout, comments, formatting) are in
[docs/CodingStyle.md](docs/CodingStyle.md), the canonical cross-project style doc. This file covers this repository's
operations (layout, build/test, scripts) and the commit conventions.

To run the test suite use the helper script with up to three minutes allowed for execution:

```bash
timeout 180 ./build.sh
```

Always execute this command before committing changes to verify that the build and regression tests succeed.

## Repository layout
The project uses a consistent folder structure. Build output is written to `output/` and no source files live there. Useful locations:

- `tools/` – scripts for building and maintaining the code and documentation.
- `projects/` – Xcode and Visual Studio project files.
- `docs/` – documentation.
- `externals/` - projects and source code from other repositories (only touch this content when explicitly asked to).
- `src/` – C++ source code for the library. The library is distributed as source rather than prebuilt binaries.
- `tests/` – regression tests.
- `output/` – contains only build artifacts (and any runtime dependencies), no source files.

Root-level `build.sh` and `build.cmd` (mirrored implementations) should build and test both the beta and release targets.

BuildCpp.sh and BuildCpp.cmd are copied from another repository. Only make changes to them if there is no other solution.

## Code style

Naming, comments, class layout, formatting (tabs width 4, braces, 120-column lines, line continuations, `#if`
indentation), and error handling are in [docs/CodingStyle.md](docs/CodingStyle.md) - the canonical cross-project style
doc. Not duplicated here, to avoid drift. Commit conventions are below.

When handling files with command-line tools (which may break tab characters):
- Always run `expand -t 4` on the file before processing.
- Always run `unexpand -t 4` on the file after processing.

## Commits and git

- **Short, concise, one-line commit subjects.** No essay in the subject.
- **Never add a `Co-Authored-By` trailer** (or any Claude / Anthropic attribution) to a commit.
- **Commit and push only when explicitly asked.** Do not commit proactively.
- **Run `./build.sh` before committing** (see above) so the build and regression tests pass.
- **Do not proliferate files.** Reuse an existing `.cpp` / `.h`; a new file has to earn its place. When something
  belongs in an existing translation unit, put it there.

## Script portability
All user-facing `.sh` and `.cmd` files must work when launched from any directory.
They should start by changing to their own folder (or the repository root) so that
relative paths resolve correctly.

`.sh` scripts must be runnable without requiring `chmod +x`; always invoke them with  
`bash path/to/script.sh` (do **not** rely on the system-default `sh`).  
Each script must start with a portable she-bang:

```
#!/usr/bin/env bash
set -e -o pipefail -u
```

Every `.sh` script must have a corresponding `.cmd` implementation with identical behavior. Use `.cmd` files rather than `.bat`.

```
# example for a shell script
cd "$(dirname "$0")"/..
```

REM example for a .cmd script  
```
CD /D "%~dp0\.."
```

For robust error handling, `.sh` scripts should begin as shown above, and `.cmd`
scripts normally use a simple error check:

```
CALL buildAndTest.cmd %target% || GOTO error
EXIT /b 0
:error
EXIT /b %ERRORLEVEL%
```

## Code Formatting

All JavaScript files in this repository should be formatted with Prettier using tab indentation.

- Install Prettier if necessary:
  ```sh
  npm install --no-save prettier
  ```
- Format sources before committing:
  ```sh
  npx prettier --write .
  ```

This project includes a Prettier configuration (`.prettierrc.json`) that enforces tab-based indentation and a wider print width.
