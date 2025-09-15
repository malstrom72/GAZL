# PikaScript

PikaScript is a minimal interpreted scripting language implemented in C++. This repository contains the language runtime, standard library, a command-line tool called `PikaCmd`, and various example scripts and documentation. A thorough language reference is provided in [docs/PikaScript Documentation.txt](docs/PikaScript%20Documentation.txt) (also available as [HTML](https://htmlpreview.github.io/?https://raw.githubusercontent.com/malstrom72/PikaScript/main/docs/PikaScript%20Documentation.html)).

## History

PikaScript began as a tiny scripting experiment and has hardly changed since 2008. It is now something of a legacy technology, but it remains very stable and is still maintained and used at Sonic Charge. See [docs/PikaScript History.txt](docs/PikaScript%20History.txt) for more background.

## Repository Layout

```
src/            - C++ source code of the PikaScript interpreter and standard library
tools/PikaCmd/  - command-line tool sources, build scripts, installation helpers
examples/       - samples and experiments (various quality, many of them unfinished)
docs/           - documentation (text and HTML formats)
tests/          - tests and helper scripts
```

## Building PikaCmd

The easiest way to build the command-line tool is to run the build script from
the repository root:

```bash
bash tools/PikaCmd/SourceDistribution/BuildPikaCmd.sh
```

`BuildPikaCmd.sh` calls `BuildCpp.sh` internally to compile `PikaCmdAmalgam.cpp` and execute available unit tests.

After building, `PikaCmd` can be installed system-wide using `InstallPika.sh`, which copies the binary and helper scripts to `/usr/local/bin`.

Equivalent scripts are also available for Windows.

For a detailed overview of `PikaCmd`, the helper script `systools.pika` and how to write portable build scripts, see [docs/PikaCmd Documentation.txt](docs/PikaCmd%20Documentation.txt).

The file `DownloadAndInstallReadMe.txt` also provides one-line boot-strap commands for Windows or Unix systems to download and install PikaScript:

```bat
powershell.exe -Command "(New-Object System.Net.WebClient).DownloadFile('https://github.com/malstrom72/PikaScript/releases/latest/download/install.bat','%TEMP%\\install.bat')" && %TEMP%\\install.bat
```
```bash
curl -fsL https://github.com/malstrom72/PikaScript/releases/latest/download/install.sh | bash
```
These install scripts fetch only the `tools/PikaCmd/SourceDistribution` package from the
latest release, keeping the download small for quick installation.

## Running Scripts

Once built, you can run a PikaScript program via:

```bash
output/PikaCmd myscript.pika
```

For an interactive REPL experience, run `output/PikaCmd` without arguments (this will invoke `interactive.pika`):

```
Enter expressions to evaluate them interactively line by line. E.g.:

    3+3
    print('hello world')
    run('chess.pika')
```

The interactive environment provides several special commands:

```
?                           this help
<page>?                     shows a page from the standard library help system
=                           displays the full definition of the last evaluated expression
<variable>=                 displays the full definition of a variable / function / container
%                           re-run last executed PikaScript source file
%['']<path>[''] [args...]   runs a PikaScript source file (optionally with arguments)
!<command>                  executes a command with the operating system's command interpreter
exit                        exits
```

## Debugging

To enable assertions and debugging helpers, include the built-in `debug.pika` script at the start of your program:

```pika
include('debug.pika');
```

This installs a tracer that prints error offsets and a short call stack whenever an exception is caught. It also defines the `debug()` function for step-by-step execution and exposes `trace()` helpers for more verbose output.

## PPEG Parser Generator

PPEG is a self-hosting parser generator. All PPEG related files live under `tools/ppeg`. For details on how to run them, see [docs/PPEG Documentation.txt](docs/PPEG%20Documentation.txt).

## Documentation

* [PikaScript Documentation](docs/PikaScript%20Documentation.txt) ([HTML](https://htmlpreview.github.io/?https://raw.githubusercontent.com/malstrom72/PikaScript/main/docs/PikaScript%20Documentation.html)) – language reference
* [PikaCmd Documentation](docs/PikaCmd%20Documentation.txt) ([HTML](https://htmlpreview.github.io/?https://raw.githubusercontent.com/malstrom72/PikaScript/main/docs/PikaCmd%20Documentation.html)) – command-line tool usage
* [PPEG Documentation](docs/PPEG%20Documentation.txt) ([HTML](https://htmlpreview.github.io/?https://raw.githubusercontent.com/malstrom72/PikaScript/main/docs/PPEG%20Documentation.html)) – parser generator manual
* [systools Documentation](docs/systools%20Documentation.txt) ([HTML](https://htmlpreview.github.io/?https://raw.githubusercontent.com/malstrom72/PikaScript/main/docs/systools%20Documentation.html)) – cross-platform utilities
* [htmlify Documentation](docs/htmlify%20Documentation.txt) ([HTML](https://htmlpreview.github.io/?https://raw.githubusercontent.com/malstrom72/PikaScript/main/docs/htmlify%20Documentation.html)) – helper for generating the HTML docs

HTML documentation for the standard library lives in `docs/help/`. Open [docs/help/index.html](https://htmlpreview.github.io/?https://raw.githubusercontent.com/malstrom72/PikaScript/main/docs/help/index.html) to browse available functions and examples.

## Testing

The standard library examples shown in `docs/help` also serve as the unit tests. `tests/unittests.pika` loads each documentation page, extracts the example code, and executes it. This double-checking system keeps the docs accurate and ensures the library functions behave as documented.

## Running the Test Suite

From the repository root, execute the build script to compile both the beta and release versions and run all regression tests:

```bash
bash build.sh
```

Windows users should run `build.cmd` instead. The script builds the tool in both modes and then runs the full test suite.

## Building the fuzz target

The `tools/buildPikaCmdFuzz.sh` script compiles `tools/PikaCmd/PikaCmd.cpp`
with libFuzzer and address sanitizer enabled. The build forces C++14 because
`PikaScript` still depends on deprecated functional helpers removed in later
standards:

```bash
bash tools/buildPikaCmdFuzz.sh
```

The resulting binary is placed in `output/PikaCmdFuzz` and can be run with a
directory containing seed inputs:

```bash
./output/PikaCmdFuzz corpus/
```

On macOS the default clang from Xcode does not ship the libFuzzer runtime.
Install the `llvm` package via Homebrew and invoke the script with that
compiler:

```bash
CPP_COMPILER=$(brew --prefix llvm)/bin/clang++ bash tools/buildPikaCmdFuzz.sh
```

## License

PikaScript is released under the BSD 2-Clause license. See [LICENSE](LICENSE) for details.

