"use strict";

/* Compile every code sample in docs/impala/WhatsNewInImpala2.md, so that page cannot drift away from the
   compiler it describes.

   This exists because the failure it prevents has happened repeatedly here: a doc stated a rule, the rule
   was never true (or stopped being true), and nothing noticed until someone tried the advice. The release
   audit found E-codes documented with no fail site, a `goto` idiom that did not parse, and a `--legacy`
   claim about an operator combination that does not fire. Prose is not executable, so the fix is to hold
   the prose to something that is.

   THE SHAPE. Fenced `impala` blocks in a readable doc are mostly FRAGMENTS - a struct declaration, two
   lines of a function - and making each one independently compilable would wreck the page. So the samples
   below are whole programs, and the gate is two-sided:

     1. every sample compiles, with exactly the outcome stated here (clean, or a specific diagnostic);
     2. every fenced `impala` block in the doc appears VERBATIM inside one of them.

   So a block cannot be edited into something that does not compile, and a sample cannot quietly stop
   covering the page. Shell blocks are checked too - each flag they name has to appear in the CLI's own
   usage text, which is how `impala build` (a subcommand that never existed) survived in four docs. */

const fs = require("fs");
const os = require("os");
const path = require("path");

/* Every doc whose samples are held to the compiler. `Impala.md` is the language reference - the one source
   of truth - so it is the LAST document that should be allowed to drift, and it carries the most examples.
   The sample pool below is shared: a diagnostic code counts as covered when ANY sample provokes it, since
   the point is that the code exists and fires, not which page happens to cite it. */
const DOCS = [
	{ file: "impala/Impala.md" },
	{ file: "impala/WhatsNewInImpala2.md", upgradeTables: true },
];
const { compileProgram } = require("./impala.node.js");

/* Whole programs. `expect` is the diagnostic code the compiler must report, or null for a clean compile -
   the FAILING samples matter as much as the passing ones, since a page that promises E201 and gets
   silence is the exact defect this gate is for. `files` adds units beside the root for the import sample. */
const SAMPLES = [
	{
		name: "structs and typed pointers",
		expect: null,
		/* Ordered so each of the page's three blocks is a contiguous substring of this one program -
		   that is what side two of the gate compares against. */
		src: [
			"struct Point { int x; int y }",
			"struct Body { Point pos; float mass; int array tags[4] }",
			"",
			"global Body b",
			"",
			"function move(Body pointer bp, int dx)",
			"{",
			"\tbp->pos.x = bp->pos.x + dx;",
			"}",
			"",
			"global Point origin = { x: 1, y: 2 }",
			"",
			"functype Step(int frame)",
			"",
			"function tick(int frame) { }",
			"",
			"global Step cb = tick",
			"",
			"export function main() locals int n",
			"{",
			"\tmove(&global b, 3);",
			"\tn = sizeof(Body);",
			"}",
			"",
		].join("\n"),
	},
	{
		name: "a typed pointer refuses the wrong element",
		expect: "E201",
		src: "global float array gf[4]\nglobal int pointer p = &global gf[0]\nexport function main() { }\n",
	},
	{
		name: "the host owns an extern struct's layout",
		expect: null,
		src: [
			"extern struct HostFrame { int width; int height }",
			"",
			"extern native printInt",
			"",
			"export function main()",
			"{",
			"\tprintInt(sizeof(HostFrame));",
			"}",
			"",
		].join("\n"),
	},
	{
		name: "import is linking",
		expect: null,
		files: {
			"lib.impala": "struct Filter { float a; float b }\n\nfunction unused() { }\n",
		},
		src: [
			'import "lib.impala"',
			"",
			"extern native printInt",
			"",
			"global Filter f = { a: 2.0, b: 1.0 }",
			"",
			"export function main()",
			"{",
			"\tprintInt(ftoi((float) global f.a));",
			"}",
			"",
		].join("\n"),
	},
	{
		name: "two different bitwise operators need parentheses",
		expect: "E101", legacy: true,
		src: "export function main() locals int x { x = 1 & 2 | 3; }\n",
	},
	{
		name: "...but bitwise mixed with ARITHMETIC does not",
		expect: null,
		src: "export function main() locals int x { x = 1 + 2 & 3; }\n",
	},
	{
		name: "a bitwise expression mixed with a comparison",
		expect: "E102", legacy: true,
		src: "export function main() locals int x, int y { if (x & 1 == y) { } }\n",
	},
	{
		name: "return/break/continue are reserved",
		expect: "E449", legacy: true,
		src: "global int break\nexport function main() { }\n",
	},
	{
		name: "a struct initializer names its fields",
		expect: "E455",
		src: "struct Point { int x; int y }\nglobal Point origin = { 1, 2 }\nexport function main() { }\n",
	},
	{
		name: "a constant index past a known extent",
		expect: "E461", legacy: false,
		src: "global int array g[4]\nexport function main() locals int x { x = global g[9]; }\n",
	},
	{ name: "a funcptr type refuses the wrong signature", expect: "E441",
		src: "functype Step(int frame)\nfunction wrong(float x) { }\nglobal Step cb = wrong\n"
				+ "export function main() { }\n" },
	{
		name: "one subscript, striding by the element",
		expect: null,
		src: [
			"struct Cell { int v; int w }",
			"global Cell array grid[4]",
			"global int array flat[4]",
			"",
			"function read(int i) returns int r",
			"{",
			"\tr = global grid[i].v + global flat[i];",
			"}",
			"",
			"export function main() { }",
			"",
		].join("\n"),
	},
	{ name: "arithmetic on a struct pointer, which subscripting replaces", expect: "E307",
		src: "struct Cell { int v }\nglobal Cell array grid[4]\n"
				+ "export function main() locals Cell pointer p { p = &global grid[0]; p = p + 1; }\n" },
	{ name: "`.` through a pointer", expect: "E416",
		src: "struct S { int a }\nglobal S s\n"
				+ "export function main() locals S pointer p, int r { p = &global s; r = p.a; }\n" },
	{ name: "a const struct must be a pointer", expect: "E447",
		src: "struct S { int a }\nconst S k\nexport function main() { }\n" },
	{ name: "return takes no value", expect: "E448",
		src: "function f() returns int r { return 5; }\nexport function main() { }\n" },
	{ name: "an extern struct field states no size", expect: "E430",
		src: "extern struct H { int a; int array b[4] }\nexport function main() { }\n" },
	/* Cited by the language reference. Each was named in prose with no sample behind it until the gate
	   started covering `Impala.md` and said so. */
	{ name: "an undeclared name", expect: "E403",
		src: "export function main() locals int x { x = zzz; }\n" },
	{ name: "a unary operator on a type that has none", expect: "E302",
		src: "export function main() locals int pointer p, int r { r = -p; }\n" },
	{ name: "the value of a call that returns nothing", expect: "E406",
		src: "extern native printInt\nfunction noret(int x) { }\n"
				+ "export function main() { printInt(noret(1)); }\n" },
	{ name: "a comparison is not a value, so it cannot be an argument", expect: "E442",
		src: "extern native printInt\n"
				+ "export function main() locals int a, int b { printInt(a == b); }\n" },
	{ name: "an extern prototype must match its definition", expect: "E437",
		src: "extern function helper(int a) returns int r\n"
				+ "function helper(float a) returns float r { r = a; }\nexport function main() { }\n" },
	/* The upgrade list. Each of these is a 1.0 program that 2.0 refuses; the page is only trustworthy if
	   every row of it is executed. `legacy` records whether `--legacy` rescues it, which is the column a
	   reader actually acts on - and the six/nine split is asserted below. */
	{ name: "! on an unparenthesised operand", expect: "E103", legacy: true,
		src: "export function main() locals int a { if (!a == 0) { } }\n" },
	{ name: "`global` on a const", expect: "E452", legacy: true,
		src: "const int K = 1\nexport function main() locals int r { r = global K; }\n" },
	{ name: "an identifier named `sizeof`", expect: "E001", legacy: false,
		src: "export function main() locals int sizeof { sizeof = 1; }\n" },
	{ name: "unspaced `--a` meaning -(-a)", expect: "E001", legacy: false,
		src: "export function main() locals int a, int r { r = --a; }\n" },
	{ name: "a case value outside the switch range", expect: "E444", legacy: false,
		src: "export function main() locals int x { switch (x == 0 to 4) { case 7: x = 1; } }\n" },
	{ name: "two case arms with one value", expect: "E443", legacy: false,
		src: "export function main() locals int x "
				+ "{ switch (x == 0 to 4) { case 1: x = 1; case 1: x = 2; } }\n" },
	{ name: "the same label twice", expect: "E446", legacy: false,
		src: "export function main() locals int x { top: ; x = 1; top: ; }\n" },
	{ name: "goto a label that does not exist", expect: "E445", legacy: false,
		src: "export function main() { goto nowhere; }\n" },
	{ name: "a negative array extent", expect: "E462", legacy: false,
		src: "global int array a[-1]\nexport function main() { }\n" },
	{ name: "a return value that is never assigned", expect: "E463", legacy: false,
		src: "function f() returns int r { }\nexport function main() { }\n" },
	{ name: "writing through a readonly global", expect: "E404", legacy: false,
		src: "readonly int array a[2] = { 1, 2 }\nexport function main() { global a[0] = 5; }\n" },
	{ name: "one name used twice at top level", expect: "E401", legacy: false,
		src: "function f() { }\nfunction f() { }\nexport function main() { }\n" },
	{ name: "a struct by value is parked", expect: "E426",
		src: "struct P { int x }\nfunction f(P p) { }\nexport function main() { }\n" },
	{ name: "...and returning one, likewise", expect: "E427",
		src: "struct P { int x }\nfunction f() returns P p { }\nexport function main() { }\n" },
	{ name: "multiple return values are parked", expect: "E428",
		src: "function f() returns int a, int b { a = 1; b = 2; }\nexport function main() { }\n" },
	{ name: "destructuring is parked", expect: "E429",
		src: "function main() locals int a, int b { a, b = 1; }\n" },
	{ name: "inline function belongs to the GAZL 2 line", expect: "E439",
		src: "inline function f() { }\nexport function main() { }\n" },
	{ name: "break is reserved so it can be refused clearly", expect: "E450",
		src: "export function main() locals int x { while (x < 4) { x = x + 1; break; } }\n" },
	{ name: "...and continue likewise", expect: "E450",
		src: "export function main() locals int x { while (x < 4) { x = x + 1; continue; } }\n" },
];

let failures = 0;
function fail(what, detail) {
	console.error("doc samples: " + what + "\n  " + detail);
	++failures;
}

function compile(sample, legacy) {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), "docsamples-"));
	try {
		for (const [name, text] of Object.entries(sample.files || {})) {
			fs.writeFileSync(path.join(dir, name), text, "latin1");
		}
		const root = path.join(dir, "main.impala");
		fs.writeFileSync(root, sample.src, "latin1");
		try {
			compileProgram(root, { randomId: 0x4d2, legacy: legacy === true });
			return null;
		} catch (err) {
			return (err && err.message) || String(err);
		}
	} finally {
		try { fs.rmSync(dir, { recursive: true, force: true }); } catch (_) { /* best effort */ }
	}
}

for (const sample of SAMPLES) {
	const err = compile(sample);
	/* The upgrade page splits its breakage into "--legacy downgrades these" and "--legacy does not help",
	   and that column is the one a reader acts on - so run it rather than asserting it in prose. */
	if (sample.legacy !== undefined) {
		/* A rescued sample WARNS, and a gate that passes should print nothing, so swallow the warning
		   text - the return value is what this is asking about. */
		const wasError = console.error;
		console.error = () => {};
		const under = compile(sample, true);
		console.error = wasError;
		if (sample.legacy === true && under !== null) {
			fail("the page promises --legacy rescues this, and it does not: " + sample.name,
					under.split("\n")[0]);
		} else if (sample.legacy === false && under === null) {
			fail("the page says --legacy does NOT rescue this, but it does: " + sample.name,
					"expected " + sample.expect + " to survive --legacy");
		}
	}
	if (sample.expect === null && err !== null) {
		fail("a sample that must compile did not: " + sample.name, err.split("\n")[0]);
	} else if (sample.expect !== null) {
		if (err === null) {
			fail("a sample that must be refused compiled clean: " + sample.name,
					"expected " + sample.expect);
		} else if (err.indexOf("[" + sample.expect + "]") < 0) {
			fail("a sample was refused for the wrong reason: " + sample.name,
					"expected " + sample.expect + ", got: " + err.split("\n")[0]);
		}
	}
}

/* Side two: the page cannot show what the samples do not cover. Compared with tabs and CRLF normalised,
   because the doc and this file are edited by different hands and a whitespace difference is not drift. */
const norm = (s) => s.replace(/\r\n?/g, "\n").replace(/\t/g, "    ").trim();
const covered = SAMPLES.map((s) => norm(s.src));
const raised = new Set(SAMPLES.map((s) => s.expect).filter(Boolean));

/* Invoked with no arguments the CLI prints its usage and exits NON-ZERO, so the text arrives on the error
   rather than the return value. */
let usage = "";
try {
	usage = require("child_process").execFileSync(process.execPath,
			[path.join(__dirname, "impala.node.js")], { encoding: "utf8", stdio: "pipe" });
} catch (err) {
	usage = ((err && err.stdout) || "") + ((err && err.stderr) || "");
}

for (const entry of DOCS) {
	const name = entry.file;
	const doc = fs.readFileSync(path.join(__dirname, "..", "docs", name), "utf8");
	/* Anchored at line starts and capturing the info string, so the two kinds of block are told apart in
	   one pass. A fence-to-fence regex without that matched the gap BETWEEN two blocks and read a markdown
	   table rule as a command-line flag. */
	const blocks = [...doc.matchAll(/^```([a-z]*)\n([\s\S]*?)^```/gm)];

	for (const [, lang, body] of blocks) {
		if (lang !== "impala") {
			continue;
		}
		const block = norm(body);
		if (!covered.some((src) => src.indexOf(block) >= 0)) {
			fail(name + ": a code block is not covered by any compiled sample",
					block.split("\n")[0] + (block.includes("\n") ? " ..." : ""));
		}
	}

	/* The side that catches PROSE. Every diagnostic code the page names has to be one a sample actually
	   provoked - so a page cannot cite `E456` where the compiler says `E455`, and cannot keep citing a code
	   after its fail site is deleted. That exact defect (E-codes documented with no fail site anywhere in
	   the compiler) is what the release audit found in three separate docs. */
	for (const code of new Set(doc.match(/\bE[0-9]{3}\b/g) || [])) {
		if (!raised.has(code)) {
			fail(name + ": names a diagnostic no sample provokes", code
					+ " - add a sample that triggers it, or stop citing it");
		}
	}

	/* Shell blocks: every `--flag` and subcommand they name must be one the CLI actually documents. */
	for (const [, lang, body] of blocks) {
		if (lang !== "") {
			continue;
		}
		for (const flag of (body.match(/--[a-z-]+/g) || [])) {
			if (usage.indexOf(flag) < 0) {
				fail(name + ": a command names a flag the CLI does not have", flag);
			}
		}
		const sub = /impala\.node\.js\s+([a-z]+)/.exec(body);
		if (sub !== null && usage.indexOf("impala.node.js " + sub[1]) < 0) {
			fail(name + ": a command names a subcommand the CLI does not have", sub[1]);
		}
	}

	/* Only the upgrade page sorts breakage into "--legacy downgrades these" and "--legacy does not help",
	   and a reader acts on WHICH TABLE a row is in. The code check above proves the facts are right; this
	   proves the page files them in the right place. */
	if (entry.upgradeTables !== true) {
		continue;
	}
	const section = (heading) => {
		const at = doc.indexOf(heading);
		if (at < 0) {
			fail(name + ": the upgrade section lost a heading", heading);
			return new Set();
		}
		const next = doc.indexOf("\n### ", at + 1);
		return new Set(doc.slice(at, next < 0 ? undefined : next).match(/\bE[0-9]{3}\b/g) || []);
	};
	const saysRescued = section("### `--legacy` downgrades these to warnings");
	const saysNot = section("### `--legacy` does not help with these");
	for (const sample of SAMPLES) {
		if (sample.legacy === undefined) {
			continue;
		}
		/* An upgrade-breakage row claims "1.0 accepted this, 2.0 does not". A sample that DECLARES a
		   struct or a functype, or imports a unit, cannot be 1.0 source at all - 1.0 has none of those
		   constructs - so the row is impossible by construction and belongs somewhere else on the page.
		   This exists because `E455` (a positional struct initializer) was filed as upgrade breakage
		   twice, the second time in the same edit that measured the 1.0 corpus and found no E455 in it.
		   Note the test is on DECLARING: using `struct` or `sizeof` as an identifier is exactly what a
		   1.0 program did, and is what several of these rows are about. */
		const impossible = /\bstruct\s+\w+\s*\{|\bfunctype\s+\w+\s*\(|\bimport\s+"/.exec(sample.src);
		if (impossible !== null) {
			fail(name + ": an upgrade-breakage row cannot happen in 1.0 source",
					sample.expect + " (" + sample.name + ") - its sample uses `"
							+ impossible[0].trim() + "`, which 1.0 cannot express");
		}
		const listedRescued = saysRescued.has(sample.expect);
		const listedNot = saysNot.has(sample.expect);
		if (sample.legacy === true && listedNot && !listedRescued) {
			fail(name + ": a rescuable case is filed under \"--legacy does not help\"", sample.expect);
		} else if (sample.legacy === false && listedRescued && !listedNot) {
			fail(name + ": an unrescuable case is filed under \"--legacy downgrades these\"", sample.expect);
		} else if (!listedRescued && !listedNot) {
			fail(name + ": a verified breakage case is in neither upgrade table",
					sample.expect + " (" + sample.name + ")");
		}
	}
}

if (failures > 0) {
	console.error("doc samples: " + failures + " problem(s)");
	process.exit(1);
}
console.log(DOCS.map((d) => d.file).join(" + ") + ": " + SAMPLES.length
		+ " samples compile as documented, every code block is covered");
