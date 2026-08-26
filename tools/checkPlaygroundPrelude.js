// The playground cannot read natives.impala at runtime (a single static file:// page), so its PRELUDE
// is a hand copy of the printing subset. This asserts every prelude line is still a verbatim line of
// natives.impala - reintroducing the manifest drift that file was created to end, minus the drift.
// Extracting NOTHING is a failure too: a vacuously-matching extractor has slipped past this repo once
// already (the docSamples CRLF hole), and a gate that checks nothing reads exactly like one that passes.

const fs = require('fs');
const path = require('path');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'impala', 'playground.html'), 'utf8');
const canon = new Set(fs.readFileSync(path.join(root, 'impala', 'natives.impala'), 'utf8')
		.split(/\r?\n/).map((l) => l.trim()));

const prelude = [...html.matchAll(/^\s*"(extern native [^"]*)",?\s*$/gm)].map((m) => m[1]);
if (prelude.length === 0) throw new Error('extracted no PRELUDE lines from playground.html');
const missing = prelude.filter((l) => !canon.has(l));
if (missing.length) {
	throw new Error('playground PRELUDE drifted from natives.impala:\n  ' + missing.join('\n  '));
}
console.log(`checkPlaygroundPrelude: ${prelude.length} prelude lines match natives.impala`);
