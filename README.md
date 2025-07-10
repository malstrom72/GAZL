# GAZL

GAZL is a lightweight, typed virtual machine with a readable assembly language. The
`impala` directory contains a small C-like compiler that targets GAZL.
See [docs/Overview.md](docs/Overview.md) for an overview of GAZL.

## Building

Cross platform build scripts are provided at the repository root. They call the
`BuildPikaCmd` helpers which leave the `PikaCmd` binary in `tools/PikaCmd/`.
`GAZLCmd` is compiled in beta and release modes with the binaries stored under
`output/`. The unit tests from `UnitTest.gazl` run through the beta build and
the Impala test suite is executed using the release build without copying the
executables around.

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
