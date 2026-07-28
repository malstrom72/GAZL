'use strict';

// Step 5 import-as-linking: build the multi-unit fixture and byte-compare against its golden.
// Run standalone: `node impala/importBuildTests.js` (optionally `makegold` to refresh the golden).

const fs = require('fs');
const path = require('path');

const { buildProgram, resolveImportClosure, deadStrip } = require('./impala.node.js');

const repoRoot = path.resolve(__dirname, '..');
const rootUnit = path.join(repoRoot, 'tests', 'impala', 'sources', 'import', 'main.impala');
const goldenPath = path.join(repoRoot, 'tests', 'impala', 'golden', 'importMain.gazl');
const stripRoot = path.join(repoRoot, 'tests', 'impala', 'sources', 'deadstrip', 'stripmain.impala');
const strippedGolden = path.join(repoRoot, 'tests', 'impala', 'golden', 'stripped.gazl');
const RANDOM_ID = 0x4d2;

const makeGold = process.argv.slice(2).some((a) => a === 'makegold' || a === '--makegold');

function canonicalizeNewlines(text) {
	return text.replace(/\r\n?/g, '\n');   // also a lone CR, matching the other two harnesses
}

function fail(message) {
	console.error(message);
	process.exit(1);
}

// The closure must gather both units, dependency-first (mathlib before main).
const closure = resolveImportClosure(rootUnit).map((u) => path.basename(u.path));
if (closure.length !== 2 || closure[0] !== 'mathlib.impala' || closure[1] !== 'main.impala') {
	fail('import closure order wrong: ' + JSON.stringify(closure));
}

const { output } = buildProgram(rootUnit, { randomId: RANDOM_ID });

// --dead-strip: exported main reaches `used`; `unused` must be dropped. Strip the ALREADY built
// program rather than resolving and compiling the same closure a second time.
const unstripped = buildProgram(stripRoot, { randomId: RANDOM_ID }).output;
const stripped = deadStrip(unstripped);

if (makeGold) {
	fs.writeFileSync(goldenPath, output, 'latin1');
	fs.writeFileSync(strippedGolden, stripped, 'latin1');
	console.log('Updated ' + path.relative(repoRoot, goldenPath) + ' and ' + path.relative(repoRoot, strippedGolden));
	process.exit(0);
}

const golden = fs.readFileSync(goldenPath, 'latin1');
if (canonicalizeNewlines(golden) !== canonicalizeNewlines(output)) {
	fail('import build output differs from golden ' + path.relative(repoRoot, goldenPath));
}

// Sanity: the linked program must actually contain the cross-unit definitions.
for (const needle of ['makeVec:', 'addVec:', 'divmod:', 'main:']) {
	if (output.indexOf(needle) < 0) {
		fail('linked program missing expected symbol: ' + needle);
	}
}

// dead-strip assertions: default build keeps everything; --dead-strip drops the unreached fn.
if (unstripped.indexOf('unused:') < 0) fail('default build must keep unused (no trimming)');
if (stripped.indexOf('unused:') >= 0) fail('--dead-strip must drop the unreachable `unused`');
if (stripped.indexOf('used:') < 0) fail('--dead-strip must keep `used` (reached from exported main)');
if (stripped.indexOf('main:') < 0) fail('--dead-strip must keep the exported `main`');
const strippedGold = fs.readFileSync(strippedGolden, 'latin1');
if (canonicalizeNewlines(strippedGold) !== canonicalizeNewlines(stripped)) {
	fail('--dead-strip output differs from golden ' + path.relative(repoRoot, strippedGolden));
}

// --- import cycles: gathered, but only half-resolved --------------------------
// A mutual cycle (even.impala <-> odd.impala) is GATHERED correctly - each file once,
// dependency-first - but the builder concatenates and the compiler is single-pass, so only the
// direction the concatenation order happens to favour resolves. The other needs a hand-written
// `extern`. The two assertions below pin that asymmetry: the SAME sources build or fail purely by
// which unit is named as root.
//
// WHEN COLLECT MODE LANDS (impala/Impala2Slices.md:155-163, docs/Impala2.md "Cycles") the LAST
// assertion is the one that flips - `odd.impala` as root must build too, and odd.impala's
// `extern function isEven` becomes redundant rather than required. Turn it into a positive
// build-and-compare then, and delete this note.
const cycleDir = path.join(repoRoot, 'tests', 'impala', 'sources', 'importcycle');
const cycleClosure = resolveImportClosure(path.join(cycleDir, 'even.impala')).map((u) => path.basename(u.path));
if (cycleClosure.length !== 2 || cycleClosure[0] !== 'odd.impala' || cycleClosure[1] !== 'even.impala') {
	fail('import cycle closure wrong (want each file once, dependency-first): ' + JSON.stringify(cycleClosure));
}

const cycleOutput = buildProgram(path.join(cycleDir, 'even.impala'), { randomId: RANDOM_ID }).output;
for (const needle of ['isEven:', 'isOdd:', 'main:']) {
	if (cycleOutput.indexOf(needle) < 0) {
		fail('cycle build missing expected symbol: ' + needle);
	}
}

let otherRootError = null;
try {
	buildProgram(path.join(cycleDir, 'odd.impala'), { randomId: RANDOM_ID });
} catch (err) {
	otherRootError = (err && err.message) || String(err);
}
if (otherRootError === null || otherRootError.indexOf('E403') < 0) {
	fail('odd.impala as root should still fail with E403 (single-pass cannot forward-reference across '
			+ 'the cycle). If collect mode landed, make this a positive build - got: ' + otherRootError);
}
// ...and while it does fail, it must fail LEGIBLY: blaming the root unit on a line number that only
// indexes the concatenation sent people looking in the wrong file.
if (otherRootError.indexOf('even.impala:') !== 0) {
	fail('cycle error must name the unit the text came from, not the root: ' + otherRootError);
}
if (otherRootError.indexOf('isOdd is defined later, at odd.impala:') < 0) {
	fail('cycle error should point at the definition it cannot see yet: ' + otherRootError);
}

console.log('import build + dead-strip + cycle tests passed.');
