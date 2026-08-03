'use strict';

// Generative fuzzer for the Impala compiler.
//
// It emits random, mostly type-valid Impala programs that lean on the intricate paths
// (nested calls, struct locals/fields/copies, arrays, pointers, funcptr dispatch) and compiles
// each one. By-value struct params, struct returns, multi-value returns and destructuring are
// parked for Impala 3.0 (see docs/ParkedFeatures.md) so they are no longer generated.
// The oracle is robustness:
//   - a clean coded diagnostic (`error[Exxx]`) is an ACCEPTABLE outcome (invalid program),
//   - a raw JS exception or an internal `Assertion failed` (e.g. the transient-register
//     `validateStock` checks) is a COMPILER BUG.
// Every compiled program is also DEAD-STRIPPED, because `deadStrip` reads the emitted GAZL back with
// its own idea of where a block starts and ends, and a throw from it is that reading going wrong on
// output the compiler accepted. It threw on every symbolically-sized initializer - the shape section
// (4) below now generates - while 3000 fuzz programs stayed green, purely because nothing here called
// it. `main` is exported so the strip has a root to walk from.
//
// Control flow covers `if`/`else`/`switch`/`for`/`do`-while/`loop`/`goto`. The last three were added
// after a landed fix for two dangling-label shapes in the short-circuit pass was found to have exactly
// one hand-written fixture and no fuzz coverage at all - the generator emitted none of the three. Both
// shapes are now built on purpose (see genDoWhile); reverting that fix makes this fuzzer fail at seed 14.
//
// Deterministic: every program is produced from a seeded PRNG, so any failure reproduces
// from its printed seed.
//
// Usage: node impala/fuzzImpala.js [iterations] [startSeed] [--vm]
//   --vm also runs each compiled program on GAZLCmd, supplying a randomly PERMUTED host layout for the
//   checked `extern struct`. It flags two different things: a VM fault (the module failed to load, or
//   trapped), and a WRONG VALUE - `main` prints a set of checked globals before its random body, and the
//   driver compares that prefix of stdout against the words the generator computed. The second is a real
//   REFERENCE oracle: it sees output that assembled and ran perfectly while holding the wrong numbers,
//   which no crash oracle can. --vm additionally runs the DEAD-STRIPPED module and requires byte-identical
//   stdout - a third reference oracle, over what the strip removed rather than over what the compiler
//   emitted. Without --vm the run is compile-only, so none of the three apply.
// The runner reloads the compiler module per call, so keep a single process to <=~20k
// iterations (chunk larger sweeps across processes: for s in 0 20000 40000; do ... done).
// First find (seed 10024): finishDestructure released its output window low-to-high (unlike every
// other multi-slot window), leaving a freed hole below a live temp; a later struct-arg call's
// window overlapped it. Fixed by releasing the window high-to-low; borrowForCall now asserts the
// pool-reaches-the-top invariant, so a future release-order regression fails loudly here.

const { compileWithJsImpala } = require('./impalaJsCompilerRunner.js');
const { deadStrip } = require('./impalaImportClosure.js');
const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');

const GAZLCMD = path.join(__dirname, '..', 'output', process.platform === 'win32' ? 'GAZLCmd.exe' : 'GAZLCmd');
const COMPILE_OPTS = { randomId: 0x4d2, retabulate: true, trailingNewline: true };

// Run a compiled program on the VM. Returns null when there is no compiler fault, or a message when
// the loader/assembler REJECTS the emitted GAZL - that means the compiler produced structurally
// invalid or wrongly-linked output from a program it accepted, which is a real compiler bug.
//
// A non-zero exit is only a fault if it happens at LOAD time. The VM prints a "Code size: ...
// functions: N" banner once the module assembles; if we see it, the module loaded fine and any
// later non-zero status is a RUNTIME trap - the generated program's own undefined behaviour (a wild
// pointer, a write through a string literal, an out-of-region index). That is the program's fault,
// not the compiler's, and without a reference oracle we cannot call it a miscompile, so we ignore it.
// The generator avoids `/` and `%` (no div-by-zero), keeps every array/pointer index strictly IN
// BOUNDS (see genIdx), and always points a pointer local at a real array, so runtime traps should be
// rare. A HANG is therefore a genuine bug: EVERY loop shape is bounded by a per-nesting-level counter
// the body cannot reach (`for (v = 0 to N<=4)`, a `do`-while counting to a literal, and `loop` with a
// counted `goto` exit), so nothing can defeat termination except a miscompile. Do not reclassify a
// timeout as benign - fix the cause.
function runGazl(gazl, defines) {                            // write, run, delete; the caller classifies the result
	const tmp = path.join(os.tmpdir(), `fuzz-${process.pid}-${Math.floor(rnd() * 1e9)}.gazl`);
	fs.writeFileSync(tmp, gazl, 'latin1');
	try {
		return cp.spawnSync(GAZLCMD, [tmp, 'main'].concat(defines || []), { encoding: 'latin1', timeout: 10000 });
	} finally {
		try { fs.unlinkSync(tmp); } catch (_) {}
	}
}

function runOnVm(res) {
	if (res.error) return 'spawn/timeout: ' + res.error.message;
	if (res.status === 0) return null;
	const err = (res.stderr || '') + (res.stdout || '');
	if (/Code size:|functions:\s*\d/.test(err)) return null;   // loaded OK -> runtime trap, program UB
	const line = err.split('\n').find((l) => /error|fault|assert|invalid|already defined|out of bounds|Status: [^0]/i.test(l));
	return 'load failure (exit ' + res.status + '): ' + (line || err.split('\n')[0] || '').trim();
}

// Dead-stripping must be OUTPUT-PRESERVING: it removes only definitions that nothing reachable from an
// export names, so the stripped module has to load and print exactly what the full one printed. A
// difference means the strip took something live - which the fixture in tests/impala/sources/deadstrip
// catches only for the shapes someone thought to write down, and this catches for any shape.
function checkStripped(res, ref) {
	const load = runOnVm(res);
	if (load) return 'stripped: ' + load;
	if (res.stdout === ref.stdout) return null;
	// Report the FIRST DIFFERING LINE, not the first 120 characters of each. Truncating the head made two
	// outputs that diverge further down print IDENTICALLY, so the report read like a false positive and a
	// real fault sat unlooked-at.
	const a = (ref.stdout || '').split(/\r?\n/), b = (res.stdout || '').split(/\r?\n/);
	let i = 0;
	while (i < a.length && i < b.length && a[i] === b[i]) ++i;
	return 'dead-strip changed output at line ' + (i + 1) + ' of ' + a.length + ': full '
			+ JSON.stringify(a[i] === undefined ? '<end>' : a[i]) + ' vs stripped '
			+ JSON.stringify(b[i] === undefined ? '<end>' : b[i]);
}

// The INVERTED oracle for a deliberate over-fill (see section (4)): Impala cannot count the words of a
// symbolic extent, so the only thing between the surplus and the next field is assertFitsExtent's
// deferred `! FAIL` guard. The compile must SUCCEED and the assembly must FAIL. A silent pass means the
// guard stopped being emitted, which every other check here would read as a healthy program.
function checkMustFail(res) {
	return /FAIL directive/.test((res.stderr || '') + (res.stdout || '')) ? null
			: 'over-filled symbolic initializer assembled without tripping the ! FAIL guard';
}

// The value oracle: `main` prints the checked initializers before anything else, so the first
// `expect.length` integers on stdout must be exactly the words the generator asked for. This is what
// catches a WRONG DATA row - a truncated one, a value landing in the next field, a dropped surplus -
// none of which faults the VM or fails to load, and so none of which runOnVm can see.
function checkExpected(res, expect) {
	const got = (res.stdout || '').split('\n').map((l) => l.trim()).filter((l) => l !== '');
	if (got.length < expect.length) {
		return `printed ${got.length} values, expected at least ${expect.length}`;
	}
	for (let i = 0; i < expect.length; ++i) {
		if (parseInt(got[i], 10) !== expect[i]) {
			return `initializer word ${i}: expected ${expect[i]}, got ${got[i]}`
					+ ` (all: [${got.slice(0, expect.length).join(',')}] vs [${expect.join(',')}])`;
		}
	}
	return null;
}

// PARKED WITH THE FEATURE: the inline differential (compile the same program with `inline` stripped
// and require identical output) lived here. It was the only check in this fuzzer with a real
// reference oracle, and the three inline miscompiles found by hand - call-window adjacency, switch
// case labels, double processBranches - would each have failed it. Restore it from history alongside
// `inline` itself; see docs/ParkedFeatures.md.
//
// `checkExpected` below is now a second reference oracle, over static initializers rather than over
// code. It was added because that path had NO fuzz coverage at all - the generator emitted only bare
// declarations - while several silent wrong-data bugs were being found there by hand. It does not
// replace the inline differential; the two cover different halves.
//
// What it reaches, all int-only so the comparison is exact: the fill loops, named-field mapping,
// zero-fill, nested structs and array fields (the recursion a value can cross into its neighbour
// through), the struct-array path, a SYMBOLIC extent (`sv[SN]` - Impala never learns SN, so every
// offset past it is assemble-time arithmetic and `tail` lands correctly only if that resolved), and an
// `extern struct` handed a PERMUTED layout at load, which a baked positional offset could not survive.
// The symbolic array is filled statically half the time and at run time the other half - the two check
// different halves (the DATA row landing inside a block whose size is not a number yet, versus the
// address arithmetic that reads it back). The extern struct stays run-time-only: E459 still allows
// nothing but zero there, since the host, not the assembler, owns that layout.
//
// Still not reached: float and pointer fields (the oracle prints ints), and `readonly`/`temporary`
// sections beyond the one `readonly` array.

function mulberry32(a) {
	return function () {
		a |= 0; a = (a + 0x6D2B79F5) | 0;
		let t = Math.imul(a ^ (a >>> 15), 1 | a);
		t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

let rnd = Math.random;
const ri = (n) => Math.floor(rnd() * n);
const pick = (a) => a[ri(a.length)];
const chance = (p) => rnd() < p;

// ---- program generator -------------------------------------------------------
function genProgram() {
	const structs = [];   // { name, fields:[{name,type}] }  type in {'i','f'} or a struct name
	const functypes = []; // { name, params:[t], rets:[t] }
	const funcs = [];     // { name, params:[{name,t}], rets:[t] }  t in {'i','f',structName}
	let uid = 0;
	const id = (p) => p + (uid++);

	const scalarTypes = ['i', 'f'];
	const structNames = () => structs.map((s) => s.name);
	// a value type usable for params/returns/locals
	function someType(allowStruct, allowPtr) {
		if (allowStruct && structs.length && chance(0.4)) return pick(structNames());
		if (allowPtr && chance(0.25)) return 'p:' + pick(scalarTypes);
		return pick(scalarTypes);
	}
	const isStruct = (t) => structs.some((s) => s.name === t);
	const isFuncType = (t) => functypes.some((ft) => ft.name === t);
	const isPtr = (t) => typeof t === 'string' && t.slice(0, 2) === 'p:';   // 'p:i' / 'p:f'
	const ptrElem = (t) => t.slice(2);
	// pointer to a named funcptr type, e.g. 'pF:FT0' -> `FT0 pointer`. Kept out of the scalar-pointer
	// scheme (isPtr is false for it) so it is only ever dereferenced-and-called, never read as a scalar.
	const isFptrPtr = (t) => typeof t === 'string' && t.slice(0, 3) === 'pF:';
	const fptrElem = (t) => t.slice(3);
	const decl = (t) =>
		isFptrPtr(t) ? fptrElem(t) + ' pointer '
		: isPtr(t) ? (ptrElem(t) === 'i' ? 'int pointer ' : 'float pointer ')
		: (isStruct(t) || isFuncType(t)) ? t + ' '
		: t === 'i' ? 'int ' : 'float ';

	// structs (0-3), each with 1-3 scalar/nested-struct fields (nested only from earlier structs)
	const nStructs = ri(4);
	for (let i = 0; i < nStructs; ++i) {
		const name = 'S' + i;
		const fields = [];
		const nf = 1 + ri(3);
		for (let j = 0; j < nf; ++j) {
			let t = pick(scalarTypes);
			if (structs.length && chance(0.25)) t = pick(structNames());   // nested by-value struct
			fields.push({ name: id('f'), type: t });
		}
		structs.push({ name, fields });
	}

	// program globals (0-3): scalar or array, int/float. Available to every function.
	const globals = [];
	const nGlobals = ri(4);
	for (let i = 0; i < nGlobals; ++i) {
		const isArr = chance(0.4);
		globals.push({ name: 'g' + i, elem: pick(scalarTypes), isArray: isArr, size: isArr ? 1 + ri(4) : 1, isGlobal: true });
	}
	const gScalars = globals.filter((g) => !g.isArray);
	const gArrays = globals.filter((g) => g.isArray);

	// ---- checked initializers: the only REFERENCE ORACLE in this fuzzer -------------------------
	// Everything else here is a crash/trap oracle, which cannot see wrong DATA - and wrong DATA is
	// exactly how the initializer path has failed (a NaN-truncated row, a field's value landing in its
	// neighbour, surplus values dropped). These globals are deliberately NOT added to `globals`, so no
	// generated statement can reference or mutate them; `main` prints them BEFORE its random body, and
	// the driver compares that prefix against the words computed here. int-only, so the comparison is
	// exact. Partial lists are on purpose: the omitted tail must zero-fill.
	const chkVal = () => 1 + ri(900);                     // never 0, so 0 unambiguously means "omitted"
	const expect = [];                                    // words, in the order main prints them
	const chkPrint = [];
	const chkDecl = [];
	// The expected word and the print that reads it are pushed TOGETHER, and only here: the oracle is
	// only as good as those two lists staying in lockstep, and appending them from separate loops is
	// how they would silently drift and start comparing the wrong words.
	const chk = (expr, word) => {
		expect.push(word);
		chkPrint.push('\tprintInt(' + expr + '); printLF();');
	};
	const trimZeros = (w) => { let n = w.length; while (n > 0 && !w[n - 1]) --n; return w.slice(0, n); };

	// The checked struct is a list of MEMBERS - a scalar, a fixed-extent int array, or a nested struct.
	// Describing them once and deriving declaration / initializer / word list / read-back path from the
	// same description is what lets NESTING and ARRAY FIELDS be covered without a hand-written block per
	// shape: `buildStructInit` recurses through both, and a value landing in the neighbouring field -
	// the bug this oracle exists for - came out of exactly that recursion.
	const innerName = 'CKI';
	const innerFields = 1 + ri(3);
	chkDecl.push('struct ' + innerName + ' { '
			+ Array.from({ length: innerFields }, (_, i) => 'int g' + i).join('; ') + ' }');
	const members = Array.from({ length: 2 + ri(3) }, (_, i) => {
		const r = rnd();
		if (r < 0.25) return { k: 's', name: 'm' + i };                    // nested struct
		if (r < 0.55) return { k: 'a', name: 'm' + i, len: 1 + ri(3) };    // int array field
		return { k: 'i', name: 'm' + i };                                  // plain scalar
	});
	const chkStruct = 'CK';
	chkDecl.push('struct ' + chkStruct + ' { ' + members.map((m) => (m.k === 'i' ? 'int ' + m.name
			: m.k === 'a' ? 'int array ' + m.name + '[' + m.len + ']'
			: innerName + ' ' + m.name)).join('; ') + ' }');

	const memWords = (m) => (m.k === 'i' ? (chance(0.6) ? chkVal() : 0)
			: Array.from({ length: m.k === 'a' ? m.len : innerFields },
					() => (chance(0.6) ? chkVal() : 0)));
	const structWords = () => members.map(memWords);
	// undefined -> omit the member entirely, so the whole field zero-fills
	const memInit = (m, v) => {
		if (m.k === 'i') return v ? m.name + ': ' + v : undefined;
		const t = trimZeros(v);
		if (!t.length) return undefined;
		return m.name + ': { ' + (m.k === 'a' ? t.join(', ')
				: t.map((x, i) => (x ? 'g' + i + ': ' + x : '')).filter(Boolean).join(', ')) + ' }';
	};
	const structInit = (vals) => '{ ' + members.map((m, i) => memInit(m, vals[i]))
			.filter(Boolean).join(', ') + ' }';
	const anyWord = (vals) => vals.some((v) => (typeof v === 'number' ? v : v.some(Boolean)));
	const readStruct = (path, vals) => members.forEach((m, i) => {
		const v = vals[i];
		if (m.k === 'i') chk(path + '.' + m.name, v);
		else if (m.k === 'a') v.forEach((x, j) => chk(path + '.' + m.name + '[' + j + ']', x));
		else v.forEach((x, j) => chk(path + '.' + m.name + '.g' + j, x));
	});

	// (1) flat scalar array, partially initialized -> InitList + zero-fill
	const aLen = 1 + ri(4);
	const aVals = Array.from({ length: aLen }, () => (chance(0.7) ? chkVal() : 0));
	const aGiven = trimZeros(aVals);
	chkDecl.push('readonly int array cA[' + aLen + ']'
			+ (aGiven.length ? ' = { ' + aGiven.join(', ') + ' }' : ''));
	aVals.forEach((v, i) => chk('global cA[' + i + ']', v));

	// (2) struct value, a random subset named -> buildStructInit + fieldEntries + the nesting recursion
	const sVals = structWords();
	chkDecl.push('global ' + chkStruct + ' cS' + (anyWord(sVals) ? ' = ' + structInit(sVals) : ''));
	readStruct('global cS', sVals);

	// (3) array OF structs -> the struct-array path, `[[ ]]` to index it
	const kLen = 1 + ri(3);
	const kVals = Array.from({ length: kLen }, structWords);
	chkDecl.push('global ' + chkStruct + ' array cK[' + kLen + ']'
			+ (kVals.some(anyWord) ? ' = { ' + kVals.map(structInit).join(', ') + ' }' : ''));
	kVals.forEach((vals, k) => readStruct('global cK[[' + k + ']]', vals));

	// (4) SYMBOLIC extent. `SN` is a const, so Impala never learns the number - every offset past `sv`
	// is assemble-time arithmetic (`! ADDi <A> #.o.CS.sv #k`, `.o.CS.tail` = `.o.CS.sv + SN`). The
	// generator DOES know it, which is the whole point: it can predict the layout the assembler must
	// produce. `head` is initialized statically (a field BEFORE the block is still placeable, E454).
	// `sv` is filled STATICALLY or at run time; `tail` is always run-time, because a field AFTER a
	// symbolic block cannot be placed at all (E454) - and it is the one that catches a wrong `.o.`, since
	// a bad offset makes it collide with the last element of `sv` and the readback disagrees.
	const symLen = 1 + ri(4);
	chkDecl.push('const int SN = ' + symLen);
	chkDecl.push('struct CS { int head; int array sv[SN]; int tail }');
	const symHead = chance(0.6) ? chkVal() : 0;
	const symVals = Array.from({ length: symLen }, () => (chance(0.7) ? chkVal() : 0));
	const symGiven = trimZeros(symVals);
	const symStatic = symGiven.length > 0 && chance(0.5);
	// A static fill BEYOND the extent (rare). Impala cannot count these words either, so the surplus is
	// stopped only by assertFitsExtent's deferred `! FAIL` guard at GAZL assembly time. checkMustFail
	// flips the oracle for such a program: it must not assemble, and it never runs, so nothing else applies.
	const symOver = symStatic && chance(0.12);
	if (symOver) { const n = symLen + 1 + ri(2); while (symGiven.length < n) { symGiven.push(chkVal()); } }
	const symInit = [symHead ? 'head: ' + symHead : '',
			symStatic ? 'sv: { ' + symGiven.join(', ') + ' }' : ''].filter(Boolean).join(', ');
	chkDecl.push('global CS cY' + (symInit ? ' = { ' + symInit + ' }' : ''));
	// A DEAD twin of cY, referenced by nothing. Without one unreachable definition the strip differential
	// compares a module to itself: `main` reads every other checked global, so there is nothing to remove.
	// This is also the shape that made deadStrip throw - now as the block being REMOVED, which has to take
	// its `! LEQi`/`! FAIL` guard and its DATA rows with it or the leftovers fail the module outright.
	chkDecl.push('global CS cD' + (symInit ? ' = { ' + symInit + ' }' : ''));
	const symTail = chkVal();
	if (!symStatic) symGiven.forEach((v, i) => chkPrint.push('\tglobal cY.sv[' + i + '] = ' + v + ';'));
	chkPrint.push('\tglobal cY.tail = ' + symTail + ';');
	chk('global cY.head', symHead);
	symVals.forEach((v, i) => chk('global cY.sv[' + i + ']', v));
	chk('global cY.tail', symTail);

	// (5) EXTERN struct: the host owns the layout, and it is handed a PERMUTED one at load. Every access
	// goes through `.o.E.*`, so a correct compile reads back whatever it wrote no matter where the host
	// put the fields; a baked positional offset would survive declaration order and fail here. Nothing is
	// statically initialized (E459 - only zero is placeable without knowing the layout).
	const extN = 2 + ri(3);
	const extOff = Array.from({ length: extN }, (_, i) => i);
	for (let i = extOff.length - 1; i > 0; --i) {         // shuffle: the host's order is NOT ours
		const j = ri(i + 1);
		const t = extOff[i]; extOff[i] = extOff[j]; extOff[j] = t;
	}
	const defines = [];
	extOff.forEach((off, i) => { defines.push('.o.E.e' + i, String(off)); });
	defines.push('.z.E', String(extN));
	chkDecl.push('extern struct E { ' + Array.from({ length: extN }, (_, i) => 'int e' + i).join('; ') + ' }');
	chkDecl.push('global E cE');
	const extVals = Array.from({ length: extN }, chkVal);
	extVals.forEach((v, i) => chkPrint.push('\tglobal cE.e' + i + ' = ' + v + ';'));
	extVals.forEach((v, i) => chk('global cE.e' + i, v));

	// how a value is referenced: globals need the `global` keyword prefix
	const ref = (e) => (e.isGlobal ? 'global ' + e.name : e.name);
	// all `base.f...` access paths from a struct that reach a scalar field of `wantElem`, recursing
	// through nested struct fields (depth-bounded) - exercises deep place-offset accumulation
	function structScalarPaths(base, structName, wantElem, depth) {
		const s = structs.find((x) => x.name === structName);
		if (!s) return [];
		let paths = [];
		for (const fld of s.fields) {
			if (fld.type === wantElem) paths.push(base + '.' + fld.name);
			else if (isStruct(fld.type) && depth > 0) paths = paths.concat(structScalarPaths(base + '.' + fld.name, fld.type, wantElem, depth - 1));
		}
		return paths;
	}
	// An index expression for `arr[k]` / `p[k]`, ALWAYS in bounds. `size` is the target element
	// count (a named array size, or a pointer pointee size). Out-of-bounds indices used to be
	// generated on purpose, but GAZL places globals directly below the data stack and does NOT
	// bounds-check runtime indices, so a stray write lands in a caller frame: one such write
	// clobbered main's for-loop counter every iteration and hung the program (fuzz seed 655).
	// That left the --vm oracle unable to tell a miscompile from the program own undefined
	// behaviour, so indices now stay inside. A dynamic index is masked by the largest (2^k - 1)
	// that keeps it below `size`.
	const idxMask = (size) => { let m = 1; while (m * 2 <= size) m *= 2; return m - 1; };
	function genIdx(scope, size) {
		const n = (size !== undefined && size > 0) ? size : 1;
		const mask = idxMask(n);
		if (mask === 0 || chance(0.6)) return String(ri(n));
		const ints = scope.locals.filter((l) => l.t === 'i').concat(scope.gscalars.filter((g) => g.elem === 'i'));
		return ints.length ? '(' + ref(ints[ri(ints.length)]) + ' & ' + mask + ')' : String(ri(n));
	}

	// Two guaranteed helpers with FIXED bodies, declared first so every later function and main can
	// call them: `fnW` writes through its pointer parameter, `fnA` is transparent and reads both of
	// its. Together they are exactly what the aliasing shape in genCall needs. Guaranteed for the same
	// reason a struct local and a local array are: assembled from the random population the shape needs
	// about seven flips to line up and simply never appeared. (Bare `p`/`a`/`b`/`r` cannot collide with
	// generated names, which all carry a digit suffix.) Both were `inline` until that was parked.
	const HELPERS = 'function fnW(int pointer p) returns int r { p[0] = 7; r = 0; }\n'
			+ 'function fnA(int a, int b) returns int r { r = a + b; }\n';
	funcs.push({ name: 'fnW', params: [{ name: 'p', t: 'p:i' }], rets: ['i'], retNames: ['r'],
			fixed: true, aliasWriter: true });
	funcs.push({ name: 'fnA', params: [{ name: 'a', t: 'i' }, { name: 'b', t: 'i' }], rets: ['i'],
			retNames: ['r'], fixed: true });

	// helper functions (2-5)
	const nFuncs = 2 + ri(4);
	for (let i = 0; i < nFuncs; ++i) {
		const params = [];
		// by-value struct params, struct returns and multi-value returns are parked for Impala 3.0
		// (see docs/ParkedFeatures.md), so params stay scalar/pointer and there is at most one
		// scalar return.
		for (let k = ri(4); k > 0; --k) params.push({ name: id('p'), t: someType(false, true) });
		const rets = rnd() < 0.35 ? [] : [pick(scalarTypes)];
		funcs.push({ name: 'fn' + i, params, rets, retNames: rets.map(() => id('r')) });
	}

	// functypes (0-2), each derived from a real function's signature so it has a valid target
	const nFts = ri(3);
	for (let i = 0; i < nFts; ++i) {                   // `inline` is parked; every function has an address
		const src = pick(funcs);
		functypes.push({ name: 'FT' + i, params: src.params.map((p) => p.t), rets: src.rets.slice(), target: src.name });
	}
	const funcMatches = (fn, ft) => fn.params.length === ft.params.length
		&& fn.params.every((p, k) => p.t === ft.params[k])
		&& fn.rets.length === ft.rets.length && fn.rets.every((t, k) => t === ft.rets[k]);

	// expression generator toward a wanted type, depth-limited. `need` (pointer wants only) is how many
	// elements the result must SPAN; see the isPtr branch.
	function genExpr(want, depth, scope, need) {
		// scope: { locals: [{name,t}] } - guaranteed to hold >=1 local of every struct type
		if (isStruct(want)) {
			const cands = scope.locals.filter((l) => l.t === want);
			// only recurse into a struct-returning call while we still have depth budget
			if (depth > 0 && chance(0.5)) {
				const callable = scope.callable.filter((f) => f.rets.length === 1 && f.rets[0] === want);
				if (callable.length) return genCall(pick(callable), depth, scope);
			}
			return pick(cands).name;   // base case: always a real struct local
		}
		if (isPtr(want)) {
			// A pointer local is REASSIGNABLE, so its declared extent must hold for every value it can
			// ever receive - `need` is a requirement passed down, not a description of what comes back.
			// (Without it `ptr = &arr[3]` could land in a local whose own derefs assume 4 elements.)
			// Everywhere else - a call argument, and so a parameter - only element 0 is dereferenced.
			// No string literal here, though one is typed `int pointer`: it is the one pointer the
			// generator can produce that is NOT writable, and now that a callee may write through its
			// pointer parameter, passing one is undefined behaviour - it traps, and a trapped run tells
			// the oracles nothing, so it silently costs coverage instead of finding anything.
			// Constant-string codegen is byte-diff gated by 36 fixtures already.
			const e = ptrElem(want);
			need = need || 1;
			// The address of a scalar local: the only pointer that can ALIAS something the program also
			// reads BY NAME, which is what makes a deferred argument read observable (Inlining.md 5).
			// Loop counters are excluded outright - a write through one could defeat termination, and a
			// hang is not distinguishable from the miscompile this is here to catch.
			const scalars = scope.locals.filter((l) => l.t === e && !l.ro && !l.loopVar);
			if (need === 1 && scalars.length && chance(0.3)) return '&' + pick(scalars).name;
			const ptrs = scope.locals.filter((l) => l.t === want && (l.pointeeSize || 1) >= need);
			if (depth > 0 && ptrs.length && chance(0.35)) {
			// pointer arithmetic - offset bounded so `need` elements remain below the pointee's end
			const pa = pick(ptrs);
			const pm = idxMask((pa.pointeeSize || 1) - need + 1);
			return pm === 0 ? ref(pa)
				: '(' + ref(pa) + ' + (' + genExpr('i', depth - 1, scope) + ' & ' + pm + '))';
			}
			if (ptrs.length && chance(0.5)) return ref(pick(ptrs));
			// non-empty: every scalar type is guaranteed a local array, and `need` only ever comes from
			// a pointer local's own extent, which was taken from one of these arrays in the first place
			const arr = pick(scope.localArrays.filter((a) => a.elem === e && a.size >= need));
			return '&' + arr.name + '[' + genIdx(scope, arr.size - need + 1) + ']';
		}
		// scalar want ('i' or 'f')
		const lit = () => (want === 'i' ? String(ri(100) - 50) : (ri(1000) / 10).toFixed(1));
		if (depth <= 0) {
			const cands = scope.locals.filter((l) => l.t === want);
			return cands.length && chance(0.5) ? pick(cands).name : lit();
		}
		const r = rnd();
		if (r < 0.2) return lit();
		if (r < 0.35) {
			// a scalar local or scalar global of this type
			const cands = scope.locals.filter((l) => l.t === want).concat(scope.gscalars.filter((g) => g.elem === want));
			return cands.length ? ref(pick(cands)) : lit();
		}
		if (r < 0.55) {
			const op = pick(['+', '-', '*']);
			return '(' + genExpr(want, depth - 1, scope) + ' ' + op + ' ' + genExpr(want, depth - 1, scope) + ')';
		}
		if (r < 0.68) {
			// array element (in-bounds constant index) or pointer dereference read (unbounded index)
			const srcs = scope.arrays.filter((a) => a.elem === want).map((a) => ({ r: ref(a), size: a.size }))
				.concat(scope.locals.filter((l) => l.t === 'p:' + want).map((l) => ({ r: ref(l) })));
			if (srcs.length) { const s = pick(srcs); return s.r + '[' + genIdx(scope, s.size) + ']'; }
		}
		if (r < 0.78) {
			// scalar struct field read - possibly a deep chain a.b.c through nested struct fields
			const opts = [];
			for (const l of scope.locals) if (isStruct(l.t)) opts.push(...structScalarPaths(l.name, l.t, want, 2));
			if (opts.length) return pick(opts);
		}
		if (r < 0.85) {
			// numeric int<->float conversion: `ftoi` (float->int) / `itof` (int->float) prefix ops
			return want === 'i'
				? 'ftoi (' + genExpr('f', depth - 1, scope) + ')'
				: 'itof (' + genExpr('i', depth - 1, scope) + ')';
		}
		if (r < 0.9 && want === 'i') {
			// sizeof yields an int
			const t = structs.length && chance(0.5) ? pick(structNames()) : pick(['int', 'float', 'pointer']);
			return 'sizeof(' + t + ')';
		}
		// a call returning this scalar (nested - struct args here exercise window sliding)
		const callable = scope.callable.filter((f) => f.rets.length === 1 && f.rets[0] === want);
		if (callable.length) return genCall(pick(callable), depth, scope);
		return lit();
	}

	// THE ALIASING SHAPE, built on purpose rather than left to chance: a call whose earlier argument is
	// a bare local and whose later argument writes that same local through a pointer. It is kept while
	// `inline` is parked because it is what the differential oracle was built around: an expansion may
	// substitute the bare local, which DEFERS its read past the write, so the two builds only agree if
	// the marshalling scan stops at the write (docs/Inlining.md 5). Assembled at random this needs
	// about seven flips to line up - measured, only 1.3% of arguments are even a bare local - and it
	// turned up in roughly one program in two hundred, which is why a real miscompile of exactly this
	// shape survived a 1000-program sweep.
	function genCall(f, depth, scope) {
		const args = f.params.map((p) => genExpr(p.t, depth - 1, scope));
		const ints = f.params.map((p, k) => (p.t === 'i' ? k : -1)).filter((k) => k >= 0);
		const xs = scope.locals.filter((l) => l.t === 'i' && !l.ro && !l.loopVar);
		const w = scope.callable.filter((fn) => fn.aliasWriter);
		if (ints.length >= 2 && xs.length && w.length && chance(0.5)) {
			const x = pick(xs).name;
			args[ints[0]] = x;
			args[ints[1]] = w[0].name + '(&' + x + ')';
		}
		return f.name + '(' + args.join(', ') + ')';
	}

	// render a function; `callable` = functions defined before it (single-pass: no forward refs)
	function renderFunc(f, isMain, callable) {
		const locals = [];
		// declare some locals
		const nl = isMain ? 3 + ri(4) : 1 + ri(3);
		for (let i = 0; i < nl; ++i) locals.push({ name: id('l'), t: someType(true) });
		// guarantee at least one local of every struct type (base case for genExpr(struct))
		for (const s of structs) {
			if (!locals.some((l) => l.t === s.name) && !f.params.some((p) => p.t === s.name)) {
				locals.push({ name: id('l'), t: s.name });
			}
		}
		// a funcptr local per functype, so funcptr assign/call statements have somewhere to land
		const cbLocals = [];
		for (const ft of functypes) {
			if (chance(0.5)) { const cb = { name: id('cb'), t: ft.name }; cbLocals.push(cb); locals.push(cb); }
		}
		// funcptr-pointer locals: `FT pointer pf`, pointed at a same-typed funcptr local and
		// dereferenced-and-called (`pf[0](...)`). Only when a callable function matches the type, so
		// the target can be pre-assigned a real function (a null funcptr call would trap under --vm).
		const fptrLocals = [];
		for (const cb of cbLocals) {
			const ft = functypes.find((x) => x.name === cb.t);
			const match = callable.filter((fn) => funcMatches(fn, ft));
			if (match.length && chance(0.6)) {
				const pf = { name: id('pf'), t: 'pF:' + cb.t, target: cb, func: pick(match).name };
				fptrLocals.push(pf);
				locals.push(pf);
			}
		}
		// non-struct array locals (0-2): int/float arrays with indexed access
		const arrLocals = [];
		for (let i = ri(3); i > 0; --i) arrLocals.push({ name: id('arr'), elem: pick(scalarTypes), size: 1 + ri(4), isArray: true });
		// pointer locals (0-2): int/float pointers, initialized to a matching array below
		const ptrLocals = [];
		for (let i = ri(3); i > 0; --i) ptrLocals.push({ name: id('ptr'), t: 'p:' + pick(scalarTypes) });
		// guarantee a LOCAL int AND float array (a valid target for &arr[..], copy, and pointer inits)
		for (const e of scalarTypes) {
			if (!arrLocals.some((a) => a.elem === e)) arrLocals.push({ name: id('arr'), elem: e, size: 1 + ri(4), isArray: true });
		}
		// one dedicated int loop variable PER nesting level for bounded `for` loops - nested loops must
		// use distinct counters (a shared one lets the inner loop reset the outer's counter forever ->
		// an infinite loop). All are kept read-only in loop bodies so the body can't defeat termination.
		const loopVars = [];
		for (let d = 0; d < 2; ++d) { const lv = { name: id('fv'), t: 'i', loopVar: true }; loopVars.push(lv); locals.push(lv); }
		for (const p of ptrLocals) locals.push(p);
		// params are in scope too, but read-only (scalar INP params can't be assigned)
		const scope = {
			locals: locals.concat(f.params.map((p) => ({ name: p.name, t: p.t, ro: true }))),
			arrays: arrLocals.concat(gArrays),
			localArrays: arrLocals,
			gscalars: gScalars,
			loopVars: loopVars,
			fptrLocals: fptrLocals,
			callable: callable,
		};

		// `main` is exported: that is what the VM enters, and what gives `deadStrip` a reachability root
		// (with no export the strip is entitled to delete the entire program, and did).
		let header = (isMain ? 'export function ' : 'function ') + f.name
				+ '(' + f.params.map((p) => decl(p.t) + p.name).join(', ') + ')';
		if (f.rets.length) header += ' returns ' + f.rets.map((t, i) => decl(t) + f.retNames[i]).join(', ');

		const localDeclList = locals.map((l) => decl(l.t) + l.name)
			.concat(arrLocals.map((a) => (a.elem === 'i' ? 'int' : 'float') + ' array ' + a.name + '[' + a.size + ']'));
		const localDecl = localDeclList.length ? '\nlocals ' + localDeclList.join(', ') : '';

		const body = [];
		// The checked initializers go out FIRST, before the random body can print anything, so the
		// driver can compare a known-length prefix of stdout against the expected words.
		if (isMain) body.push(...chkPrint);
		// Locals are NOT zero-initialized (a callee sees the previous call's leftovers), so reading one
		// before writing it has no defined value - and an expansion relocates that garbage, which the
		// parked differential oracle would have reported as a miscompile. Seed every local first.
		for (const l of locals) {
			if (isStruct(l.t)) {
				// EVERY scalar leaf, nested ones included. This used to skip a struct-typed field instead
				// of descending into it, so `S2 { S0 f5; int f6 }` seeded only `f6` and a read of
				// `l.f5.f0` returned stack garbage. That is not merely untidy: it made the program's
				// output depend on the module LAYOUT, which is exactly what --dead-strip changes, so the
				// strip oracle reported a "changed output" fault against a compiler that was correct
				// (seed 20986). `structs.length` bounds the descent - a field's struct type is always an
				// EARLIER declaration, so nesting cannot exceed the number of structs.
				for (const e of scalarTypes) {
					for (const pth of structScalarPaths(l.name, l.t, e, structs.length)) {
						body.push('\t' + pth + ' = ' + (e === 'f' ? '0.0' : '0') + ';');
					}
				}
			} else if (l.t === 'i' || l.t === 'f') {
				body.push('\t' + l.name + ' = ' + (l.t === 'f' ? '0.0' : '0') + ';');
			}
		}
		for (const a of arrLocals) {
			for (let i = 0; i < a.size; ++i) body.push('\t' + a.name + '[' + i + '] = ' + (a.elem === 'f' ? '0.0' : '0') + ';');
		}
		// initialize pointer locals to a valid local array so dereferences stay in VM memory
		for (const p of ptrLocals) {
			const arr = arrLocals.find((a) => a.elem === ptrElem(p.t));
			if (arr) { p.pointeeSize = arr.size; body.push('\t' + p.name + ' = &' + arr.name + '[0];'); }
		}
		// assign each funcptr-pointer's target a real function, then point the pointer at it, so a
		// later `pf[0](...)` dispatches to a valid function (exercises &funcptr -> FT pointer)
		for (const pf of fptrLocals) {
			body.push('\t' + pf.target.name + ' = ' + pf.func + ';');
			body.push('\t' + pf.name + ' = &' + pf.target.name + ';');
		}
		const nStmt = 1 + ri(isMain ? 8 : 4);
		for (let i = 0; i < nStmt; ++i) body.push(genStmt(scope, f, 0));
		// main ends by PRINTING every int-valued thing it can reach. Without this the program has no
		// observable output for a differential oracle to compare.
		if (isMain) {
			for (const l of locals) if (l.t === 'i') body.push('\tprintInt(' + l.name + '); printLF();');
			for (const a of arrLocals) {
				if (a.elem !== 'i') continue;
				for (let i = 0; i < a.size; ++i) body.push('\tprintInt(' + a.name + '[' + i + ']); printLF();');
			}
			for (const g of globals) {
				if (g.elem !== 'i') continue;
				if (g.isArray) {
					for (let i = 0; i < g.size; ++i) body.push('\tprintInt(global ' + g.name + '[' + i + ']); printLF();');
				} else {
					body.push('\tprintInt(global ' + g.name + '); printLF();');
				}
			}
		}
		// give the function's own return/OUT vars something (harmless if omitted, but exercises OUT writes)
		for (let i = 0; i < f.rets.length; ++i) {
			const t = f.rets[i];
			if (isStruct(t)) {
				for (const e of scalarTypes) {                // nested leaves too - a returned struct is READ
					for (const pth of structScalarPaths(f.retNames[i], t, e, structs.length)) {
						body.push('\t' + pth + ' = ' + genExpr(e, 2, scope) + ';');
					}
				}
			} else {
				body.push('\t' + f.retNames[i] + ' = ' + genExpr(t, 2, scope) + ';');
			}
		}
		return header + localDecl + '\n{\n' + body.join('\n') + '\n}\n';
	}

	// a boolean condition: one comparison, sometimes two joined with && / ||
	function genCond(scope) {
		const one = () => {
			const t = pick(scalarTypes);
			return genExpr(t, 2, scope) + ' ' + pick(['<', '>', '<=', '>=', '==', '!=']) + ' ' + genExpr(t, 2, scope);
		};
		return chance(0.3) ? one() + ' ' + pick(['&&', '||']) + ' ' + one() : one();
	}

	// a braced block of 1-3 statements at the next control-nesting depth
	function genBlock(scope, f, ctrlDepth) {
		const n = 1 + ri(3);
		const stmts = [];
		for (let i = 0; i < n; ++i) stmts.push(genStmt(scope, f, ctrlDepth));
		return '{\n' + stmts.join('\n') + '\n\t}';
	}

	// a `switch (intExpr == from to to) { case ...: { block }  default: { block } }` (bounded, VM-safe).
	// Case bodies are braced blocks: a single Statement can't hold e.g. `copy(...);` (Copy leaves the `;`).
	function genSwitch(scope, f, ctrlDepth) {
		const parts = [];
		// Pick the range FIRST and draw case values from inside it: `SWCH` resolves offsets 0..size-1, so
		// anything else is E444 and the program never reaches codegen. Drawing from a wider pool than the
		// range rejected a third of every run. Values stay distinct because a repeat is E443.
		const size = 1 + ri(8);
		const used = new Set();
		for (let c = 1 + ri(3); c > 0; --c) {
			const labels = [];
			for (let j = 1 + ri(2); j > 0; --j) {
				if (used.size >= size) break;   // exhausted the reachable label space (guard BEFORE the probe)
				let v = ri(size);
				while (used.has(v)) v = (v + 1) % size;
				used.add(v);
				labels.push(String(v));
			}
			if (labels.length) parts.push('\tcase ' + labels.join(', ') + ': ' + genBlock(scope, f, ctrlDepth));
		}
		if (chance(0.6)) parts.push('\tdefault: ' + genBlock(scope, f, ctrlDepth));
		return '\tswitch (' + genExpr('i', 2, scope) + ' == 0 to ' + size + ') {\n' + parts.join('\n') + '\n\t}';
	}

	// The body scope for any loop: every loop counter is read-only inside it, so the body cannot defeat
	// termination no matter what the generator puts there.
	const loopBody = (scope) => ({ ...scope,
			locals: scope.locals.map((l) => (l.loopVar ? { ...l, ro: true } : l)) });

	// `do`-while. The counter is incremented LAST, not first, because the bug this family exists to cover
	// needs two `do`s at the SAME address - only `{ }`, a `loop` header, a bare label or a trailing
	// `assert` may sit between them, and an increment at the top would separate them. Both dangling-label
	// shapes are built ON PURPOSE, for the reason the aliasing shape is: adjacency, and a `goto` as the
	// first statement of a body, do not come up by chance in a few thousand programs.
	function genDoWhile(scope, f, ctrlDepth) {
		const lv = scope.loopVars[ctrlDepth];
		if (!lv) return undefined;
		const inits = ['\t' + lv.name + ' = 0;'];
		const parts = [];
		let after = '';
		const r = rnd();
		if (r < 0.25) {                                   // SHAPE 2: body STARTS with an unconditional goto
			const lbl = id('L');
			parts.push('\t\tgoto ' + lbl + ';');
			after = '\n\t' + lbl + ': ;';
		} else if (r < 0.55 && scope.loopVars[ctrlDepth + 1]) {   // SHAPE 1: two `do`s at one address
			const iv = scope.loopVars[ctrlDepth + 1];
			inits.push('\t' + iv.name + ' = 0;');         // HOISTED - an init between the two `do`s would
			                                              // break the very adjacency the shape needs. It
			                                              // only resets once, which still terminates: the
			                                              // inner runs its count on the first pass and
			                                              // once per pass after.
			parts.push('\t\tdo { ' + iv.name + ' = ' + iv.name + ' + 1; } while ('
					+ iv.name + ' < ' + (1 + ri(3)) + ');');
		}
		parts.push(genStmt(loopBody(scope), f, ctrlDepth + 1));
		parts.push('\t\t' + lv.name + ' = ' + lv.name + ' + 1;');
		return inits.join('\n') + '\n\tdo {\n' + parts.join('\n') + '\n\t} while ('
				+ lv.name + ' < ' + (1 + ri(4)) + ');' + after;
	}

	// `loop` is an UNCONDITIONAL back-edge, so its counted `goto` is the only way out and the counter must
	// be stepped before the test. Nothing in the body can reach the counter (loopBody), so the exit is
	// reached in a fixed number of passes whatever the body does.
	function genLoop(scope, f, ctrlDepth) {
		const lv = scope.loopVars[ctrlDepth];
		if (!lv) return undefined;
		const lbl = id('L');
		return '\t' + lv.name + ' = 0;\n\tloop {\n'
				+ '\t\t' + lv.name + ' = ' + lv.name + ' + 1;\n'
				+ '\t\tif (' + lv.name + ' > ' + (1 + ri(3)) + ') { goto ' + lbl + '; }\n'
				+ genStmt(loopBody(scope), f, ctrlDepth + 1) + '\n\t}\n\t' + lbl + ': ;';
	}

	function genStmt(scope, f, ctrlDepth) {
		const roll = rnd();
		// control flow (if / else / switch / for / do-while / loop / goto), depth-limited to keep
		// programs small. Every loop here is bounded: a HANG cannot be told apart from the miscompile
		// these oracles exist to catch, so termination is structural, never left to the body.
		if (roll < 0.22 && ctrlDepth < 2) {
			const cd = ctrlDepth + 1;
			const k = rnd();
			if (k < 0.34) return '\tif (' + genCond(scope) + ') ' + genBlock(scope, f, cd);
			if (k < 0.56) return '\tif (' + genCond(scope) + ') ' + genBlock(scope, f, cd) + ' else ' + genBlock(scope, f, cd);
			if (k < 0.66) return genSwitch(scope, f, cd);
			if (k < 0.86) {
				const shape = (k < 0.74) ? genDoWhile(scope, f, ctrlDepth) : genLoop(scope, f, ctrlDepth);
				if (shape) return shape;
			}
			if (k < 0.93) {
				// a FORWARD `goto` over the next statement - the only unconditional jump that cannot loop
				const lbl = id('L');
				return '\tif (' + genCond(scope) + ') { goto ' + lbl + '; }\n'
						+ genStmt(scope, f, cd) + '\n\t' + lbl + ': ;';
			}
			// bounded `for (fv = 0 to N)` - a loop var unique to this nesting level (so a nested loop
			// can't reset an enclosing counter), read-only inside the body so it always terminates
			const lv = scope.loopVars[ctrlDepth];
			if (lv) return '\tfor (' + lv.name + ' = 0 to ' + (1 + ri(4)) + ') ' + genBlock(loopBody(scope), f, cd);
		}
		// pointer-dereference write through any pointer in scope. `ro` marks a local that cannot be
		// REASSIGNED, which says nothing about writing through it - and a pointer PARAMETER is exactly
		// how a callee reaches out and writes a caller's local, so excluding params here left the whole
		// aliasing family ungenerated.
		if (roll < 0.24) {
			const ptrs = scope.locals.filter((l) => isPtr(l.t));
			if (ptrs.length) {
				const p = pick(ptrs);
				return '\t' + p.name + '[' + genIdx(scope, p.pointeeSize) + '] = ' + genExpr(ptrElem(p.t), 3, scope) + ';';
			}
		}
		// copy(N words from &a[0] to &b[0]) between two local arrays (N within bounds - VM-safe)
		if (roll < 0.28 && scope.localArrays.length >= 2) {
			const a = pick(scope.localArrays), b = pick(scope.localArrays);
			const n = 1 + ri(Math.min(a.size, b.size));
			return '\tcopy(' + n + ' from &' + a.name + '[0] to &' + b.name + '[0]);';
		}
		// array element write (local or global array; in-bounds constant or dynamic index)
		if (roll < 0.34 && scope.arrays.length) {
			const a = pick(scope.arrays);
			return '\t' + ref(a) + '[' + genIdx(scope, a.size) + '] = ' + genExpr(a.elem, 3, scope) + ';';
		}
		// scalar global write
		if (roll < 0.36 && scope.gscalars.length) {
			const g = pick(scope.gscalars);
			return '\tglobal ' + g.name + ' = ' + genExpr(g.elem, 3, scope) + ';';
		}
		// (destructuring of a multi-return call is parked for Impala 3.0 - see docs/ParkedFeatures.md)
		// funcptr: assign a matching function to a funcptr local, then call through it
		if (roll < 0.35) {
			const cbLocals = scope.locals.filter((l) => isFuncType(l.t));
			if (cbLocals.length) {
				const cb = pick(cbLocals);
				const ft = functypes.find((x) => x.name === cb.t);
				const match = scope.callable.filter((fn) => funcMatches(fn, ft));
				if (match.length) {
					const assign = '\t' + cb.name + ' = ' + pick(match).name + ';';
					// then an indirect call through the funcptr
					const args = ft.params.map((t) => genExpr(t, 2, scope));
					let call = '\t' + cb.name + '(' + args.join(', ') + ');';
					if (ft.rets.length === 1) {
						const dst = scope.locals.filter((l) => l.t === ft.rets[0] && !l.ro)[0];
						if (dst) call = '\t' + dst.name + ' = ' + cb.name + '(' + args.join(', ') + ');';
					} else if (ft.rets.length > 1) {
						const targets = ft.rets.map((t) => (scope.locals.filter((l) => l.t === t && !l.ro)[0] || { name: '_' }).name);
						call = '\t' + targets.join(', ') + ' = ' + cb.name + '(' + args.join(', ') + ');';
					}
					return assign + '\n' + call;
				}
			}
		}
		// deref-and-call through a funcptr-pointer: `pf[0](args)` (its target is a real function)
		if (roll < 0.42 && scope.fptrLocals.length) {
			const pf = pick(scope.fptrLocals);
			const ft = functypes.find((x) => x.name === fptrElem(pf.t));
			const args = ft.params.map((t) => genExpr(t, 2, scope));
			const callee = pf.name + '[0](' + args.join(', ') + ')';
			if (ft.rets.length === 1) {
				const dst = scope.locals.filter((l) => l.t === ft.rets[0] && !l.ro)[0];
				if (dst) return '\t' + dst.name + ' = ' + callee + ';';
			} else if (ft.rets.length > 1) {
				const targets = ft.rets.map((t) => (scope.locals.filter((l) => l.t === t && !l.ro)[0] || { name: '_' }).name);
				return '\t' + targets.join(', ') + ' = ' + callee + ';';
			}
			return '\t' + callee + ';';
		}
		// struct field assignment - a scalar leaf, possibly deep (a.b.c) through nested structs
		if (roll < 0.5) {
			const sLocals = scope.locals.filter((l) => isStruct(l.t));
			if (sLocals.length) {
				const l = pick(sLocals);
				const cands = [];
				for (const e of scalarTypes) for (const pth of structScalarPaths(l.name, l.t, e, 2)) cands.push({ pth, e });
				if (cands.length) {
					const c = pick(cands);
					return '\t' + c.pth + ' = ' + genExpr(c.e, 3, scope) + ';';
				}
			}
		}
		// whole-struct or scalar assignment to a local (funcptr + funcptr-pointer locals and read-only
		// params excluded - they are assigned only at body start / through their dedicated statements)
		if (roll < 0.8) {
			const assignable = scope.locals.filter((x) => !isFuncType(x.t) && !isFptrPtr(x.t) && !x.ro);
			if (assignable.length) {
				const l = pick(assignable);
				return '\t' + l.name + ' = ' + genExpr(l.t, 3, scope, l.pointeeSize) + ';';
			}
		}
		// a call for side effect (only functions defined earlier; a bare multi-return call is illegal)
		const bareCallable = scope.callable.filter((fn) => fn.rets.length <= 1);
		if (bareCallable.length) return '\t' + genCall(pick(bareCallable), 3, scope) + ';';
		// nothing callable yet: a harmless scalar assignment to a writable local
		const sl = scope.locals.filter((x) => !x.ro && (x.t === 'i' || x.t === 'f'))[0];
		return sl ? '\t' + sl.name + ' = ' + genExpr(sl.t, 2, scope) + ';' : '\t;';
	}

	// assemble
	let out = 'const int DEBUG = 1\nextern native printInt\nextern native printLF\n';
	for (const s of structs) {
		out += 'struct ' + s.name + ' { ' + s.fields.map((f) => decl(f.type) + f.name).join('; ') + ' }\n';
	}
	for (const ft of functypes) {
		out += 'functype ' + ft.name + '(' + ft.params.map((t) => decl(t) + id('a')).join(', ') + ')';
		if (ft.rets.length) out += ' returns ' + ft.rets.map((t) => decl(t)).join(', ');
		out += '\n';
	}
	for (const g of globals) {
		const ty = g.elem === 'i' ? 'int' : 'float';
		out += g.isArray ? 'global ' + ty + ' array ' + g.name + '[' + g.size + ']\n' : 'global ' + ty + ' ' + g.name + '\n';
	}
	out += chkDecl.join('\n') + '\n';
	out += HELPERS;
	for (let i = 0; i < funcs.length; ++i) {
		if (!funcs[i].fixed) out += renderFunc(funcs[i], false, funcs.slice(0, i));
	}
	out += renderFunc({ name: 'main', params: [], rets: [], retNames: [] }, true, funcs.slice());
	return { src: out, expect: expect, defines: defines, mustFail: symOver };
}

// ---- crash oracle ------------------------------------------------------------
const CLEAN = /error\[E\d+\]/;                 // a coded diagnostic - acceptable
const BENIGN = /compiler stopped at \d+/;      // partial parse - acceptable
function classify(err) {
	const msg = (err && err.message) ? err.message : String(err);
	if (CLEAN.test(msg) || BENIGN.test(msg)) return null;
	return msg.split('\n')[0];
}

function main() {
	// `--print <seed>`: emit one generated program's source and exit (for inspecting coverage)
	const printIdx = process.argv.indexOf('--print');
	if (printIdx >= 0) {
		rnd = mulberry32(parseInt(process.argv[printIdx + 1] || '1', 10));
		const shown = genProgram();
		process.stdout.write(shown.src + '\n; expected checked words: ' + shown.expect.join(' ')
				+ '\n; host layout supplied at load: ' + shown.defines.join(' ')
				+ (shown.mustFail ? '\n; THIS ONE MUST NOT ASSEMBLE: cY.sv is over-filled on purpose' : '')
				+ '\n');
		return;
	}
	const args = process.argv.slice(2).filter((a) => a !== '--vm');
	const useVm = process.argv.includes('--vm');
	const iterations = parseInt(args[0] || '2000', 10);
	const startSeed = parseInt(args[1] || '1', 10);
	let bugs = 0;
	let compiled = 0;
	let rejected = 0;
	let vmRun = 0;
	const codeTally = {};
	for (let i = 0; i < iterations; ++i) {
		const seed = startSeed + i;
		rnd = mulberry32(seed);
		let p;
		try {
			p = genProgram();
		} catch (genErr) {
			console.error(`[gen-error seed=${seed}] ${genErr.message}`);
			continue;
		}
		try {
			const gazl = compileWithJsImpala(p.src + '\n', COMPILE_OPTS);
			compiled++;
			// A throw here is a compiler bug and lands in the catch below with every other one: `classify`
			// keeps only what is not a coded diagnostic, and `deadStrip` has no diagnostics to emit.
			// `cD` is the one definition nothing references, so it is also the one the strip must drop -
			// checked here rather than under --vm so the default gate covers it.
			const stripped = deadStrip(gazl);
			if (/^\s*cD:/m.test(stripped)) throw new Error('dead-strip kept the unreferenced global cD');
			if (useVm) {
				const res = runGazl(gazl, p.defines);
				const vmFault = p.mustFail ? checkMustFail(res)
						: (runOnVm(res) || checkExpected(res, p.expect)
								|| checkStripped(runGazl(stripped, p.defines), res));
				vmRun++;
				if (vmFault) {
					bugs++;
					console.error(`\n=== VM FAULT seed=${seed}: ${vmFault} ===`);
					console.error(p.src);
					console.error(`=== end seed=${seed} ===\n`);
					if (bugs >= 5) { console.error('stopping after 5 faults'); break; }
				}
			}
		} catch (err) {
			const crash = classify(err);
			if (crash) {
				bugs++;
				console.error(`\n=== CRASH seed=${seed}: ${crash} ===`);
				console.error(p.src);
				console.error(`=== end seed=${seed} ===\n`);
				if (bugs >= 5) { console.error('stopping after 5 crashes'); break; }
			} else {
				rejected++;   // clean diagnostic
				const code = ((err && err.message) || '').match(/error\[(E\d+)\]/);
				const k = code ? code[1] : 'other';
				codeTally[k] = (codeTally[k] || 0) + 1;
			}
		}
	}
	const top = Object.entries(codeTally).sort((a, b) => b[1] - a[1]).slice(0, 8).map(([k, v]) => `${k}:${v}`).join(' ');
	console.error(`fuzz: ${iterations} programs, ${compiled} compiled${useVm ? ` (${vmRun} run on VM)` : ''}, ${rejected} cleanly rejected, ${bugs} ${useVm ? 'FAULTS' : 'CRASHES'} (seeds ${startSeed}..${startSeed + iterations - 1})`);
	if (top) console.error(`rejection codes: ${top}`);
	process.exit(bugs > 0 ? 1 : 0);
}

main();
