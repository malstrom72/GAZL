#!/usr/bin/env node
/*
	Check, or carry out, the branch cascade main -> Impala2 -> GAZL2 -> jit-compiler (design/README.md, "Branches").

	Without options it only reports: for each step it fetches and says whether the upper branch is already merged
	into the lower one, and exits 1 if any step is behind.

	With --merge it brings every step that is behind up to date, one step at a time, top to bottom:

	  1. merge origin/<upper> into a branch `cascade-<lower>` made from origin/<lower>
	  2. push it, open a pull request against <lower>, and wait for CI
	  3. fast-forward <lower> to it (a plain push, so it fails rather than overwrite anything new on <lower>)
	  4. delete `cascade-<lower>`, locally and on origin

	It stops at the first conflict or failed check. After resolving a conflict, commit the merge on
	`cascade-<lower>` and run it again: a local `cascade-<lower>` that already contains origin/<upper> is pushed as
	it is. It never pushes `main`, and never force-pushes.

	Needs git and an authenticated `gh`. Runs no local build; CI runs build.sh / build.cmd on every pull request.

	Usage: node tools/cascade.js [--merge] [-m "what changed"]
	       (merge commits read "Merge <upper>: what changed", or "Merge <upper>" without -m)
*/
"use strict";

const { spawnSync } = require("child_process");
const path = require("path");

const CASCADE = ["main", "Impala2", "GAZL2", "jit-compiler"];

function run(command, args, options) {
	const result = spawnSync(command, args, Object.assign({ encoding: "utf8", cwd: ROOT }, options));
	if (result.error) throw result.error;
	return result;
}

function git(...args) {
	const result = run("git", args);
	if (result.status !== 0) fail(`git ${args.join(" ")} failed:\n${result.stderr}`);
	return result.stdout.trim();
}

function gitOk(...args) {
	return run("git", args).status === 0;
}

function fail(message) {
	console.error(`cascade: ${message}`);
	process.exit(1);
}

const ROOT = path.join(__dirname, "..");

const args = process.argv.slice(2);
let doMerge = false;
let what = "";
for (let i = 0; i < args.length; ++i) {
	if (args[i] === "--merge") doMerge = true;
	else if (args[i] === "-m" && i + 1 < args.length) what = args[++i];
	else fail(`unknown argument '${args[i]}' (usage: cascade [--merge] [-m "what changed"])`);
}

git("fetch", "--quiet", "--prune", "origin");

function behind(upper, lower) {
	return Number(git("rev-list", "--count", `origin/${lower}..origin/${upper}`));
}

let anyBehind = false;
for (let i = 1; i < CASCADE.length; ++i) {
	const n = behind(CASCADE[i - 1], CASCADE[i]);
	console.log(`${CASCADE[i - 1]} -> ${CASCADE[i]}: ${n === 0 ? "merged" : `${n} commit(s) to merge`}`);
	if (n !== 0) anyBehind = true;
}
if (!doMerge) process.exit(anyBehind ? 1 : 0);

if (git("status", "--porcelain", "--untracked-files=no") !== "") fail("the working tree has changes; commit or stash them first");
if (run("gh", ["auth", "status"]).status !== 0) fail("gh is not authenticated (run `gh auth login`)");
const startBranch = git("rev-parse", "--abbrev-ref", "HEAD");

for (let i = 1; i < CASCADE.length; ++i) {
	const upper = CASCADE[i - 1];
	const lower = CASCADE[i];
	if (behind(upper, lower) === 0) continue;
	const work = `cascade-${lower}`;
	const title = what !== "" ? `Merge ${upper}: ${what}` : `Merge ${upper}`;
	console.log(`\ncascade: ${upper} -> ${lower}`);

	if (gitOk("rev-parse", "--verify", "--quiet", `refs/heads/${work}`) && gitOk("merge-base", "--is-ancestor", `origin/${upper}`, work)) {
		console.log(`cascade: using the existing ${work}`);
		git("checkout", "--quiet", work);
	} else {
		git("checkout", "--quiet", "-B", work, `origin/${lower}`);
		const merge = run("git", ["merge", "--no-edit", "-m", title, `origin/${upper}`], { stdio: "inherit" });
		if (merge.status !== 0) {
			fail(`conflict merging ${upper} into ${lower}. Resolve it, commit on ${work}, then run this again.`);
		}
	}
	if (!gitOk("merge-base", "--is-ancestor", `origin/${lower}`, work)) fail(`${work} does not contain origin/${lower}`);

	git("push", "--quiet", "--force-with-lease", "-u", "origin", work);
	if (run("gh", ["pr", "view", work, "--json", "state"]).status !== 0) {
		const pr = run("gh", ["pr", "create", "--base", lower, "--head", work, "--title", title, "--body", `Cascade of ${upper}.`]);
		if (pr.status !== 0) fail(`gh pr create failed:\n${pr.stderr}`);
		console.log(pr.stdout.trim());
	}

	// `gh pr checks --watch` can return before the checks are registered, so wait until there is something to watch.
	for (let tries = 0; run("gh", ["pr", "checks", work]).stdout.trim() === ""; ++tries) {
		if (tries === 30) fail(`no CI checks appeared for ${work}`);
		run(process.execPath, ["-e", "setTimeout(() => {}, 10000)"]);
	}
	const checks = run("gh", ["pr", "checks", work, "--watch", "--interval", "30"], { stdio: "inherit" });
	if (checks.status !== 0) fail(`CI failed on ${work}; fix it there and run this again`);

	git("push", "--quiet", "origin", `${work}:refs/heads/${lower}`);
	git("fetch", "--quiet", "origin");
	if (!gitOk("merge-base", "--is-ancestor", `origin/${upper}`, `origin/${lower}`)) fail(`${lower} still lacks ${upper}`);
	git("checkout", "--quiet", startBranch);
	git("push", "--quiet", "origin", "--delete", work);
	git("branch", "--quiet", "-D", work);
	if (startBranch !== lower && gitOk("rev-parse", "--verify", "--quiet", `refs/heads/${lower}`)) {
		git("branch", "--quiet", "-f", lower, `origin/${lower}`);
	}
	console.log(`cascade: ${lower} now contains ${upper}`);
}
console.log("\ncascade: every branch contains the one above it");
