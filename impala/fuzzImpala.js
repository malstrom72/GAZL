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

// Run a compiled program on the VM. Returns null on clean exit, or a message on a VM fault —
// a miscompile (structurally invalid or wrongly-linked GAZL crashing the loader/VM) is a
// compiler bug. The generator avoids `/` and `%`, so no legitimate div-by-zero traps arise.
function runOnVm(gazl) {
	const tmp = path.join(os.tmpdir(), `fuzz-${process.pid}-${Math.floor(rnd() * 1e9)}.gazl`);
	fs.writeFileSync(tmp, gazl, 'latin1');
	try {
		const res = cp.spawnSync(GAZLCMD, [tmp, 'main'], { encoding: 'latin1', timeout: 10000 });
		if (res.error) return 'spawn: ' + res.error.message;
		if (res.status !== 0) {
			const err = (res.stderr || '') + (res.stdout || '');
			return 'VM exit ' + res.status + ': ' + err.split('\n').find((l) => /error|fault|assert|invalid|Status: [^0]/i.test(l)) || ('VM exit ' + res.status);
		}
		return null;
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
	const structs = [];   // { name, fields:[{name,type}] }  type ∈ {'i','f'} or a struct name
	const functypes = []; // { name, params:[t], rets:[t] }
	const funcs = [];     // { name, params:[{name,t}], rets:[t] }  t ∈ {'i','f',structName}
	let uid = 0;
	const id = (p) => p + (uid++);

	const scalarTypes = ['i', 'f'];
	const structNames = () => structs.map((s) => s.name);
	// a value type usable for params/returns/locals
	function someType(allowStruct) {
		if (allowStruct && structs.length && chance(0.4)) return pick(structNames());
		return pick(scalarTypes);
	}
	const isStruct = (t) => structs.some((s) => s.name === t);
	const isFuncType = (t) => functypes.some((ft) => ft.name === t);
	const decl = (t) => ((isStruct(t) || isFuncType(t)) ? t + ' ' : (t === 'i' ? 'int ' : 'float '));

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

	// helper functions (2-5)
	const nFuncs = 2 + ri(4);
	for (let i = 0; i < nFuncs; ++i) {
		const params = [];
		for (let k = ri(4); k > 0; --k) params.push({ name: id('p'), t: someType(true) });
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
		// scope: { locals: [{name,t}] } — guaranteed to hold >=1 local of every struct type
		if (isStruct(want)) {
			const cands = scope.locals.filter((l) => l.t === want);
			// only recurse into a struct-returning call while we still have depth budget
			if (depth > 0 && chance(0.5)) {
				const callable = scope.callable.filter((f) => f.rets.length === 1 && f.rets[0] === want);
				if (callable.length) return genCall(pick(callable), depth, scope);
			}
			return pick(cands).name;   // base case: always a real struct local
		}
		// scalar want ('i' or 'f')
		const lit = () => (want === 'i' ? String(ri(100) - 50) : (ri(1000) / 10).toFixed(1));
		if (depth <= 0) {
			const cands = scope.locals.filter((l) => l.t === want);
			return cands.length && chance(0.5) ? pick(cands).name : lit();
		}
		const r = rnd();
		if (r < 0.25) return lit();
		if (r < 0.45) {
			const cands = scope.locals.filter((l) => l.t === want);
			return cands.length ? pick(cands).name : lit();
		}
		if (r < 0.7) {
			const op = want === 'i' ? pick(['+', '-', '*']) : pick(['+', '-', '*']);
			return '(' + genExpr(want, depth - 1, scope) + ' ' + op + ' ' + genExpr(want, depth - 1, scope) + ')';
		}
		if (r < 0.85) {
			// field read of a scalar field from a struct local
			const opts = [];
			for (const l of scope.locals) if (isStruct(l.t)) {
				const s = structs.find((x) => x.name === l.t);
				for (const f of s.fields) if (f.type === want) opts.push(l.name + '.' + f.name);
			}
			if (opts.length) return pick(opts);
			return lit();
		}
		// a call returning this scalar (nested — struct args here exercise window sliding)
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
		// params are in scope too, but read-only (scalar INP params can't be assigned)
		const scope = { locals: locals.concat(f.params.map((p) => ({ name: p.name, t: p.t, ro: true }))), callable: callable };

		let header = 'function ' + f.name + '(' + f.params.map((p) => decl(p.t) + p.name).join(', ') + ')';
		if (f.rets.length) header += ' returns ' + f.rets.map((t, i) => decl(t) + f.retNames[i]).join(', ');

		const localDecl = locals.length ? '\nlocals ' + locals.map((l) => decl(l.t) + l.name).join(', ') : '';

		const body = [];
		const nStmt = 1 + ri(isMain ? 8 : 4);
		for (let i = 0; i < nStmt; ++i) body.push(genStmt(scope, f));
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

	function genStmt(scope, f) {
		const roll = rnd();
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
		// struct field assignment
		if (roll < 0.5) {
			const sLocals = scope.locals.filter((l) => isStruct(l.t));
			if (sLocals.length) {
				const l = pick(sLocals);
				const s = structs.find((x) => x.name === l.t);
				const scalarFields = s.fields.filter((fl) => !isStruct(fl.type));
				if (scalarFields.length) {
					const fld = pick(scalarFields);
					return '\t' + l.name + '.' + fld.name + ' = ' + genExpr(fld.type, 3, scope) + ';';
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
	for (let i = 0; i < funcs.length; ++i) out += renderFunc(funcs[i], false, funcs.slice(0, i));
	out += renderFunc({ name: 'main', params: [], rets: [], retNames: [] }, true, funcs.slice());
	return out;
}

// ---- crash oracle ------------------------------------------------------------
const CLEAN = /error\[E\d+\]/;                 // a coded diagnostic — acceptable
const BENIGN = /compiler stopped at \d+/;      // partial parse — acceptable
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
