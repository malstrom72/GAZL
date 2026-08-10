'use strict';

// Step 5 import-as-linking: build the multi-unit fixture and byte-compare against its golden.
// Run standalone: `node impala/importBuildTests.js` (optionally `makegold` to refresh the golden).

const fs = require('fs');
const path = require('path');

const { compileProgram, resolveImportClosure, deadStrip } = require('./impala.node.js');
const { compileWithJsImpala } = require('./impalaJsCompilerRunner');
const { haveGazlCmd, runExpected, parseExpectedRun } = require('./gazlAssembleCheck');

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

// Assemble a gazl string through GAZLCmd under a temp name and check it runs to `spec` ({ args, want }),
// cleaning up the temp file either way. `failPrefix` labels the failure for whichever check called in.
function assembleAndRun(label, gazl, spec, failPrefix) {
	const gazlPath = path.join(repoRoot, 'output', `deadstrip-${label}.gazl`);
	fs.writeFileSync(gazlPath, gazl, 'latin1');
	const failure = runExpected(gazlPath, spec);
	fs.unlinkSync(gazlPath);
	if (failure) {
		fail(failPrefix + failure);
	}
}

// The closure must gather both units, dependency-first (mathlib before main).
const closure = resolveImportClosure(rootUnit).map((u) => path.basename(u.path));
if (closure.length !== 2 || closure[0] !== 'mathlib.impala' || closure[1] !== 'main.impala') {
	fail('import closure order wrong: ' + JSON.stringify(closure));
}

const { output } = compileProgram(rootUnit, { randomId: RANDOM_ID });

// --dead-strip: exported main reaches `used`; `unused` must be dropped. Strip the ALREADY built
// program rather than resolving and compiling the same closure a second time.
const unstripped = compileProgram(stripRoot, { randomId: RANDOM_ID }).output;
const stripped = deadStrip(unstripped);

/* Only the two byte-compares are skipped when minting goldens. Everything below them - the symbol
   needles, the dead-strip assertions and the run-both-and-compare - is exactly what catches a golden
   that faithfully records corrupt output, so it must run while the golden is being REPLACED, which is
   the one moment nothing else is guarding it. */
if (!makeGold) {
	const golden = fs.readFileSync(goldenPath, 'latin1');
	if (canonicalizeNewlines(golden) !== canonicalizeNewlines(output)) {
		fail('import build output differs from golden ' + path.relative(repoRoot, goldenPath));
	}
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
if (!makeGold) {
	const strippedGold = fs.readFileSync(strippedGolden, 'latin1');
	if (canonicalizeNewlines(strippedGold) !== canonicalizeNewlines(stripped)) {
		fail('--dead-strip output differs from golden ' + path.relative(repoRoot, strippedGolden));
	}
}

/* A byte-compare cannot prove this one: dropping a data block used to leave its unlabelled `DATA`
   continuation rows behind, where the PRECEDING block silently adopted them - so the golden would just
   record the corruption. Stripping must not change what the program prints, so run both. */
if (haveGazlCmd()) {
	// The expected output lives in the fixture, via the same `Expected (GAZLCmd ...)` row the other two
	// harnesses read. It was a literal here until 2026-08-02, and extending stripmain.impala silently
	// left it stale - the run then fails as a behaviour change, blaming --dead-strip for the fixture.
	const want = parseExpectedRun(fs.readFileSync(stripRoot, 'latin1'));
	if (!want) {
		fail('stripmain.impala must carry an `Expected (GAZLCmd ...)` row for the dead-strip run check');
	}
	for (const [label, gazl] of [['unstripped', unstripped], ['stripped', stripped]]) {
		assembleAndRun(label, gazl, want, `--dead-strip changed program behaviour (${label}): `);
	}
}

/* Written only now, once everything above has vouched for what is about to be recorded. */
if (makeGold) {
	fs.writeFileSync(goldenPath, output, 'latin1');
	fs.writeFileSync(strippedGolden, stripped, 'latin1');
	console.log('Updated ' + path.relative(repoRoot, goldenPath) + ' and ' + path.relative(repoRoot, strippedGolden));
	process.exit(0);
}

// --- import cycles: gathered, but only half-resolved --------------------------
// A mutual cycle (even.impala <-> odd.impala) is GATHERED correctly - each file once,
// dependency-first - but the builder concatenates and the compiler is single-pass, so only the
// direction the concatenation order happens to favour resolves. The other needs a hand-written
// `extern`. The two assertions below pin that asymmetry: the SAME sources build or fail purely by
// which unit is named as root.
//
// This is the 2.0 RULE, not a known gap: collect mode is deferred to Impala 3.0
// (design/ParkedFeatures.md). If it ever lands the LAST assertion is the one that flips - `odd.impala`
// as root must build too, and odd.impala's `extern function isEven` becomes redundant rather than
// required. Turn it into a positive build-and-compare then, and delete this note.
const cycleDir = path.join(repoRoot, 'tests', 'impala', 'sources', 'importcycle');
const cycleClosure = resolveImportClosure(path.join(cycleDir, 'even.impala')).map((u) => path.basename(u.path));
if (cycleClosure.length !== 2 || cycleClosure[0] !== 'odd.impala' || cycleClosure[1] !== 'even.impala') {
	fail('import cycle closure wrong (want each file once, dependency-first): ' + JSON.stringify(cycleClosure));
}

const cycleOutput = compileProgram(path.join(cycleDir, 'even.impala'), { randomId: RANDOM_ID }).output;
for (const needle of ['isEven:', 'isOdd:', 'main:']) {
	if (cycleOutput.indexOf(needle) < 0) {
		fail('cycle build missing expected symbol: ' + needle);
	}
}

let otherRootError = null;
try {
	compileProgram(path.join(cycleDir, 'odd.impala'), { randomId: RANDOM_ID });
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

// --- body-carrying `extern struct` across a cycle -----------------------------
// The one cycle shape that used to fail SILENTLY. Rows 1-3 of the cycle story (a call backwards, a type
// backwards, the opaque `extern struct`) all fail at Impala compile time with a diagnostic naming the
// unit and a remedy. This one compiled clean and died at GAZL assembly on `Symbol not previously
// defined (in expected scope): .o.AA.x` - a symbol the user never wrote - because `! DEFi` constants
// resolve strictly top-down and the layout block sat in the unit emitted LATER. Now E464.
const structCycle = path.join(repoRoot, 'tests', 'impala', 'sources', 'importstructcycle');
let cycErr = null;
try {
	compileProgram(path.join(structCycle, 'owner.impala'), { randomId: RANDOM_ID });
} catch (err) {
	cycErr = (err && err.message) || String(err);
}
if (cycErr === null || cycErr.indexOf('E464') < 0) {
	fail('a body-carrying extern struct used before its real definition must be E464 - got: ' + cycErr);
}
// It must point at the USE, in the unit that made it, not at the definition that arrived too late.
if (cycErr.indexOf('user.impala:') !== 0) {
	fail('E464 must name the unit holding the use, not the root: ' + cycErr);
}
if (cycErr.indexOf('opaque form') < 0) {
	fail('E464 should name the opaque `extern struct` remedy: ' + cycErr);
}
// ...and the OPAQUE form across the same cycle still builds, which is what the remedy tells them to do.
compileProgram(path.join(structCycle, 'opaqueOwner.impala'), { randomId: RANDOM_ID });

// --- dead-strip on compiler-minted dotted data blocks -------------------------
// Three defects shared ONE blind spot: the block-boundary regex could not match a compiler-minted DOT
// label, so string/assert constants (`.s_...`/`.a_...`) were invisible as data definitions. None was
// gated because `stripmain.impala` has no string constant, no exported global, and no trailing dead
// function - so all three are reproduced from source here. See impalaImportClosure.js (DATA_LABEL_RE).

// A. A string used by a LIVE function, with a trailing DEAD one: the string table trails the dead
//    function and used to be absorbed and stripped with it, leaving a dangling `&.s_...` that will not
//    assemble. The kept reference must still have its definition.
const aStripped = deadStrip(compileWithJsImpala(
	'extern native print;\nexport function live()\n{\n\tprint("keep me alive");\n}\n'
		+ 'function dead()\n{\n\tprint("drop me");\n}\n', { randomId: RANDOM_ID }));
const aRef = aStripped.match(/&(\.s_\w+)/);
if (!aRef) fail('A: the live string reference vanished from the stripped output');
if (aStripped.indexOf(aRef[1] + ':') < 0) {
	fail('A: stripped output references ' + aRef[1] + ' but dropped its definition - would not assemble');
}
if (aStripped.indexOf('drop me') >= 0) fail('A: the dead function\'s string survived the strip');
if (haveGazlCmd()) {
	assembleAndRun('protoA', aStripped, { args: ['live'], want: ['keep', 'me', 'alive'] },
		'A: stripped output must assemble and still print its string - ');
}

// B. An exported global that nothing references is a ROOT and must survive (the CLI's own help text).
const bStripped = deadStrip(compileWithJsImpala(
	'export global int array arr[2];\nexport function keep() returns int r { r = 1; }\n', { randomId: RANDOM_ID }));
if (bStripped.indexOf('arr:') < 0) fail('B: --dead-strip dropped the exported (unreferenced) global `arr`');

// C. The three corpus programs whose `.s_` constants sit among other data used to throw
//    "initializer row belongs to no data block". They must strip cleanly.
for (const name of ['chess', 'Priyome', 'ImpalaDemo']) {
	try {
		deadStrip(compileProgram(path.join(repoRoot, 'tests', 'impala', 'sources', name + '.impala'),
			{ randomId: RANDOM_ID }).output);
	} catch (err) {
		fail('C: --dead-strip threw on ' + name + ': ' + ((err && err.message) || String(err)));
	}
}

console.log('import build + dead-strip + cycle tests passed.');
