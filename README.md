# GAZL

GAZL is a lightweight, typed virtual machine with a simple, readable assembly language. It includes a small C-like compiler called **Impala**, located in the `impala/` directory, which targets the GAZL VM.

> **Disclaimer**
> While the GAZL VM is largely complete for its design goals, the Impala compiler is merely a minimum viable implementation.
> It has been used extensively in Permut8 firmwares and other projects, but it lacks many modern language features.

## Features

- Lightweight typed **stack machine** with no registers and a uniform word size
- **Human-readable assembly** stored as plain text with compile-time macros and immediate assembly
- **Sandboxed** runtime supporting cooperative multitasking and full state suspension/resume
- **Impala** C-like compiler in PikaScript demonstrates higher-level language support
- Single header/source **C++** implementation with minimal dependencies
- Includes extensive **unit tests**, self-checks and a working language demo
- **Cross-platform** scripts for building, testing and running

## Prerequisites

You will need a standard C++ compiler.

- On **macOS** or **Linux**, use `g++` or `clang++`.
- On **Windows**, the build requires Microsoft Visual C++. Any version from Visual Studio 2008 (VC9.0) onward should work. The build scripts locate the compiler automatically using `vswhere.exe`, falling back to known versions if needed.

## Build & Test

Run `bash build.sh` (or `build.cmd` on Windows) from the root. This builds the GAZLCmd and PikaCmd binaries, runs the Impala compiler tests, and executes the demo program from the `output/` folder.

Both the **beta** and **release** targets are compiled with optimizations enabled. The **beta** build additionally has assertions turned on.

## Architecture

- `src/` – C++ VM implementation
- `impala/` – Impala compiler and demo sources
- `tools/` – build/maintenance scripts
- `externals/` – third-party code such as `PikaCmd`

### Getting Started

1. Build everything from the repository root:
   ```
   bash build.sh
   ```
2. Run the demo from the `output/` directory:
   ```
   cd output
   ./PikaCmd impala.pika run ../impala/ImpalaDemo.impala
   ```
3. See the [Using from C++](docs/Overview.md#using-from-c) notes in `docs/Overview.md` for integrating the VM in your own projects.

## Helper Scripts

- `build.sh` / `build.cmd` – build all tools and run the full test + demo sequence
- `tools/buildGAZLCmd.sh` / `.cmd` – build just `GAZLCmd` (VM executable)
- `tools/BuildImpala.sh` / `.cmd` – build `PikaCmd` and stage the compiler into `output/`
- `tools/buildGazlFuzz.sh` / `.cmd` – build libFuzzer harness for `GAZLCmd`

## Building the fuzz target

The `tools/buildGazlFuzz.sh` script compiles `tools/GAZLCmd.cpp` using clang and libFuzzer:

```bash
bash tools/buildGazlFuzz.sh
```

The resulting binary is placed in `output/GAZLFuzz` and can be run with a directory containing seed inputs:

```bash
./output/GAZLFuzz corpus/
```

On macOS the default clang from Xcode does not ship the libFuzzer runtime.
Install the `llvm` package via Homebrew and invoke the script with that compiler:

```bash
CPP_COMPILER=$(brew --prefix llvm)/bin/clang++ bash tools/buildGazlFuzz.sh
```

## Documentation

- [Overview](docs/Overview.md) – general architecture and goals
- [Impala Quick Start](docs/Impala.md) – basics of the language and toolchain
- [Instruction Set](docs/InstructionSet.md) – extracted opcode descriptions
- [Usage Example](docs/UsageExample.md) – compile and run a simple program

More technical notes are embedded in the Impala source files.

## AI Usage

AI tools (such as OpenAI Codex) have occasionally been used to assist with documentation, code comments, test generation, and repetitive edits. All core source code has been written and refined by hand over many years.

## License

This project is released under the [BSD 2-Clause License](LICENSE).


