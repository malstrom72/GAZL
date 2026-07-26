# GAZL Overview

GAZL is a lightweight virtual machine and assembler intended for real-time or embedded use. The core library lives in `src/GAZL.cpp` and `src/GAZL.h` and the project includes an example compiler for a C-like language under `impala`. The assembly language is human readable and can be stored as plain text.

This document collects notes about the philosophies behind GAZL, how the VM works and how to use it from C++.

## VM Philosophy

* **Keep it simple.** GAZL has a small instruction set and no registers. All storage is through variables allocated as locals or globals.
* **Typed words.** Every value is a word of the same size regardless of whether it holds an integer, float or pointer.
* **Portable text format.** Programs are distributed as assembly text so compile-time decisions can be made while loading code. See `src/UnitTest.gazl` for numerous examples.
* **Safety.** The runtime is sandboxed and cooperatively multitasked. A host application typically calls `Processor::run()` for a limited amount of cycles and can suspend a VM by returning non‑zero from a native function. Execution resumes by calling `run()` again on the same `Processor` instance.

## Assembler

The assembler accepts the textual instruction format shown in `src/UnitTest.gazl`. Data sections are introduced with directives like `CNST` or `GLOB` and code is expressed using instructions such as `MOVi`, `PEEK` and `CALL`. Compile‑time constants (`#NAME`) can be defined with `!` directives. Setting different constant values allows the same text file to compile to code with different behaviour (for example `! DEFi DEBUG #0` or `! DEFi DEBUG #1`).

Globals are accessed explicitly through memory operations. The VM has no registers so locals are declared with `LOCi`, `LOCf` or `LOCp` and accessed directly in instructions. For global data you must use `PEEK` to read and `POKE` to write or copy ranges via `COPY`.

### Operand Types

Instruction comments in [`src/UnitTest.gazl`](../src/UnitTest.gazl) document the operand notation used by the assembler. The most common markers are:

```
%temp      local "transient" variable   (e.g. %0, %1)
int        local int variable           (e.g. myInt, localArray:3, %0)
float      local float variable         (e.g. myFloat, localArray:3, %0)
ptr        local pointer variable       (e.g. myPointer, localArray:3, %0)
var        local variable (any type)    (e.g. myVar, localArray:3, %0)
#int       constant int                 (e.g. #0, #1, #DEFINED_INT, <A>, <B>)
#float     constant float               (e.g. #0.0, #1.0, #DEFINED_FLOAT, <A>, <B>)
&address   constant pointer             (e.g. &global, &globalArray:3, &myFunction, <A>, <B>)
#const     any constant (incl address)  (e.g. #-1, #3.3, #DEFINED_INT, &global, &globalArray:3, &myFunction, <A>, <B>)
&function  GAZL function address        (e.g. &myFunction)
^native    native function              (e.g. ^print)
@label     branch label                 (e.g. @jumphere, @choice.1)
*size      constant integer size        (e.g. *1, *<A>)
```

These markers help the assembler verify operands and are reproduced here for easy reference.

## Implementation Notes

The virtual machine and assembler are implemented in standard C++ in a single header/source pair. `GAZL.h` describes the goals of the project:

```text
     Goals
        - Make it the world's fastest interpreting virtual machine.
        - Should use a highly portable ASCII assembly language format.
        - Assembly language should be easy to learn and write and require few optimization tricks.
        - Assembly source should be compiled to internal representation immediately prior to execution.
        - Compile-time calculations and conditions should be supported.
        - Instruction set should be sufficiently advanced to allow for C-like languages.
        - Run-time should be sandboxed, 100% safe and interruptable.
        - Run-time should allow for cooperative multi-tasking within a single OS thread.
        - Should be possible to suspend and resume full machine state.
        - Any future changes to the assembler format should be backwards compatible.
        - Implementation should be portable and CPU-agnostic using standard C/C++.
        - Should be multi-thread safe (no global variables).
        - Easy to interface with C/C++ functions.
        - Compact and documented single C++ file and header.
     Non-goals
        - Run-time memory foot-print does not have to be small.
        - No need for a compact persistent code format.
        - Data types of different sizes and precisions are not necessary.
        - No need to support high-level language concepts like dynamic memory management, exceptions etc
```

(See lines [10-33](../src/GAZL.h) in `GAZL.h`.)

## Using from C++

Use the `Assembler` class to parse assembly source and the `Processor` class to execute code. An example is found in `tools/GAZLCmd.cpp`:

```cpp
Assembler assem(100000, codeMemory, 100000, dataMemory, globals);
assem.newUnit("file.gazl");
const char* cp = source.c_str();
while (*cp != 0) {
    cp = assem.feed(cp);
    if (*cp == 0) assem.finalize(codeSize, rwSize, constSize);
}

Processor vm(codeSize, codeMemory, memorySize, dataMemory,
             rwSize + 30000, rwSize, 30000,
             ipStackSize, callStack, nativeFuncs);
vm.enterCall(globals.findFunction("main"));
Status result = vm.run();
```

The processor exposes helpers to access memory and parameters (`accessMemory`, `accessParams`). Writing your own native functions involves the `Processor*` interface; see the unit tests for examples.

For a smaller example, inspect the comments around the `Assembler` and `Processor` usage in `tools/GAZLCmd.cpp`.

## Textual Representation and Compile‑Time Constants

GAZL source is plain text so version control systems can store programs directly. The assembler supports compile‑time instructions introduced by `!`. These operate on special compile‑time variables written as `<A>` through `<Z>`. Instructions such as `! ADDi`, `! MULf` or `! IFDF` are executed when the file is assembled and can define constants, evaluate conditions or skip blocks of code. Changing a compile‑time definition (for example `! DEFi DEBUG #0` versus `! DEFi DEBUG #1`) changes the generated code without altering the original source.

A complete list of compile‑time opcodes is found in [`src/UnitTest.gazl`](../src/UnitTest.gazl) and reproduced below for convenience:

```
! ABSf    <?>             #float
! ABSi    <?>             #int
! ADDf    <?>             #float          #float
! ADDi    <?>             #int            #int
! ADDp    <?>             &address        #int
! DEFf    #float
! DEFi    #int
! DEFp    &address
! DIFp    <?>             &address        &address
! DIVf    <?>             #float          #float
! DIVi    <?>             #int            #int
! EQUi    #int            #int            @label
! EQUp    &address        &address        @label
! GEQi    #int            #int            @label
! GEQp    &address        &address        @label
! GOTO    @label
! GRTi    #int            #int            @label
! GRTp    &address        &address        @label
! IFDF    &address        @label
           #const          @label
           ^native         @label
! IFND    &address        @label
           #const          @label
           ^native         @label
! IORi    <?>             #int            #int
! LEQi    #int            #int            @label
! LEQp    &address        &address        @label
! LSSi    #int            #int            @label
! LSSp    &address        &address        @label
! MODi    <?>             #int            #int
! MOVf    <?>             #float
! MOVi    <?>             #int
! MULf    <?>             #float          #float
! MULi    <?>             #int            #int
! NEQi    #int            #int            @label
! NEQp    &address        &address        @label
! SHLi    <?>             #int            #int
! SHRi    <?>             #int            #int
! SHRu    <?>             #int            #int
! SUBf    <?>             #float          #float
! SUBi    <?>             #int            #int
! SUBp    <?>             &address        #int
! XORi    <?>             #int            #int
! fTOi    <?>             #float          #float
! iTOf    <?>             #int            #float
```

## Further Reference

The file `src/UnitTest.gazl` acts as a living specification of the instruction set. It contains comments for nearly every operation and illustrates how globals, constants and memory operations work. Consult it when implementing new code or interfacing with the VM.
For a condensed view, see the [Instruction Set](InstructionSet.md) reference which extracts these comment blocks into a single document.
