'use strict';

// Generative fuzzer for the Impala compiler.
//
// It emits random, mostly type-valid Impala programs that lean on the intricate paths
// (nested calls, struct-value params, struct/multi-value returns used as arguments,
// destructuring, funcptr dispatch) and compiles each one. The oracle is robustness:
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

// Run a compiled program on the VM. Returns null when there is no compiler fault, or a message when
// the loader/assembler REJECTS the emitted GAZL - that means the compiler produced structurally
// invalid or wrongly-linked output from a program it accepted, which is a real compiler bug.
//
// A non-zero exit is only a fault if it happens at LOAD time. The VM prints a "Code size: ...
// functions: N" banner once the module assembles; if we see it, the module loaded fine and any
// later non-zero status is a RUNTIME trap - the generated program's own undefined behaviour (a wild
// pointer, a write through a string literal, an out-of-region index). That is the program's fault,
// not the compiler's, and without a reference oracle we cannot call it a miscompile, so we ignore it.
// The generator avoids `/` and `%` (no div-by-zero) and bounds its indices/pointer offsets to keep
// runtime traps rare, but pointer/string undefined behaviour is inherent and expected here.
function runOnVm(gazl) {
	const tmp = path.join(os.tmpdir(), `fuzz-${process.pid}-${Math.floor(rnd() * 1e9)}.gazl`);
	fs.writeFileSync(tmp, gazl, 'latin1');
	try {
		const res = cp.spawnSync(GAZLCMD, [tmp, 'main'], { encoding: 'latin1', timeout: 10000 });
		if (res.error) return 'spawn/timeout: ' + res.error.message;
		if (res.status === 0) return null;
		const err = (res.stderr || '') + (res.stdout || '');
		if (/Code size:|functions:\s*\d/.test(err)) return null;   // loaded OK -> runtime trap, program UB
		const line = err.split('\n').find((l) => /error|fault|assert|invalid|already defined|out of bounds|Status: [^0]/i.test(l));
		return 'load failure (exit ' + res.status + '): ' + (line || err.split('\n')[0] || '').trim();
	} finally {
		try { fs.unlinkSync(tmp); } catch (_) {}
	}
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
	const decl = (t) =>
		isPtr(t) ? (ptrElem(t) === 'i' ? 'int pointer ' : 'float pointer ')
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
	// an int index expression for array access (constant, or a scalar int in scope)
	// An index expression. `size` is the target array's element count when indexing a NAMED array
	// directly (`arr[k]`): a constant then stays in-bounds because the GAZL assembler statically
	// bounds-checks constant array indices. Omit `size` for pointer derefs (`p[k]`), which are not
	// statically checked - any constant is legal there, and dynamic out-of-bounds never traps.
	function genIdx(scope, size) {
		if (chance(0.6)) return String(ri(size || 8));
		const ints = scope.locals.filter((l) => l.t === 'i').concat(scope.gscalars.filter((g) => g.elem === 'i'));
		// a dynamic index is masked non-negative and small: a wild negative/huge offset escapes the VM
		// memory region and traps, whereas a small overrun stays in-region (no trap) - keeps --vm defined
		return ints.length ? '(' + ref(ints[ri(ints.length)]) + ' & 7)' : String(ri(size || 8));
	}

	// helper functions (2-5)
	const nFuncs = 2 + ri(4);
	for (let i = 0; i < nFuncs; ++i) {
		const params = [];
		for (let k = ri(4); k > 0; --k) params.push({ name: id('p'), t: someType(true, true) });
		let rets;
		const roll = rnd();
		if (roll < 0.3) rets = [];
		else if (roll < 0.6) rets = [pick(scalarTypes)];
		else if (roll < 0.8) rets = [pick(scalarTypes), pick(scalarTypes)];
		else rets = [someType(true)];   // possibly a struct return
		// a struct return must be the only return value (enforced by the language)
		if (rets.length > 1 && rets.some(isStruct)) rets = [pick(scalarTypes)];
		funcs.push({ name: 'fn' + i, params, rets, retNames: rets.map(() => id('r')) });
	}

	// functypes (0-2), each derived from a real function's signature so it has a valid target
	const nFts = ri(3);
	for (let i = 0; i < nFts && funcs.length; ++i) {
		const src = pick(funcs);
		functypes.push({ name: 'FT' + i, params: src.params.map((p) => p.t), rets: src.rets.slice(), target: src.name });
	}
	const funcMatches = (fn, ft) => fn.params.length === ft.params.length
		&& fn.params.every((p, k) => p.t === ft.params[k])
		&& fn.rets.length === ft.rets.length && fn.rets.every((t, k) => t === ft.rets[k]);

	// expression generator toward a wanted type, depth-limited
	function genExpr(want, depth, scope) {
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
			const e = ptrElem(want);
			if (e === 'i' && chance(0.15)) {
				// a string literal is typed as `int pointer` - exercises the constant-string path
				let s = '';
				for (let n = ri(8); n > 0; --n) s += String.fromCharCode(97 + ri(26));
				return '"' + s + '"';
			}
			const ptrs = scope.locals.filter((l) => l.t === want);
			if (depth > 0 && ptrs.length && chance(0.35)) {
				// pointer arithmetic - offset masked small/non-negative so the result stays in-region
				return '(' + ref(pick(ptrs)) + ' + (' + genExpr('i', depth - 1, scope) + ' & 7))';
			}
			if (ptrs.length && chance(0.5)) return ref(pick(ptrs));
			const arr = pick(scope.localArrays.filter((a) => a.elem === e));   // guaranteed non-empty
			return '&' + arr.name + '[' + genIdx(scope, arr.size) + ']';
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

	function genCall(f, depth, scope) {
		const args = f.params.map((p) => genExpr(p.t, depth - 1, scope));
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
		for (const ft of functypes) {
			if (chance(0.5)) locals.push({ name: id('cb'), t: ft.name });
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
			callable: callable,
		};

		let header = 'function ' + f.name + '(' + f.params.map((p) => decl(p.t) + p.name).join(', ') + ')';
		if (f.rets.length) header += ' returns ' + f.rets.map((t, i) => decl(t) + f.retNames[i]).join(', ');

		const localDeclList = locals.map((l) => decl(l.t) + l.name)
			.concat(arrLocals.map((a) => (a.elem === 'i' ? 'int' : 'float') + ' array ' + a.name + '[' + a.size + ']'));
		const localDecl = localDeclList.length ? '\nlocals ' + localDeclList.join(', ') : '';

		const body = [];
		// initialize pointer locals to a valid local array so dereferences stay in VM memory
		for (const p of ptrLocals) {
			const arr = arrLocals.find((a) => a.elem === ptrElem(p.t));
			if (arr) body.push('\t' + p.name + ' = &' + arr.name + '[0];');
		}
		const nStmt = 1 + ri(isMain ? 8 : 4);
		for (let i = 0; i < nStmt; ++i) body.push(genStmt(scope, f, 0));
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
		// pointer-dereference write through a writable pointer local
		if (roll < 0.24) {
			const ptrs = scope.locals.filter((l) => isPtr(l.t) && !l.ro);
			if (ptrs.length) {
				const p = pick(ptrs);
				return '\t' + p.name + '[' + genIdx(scope) + '] = ' + genExpr(ptrElem(p.t), 3, scope) + ';';
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
		// destructuring of a multi-return call
		const multi = scope.callable.filter((fn) => fn.rets.length > 1);
		if (roll < 0.2 && multi.length) {
			const fn = pick(multi);
			const targets = fn.rets.map((t) => {
				const cands = scope.locals.filter((l) => l.t === t && !l.ro);   // '_' discards are always legal
				return cands.length && chance(0.7) ? pick(cands).name : '_';
			});
			return '\t' + targets.join(', ') + ' = ' + genCall(fn, 3, scope) + ';';
		}
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
		// whole-struct or scalar assignment to a local (funcptr locals + read-only params excluded)
		if (roll < 0.8) {
			const assignable = scope.locals.filter((x) => !isFuncType(x.t) && !x.ro);
			if (assignable.length) {
				const l = pick(assignable);
				return '\t' + l.name + ' = ' + genExpr(l.t, 3, scope) + ';';
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
	let out = 'const int DEBUG = 1\n';
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
	for (let i = 0; i < funcs.length; ++i) out += renderFunc(funcs[i], false, funcs.slice(0, i));
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
			const gazl = compileWithJsImpala(src + '\n', { randomId: 0x4d2, retabulate: true, trailingNewline: true });
			compiled++;
			if (useVm) {
				const vmFault = runOnVm(gazl);
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
