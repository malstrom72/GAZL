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
	it is. It never pushes `main`, and never force-pushes a long-lived branch.

	--to <branch> ends the chain at <branch>, for both modes. While work on a lower branch is on hold (2026-10-12:
	GAZL2 and jit-compiler), run it with `--to Impala2`, or it will merge into the branches that are on hold.

	Needs git and an authenticated `gh`. Runs no local build; CI runs build.sh / build.cmd on every pull request.

	Usage: node tools/cascade.js [--merge] [--to <branch>] [-m "what changed"]
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

function gh(...args) {
	const result = run("gh", args);
	if (result.status !== 0) fail(`gh ${args.join(" ")} failed:\n${result.stderr}`);
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
let last = CASCADE.length - 1;
for (let i = 0; i < args.length; ++i) {
	if (args[i] === "--merge") doMerge = true;
	else if (args[i] === "-m" && i + 1 < args.length) what = args[++i];
	else if (args[i] === "--to" && i + 1 < args.length) {
		last = CASCADE.indexOf(args[++i]);
		if (last < 1) fail(`--to takes one of ${CASCADE.slice(1).join(", ")}`);
	} else fail(`unknown argument '${args[i]}' (usage: cascade [--merge] [--to <branch>] [-m "what changed"])`);
}

git("fetch", "--quiet", "--prune", "origin");

function behind(upper, lower) {
	return Number(git("rev-list", "--count", `origin/${lower}..origin/${upper}`));
}

let anyBehind = false;
for (let i = 1; i <= last; ++i) {
	const n = behind(CASCADE[i - 1], CASCADE[i]);
	console.log(`${CASCADE[i - 1]} -> ${CASCADE[i]}: ${n === 0 ? "merged" : `${n} commit(s) to merge`}`);
	if (n !== 0) anyBehind = true;
}
if (!doMerge) process.exit(anyBehind ? 1 : 0);

if (git("status", "--porcelain", "--untracked-files=no") !== "") fail("the working tree has changes; commit or stash them first");
if (run("gh", ["auth", "status"]).status !== 0) fail("gh is not authenticated (run `gh auth login`)");
const startBranch = git("rev-parse", "--abbrev-ref", "HEAD");

for (let i = 1; i <= last; ++i) {
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

	const sha = git("rev-parse", work);
	git("push", "--quiet", "--force-with-lease", "-u", "origin", work);

	// Only an OPEN pull request counts: a merged one from an earlier cascade has the same head branch name, and its
	// old passing checks say nothing about this merge.
	const openPr = () => gh("pr", "list", "--head", work, "--base", lower, "--state", "open", "--json", "number", "-q", ".[0].number");
	let pr = openPr();
	if (pr === "") {
		console.log(gh("pr", "create", "--base", lower, "--head", work, "--title", title, "--body", `Cascade of ${upper}.`));
		pr = openPr();
		if (pr === "") fail(`cannot find the pull request just opened for ${work}`);
	}

	// Checks on THIS commit only, and ALL of them. `gh pr checks --watch` returns once the checks registered so far
	// are done, and GitHub registers them as the run starts, so a watch begun early can cover only some. Accept the
	// set only when every check has completed and two reads 20 seconds apart count the same number.
	const rollup = () => {
		const view = JSON.parse(gh("pr", "view", pr, "--json", "headRefOid,statusCheckRollup"));
		return view.headRefOid === sha ? view.statusCheckRollup : [];
	};
	let settled = -1;
	for (let tries = 0; ; ++tries) {
		if (tries === 60) fail(`the CI checks on pull request #${pr} did not settle`);
		run("gh", ["pr", "checks", pr, "--watch", "--interval", "30"], { stdio: "inherit" });
		run(process.execPath, ["-e", "setTimeout(() => {}, 20000)"]);
		const seen = rollup();
		const done = seen.length > 0 && seen.every((c) => c.status === "COMPLETED");
		if (done && seen.length === settled) break;
		settled = (done ? seen.length : -1);
	}
	const checks = rollup();
	if (checks.length === 0 || !checks.every((c) => c.conclusion === "SUCCESS")) {
		fail(`CI did not pass on pull request #${pr}; fix it on ${work} and run this again`);
	}

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
