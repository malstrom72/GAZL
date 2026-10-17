# GAZL backlog

Known improvements to the GAZL assembler and VM that are not done. None of them is a correctness or safety
problem; each says why.

## Check constant COPY ranges at assembly time

`COPY` with a constant destination or source (`COPY_CCC`, `COPY_CVC`, `COPY_VCC`) is range-checked only at run
time, like every `COPY`. When both the address and the count are constants, the assembler could reject an
out-of-range copy instead. That needs operand 2 parsed before the address operands, and a forward-linked
address would have to carry the count into the deferred check.

Not a safety issue: the run-time check in `Processor::run` covers every form, so nothing out of range is ever
copied. The gain is an earlier, clearer error.

## Use or drop `newUnit`'s unit name

`Assembler::newUnit(const Char* unitName)` ignores `unitName`. Either put it in error messages, so a failure
names the unit it came from, or remove the parameter.

## Precompute call targets

`CALL` looks up the callee's code offset in the function table and adds `codeBase` on every call. A constant
call target could be resolved to its instruction once, after assembly. A performance idea; measure it, since
`Processor::run` is layout-sensitive.

## Drop the `MEMORY_OFFSET` bias from constant addresses

Constant addresses are stored with `MEMORY_OFFSET` added, so `Processor::run` indexes through
`mb = memoryBase - MEMORY_OFFSET`. Storing them unbiased would let constant-address PEEK and POKE index
`memoryBase` directly. Touches the assembler, the VM and the JIT together.

## Unicode source

`Char` is `char`, and identifiers and string data are byte-based. Supporting Unicode source needs a decision on
encoding (UTF-8 in, code points in DATA strings) more than code.

## `lookupConstant` out-parameters

`Symbols::lookupConstant` returns through pointers (`bool* isFloat, Value* value`) where the rest of the API
uses references. Changing it breaks hosts, so it waits for a version bump.

## Stack allocation for hosts

There is no host call to reserve space on the GAZL data stack, for a native that needs scratch memory GAZL code
can see. `accessParams` covers arguments only.

## C-style casts outside the dispatch loop

Casts are written `static_cast<Int>(v)`, not `(Int)v`, everywhere except the tight VM loop in `Processor::run` and
its operand macros, where the short form stays. `src/GAZL.cpp` still has about 58 C-style casts,
most of them in the assembler, `finalize` and the snapshot code. Convert them in one pass, leaving `run()` and its
macros alone, so the pass changes no generated code there.

## GAZL 2: rework `SEEK`

`SEEK :offset [*extent]` (GAZL 2 only) needs another design pass before GAZL 2 is final:
- `:offset` is measured from the section start, but reads like a relative jump of `offset` words. The base should
  be explicit, or the mnemonic clearer.
- It does not really seek. It never leaves the current section, and `*extent` fences the `DAT` rows that follow
  into an even smaller range, so it opens a part of the section and fills it. A name like `PART`, `SLOT` or `AREA`
  would say that.
- The common case, zeros and then more data, would be simpler as plain data, for example `DATz *n` (n zero words),
  leaving absolute placement to what Impala 2 really needs: struct fields at offsets the host decides.
- Data label extents (a label covers up to the next label or the end of its section, from #135) must become
  region-aware: regions may be opened in any offset order, so "the next label" cannot follow statement order, and
  `SEEK` labels have their own size (the extent, or 1).

## GAZL 2: the prefix decides a name's kind

GAZL 1 lets `#name` and `&name` take any name the operand accepts (`#table`, `#NULL`, `DATA &GAZL_VERSION`), because
compiled programs rely on it: Impala writes `#name` for a `! DEFp` name. Inside a `GAZL #2` region it should be
strict: `#` only numbers (literals, `DEFi`, `DEFf`), `&` only pointers (data labels, functions, `DEFp`, `NULL`).
That also covers the region's function-pointer rule on the `#` path, which today only the `&` path applies. Impala's
`--gazl2` output then writes `&name` for a `DEFp`.
