# GAZL

GAZL is a lightweight, typed virtual machine with a simple, readable assembly language. It includes a small C-like compiler called **Impala**, located in the `impala/` directory, which targets the GAZL VM.

## Features

- Typed stack-based **virtual machine** with a compact, readable assembly format  
- **Impala**, a small C-like compiler implemented in [PikaScript](https://pikascript.com), targeting GAZL  
- Easily extendable syntax, semantics, and runtime behavior  
- Includes **unit tests**, compiler self-checks, and a working language demo  
- Written in **standard C++**, with minimal dependencies  
- **Cross-platform scripts** for building, testing, and demo execution

## Prerequisites

You will need a standard C++ compiler.

- On **macOS** or **Linux**, use `g++` or `clang++`.
- On **Windows**, the build requires Microsoft Visual C++. Any version from Visual Studio 2008 (VC9.0) onward should work. The build scripts locate the compiler automatically using `vswhere.exe`, falling back to known versions if needed.

## Build & Test

Run `./build.sh` (or `build.cmd` on Windows) from the root. This builds the GAZLCmd and PikaCmd binaries, runs the Impala compiler tests, and executes the demo program from the `output/` folder.

Both the **beta** and **release** targets are compiled with optimizations enabled. The **beta** build additionally has assertions turned on.

## Helper Scripts

- `build.sh` / `build.cmd` – build all tools and run the full test + demo sequence  
- `tools/buildGAZLCmd.sh` / `.bat` – build just `GAZLCmd` (VM executable)  
- `tools/BuildImpala.sh` / `.bat` – build `PikaCmd` and stage the compiler into `output/`  
- `tools/runTests.sh` / `.bat` – run all unit tests  
- `tools/runDemo.sh` / `.bat` – execute the Impala demo program  

## Running the Demo

You can manually rerun the demo after building:

```
cd output
./PikaCmd impala.pika run ../impala/ImpalaDemo.impala
```

This compiles and runs a small Impala program using the staged compiler.

## Running the Test Suite

Run the full Impala test suite using the staged `PikaCmd` binary:

```
cd impala
../output/PikaCmd runTests.pika
```

When set up correctly, the suite reports:

```
Total errors: 0 / 53
```

The tests compile each file in `tests/sources` and compare the results with golden outputs in `tests/golden`.

## Documentation

- [Overview](docs/Overview.md) – general architecture and goals  

More technical notes are embedded in the Impala source files.

## AI Usage

AI tools (such as OpenAI Codex) have occasionally been used to assist with documentation, code comments, test generation, and repetitive edits. All core source code has been written and refined by hand over many years.

## License

This project is released under the [BSD 2-Clause License](LICENSE).


