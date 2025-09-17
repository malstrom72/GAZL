# JSPEG TODO

JSPEG aims to port PikaScript's PPEG parser generator to JavaScript while keeping the familiar PEG syntax with embedded actions, tags, and captures.

See [PPEG.md](PPEG.md) for background on the original PikaScript-based implementation.

## Tasks
- [x] Catalog the differences between the existing PikaScript `.ppeg` grammar and the reference `jspeg.jspeg` file without modifying either source.
        - [x] Diff `jspeg.ppeg` and `jspeg.jspeg` to catalog missing constructs.
                - Confirmed both grammars share rule structure; remaining gaps center on action/tag semantics and runtime helpers rather than missing PEG forms.
                - Documented that character class expansion and local-variable synthesis still mirror the slower PPEG behavior and require dedicated optimization work.
        - [x] Summarize how captures, tags, and actions currently diverge so future updates can be scoped precisely.
                - JSPEG now mirrors the PPEG variable bookkeeping by guarding lookups with a shared `hasOwn` helper, keeping the generated locals aligned with the original dictionary semantics.
                - The action rewriter preserves `$` substitutions, normalizes whitespace, and short-circuits on empty segments, matching the control flow of the PPEG grammar while staying in JavaScript.
        - [x] Added `tagCaptureTest.jspeg` regression grammar to verify JSPEG handles nested tags, captures, and action-based aggregation.
- [x] Strengthen regression coverage around the current toolchain.
        - [x] Ensure `jspegCompilerTests.js` runs from the repository root.
        - [x] Expand JSPEG test coverage with an arithmetic grammar test.
        - [x] Add regression tests compiling both grammars and comparing parser behavior.
                - `jspegCompilerTests.js` now compiles the regression grammars with both the recorded compiler and the self-hosted build, comparing generated code and parse results.
                - The Impala grammar is rebuilt under both compilers to verify the generated JavaScript stays byte-for-byte identical.
- [ ] Maintain and modernize the generated compilers (`impalaCompiler.js`, `jspegCompiler.js`) once grammar updates are defined.
        - [x] Convert compiler outputs to ES modules or modern syntax while retaining Node compatibility.
                - Compilers now wrap the generated function in a CommonJS export shim so `require('jspegCompiler.js')` yields the compiler directly while still supporting the sandboxed VM loader used during regeneration.
        - [x] Provide a Node script that rebuilds the compilers from `jspeg.jspeg` (self-hosting step).
                - Added `updateJSPEG.js`, which regenerates both compilers or verifies they are current via `node updateJSPEG.js --check`.
        - [x] Verify regenerated compilers pass `jspegCompilerTests.js` and any new tests.
                - `updateJSPEG.js` now runs the regression suite after both regeneration and `--check` validation so stale or broken outputs are caught immediately.
- [x] Document how JSPEG relates to the original PPEG and provide build and usage instructions once the port stabilizes.
        - [x] Expand `PPEG.md` or create a dedicated `JSPEG.md` outlining differences and migration notes.
                - Added `JSPEG.md` covering the port's goals, directory layout, and how the JavaScript compiler fits alongside the legacy PPEG sources.
        - [x] Write a quickstart example showing grammar definition, compiler invocation, and parser usage.
                - Documented a Node.js REPL-style snippet that compiles a toy grammar and evaluates it against sample input.
        - [x] Link documentation from the repository's top-level README or `docs/` directory.
                - Referenced `impala/jspeg/JSPEG.md` from the root `README.md` so the new guide is easy to discover.
- [ ] Translate `impala.ppeg` semantics into `impala.jspeg` while preserving runtime behavior.
	- [x] Inventory PikaScript helpers referenced from the PPEG grammar and sketch JavaScript equivalents.
		- Catalogued the support routines that PPEG actions call (`map`, `clone`, `args`, string/queue helpers, math utilities) and confirmed their JavaScript mirrors live at the top of `impala.jspeg` with matching semantics.
		- Verified the control-flow helpers (`processBranches`, stock borrowing/return, meta emitters, unary/binary operator dispatch, type errors) are ported one-for-one so future grammar work can reference the same names.
		- Noted that PikaScript macros like `vargs`/`defaults` translate cleanly to optional parameters in the JavaScript helpers, and added a `$$parser["return"]` alias so future grammar work can reference the original helper name without tripping the JavaScript `return` keyword.
	- [x] Ensure the JSPEG action library covers queue/state helpers so the generated `impalaCompiler.js` matches the PPEG output on sample programs.
		- `$$parser.start` now resets the symbol tables, transient pools, and deferred string queues before each compilation so repeated runs mirror the PPEG lifecycle.
	- [ ] Diff remaining statement actions against the PPEG grammar to confirm label management and branch folding stay byte-for-byte compatible.
                - [ ] Reconcile the `BoolGroup`, `Comp`, and `If` handlers so the `$$parser.dry` toggles, `?->` / `<-?` sequencing, and `#DEBUG`-gated `Assert` emission match the original PikaScript actions before enabling new optimizations.
                        - The JS port mirrors the PPEG sentinel flow by swapping `void` for `undefined`, flipping `$$parser.dry` around `!Group`, and wiring `Assert` cleanup through `$$parser["return"]`; parity tests should ensure those helpers keep borrowed temporaries and labels identical to the `impala.ppeg` actions when nested boolean expressions short-circuit.【F:impala/impala.ppeg†L837-L870】【F:impala/jspeg/impala.jspeg†L1703-L1770】【F:impala/jspeg/impala.jspeg†L372-L391】
                        - `$$parser.processBranches` still rewrites the emitted comparison and branch pairs after `BoolGroup`/`If` run, so diffing should confirm both grammars leave comparisons with operands `[value, rhs, jump]` before the optimizer injects the alias target.【F:impala/impala.ppeg†L837-L884】【F:impala/jspeg/impala.jspeg†L1703-L1798】【F:impala/jspeg/impala.jspeg†L209-L295】
                        - Updated the JSPEG `Assert` rule to recycle the `BoolGroup` meta-record when building the failure message and copying the argument into `%<r+1>`, matching the PPEG action’s borrow/return choreography before branch folding runs.【F:impala/jspeg/impala.jspeg†L1750-L1771】
                - [ ] Compare the `DoWhile`, `While`, `Loop`, and `For` rules to ensure borrowed temporaries, loop labels, and `returnBack` usage mirror the `impala.ppeg` lifecycle, keeping `$$parser.processBranches` fed with identical meta-instruction streams.
                        - The original grammar keeps the `for` upper bound borrowed until after the `...` increment emits, then returns both the bound and loop-variable meta; verify the JS helpers’ `returnBack`/`releaseMeta` path leaves the same temporaries available when the initializer is omitted and the `<` comparison binds against the existing variable slot.【F:impala/impala.ppeg†L893-L913】【F:impala/jspeg/impala.jspeg†L1811-L1880】
                        - `DoWhile`, `While`, and `Loop` each create `l`/`e` labels with `?->` guards framing the body; parity checks need to confirm both grammars emit the leading `<--` and trailing `<-?` so `processBranches` can fold empty bodies without losing the exit jump target.【F:impala/impala.ppeg†L886-L902】【F:impala/jspeg/impala.jspeg†L1801-L1830】【F:impala/jspeg/impala.jspeg†L209-L295】
                - [ ] Exercise `Copy` and `Switch` (including `CaseExpr`) through both toolchains to verify `makeConstant`/`dropHash` folding, case-comment emission, and default-label wiring remain in sync.
                        - Both grammars compute the copy length through `makeConstant` before feeding `makeRValue` for the source and destination; confirm the JS version’s use of the length meta record (`$l`) does not leak borrowed operands when validation raises a type error mid-evaluation.【F:impala/impala.ppeg†L915-L923】【F:impala/jspeg/impala.jspeg†L1883-L1909】
                - [x] `Switch` still tracks a `progress` sentinel to guard multiple `case`/`default` blocks and extracts inline comments from the source stream; align substring boundaries between `$$s{$$i:…}` and `$$s.substr(...)` so label aliases created during `processBranches` do not disturb the emitted comment text.【F:impala/impala.ppeg†L926-L958】【F:impala/jspeg/impala.jspeg†L1909-L1992】【F:impala/jspeg/impala.jspeg†L209-L295】
                - Each `CaseExpr` subtracts `switch.from` before minting the per-case label; ensure the JS helper’s `returnBack` leaves that constant available for the next case, matching the `<` stock reuse in the PPEG runtime.【F:impala/impala.ppeg†L960-L963】【F:impala/jspeg/impala.jspeg†L1997-L2012】
        - [ ] Break down the remaining feature work blocking advanced fixtures so each gap in `impala.jspeg` is documented alongside its runtime touch points.
                - [ ] Function arguments: audit `FuncCall`, `borrowForCall`, and `makeArgValue` so multi-argument and nested calls preserve borrowed slot ranges, emit the correct `%arg` stride, and release temporaries in the same order as the PPEG runtime before reenabling `perfTest2.impala` comparisons.【F:impala/jspeg/impala.jspeg†L1660-L1693】【F:impala/impala.ppeg†L725-L784】
                        - [x] `ArgsDecl` now materialises declarator clones in a dense array so the subsequent `iterate` loop replays each argument through `$$parser.declare`, matching the PPEG behaviour and eliminating the `Undeclared identifier` failures when parsing `perfTest2.impala`.
                        - `FuncCall` in `impala.ppeg` borrows a contiguous `%` window, verifies the callee’s type contains the `'FN'` signature, emits the `'()'` meta instruction, and then walks the `count` down to return each temporary before seeding a `:=` record for the borrowed return slot; the JSPEG helpers should replicate that lifecycle so argument windows align byte-for-byte.【F:impala/impala.ppeg†L816-L828】
                        - The `Argument` rule increments the shared `count` and forwards each expression to `makeArgValue`, so the port must keep the borrow ordering intact when spreading arguments across `%base + offset` temporaries.【F:impala/impala.ppeg†L830-L833】
                - [ ] Local arrays and compound locals: extend `LocalsDecl`/`ArgsDecl` to recognise `array` declarators, plumb the resulting pointer/length bookkeeping into `makeLValue`, and ensure `InitList` materialises the same `TEMP`/`DATA` runs used for global arrays so fixtures relying on stack arrays (e.g., `inputTest.impala`) compile.【F:impala/jspeg/impala.jspeg†L1571-L1597】【F:impala/jspeg/impala.jspeg†L1888-L1909】【F:impala/impala.ppeg†L704-L789】
                        - [x] `LocalsDecl` now mirrors the PPEG clone semantics by copying each declarator into a fresh object inside the accumulator array, allowing `iterate` to visit array and scalar locals in order while preserving their recorded `type`/`size` metadata.
                        - `ArrayDecl` evaluates the compile-time size with `makeConstant`, strips the leading hash via `dropHash`, and immediately returns the borrowed constant so later declarators can reuse the slot; the JSPEG port still needs to mirror that borrow/return choreography while recognising array declarators inside local lists.【F:impala/impala.ppeg†L772-L788】
                - [ ] Native bridge coverage: replicate the `extern native` scaffolding by mapping `native` function declarations through `declare`, translating parameter passing through `borrowForCall`, and threading the host I/O helpers referenced by `inputTest.impala` so runtime glue matches the legacy compiler.【F:impala/jspeg/impala.jspeg†L1445-L1486】【F:impala/jspeg/impala.jspeg†L1660-L1693】【F:impala/impala.ppeg†L618-L686】
                        - `ExternDecl` already tags `NATIVE` entries with scope `functions` before handing them to `declare`, and the PikaScript runtime exposes those natives via `Script::Native` plus the `addLibraryNatives` bundle; parity testing will need the JS bridge to surface the same catalogue so fixtures invoking `print`, `input`, or `system` succeed.【F:impala/impala.ppeg†L738-L744】【F:externals/PikaScript/docs/PikaScript Documentation.txt†L497-L528】
                - [ ] `_` sentinel initialisation: repair the default `stock` record seeded in `$$parser.start` so the throwaway `_` slot begins at `-1` (matching the PPEG dictionary) and walks through `returnBack` correctly when statements discard results, eliminating the undefined-operand guard observed in the JS harness.【F:impala/jspeg/impala.jspeg†L1283-L1351】【F:impala/jspeg/impala.jspeg†L302-L391】【F:impala/impala.ppeg†L113-L172】
                - [ ] Meta clean-up sequencing: ensure `$$parser.end` flushes deferred strings, resets counters, and clears borrowed temporaries when compilation aborts mid-rule so subsequent runs do not inherit stale meta instructions or leaked `%` tokens.【F:impala/jspeg/impala.jspeg†L1283-L1351】【F:impala/jspeg/impala.jspeg†L209-L295】
- [ ] Establish regression comparisons between the PPEG-driven and JSPEG-driven Impala compilers.

### Test fixtures and smoke checks

- [x] Exercise `testdata/smoke.impala` through the JSPEG runner so the generated compiler can be sanity-checked without loading the full `ImpalaDemo.impala` example.
- The JS port now emits `MOVi`/`RETU` for the sample program after wiring up base-type decoding and constant assignment handling, and the Node harness now retabulates output using the Impala CLI tab stops so the smoke fixture matches the Pika baseline byte-for-byte.
- [x] Record additional parity fixtures that cover extern/native declarations and recursive control flow so the JS harness can guard more of the Impala surface area.
- [ ] `perfTest2.impala` fixture is recorded; JSPEG still needs function argument and local-variable support before the comparison can pass.
- [ ] `inputTest.impala` fixture is recorded; array locals and native call scaffolding remain TODO before the JSPEG compiler can match the baseline output.
- [ ] Fix the `_`-slot initialisation bug uncovered while running the JS harness: the root parse object currently ships with `_{_: void}`, so `assign()` still sees `leftx.operator === undefined` and triggers the meta-missing guard.

## Notes from documentation review

### JSPEG.md insights

- JSPEG is the Node.js port of the PPEG parser generator, aiming to deliver a 100% JavaScript toolchain for Impala by keeping PEG syntax but executing actions in Node instead of PikaScript.
- The `impala/jspeg/` directory contains the self-hosting grammar (`jspeg.jspeg`), its generated compiler (`jspegCompiler.js`), the Impala grammar (`impala.jspeg`), the generated Impala compiler, regression grammars, and helper scripts such as `updateJSPEG.js`.
- `jspegCompiler.js` exports `compileJSPEG` via CommonJS; the quickstart example shows compiling an inline grammar, `eval`ing the generated parser, and confirming parse tuples `[ok, value, index]`.
- `updateJSPEG.js` rebuilds or verifies the checked-in compilers (`--check` for CI) and should be followed by the repository-level `build.sh` to run regression coverage.
- Legacy `.ppeg` files remain alongside the port for behavioural diffs; open TODO items track remaining semantic and performance parity work.

### PPEG.md insights

- PPEG is the PikaScript parser generator backing the legacy toolchain; sources live under `tools/ppeg` with runtime entry point `ppeg.pika` loading `initPPEG.pika`.
- Two self-hosted grammars exist: `ppegGlobal.ppeg` writes functions into `ppeg.$compileTo` (used to rebuild `initPPEG.pika`), while `ppegLocal.ppeg` returns standalone parser functions.
- `updatePPEG.pika` refreshes `initPPEG.pika` and is also the entry point for experimenting with the compilers; regression tests run via `output/PikaCmd tests/ppegTest.pika` exercising both variants.
- `ppeg.compileFunction(source)` returns parsers invoked as `parser(text, [@result], [@endIndex], [rule='root'])`, exposing PEG actions to `$$`, `$$s`, `$$i`, and `$$parser`.
- Tags (`name:expr`) and captures (`name=expr`) rebind `$$` so actions can accumulate state; tagged variables persist until the rule returns, mirroring the example that builds a dictionary from colon-separated pairs.

### PikaScript documentation insights

- PikaScript is an interpreted, C-inspired scripting language prioritising a compact C++ core, straightforward host integration via string-backed variables, and pervasive runtime reflection while explicitly de-emphasising raw speed and memory footprint.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L7-L43】
- All values are internally strings with contextual typing, grouped into classes (void, boolean, number, string, reference, function/lambda, native). The documentation stresses that only numbers participate in arithmetic, only `true`/`false` drive conditionals, and concatenation uses the dedicated `#` operator, with numeric literals normalised so identical magnitudes share a canonical string form.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L53-L96】
- The language treats every construct as an expression: `if`/`for` yield values, compound blocks `{}` return their final sub-expression, and functions omit explicit `return`, so semicolons merely separate expressions unless syntax (e.g. `else`) forbids them.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L101-L155】
- Operator precedence spans function definitions, calls, element/subscript access, arithmetic, logical, assignment, and control-flow forms, matching the documentation table; postfix/prefix increments demand integer lvalues and substring syntax uses `x{[o]:[l]}` with optional bounds for slicing.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L101-L158】
- Arrays and associative containers are implemented via dotted variable naming (`a.0`, `phone.John.Doe`); the conventional `.n` member exposes the element count, and helpers like `compose`, `remove`, `append`, `map`, `set`, and `prune` in `stdlib.pika` streamline building and pruning tables.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L217-L266】
- Functions are first-class strings with two flavours: `function {}` creates fresh scopes, while lambda `>` shares the creator's scope; higher-order utilities such as `foreach` rely on `$n`/`$[i]` argument indexing and `$`-prefixed temporaries to walk caller-supplied data.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L273-L340】
- Scope control includes global variables (`::name`) and caller introspection via `^$0`; the standard `args` helper uses these hooks to validate arity and assign named references inside callees.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L347-L369】
- References use `@` to capture variables or arrays and `[]` to dereference, encoding frame identifiers like `:a93:` in the reference string so lookups can detect expired scopes while still allowing references to deferred variables.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L377-L434】
- Rudimentary object orientation leverages `$callee`, plus helpers `this()`, `method()`, `construct`, `new`, and `gc()` to build, name, and collect objects allocated under numeric globals via the library-managed heap counter.【F:externals/PikaScript/docs/PikaScript Documentation.txt†L439-L492】
- The C++ host interface centres on `Pika::Script` (`StdScript` default) with optional `QString`/`QuickVars` accelerators; host code instantiates `FullRoot`, registers `Script::Native` functions via `assignNative`, and can toggle the `addLibraryNatives` bundle (which also registers `run`/`include` and optional I/O helpers).【F:externals/PikaScript/docs/PikaScript Documentation.txt†L497-L528】

### PPEG Documentation insights

- The reference guide reiterates how `ppeg.compileFunction` returns a parser invoked as `parser(text, [@result], [@endIndex], [rule='root'])`, with actions gaining access to `$$`, `$$s`, `$$i`, and `$$parser` for stateful emission and diagnostics.【F:externals/PikaScript/docs/PPEG Documentation.txt†L67-L92】
- Its dictionary example shows `pair` tagging the identifier and number before storing into the accumulator container, while `number` captures the digits substring so the action can normalise it via `evaluate`; tagged variables stay live until the surrounding rule finishes, underscoring why JSPEG must preserve tag scopes when porting complex actions.【F:externals/PikaScript/docs/PPEG Documentation.txt†L102-L114】

### GAZL documentation insights

- GAZL keeps the VM intentionally register-free and operates on fixed-size “typed words,” so every
  instruction emitted by the Impala grammar must target locals or globals explicitly—mirroring why the
  JSPEG helpers carefully juggle borrowed temporaries and enforce `PEEK`/`POKE` when touching global
  state.【F:docs/Overview.md†L9-L19】
- Operand markers such as `%temp`, `int`, `ptr`, and `#const` establish the vocabulary the generated code
  must follow, clarifying why helpers like `borrow`/`returnBack` and `makeArgValue` preserve `%`-prefixed
  transients when constructing instructions like `CALL` or `=&` fetches.【F:docs/Overview.md†L22-L37】【F:docs/InstructionSet.md†L56-L67】
- The instruction reference reinforces how `CALL` consumes a contiguous `%temp` window and how data
  sections are emitted via `CNST`, `GLOB`, and `DATA`/`DAT*` records, matching the JSPEG actions that
  preallocate argument spans and chunk constant/temporary initialisers into `DATA` blocks during
  `GlobalDecl` and `InitList`.【F:docs/InstructionSet.md†L56-L125】
- Compile-time directives (`! DEFi`, `! IFDF`, etc.) manipulate assembler-time variables, which the Impala
  runtime mirrors by guarding assertions and string dumps with `#DEBUG` and by threading constant folding
  through helpers such as `makeConstant` and `dumpString`.【F:docs/Overview.md†L95-L147】
- Impala’s quick-start guide highlights that globals must be prefixed with `global`, arrays/pointers remain
  untyped, and qualifiers like `temporary`, `readonly`, and `extern native` map directly onto the
  declaration helpers exercised in `ExternDecl`, `ConstDecl`, and `declare`.【F:docs/Impala.md†L19-L81】
- Function definitions group argument/local declarations and rely on untyped values (with explicit casts
  and `itof`/`ftoi`), explaining why `FuncDecl` and `makeRValue` aggressively normalise operand records
  instead of tracking static types beyond the supported signature table.【F:docs/Impala.md†L105-L127】
- Control-flow descriptions document that `for` loops auto-increment and stop before the bound, `switch`
  covers a bounded integer range without fall-through, and statements like `copy`/`assert` map to specific
  runtime helpers—informing the JSPEG actions that precompute bounds, emit range guards, and wrap asserts
  in `#DEBUG` fences.【F:docs/Impala.md†L141-L166】

## Implementation notes from impala.jspeg review

### Runtime scaffolding

- The JavaScript grammar starts by recreating the tiny helper toolbox that the original PikaScript runtime provided—`map`, `clone`, `bake`, `assert`, `args`, plus string, queue, and math utilities—so the rest of the actions can assume ES3-friendly equivalents for collection management, template expansion, and diagnostics.【F:impala/jspeg/impala.jspeg†L6-L118】
- Core compiler tables live on `$$parser`: instruction-to-GAZL mappings, supported type signatures, cast destinations, zero constants, suffix tables, verbose type names, and the transient pools (`metacode`, `strings`, `stock`, `counters`, and symbol dictionaries) that track compiler state through a parse.【F:impala/jspeg/impala.jspeg†L121-L197】
- `$$parser.processBranches` mirrors the legacy optimiser by walking pending meta-code backwards to fuse boolean short-circuit chains, alias redundant labels, and invert comparisons whenever the target fall-through is flipped.【F:impala/jspeg/impala.jspeg†L209-L295】
- Transient management reuses the original “stock” model: validators prevent duplicate `%`/`<…>` ids, `borrow`/`borrowForCall` mint deterministic temporaries, and `returnBack` (also exposed as `$$parser["return"]`) puts tokens back—recursing through compile-time suffixed names when necessary.【F:impala/jspeg/impala.jspeg†L302-L391】

### Expression and type helpers

- `makeRValue` and `makeArgValue` decide when expressions can reuse existing l-values versus requiring fresh temporaries, cloning meta-records in place so downstream helpers always see canonicalised operands.【F:impala/jspeg/impala.jspeg†L408-L495】
- Binary arithmetic funnels through `binaryOp`, which enforces signature validity via `SUPPORTED_OPS`, rewrites pointer subtraction into `d`, and special-cases indexed loads/stores to emit the same meta-instructions the PPEG emitter produced for `=[]` and friends.【F:impala/jspeg/impala.jspeg†L512-L580】
- `mulDivOp` recognises the `(itof X) * 1.0` pattern (on either side) and collapses it back to a pure `=itof`, avoiding redundant temporaries before falling through to the standard multiply/divide handling.【F:impala/jspeg/impala.jspeg†L587-L640】
- The assignment helper accepts every l-value shape (`=`, `=*`, `=[]`, `=[]$`) while enforcing type compatibility, picks an r-value to keep alive, and emits a final `=` meta-instruction so chained expressions get the stored result.【F:impala/jspeg/impala.jspeg†L646-L734】
- Pointer-facing unary helpers translate the Impala operators into canonical forms: dereference rewrites address arithmetic into indexed loads, while reference turns locals and array elements into `=&`/`+` meta-ops or raises a structured failure for invalid l-values.【F:impala/jspeg/impala.jspeg†L741-L794】
- Symbol lookup walks the tracked scopes (`locals`, `globals`, `functions`, `defines`) and emits the correct meta op for each case—arrays become `=&`/`:=` pointer fetches, natives emit `^name`, and unknown names call `fail` immediately.【F:impala/jspeg/impala.jspeg†L963-L1111】
- Compile-time utilities such as `makeConstant`, `subConstInt`, and `dumpString` keep the optimizer path intact by forcing literal evaluation, materialising constant differences into borrowed `<` registers when needed, and serialising string data with printable/non-printable segmentation.【F:impala/jspeg/impala.jspeg†L1114-L1212】
- `makeString` layers a suffix-based dedup table over the string pools so repeated literal fragments point at the same emitted label, matching the behaviour of the PPEG toolchain during deferred string dumping.【F:impala/jspeg/impala.jspeg†L1214-L1254】

### Compilation lifecycle and grammar hooks

- `$$parser.start`/`end` bracket the compilation: they reset transient pools, symbol dictionaries, and deferred string registries, seed the random id (optionally from `impalaRandomId`), print the compiler banner, and later flush pending string literals—wrapping assert strings in a `#DEBUG` guard before resuming normal output.【F:impala/jspeg/impala.jspeg†L1283-L1351】
- The `root` rule kicks off by invoking `$$parser.start`, parses zero or more declarations (`FuncDecl`, `ExternDecl`, `ConstDecl`, `GlobalDecl`), then ensures EOF and calls `$$parser.end`, providing the same lifecycle boundaries as the PPEG grammar.【F:impala/jspeg/impala.jspeg†L1359-L1361】
- `FuncDecl` mirrors the legacy action block: it revalidates the transient stocks, prints a section banner, registers the function symbol (`FUNC`) as read-only, synthesises the implicit one-word return slot when `returns` is omitted, registers inputs and locals, and after the body flushes meta code, prunes the local symbol table, and resets label counters to match the PPEG lifecycle.【F:impala/jspeg/impala.jspeg†L1366-L1443】

### Declaration and statement actions

- `ExternDecl` covers `FUNCTION`, `NATIVE`, array, and plain variable forms by forwarding each through `$$parser.declare`, while `ConstDecl` temporarily disables forward references so constant initialisers can be evaluated via `makeConstant` before normal symbol lookup resumes.【F:impala/jspeg/impala.jspeg†L1445-L1486】
- `GlobalDecl` and `InitList` predeclare section headers (`GLOB`, `CNST`, `TEMP`), synthesise zero initialisers, and chunk large array literals into `DATA` records so emitted constants mirror the PPEG layout without overflowing the 55-character data line budget.【F:impala/jspeg/impala.jspeg†L1488-L1565】
- `ArgsDecl` and `LocalsDecl` clone every declarator into sequential slots on the current value stack, tracking `.n` so downstream actions can enumerate parameters and locals in definition order just like the PPEG helpers did.【F:impala/jspeg/impala.jspeg†L1571-L1597】
- `FuncCall` borrows a contiguous temporary window for arguments, asserts that the callee exposes a function signature, emits the `()` meta-op, and rewrites the original expression to fetch the borrowed return slot before releasing each temporary in reverse order.【F:impala/jspeg/impala.jspeg†L1660-L1693】
- `Statement` seeds every statement with a `';'` meta-record, slicing the upcoming source with `find(snippet, "{;\r\n")` so diagnostics mirror the PPEG grammar’s `$$s{$$i:find(...)}` snippet, then emits `<--` anchors for inline labels and releases temporary operands after expression statements finish parsing.【F:impala/jspeg/impala.jspeg†L1618-L1643】【F:impala/impala.ppeg†L790-L796】
- Boolean combinators reuse label allocators for short-circuiting: `BoolGroup`/`And` synthesise branch labels for each `||`/`&&`, while `Comp` flips `$$parser.dry` around nested groups so lookahead doesn’t emit duplicate meta code, and `Assert` gates its failure call behind `#DEBUG`, builds the error string via `makeString`, and jumps past the handler on success while returning borrowed registers.【F:impala/jspeg/impala.jspeg†L1703-L1772】【F:impala/impala.ppeg†L837-L870】
- `Goto` and `If` carve deterministic jump structure: `Goto` maps directly to a `-->` meta jump toward the tagged label, while `If` strings together fresh `?->`/`<-?` guards and (for `else`) an additional `<--` done-label so branch folding sees the same shape as the PPEG grammar.【F:impala/jspeg/impala.jspeg†L1776-L1798】
- Looping constructs enforce their static contracts; the `for` rule verifies the loop variable is a writable int/pointer, optionally applies an assignment before selecting either the initialiser result or the original l-value for the `<` comparison, and reuses the borrowed upper-bound meta record once the increment and exit guard have been emitted.【F:impala/jspeg/impala.jspeg†L1822-L1883】【F:impala/impala.ppeg†L893-L913】
- `DoWhile`, `Loop`, and `While` each preallocate paired labels (`l`/`e`) and emit the same `<--`, `-->`, `?->`, and `<-?` sentinels as the PPEG version, guaranteeing `processBranches` sees the identical entry/exit skeleton when collapsing empty bodies.【F:impala/jspeg/impala.jspeg†L1803-L1834】【F:impala/jspeg/impala.jspeg†L2024-L2035】【F:impala/impala.ppeg†L886-L971】
- `Copy` evaluates the transfer length as a compile-time constant, enforces pointer operands on both sides, and emits a `'copy'` meta instruction that borrows r-values for the destination and source before flushing the staging record—matching the original grammar’s error handling and resource cleanup.【F:impala/jspeg/impala.jspeg†L1888-L1909】【F:impala/impala.ppeg†L915-L923】
- `Switch` requires integer discriminants, precomputes the `(expr − from)` index, tracks case/default presence with a `progress` sentinel, and emits literal `case …` comments using the same colon-terminated slice as the PPEG grammar so regenerated diagnostics stay aligned.【F:impala/jspeg/impala.jspeg†L1913-L1998】【F:impala/impala.ppeg†L925-L957】
- `CaseExpr` subtracts the recorded `switch.from` constant to mint deterministic `label#offset` anchors before returning the borrowed compile-time difference, keeping per-case labels in sync with the surrounding `Switch` scaffolding.【F:impala/jspeg/impala.jspeg†L1997-L2018】【F:impala/impala.ppeg†L960-L963】

