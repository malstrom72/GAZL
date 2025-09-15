PPEG
####

What is PPEG?
=============

 PPEG is a parser generator written in PikaScript. It consumes grammars described using Parsing Expression Grammars
(PEG) and produces parser functions. All related files live in the `tools/ppeg` folder. The runtime interface
`ppeg.pika` loads the generated `initPPEG.pika` and exposes the `PPEG` class used to build parsers. The folder also
holds the grammar sources and an `updatePPEG.pika` script for regenerating `initPPEG.pika`.

Global vs Local Compilers
=========================

 The project provides two self-hosted grammars:

-  `tools/ppeg/ppegGlobal.ppeg` – builds parser functions stored in whatever dictionary `ppeg.$compileTo` points at.
  This variant uses global variables and is what regenerates PPEG itself (`initPPEG.pika`).

-  `tools/ppeg/ppegLocal.ppeg` – returns a self-contained parsing function. It relies on no globals and is easier to
  embed in other projects.

Running the Compilers
=====================

 Build the command line tool used to run scripts:
   
    bash tools/PikaCmd/SourceDistribution/BuildPikaCmd.sh       # or BuildPikaCmd.cmd on Windows

 Run the regression test that exercises both compiler variants from the repository root:

    output/PikaCmd tests/ppegTest.pika

 The test first uses the *global* compiler and then the *local* one. Both must compile themselves successfully for the
test to pass.

Regenerating the PPEG Implementation
====================================

 After changing a grammar, refresh `initPPEG.pika` with:

    output/PikaCmd tools/ppeg/updatePPEG.pika

 The same script can be run to experiment directly with the compilers. It rebuilds both variants and writes the new
implementation to `initPPEG.pika` if everything succeeds.

Example: Using the Local Compiler
=================================

 Below is a minimal example that compiles `examples/digits.ppeg` into a local parsing function and runs it. The script
locates both `ppeg.pika` and `digits.ppeg` relative to its own path so it works no matter which directory it is
launched from:

    include('systools.pika');
    include('stdlib.pika');
    include(run.root # '../tools/ppeg/ppeg.pika');

    src = load(run.root # 'digits.ppeg');
    parseDigits = ppeg.compileFunction(src);

    assert(> parseDigits('12345'));
    assert(> !parseDigits('12a45'));

compileFunction
===============

    >parser = ppeg.compileFunction(source)

 Converts `source` into a parser function. The generated parser has the following signature:

    ?success = parser(text, [@result], [@endIndex], [rule = 'root'])

 `result` is a reference supplying the starting value of `$$`, normally a container to collect results. `endIndex`
receives the position where parsing stopped. The optional `rule` argument selects the entry rule and defaults to
`'root'`.

PikaScript Actions
==================

 PPEG grammars may attach PikaScript code to an expression. The code is written inside braces `{}`. Before the block
runs, `$$` already contains the value produced by the expression. The action can inspect or modify this value
directly.

 The following helpers are available inside an action:

- `$$` – current value passed between rules.
- `$$s` – the source string being parsed.
- `$$i` – index within `$$s` for the next character.
- `$$parser` – dictionary holding all generated parser functions.

 These identifiers let actions look at the input, build data structures, or report errors via `ppeg.fail()`.

Tags and Captures
=================

 Each rule receives `$$` as both input and output. Without a tag, sub-rules operate on the same value. Tagging with
`name:expr` temporarily rebinds `$$` to `$name` while `expr` runs. Afterward `$name` holds the result and can be used
in later actions. A capture `name=expr`(or `$$=expr`) stores the consumed substring before any attached actions run.

 The grammar below builds a dictionary from colon-separated pairs:

     root   <-  "{" _                           { $$ = new(Container) }
                pair ("," _ pair)* "}" _ !.

     pair   <-  key:ident ":" _ val:number _    { [$$][$key] = $val }

     ident  <-  $$=[a-zA-Z]+ _
     number <-  digits=[0-9]+                   { $$ = evaluate('+' # digits) }
                _
     
     _      <-  [ \t\r\n]*

 `pair` tags the identifier and number so the action can read `$key` and `$val`. `number` captures the digits in
`$digits` before converting them. Tagged variables remain available until the rule returns.

