'use strict';
/*
	The hand-written .gazl fragments under docs/ are "runnable proofs" - a claim in prose that can be
	executed instead of believed. Nothing ran them, which is the failure mode this file exists to close:
	a proof nobody runs is a claim that rots silently, and these are the claims a reader is LEAST likely
	to re-derive, because they demonstrate things the compiler cannot emit yet.

	Every expectation below is copied from the fragment's own header comment or from the doc that cites
	it, so this file asserts what the docs promise and not something separately invented. When a proof
	and its doc disagree, one of them is wrong and the build should say so.

	Degrades rather than fails without output/GAZLCmd, exactly as runJspegTests and jspegCompilerTests
	do - tools/test-js.sh is the no-C++-toolchain gate and must stay runnable on a bare checkout.
*/

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const repoRoot = path.join(__dirname, '..');
const gazlCmd = ['output/GAZLCmd.exe', 'output/GAZLCmd']
		.map((p) => path.join(repoRoot, p))
		.find((p) => fs.existsSync(p));

/* `main` plus the host-supplied symbols the fragment's header documents. `expect` lines must all appear
   in the output; `reject` must not. A FAIL directive makes GAZLCmd exit non-zero, so `fails` records
   which cases are SUPPOSED to - a proof that only ever passes proves nothing. */
const PROOFS = [
	{
		file: 'design/proofs/symbolicWindows.gazl',
		doc: 'design/gazl/GAZLSymbolicWindows.md',
		args: ['main'],
		expect: ['55', 'globals size: 6'],
	},
	{
		file: 'design/proofs/symbolicWindowsRepacked.gazl',
		doc: 'design/gazl/GAZLSymbolicWindows.md',
		args: ['main'],
		/* Same answer from a re-packed layout - 6 words become 8 and no instruction changed. */
		expect: ['55', 'globals size: 8'],
	},
	{
		file: 'design/proofs/deferredShapeCheck.gazl',
		doc: 'docs/impala/MultidimensionalArrays.md',
		args: ['main', 'N', '4', 'W', '4'],
		/* [4][3] against [N][W-1]: neither Impala nor the signature validator can accept this, the
		   assembler can, because by then it has the numbers. */
		expect: ['Code size: 2'],
		reject: ['FAIL'],
	},
	{
		file: 'design/proofs/deferredShapeCheck.gazl',
		doc: 'docs/impala/MultidimensionalArrays.md',
		args: ['main', 'N', '5', 'W', '4'],
		fails: true,
		expect: ['axis 0 is b[N] but f expects [4]'],
	},
	{
		file: 'design/proofs/deferredShapeCheck.gazl',
		doc: 'docs/impala/MultidimensionalArrays.md',
		args: ['main', 'N', '4', 'W', '9'],
		fails: true,
		expect: ['axis 1 is b[W - 1] but f expects [3]'],
	},
	{
		file: 'design/proofs/seekRegions.gazl',
		doc: 'design/gazl/GAZL2DataRegions.md',
		args: ['main'],
		expect: ['60906', 'globals size: 6'],
	},
	{
		file: 'design/proofs/seekRegionsRepacked.gazl',
		doc: 'design/gazl/GAZL2DataRegions.md',
		args: ['main'],
		/* Same answer from a re-packed layout - gain first, a pad word, note last - via SEEK regions. */
		expect: ['60906', 'globals size: 7'],
	},
];

/* The re-pack claim is a DIFF, not an output: every line below the layout header is byte-identical, so
   a host re-packing a struct re-assembles rather than recompiles. Asserted here because it is the whole
   point of keeping two nearly identical files, and a careless edit to one would silently break it. */
const REPACK_PAIRS = [
	{ a: 'design/proofs/symbolicWindows.gazl', b: 'design/proofs/symbolicWindowsRepacked.gazl',
			skip: 28, doc: 'design/gazl/GAZLSymbolicWindows.md' },
	{ a: 'design/proofs/seekRegions.gazl', b: 'design/proofs/seekRegionsRepacked.gazl',
			skip: 19, doc: 'design/gazl/GAZL2DataRegions.md' },
];

function tail(file, skip) {
	return fs.readFileSync(path.join(repoRoot, file), 'utf8')
			.replace(/\r\n?/g, '\n').split('\n').slice(skip).join('\n');
}

let failures = 0;

function fail(label, detail) {
	console.error(`  FAIL ${label}`);
	console.error(`       ${detail}`);
	failures += 1;
}

if (!gazlCmd) {
	console.log('(no output/GAZLCmd - skipping the runnable doc proofs)');
} else {
	for (const p of PROOFS) {
		const label = `${p.file} ${p.args.join(' ')}`;
		const r = spawnSync(gazlCmd, [path.join(repoRoot, p.file), ...p.args], { encoding: 'utf8' });
		const out = `${r.stdout || ''}${r.stderr || ''}`;
		const died = (r.status !== 0);
		if (died !== !!p.fails) {
			fail(label, p.fails
					? `expected a FAIL directive to stop it, but it exited 0 (${p.doc})`
					: `expected a clean run, got exit ${r.status} (${p.doc})`);
			continue;
		}
		let ok = true;
		for (const want of p.expect || []) {
			if (!out.includes(want)) { fail(label, `missing ${JSON.stringify(want)} - ${p.doc}`); ok = false; }
		}
		for (const bad of p.reject || []) {
			if (out.includes(bad)) { fail(label, `unexpected ${JSON.stringify(bad)} - ${p.doc}`); ok = false; }
		}
		if (ok) console.log(`  ok   ${label}`);
	}
}

for (const p of REPACK_PAIRS) {
	if (tail(p.a, p.skip) !== tail(p.b, p.skip)) {
		fail(`${path.basename(p.a, '.gazl')} re-pack`, 'instructions below the layout header are NOT identical, '
				+ `so the "re-packing re-assembles, it does not recompile" claim in ${p.doc} no longer holds`);
	} else {
		console.log(`  ok   ${path.basename(p.a, '.gazl')} vs Repacked: every line below the header is identical`);
	}
}

if (failures > 0) {
	console.error(`doc proofs: ${failures} failure(s)`);
	process.exit(1);
}
console.log('doc proofs: the runnable .gazl fragments still prove what their docs claim');
