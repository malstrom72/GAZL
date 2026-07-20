'use strict';

// Step 5 import-as-linking: build the multi-unit fixture and byte-compare against its golden.
// Run standalone: `node impala/importBuildTests.js` (optionally `makegold` to refresh the golden).

const fs = require('fs');
const path = require('path');

const { buildProgram, resolveImportClosure } = require('./impala.node.js');

const repoRoot = path.resolve(__dirname, '..');
const rootUnit = path.join(repoRoot, 'tests', 'impala', 'sources', 'import', 'main.impala');
const goldenPath = path.join(repoRoot, 'tests', 'impala', 'golden', 'importMain.gazl');
const RANDOM_ID = 0x4d2;

const makeGold = process.argv.slice(2).some((a) => a === 'makegold' || a === '--makegold');

function canonicalizeNewlines(text) {
	return text.replace(/\r\n/g, '\n');
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

if (makeGold) {
	fs.writeFileSync(goldenPath, output, 'latin1');
	console.log('Updated ' + path.relative(repoRoot, goldenPath));
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

console.log('import build test passed (2 units linked, golden matches).');
