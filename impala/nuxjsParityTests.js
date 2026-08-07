"use strict";

/*
	NuXJS <-> node PARITY.

	`impalaCompiler.js` has to run under NuXJS as well as node, and until 2026-08-07 nothing in the gate
	checked that. `tools/run-nuxjs-impala-smoke.cmd` compiles four programs; none of them declares a SHAPE,
	so `arrayExtent`'s `dims.map(...)` sat there breaking every multidimensional program under NuXJS
	("TypeError: map is not a function") from commit 1aae39a until it was found by hand. This exists so the
	next one is caught by the gate instead.

	Both engines are driven through their own CLI entry (`impala.node.js` / `impala.nuxjs.js`) so the two
	sides format identically - the goldens are NOT usable as the reference here, because runJspegTests
	writes them through a different harness that omits the CLI's leading space.

	The list is a SUBSET chosen to span the feature matrix rather than the whole corpus, so the gate stays
	quick; `--all` runs every source. Keep `multidimFields` in it whatever else changes - it is the witness
	for the bug that motivated the file.
*/

const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const repoRoot = path.resolve(__dirname, "..");
const sourcesDir = path.join(repoRoot, "tests", "impala", "sources");
const RANDOM_ID = "1234";

const SUBSET = [
	"multidimFields",      // shaped arrays - the case the smoke test misses
	"structFieldExtents",  // symbolic extents, per-axis .d. symbols
	"structArrayFields",   // struct-element arrays, .z./.o. symbols
	"structSymbolicFill",  // host-supplied extents
	"typedPointers",       // Impala 2 descriptors
	"funcType",            // named funcptr types
	"funcTypeCasts",       // funcptr casts and comparisons
	"externStruct",        // host-owned layout
	"switchtest",          // switch lowering
	"calc",                // funcptr table + strings
	"unarytest",
	"ImpalaDemo",          // a broad, ordinary program
];

function findNuxjs() {
	if (process.env.NUXJS && fs.existsSync(process.env.NUXJS)) return process.env.NUXJS;
	for (const rel of ["output/NuXJS.exe", "output/NuXJS"]) {
		const p = path.join(repoRoot, rel);
		if (fs.existsSync(p)) return p;
	}
	return undefined;
}

function compile(argv) {
	try {
		execFileSync(argv[0], argv.slice(1), { stdio: "pipe" });
		return true;
	} catch (e) {
		return false;
	}
}

function main() {
	const nuxjs = findNuxjs();
	if (nuxjs === undefined) {
		/* Not a failure - the C++ engine may simply not be built here. Say so LOUDLY, because a silent
		   skip is how this gate would rot back into checking nothing. */
		console.log("NuXJS parity: SKIPPED - no NuXJS binary "
				+ "(build with tools/BuildNuXJS.cmd, or set NUXJS). NuXJS was NOT checked.");
		return 0;
	}

	const all = process.argv.slice(2).some((a) => a === "--all");
	const names = all
		? fs.readdirSync(sourcesDir).filter((f) => f.endsWith(".impala")).map((f) => path.basename(f, ".impala"))
		: SUBSET;

	const tmp = fs.mkdtempSync(path.join(require("os").tmpdir(), "nuxjs-parity-"));
	let checked = 0;
	const failures = [];

	for (const name of names) {
		const src = path.join(sourcesDir, name + ".impala");
		if (!fs.existsSync(src)) { failures.push(name + ": no such source"); continue; }
		const outNode = path.join(tmp, name + ".node.gazl");
		const outNux = path.join(tmp, name + ".nuxjs.gazl");

		const okNode = compile(["node", path.join(repoRoot, "impala", "impala.node.js"),
				"compile", src, outNode, RANDOM_ID]);
		const okNux = compile([nuxjs, path.join(repoRoot, "impala", "impala.nuxjs.js"),
				src, outNux, RANDOM_ID, name + ".impala",
				path.join(repoRoot, "impala", "impalaCompiler.js")]);

		if (!okNode && !okNux) { continue; }             /* both reject it: agreement enough for this gate */
		if (!okNode) { failures.push(name + ": node failed, NuXJS did not"); continue; }
		if (!okNux) { failures.push(name + ": NuXJS FAILED to compile (node succeeded)"); continue; }

		const a = fs.readFileSync(outNode);
		const b = fs.readFileSync(outNux);
		if (!a.equals(b)) {
			let at = 0;
			while (at < a.length && at < b.length && a[at] === b[at]) ++at;
			failures.push(name + ": output differs at byte " + at);
		}
		++checked;
	}

	fs.rmSync(tmp, { recursive: true, force: true });

	if (failures.length > 0) {
		console.log("NuXJS parity: " + failures.length + " FAILED of " + names.length);
		for (const f of failures) console.log("  " + f);
		return 1;
	}
	console.log("NuXJS parity: " + checked + " programs compile identically under node and NuXJS"
			+ (all ? " (full corpus)" : " (subset; --all for every source)"));
	return 0;
}

process.exit(main());
