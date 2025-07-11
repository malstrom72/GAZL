# GAZL

GAZL is a lightweight, typed virtual machine with a readable assembly language. The
`impala` directory contains a small C-like compiler that targets GAZL.
See [docs/Overview.md](docs/Overview.md) for an overview of GAZL.

## Building

Cross platform build scripts are provided at the repository root. They use
`tools/buildGAZLCmd.sh` or `.bat` to compile a beta build of `GAZLCmd` for the
unit tests and then produce the release binary. `tools/BuildImpala.sh` or `.bat`
copies the files required to run Impala into the `output/` directory, rebuilds
the compiler from the `impala.ppeg` grammar and runs the demo using the freshly
built tools. The release executables for `GAZLCmd` and `PikaCmd` end up in
`output/` alongside the Impala scripts. The Impala test suite is executed
automatically at the end of the build.

- On Unix or macOS run `./build.sh`.
- On Windows run `build.cmd`.

## Running the demo

The build script runs this demo once automatically. You can run it again
manually with:

```sh
cd output
./PikaCmd impala.pika run ImpalaDemo.impala
```

## Running the test suite

Run the Impala test suite using `PikaCmd`:

```sh
cd output
./PikaCmd runTests.pika
```

When set up correctly the suite reports `Total errors: 0 / 53`.

The tests compile each file in `tests/sources` and compare the output with the
reference files in `tests/golden`.

Running `./build.sh` or `build.cmd` also performs this step automatically after
building the tools.

## AI-Assisted Content

This project occasionally uses AI (such as OpenAI Codex) to help with writing documentation, generating code comments, producing test code, and automating repetitive edits. All of the underlying source code has been hand-written and refined over many years.
