# GAZL

GAZL is a lightweight, typed virtual machine with a readable assembly language. The
`impala` directory contains a small C-like compiler that targets GAZL.
See [docs/Overview.md](docs/Overview.md) for an overview of GAZL.

## Building

The project is built using the helper scripts in `tools/`. On a UNIX like system
you can build both `PikaCmd`, `GAZLCmd` and the Impala compiler using:

```sh
# from the repository root
./tools/BuildImpala.sh
```

This builds using the native model and runs `ImpalaDemo.impala` once the build
completes.

## Running the demo

The build script runs this demo once automatically. You can run it again
manually with:

```sh
cd impala
./PikaCmd impala.pika run ImpalaDemo.impala
```

## Running the test suite

Run the Impala test suite using `PikaCmd`:

```sh
cd impala
./PikaCmd runTests.pika
```

When set up correctly the suite reports `Total errors: 0 / 53`.

The tests compile each file in `tests/sources` and compare the output with the
reference files in `tests/golden`.

## AI-Assisted Content

This project occasionally uses AI (such as OpenAI Codex) to help with writing documentation, generating code comments, producing test code, and automating repetitive edits. All of the underlying source code has been hand-written and refined over many years.
