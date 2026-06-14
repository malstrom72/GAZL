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
./output/NuXJS -s output/impala.nuxjs.js hello.impala 0x4d2 hello.impala > hello.gazl
./output/GAZLCmd hello.gazl main
```

The command prints:

```text
Hello, world!
```

## 3. Run the compiled code directly (optional)

You can also keep the generated assembly and invoke the VM yourself:

```bash
./output/NuXJS -s output/impala.nuxjs.js hello.impala 0x4d2 hello.impala > hello.gazl
./output/GAZLCmd hello.gazl main
```

This produces the same output as above.
