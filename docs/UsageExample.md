# Usage Example: Hello Impala

This short tutorial shows how to compile and run a tiny Impala program using the
tools staged in the `output/` directory after running `build.sh`.

## 1. Write a program

Create a file `hello.impala` with the following contents:

```impala
extern native print;

function main()
{
	print("Hello, world!\n");
}
```

## 2. Compile and run

Use the staged NuXJS executable to compile the file, then run it via `GAZLCmd`:

```bash
./output/NuXJS output/impala.nuxjs.js hello.impala hello.gazl 0x4d2 hello.impala
./output/GAZLCmd hello.gazl main
```

The command prints:

```text
Hello, world!
```

## 3. Re-run without recompiling (optional)

`hello.gazl` is a self-contained assembly file, so once it is compiled you can run it again at any
time with just `GAZLCmd` - no need to invoke the compiler:

```bash
./output/GAZLCmd hello.gazl main
```

This produces the same output as above.
