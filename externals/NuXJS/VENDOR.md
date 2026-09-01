# Vendored NuXJS

NuXJS is Magnus Lidström's own ES5.1 JavaScript engine, BSD 2-Clause, upstream at
<https://github.com/malstrom72/NuXJS>. It is vendored here by **file copy**, not as a submodule, so
nothing in `.git` records which upstream revision these files came from. This file is that record.

**Pinned at `5fb318dac900069849206ada8a1920e73efd3a51`** (2026-08-31, "Guard the local-time
conversions"). Take releases from `main` only. Upstream also publishes work-in-progress branches
(`ES51`, `codex/*`, `opcode-layout-opt-experiment`, `stepwise-gc`), and a local `NuXJS` clone sitting
on one of those is work in progress, not a release.

Only five files are taken:

| Vendored | Upstream |
|---|---|
| `LICENSE` | `LICENSE` |
| `src/NuXJS.cpp` `src/NuXJS.h` `src/stdlibJS.cpp` | same paths |
| `tools/NuXJSREPL.cpp` | same path |

`tools/NuXJSREPL.cpp` supplies the host globals the repo's `*.nuxjs.js` scripts rely on - `print`,
`printErr`, `read` - so a bump that changes them ripples into every script that runs under the
engine, the Impala front end included. The REPL takes its time-out from `-T` / `--timeout` and
imposes none by default, so a long compile runs to completion.

## Bumping

From the repository root, with the pin above as the `..` endpoint so you can read what you are taking:

```
git clone https://github.com/malstrom72/NuXJS.git /tmp/nuxjs
git -C /tmp/nuxjs log --oneline 5fb318d..origin/main
cp /tmp/nuxjs/LICENSE externals/NuXJS/LICENSE
cp /tmp/nuxjs/src/NuXJS.cpp /tmp/nuxjs/src/NuXJS.h /tmp/nuxjs/src/stdlibJS.cpp externals/NuXJS/src/
cp /tmp/nuxjs/tools/NuXJSREPL.cpp externals/NuXJS/tools/
bash build.sh
```

Then update the pin in this file. Expect `src/stdlibJS.cpp` to show a whole-file diff even for a small
change: it is the minified JS standard library re-encoded as C string literals, so any edit reflows
every line.
