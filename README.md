# GAZL

GAZL is a lightweight, typed virtual machine with a readable assembly language. The
`impala` directory contains a small C-like compiler that targets GAZL.

## Building

The project is built using the helper scripts in `tools/`. On a UNIX like system
you can build both `PikaCmd`, `GAZLCmd` and the Impala compiler using:

```sh
# from the repository root
CPP_MODEL=native ./tools/BuildImpala.sh
```

The default build script uses macOS specific compiler flags which may not work on
Linux. Setting `CPP_MODEL=native` avoids these cross compilation flags. The
script also runs `ImpalaDemo.impala` once the build completes.

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
