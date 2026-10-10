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
