# GAZL

GAZL is a lightweight, typed virtual machine with a readable assembly language. The
`impala` directory contains a small C-like compiler that targets GAZL.
See [docs/Overview.md](docs/Overview.md) for an overview of GAZL.

## Building

Cross platform build scripts are provided at the repository root. They first
compile `GAZLCmd` in beta mode and run the unit tests from `UnitTest.gazl`.
Next they delegate to `tools/BuildImpala.sh` or `.bat` which builds `PikaCmd`
and the release version of `GAZLCmd`, rebuilds the Impala compiler and runs the
demo. The release executable is copied to `impala/` and also stored under
`output/` for convenience. The Impala test suite is run after the tools are
built.

- On Unix or macOS run `./build.sh`.
- On Windows run `build.cmd`.

## Running the demo

The build script runs this demo once automatically. You can run it again
manually with:

```sh
cd impala
../tools/PikaCmd/PikaCmd impala.pika run ImpalaDemo.impala
```

## Running the test suite

Run the Impala test suite using `PikaCmd`:

```sh
cd impala
../tools/PikaCmd/PikaCmd runTests.pika
```

When set up correctly the suite reports `Total errors: 0 / 53`.

The tests compile each file in `tests/sources` and compare the output with the
reference files in `tests/golden`.

Running `./build.sh` or `build.cmd` also performs this step automatically after
building the tools.

## AI-Assisted Content

This project occasionally uses AI (such as OpenAI Codex) to help with writing documentation, generating code comments, producing test code, and automating repetitive edits. All of the underlying source code has been hand-written and refined over many years.
