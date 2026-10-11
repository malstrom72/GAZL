#!/usr/bin/env node
/*
	Keep the files GAZL shares with other repositories in step across the four long-lived branches.

	The shared files are design/CodingStyle.md, design/fuzzing.md and tools/BuildCpp.sh / .cmd. The two .md files
	are shared above their "## Local additions" heading, and each repo keeps its own section from there down;
	BuildCpp is shared whole. Each file's version line names its release. Line endings are ignored.

	Without options it checks: on every branch in the cascade (as on origin, after a fetch), each shared file must
	have the same shared part, and so the same version line, as on `main`. Exits 1 on any difference.

	With --apply <file> it installs a new release of one shared file into the working tree, chosen by its name: the
	shared part comes from <file>, the "## Local additions" section stays GAZL's own, and the line endings stay as
	they were. Do this on `main`, open a pull request, then carry it down with tools/cascade.js; check again after.

	Usage: node tools/sharedDocs.js [--apply <file>]
*/
"use strict";

const { spawnSync } = require("child_process");
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..");
const CASCADE = ["main", "Impala2", "GAZL2", "jit-compiler"];
const SHARED = ["design/CodingStyle.md", "design/fuzzing.md", "tools/BuildCpp.sh", "tools/BuildCpp.cmd"];
const LOCAL_HEADING = "## Local additions";

function fail(message) {
	console.error(`sharedDocs: ${message}`);
	process.exit(1);
}

function git(...args) {
	const result = spawnSync("git", args, { encoding: "utf8", cwd: ROOT, maxBuffer: 64 << 20 });
	if (result.error) throw result.error;
	if (result.status !== 0) fail(`git ${args.join(" ")} failed:\n${result.stderr}`);
	return result.stdout;
}

// Splits `text` (LF only) into the shared part and the local part (empty for a file shared whole).
function split(file, text) {
	if (!file.endsWith(".md")) return { shared: text, local: "" };
	const at = text.search(new RegExp(`^${LOCAL_HEADING}$`, "m"));
	if (at < 0) fail(`${file} has no "${LOCAL_HEADING}" heading`);
	return { shared: text.slice(0, at), local: text.slice(at) };
}

function versionLine(text) {
	const line = text
		.split("\n")
		.slice(0, 5)
		.find((l) => /version/i.test(l));
	return line !== undefined ? line.trim() : "(no version line)";
}

const lf = (text) => text.replace(/\r\n/g, "\n");

const args = process.argv.slice(2);
if (args.length === 2 && args[0] === "--apply") {
	const source = path.resolve(args[1]);
	const file = SHARED.find((f) => path.basename(f) === path.basename(source));
	if (file === undefined) fail(`${path.basename(source)} is not a shared file (${SHARED.join(", ")})`);
	const target = path.join(ROOT, file);
	const old = fs.readFileSync(target, "utf8");
	const incoming = split(file, lf(fs.readFileSync(source, "utf8")));
	let text = incoming.shared + split(file, lf(old)).local;
	if (old.includes("\r\n")) text = text.replace(/\n/g, "\r\n");
	fs.writeFileSync(target, text);
	console.log(`sharedDocs: ${file}: ${versionLine(lf(old))} -> ${versionLine(incoming.shared)}`);
	process.exit(0);
}
if (args.length !== 0) fail("usage: node tools/sharedDocs.js [--apply <file>]");

git("fetch", "--quiet", "origin");
let differences = 0;
for (const file of SHARED) {
	const reference = split(file, lf(git("show", `origin/main:${file}`))).shared;
	for (const branch of CASCADE) {
		const shared = split(file, lf(git("show", `origin/${branch}:${file}`))).shared;
		const same = shared === reference;
		if (!same) ++differences;
		console.log(`${same ? "ok  " : "DIFF"} ${branch.padEnd(12)} ${file}: ${versionLine(shared)}`);
	}
}
if (differences !== 0) fail(`${differences} shared part(s) differ from main's`);
console.log("sharedDocs: every branch has main's shared parts");
