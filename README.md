# GAZL

GAZL is a lightweight, typed virtual machine with a readable assembly language. The
`impala` directory contains a small C-like compiler that targets GAZL.
See [docs/Overview.md](docs/Overview.md) for an overview of GAZL.

This project is released under the [BSD 2-Clause License](LICENSE).

## Building

Cross platform build scripts are provided at the repository root. They use
`tools/buildGAZLCmd.sh` or `.bat` to compile a beta build of `GAZLCmd` for the
unit tests and then produce the release binary. `tools/BuildImpala.sh` or `.bat`
builds `PikaCmd`, copies the binary to `output/` and rebuilds the Impala
compiler in place. Only the files required to run the compiler
(`impala.pika`, `impalaCompiler.pika`, `initPPEG.pika` and `systools.pika`)
are copied next to the `PikaCmd` binary so Impala can be executed directly from
the `output/` directory. The release `GAZLCmd` binary is also copied to the
`impala/` folder for convenience. After building the compiler the unit tests are
run and finally the demo is executed from the `output` directory to verify the
minimal setup.

- On Unix or macOS run `./build.sh`.
- On Windows run `build.cmd`.

## Running the demo

The build script runs this demo once automatically. You can run it again
manually with:

```sh
cd output
./PikaCmd impala.pika run ../impala/ImpalaDemo.impala
```

## Running the test suite

Run the Impala test suite using the `PikaCmd` binary in `output/`:

```sh
cd impala
../output/PikaCmd runTests.pika
```

When set up correctly the suite reports `Total errors: 0 / 53`.

The tests compile each file in `tests/sources` and compare the output with the
reference files in `tests/golden`.

Running `./build.sh` or `build.cmd` performs this step automatically after
building the tools and finally runs the demo from the `output` folder to verify
that the copied files are sufficient.

## AI-Assisted Content

This project occasionally uses AI (such as OpenAI Codex) to help with writing documentation, generating code comments, producing test code, and automating repetitive edits. All of the underlying source code has been hand-written and refined over many years.
