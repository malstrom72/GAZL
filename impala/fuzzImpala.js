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
//
// Deterministic: every program is produced from a seeded PRNG, so any failure reproduces
// from its printed seed.
//
// Usage: node impala/fuzzImpala.js [iterations] [startSeed] [--vm]
//   --vm also runs each compiled program on GAZLCmd and flags VM faults (miscompiles).
// The runner reloads the compiler module per call, so keep a single process to <=~20k
// iterations (chunk larger sweeps across processes: for s in 0 20000 40000; do ... done).
// First find (seed 10024): finishDestructure released its output window low-to-high (unlike every
// other multi-slot window), leaving a freed hole below a live temp; a later struct-arg call's
// window overlapped it. Fixed by releasing the window high-to-low; borrowForCall now asserts the
// pool-reaches-the-top invariant, so a future release-order regression fails loudly here.

const { compileWithJsImpala } = require('./impalaJsCompilerRunner.js');
const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');

const GAZLCMD = path.join(__dirname, '..', 'output', process.platform === 'win32' ? 'GAZLCmd.exe' : 'GAZLCmd');
// Both halves of the inline differential must compile under IDENTICAL settings or the oracle compares
// two different programs.
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
// rare. A HANG is therefore a genuine bug: all loops are bounded `for (v = 0 to N<=4)` with a
// per-nesting-level counter that is read-only inside the body, so nothing can defeat termination
// except a miscompile. Do not reclassify a timeout as benign - fix the cause.
function runGazl(gazl) {                            // write, run, delete; the caller classifies the result
	const tmp = path.join(os.tmpdir(), `fuzz-${process.pid}-${Math.floor(rnd() * 1e9)}.gazl`);
	fs.writeFileSync(tmp, gazl, 'latin1');
	try {
		return cp.spawnSync(GAZLCMD, [tmp, 'main'], { encoding: 'latin1', timeout: 10000 });
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

// What a program PRINTED, for the inline differential below. null when it did not load cleanly or
// trapped - in that case there is nothing to compare.
function outputOf(res) {
	return (res.error || res.status !== 0 ? null : (res.stdout || ''));
}

// THE INLINE ORACLE. `inline` is a lowering, not a semantic change, so the same program with the
// keyword stripped must print exactly the same thing. This is the only check here with a real
// reference - the plain --vm oracle can just see that a module loads. It is what the three inline
// miscompiles found by hand (call-window adjacency, switch case labels, double processBranches)
// would each have failed, and all three were silent.
function inlineDifferential(src, res) {
	if (src.indexOf('inline function') < 0) return null;
	const plain = src.split('inline function').join('function');
	let plainGazl;
	try {
		plainGazl = compileWithJsImpala(plain + '\n', COMPILE_OPTS);
	} catch (e) {
		return 'inline differential: the same program without `inline` failed to compile: '
				+ ((e && e.message) || e);
	}
	const a = outputOf(res);                        // the inlined build already ran; do not run it twice
	const b = outputOf(runGazl(plainGazl));
	if (a === null || b === null) return null;      // trapped or did not load: nothing to compare
	if (a !== b) {
		return 'inline differential: inlined printed ' + JSON.stringify(a.slice(0, 200))
				+ ' but the out-of-line build printed ' + JSON.stringify(b.slice(0, 200));
	}
	return null;
}

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

	// Two guaranteed inline helpers with FIXED bodies, declared first so every later function and main
	// can call them: `fnW` writes through its pointer parameter, `fnA` is transparent and reads both of
	// its. Together they are exactly what the aliasing shape in genCall needs, and the bodies are fixed
	// because a random one would usually make `fnA` opaque - and then nothing is ever substituted.
	// Guaranteed for the same reason a struct local and a local array are: assembled from the random
	// population the shape needs about seven flips to line up and simply never appeared.
	// (Bare `p`/`a`/`b`/`r` cannot collide with generated names, which all carry a digit suffix.)
	const HELPERS = 'inline function fnW(int pointer p) returns int r { p[0] = 7; r = 0; }\n'
			+ 'inline function fnA(int a, int b) returns int r { r = a + b; }\n';
	funcs.push({ name: 'fnW', params: [{ name: 'p', t: 'p:i' }], rets: ['i'], retNames: ['r'],
			inline: true, fixed: true, aliasWriter: true });
	funcs.push({ name: 'fnA', params: [{ name: 'a', t: 'i' }, { name: 'b', t: 'i' }], rets: ['i'],
			retNames: ['r'], inline: true, fixed: true });

	// helper functions (2-5)
	const nFuncs = 2 + ri(4);
	for (let i = 0; i < nFuncs; ++i) {
		const params = [];
		// by-value struct params, struct returns and multi-value returns are parked for Impala 3.0
		// (see docs/ParkedFeatures.md), so params stay scalar/pointer and there is at most one
		// scalar return.
		for (let k = ri(4); k > 0; --k) params.push({ name: id('p'), t: someType(false, true) });
		const rets = rnd() < 0.35 ? [] : [pick(scalarTypes)];
		funcs.push({ name: 'fn' + i, params, rets, retNames: rets.map(() => id('r')),
				inline: chance(0.35) });   // no symbol is emitted, so never a funcptr target
	}

	// functypes (0-2), each derived from a real function's signature so it has a valid target
	const nFts = ri(3);
	const outOfLine = funcs.filter((f) => !f.inline);   // an inline function has no address to point at
	for (let i = 0; i < nFts && outOfLine.length; ++i) {
		const src = pick(outOfLine);
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
			// pointer parameter, passing one is undefined behaviour - it traps, and a trapped run has
			// no output for the inline differential to compare, so it silently costs coverage instead
			// of finding anything. Constant-string codegen is byte-diff gated by 36 fixtures already.
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

	// THE ALIASING SHAPE, built on purpose rather than left to chance: an INLINE call whose earlier
	// argument is a bare local and whose later argument writes that same local through a pointer.
	// The inliner may substitute the bare local, which DEFERS its read past the write, so the two
	// builds only agree if the marshalling scan stops at the write (docs/Inlining.md 5).
	// Assembled at random this needs about seven flips to line up - measured, only 1.3% of arguments
	// are even a bare local - and it turned up in roughly one program in two hundred, which is why a
	// real miscompile of exactly this shape survived a 1000-program sweep.
	function genCall(f, depth, scope) {
		const args = f.params.map((p) => genExpr(p.t, depth - 1, scope));
		const ints = f.params.map((p, k) => (p.t === 'i' ? k : -1)).filter((k) => k >= 0);
		const xs = scope.locals.filter((l) => l.t === 'i' && !l.ro && !l.loopVar);
		const w = scope.callable.filter((fn) => fn.aliasWriter);
		if (f.inline && ints.length >= 2 && xs.length && w.length && chance(0.5)) {
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
			const match = callable.filter((fn) => funcMatches(fn, ft) && !fn.inline);
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

		let header = (!isMain && f.inline ? 'inline ' : '') + 'function ' + f.name + '(' + f.params.map((p) => decl(p.t) + p.name).join(', ') + ')';
		if (f.rets.length) header += ' returns ' + f.rets.map((t, i) => decl(t) + f.retNames[i]).join(', ');

		const localDeclList = locals.map((l) => decl(l.t) + l.name)
			.concat(arrLocals.map((a) => (a.elem === 'i' ? 'int' : 'float') + ' array ' + a.name + '[' + a.size + ']'));
		const localDecl = localDeclList.length ? '\nlocals ' + localDeclList.join(', ') : '';

		const body = [];
		// Locals are NOT zero-initialized (an out-of-line callee sees the previous call's leftovers),
		// so reading one before writing it has no defined value. Inlining relocates a local from the
		// frame into a transient, which relocates that garbage - a legitimate difference that would
		// make the inline differential report false miscompiles. Seed every local first.
		for (const l of locals) {
			if (isStruct(l.t)) {
				const s = structs.find((x) => x.name === l.t);
				for (const fld of s.fields) if (!isStruct(fld.type)) body.push('\t' + l.name + '.' + fld.name + ' = ' + (fld.type === 'f' ? '0.0' : '0') + ';');
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
		// observable output, and the inline differential below compares "" against "" forever.
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
				const s = structs.find((x) => x.name === t);
				for (const fld of s.fields) if (!isStruct(fld.type)) body.push('\t' + f.retNames[i] + '.' + fld.name + ' = ' + genExpr(fld.type, 2, scope) + ';');
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
		const used = new Set();   // case labels must be distinct across the whole switch (else a duplicate GAZL label)
		for (let c = 1 + ri(3); c > 0; --c) {
			const labels = [];
			for (let j = 1 + ri(2); j > 0; --j) {
				let v = ri(12);
				while (used.has(v)) v = (v + 1) % 12;
				if (used.size >= 12) break;   // exhausted the label space
				used.add(v);
				labels.push(String(v));
			}
			if (labels.length) parts.push('\tcase ' + labels.join(', ') + ': ' + genBlock(scope, f, ctrlDepth));
		}
		if (chance(0.6)) parts.push('\tdefault: ' + genBlock(scope, f, ctrlDepth));
		return '\tswitch (' + genExpr('i', 2, scope) + ' == 0 to ' + (1 + ri(8)) + ') {\n' + parts.join('\n') + '\n\t}';
	}

	function genStmt(scope, f, ctrlDepth) {
		const roll = rnd();
		// control flow (if / else / bounded for), depth-limited to keep programs small
		if (roll < 0.18 && ctrlDepth < 2) {
			const cd = ctrlDepth + 1;
			const k = rnd();
			if (k < 0.45) return '\tif (' + genCond(scope) + ') ' + genBlock(scope, f, cd);
			if (k < 0.75) return '\tif (' + genCond(scope) + ') ' + genBlock(scope, f, cd) + ' else ' + genBlock(scope, f, cd);
			if (k < 0.88) return genSwitch(scope, f, cd);
			// bounded `for (fv = 0 to N)` - a loop var unique to this nesting level (so a nested loop
			// can't reset an enclosing counter), read-only inside the body so it always terminates
			const lv = scope.loopVars[ctrlDepth];
			if (lv) {
				const bodyScope = { ...scope, locals: scope.locals.map((l) => (l.loopVar ? { ...l, ro: true } : l)) };
				return '\tfor (' + lv.name + ' = 0 to ' + (1 + ri(4)) + ') ' + genBlock(bodyScope, f, cd);
			}
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
				const match = scope.callable.filter((fn) => funcMatches(fn, ft) && !fn.inline);
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
	out += HELPERS;
	for (let i = 0; i < funcs.length; ++i) {
		if (!funcs[i].fixed) out += renderFunc(funcs[i], false, funcs.slice(0, i));
	}
	out += renderFunc({ name: 'main', params: [], rets: [], retNames: [] }, true, funcs.slice());
	return out;
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
		process.stdout.write(genProgram() + '\n');
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
		let src;
		try {
			src = genProgram();
		} catch (genErr) {
			console.error(`[gen-error seed=${seed}] ${genErr.message}`);
			continue;
		}
		try {
			const gazl = compileWithJsImpala(src + '\n', COMPILE_OPTS);
			compiled++;
			if (useVm) {
				const res = runGazl(gazl);
				const vmFault = runOnVm(res) || inlineDifferential(src, res);
				vmRun++;
				if (vmFault) {
					bugs++;
					console.error(`\n=== VM FAULT seed=${seed}: ${vmFault} ===`);
					console.error(src);
					console.error(`=== end seed=${seed} ===\n`);
					if (bugs >= 5) { console.error('stopping after 5 faults'); break; }
				}
			}
		} catch (err) {
			const crash = classify(err);
			if (crash) {
				bugs++;
				console.error(`\n=== CRASH seed=${seed}: ${crash} ===`);
				console.error(src);
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
