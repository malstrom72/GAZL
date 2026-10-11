#!/usr/bin/env node
/*
	layoutScore.js - rank the dispatch case orders measured by tools/layoutSearch.js.

	Usage: node tools/layoutScore.js <results.csv> [more.csv ...] [--top=10]

	Machines drift during a run (the first Mac run's opening block ran ~5% faster than the rest), so a seed is compared
	with its own neighborhood, not one global baseline: per kernel, its baseline is the cpu_median of the seed-0 entries
	just before and after it in the same CSV, interpolated geometrically by list position. A seed's speedup on a
	(platform, kernel) is baseline / its cpu_median (averaged in log space over repeats), and its score is the geometric
	mean over every kernel and platform it was measured on (> 1 is faster than seed 0). Seeds missing from a platform are
	scored on what they have. The spread between adjacent seed-0 entries is printed as the noise floor to judge scores by.
*/
"use strict";

const fs = require("fs");

const argv = process.argv.slice(2);
const files = argv.filter((a) => !a.startsWith("--"));
const topArg = argv.find((a) => a.startsWith("--top="));
const top = topArg ? Number(topArg.slice(6)) : 10;
if (!files.length) {
	process.stderr.write("usage: node tools/layoutScore.js <results.csv> [more.csv ...] [--top=10]\n");
	process.exit(1);
}

const geomean = (xs) => Math.exp(xs.reduce((sum, x) => sum + Math.log(x), 0) / xs.length);
const pct = (x) => ((x - 1) * 100).toFixed(2) + "%";

// Entries in run order per (file, platform): one entry = one seed's rows, { seed, cpu: Map(kernel -> cpu_median) }.
const runs = [];
const invalid = {};
for (const file of files) {
	const byPlatform = new Map();
	for (const row of fs.readFileSync(file, "latin1").split(/\r?\n/).slice(1).filter(Boolean)) {
		const [platform, seed, kernel, cpu] = row.split(",");
		if (kernel === "#invalid") {
			(invalid[seed] = invalid[seed] || []).push(platform);
			continue;
		}
		if (!byPlatform.has(platform)) byPlatform.set(platform, []);
		const entries = byPlatform.get(platform);
		let last = entries[entries.length - 1];
		if (!last || last.seed !== seed || last.cpu.has(kernel)) entries.push((last = { seed, cpu: new Map() }));
		last.cpu.set(kernel, Number(cpu));
	}
	for (const [platform, entries] of byPlatform) runs.push({ file, platform, entries });
}

// logSpeedups[seed][platform] = [log speedup per kernel per repeat]
const logSpeedups = {};
const platforms = [...new Set(runs.map((r) => r.platform))].sort();
for (const { file, platform, entries } of runs) {
	const zeros = entries.map((e, i) => (e.seed === "0" ? i : -1)).filter((i) => i >= 0);
	if (!zeros.length) {
		process.stderr.write("layoutScore: no seed-0 entries for " + platform + " in " + file + "\n");
		process.exit(1);
	}
	const kernels = [...entries[zeros[0]].cpu.keys()];
	const adjacent = [];
	for (let j = 1; j < zeros.length; ++j) {
		adjacent.push(geomean(kernels.map((k) => entries[zeros[j - 1]].cpu.get(k) / entries[zeros[j]].cpu.get(k))));
	}
	process.stdout.write(
		platform +
			" (" +
			file +
			"): " +
			(entries.length - zeros.length) +
			" seed entries, " +
			zeros.length +
			" seed-0 entries; " +
			"adjacent seed-0 ratios " +
			(adjacent.length ? pct(Math.min(...adjacent)) + " .. " + pct(Math.max(...adjacent)) : "n/a") +
			"\n",
	);
	entries.forEach((e, i) => {
		if (e.seed === "0") return;
		const before = zeros.filter((z) => z < i);
		const after = zeros.filter((z) => z > i);
		const prev = before.length ? before[before.length - 1] : after[0];
		const next = after.length ? after[0] : prev;
		const t = next === prev ? 0 : (i - prev) / (next - prev);
		const logs = [];
		for (const k of kernels) {
			if (!e.cpu.has(k)) continue;
			const base = (1 - t) * Math.log(entries[prev].cpu.get(k)) + t * Math.log(entries[next].cpu.get(k));
			logs.push(base - Math.log(e.cpu.get(k)));
		}
		const bySeed = (logSpeedups[e.seed] = logSpeedups[e.seed] || {});
		(bySeed[platform] = bySeed[platform] || []).push(...logs);
	});
}
const badSeeds = Object.keys(invalid);
if (badSeeds.length) process.stdout.write("INVALID (results differ from seed 0): seeds " + badSeeds.join(", ") + "\n");

const mean = (xs) => xs.reduce((a, b) => a + b, 0) / xs.length;
const ranked = Object.keys(logSpeedups)
	.map((seed) => {
		const per = logSpeedups[seed];
		const all = Object.values(per).flat();
		const perPlatform = {};
		for (const p of Object.keys(per)) perPlatform[p] = Math.exp(mean(per[p]));
		return { seed, score: Math.exp(mean(all)), perPlatform };
	})
	.sort((a, b) => b.score - a.score);
process.stdout.write("\nrank  seed        score  " + platforms.map((p) => p.padStart(16)).join("") + "\n");
ranked.slice(0, top).forEach((r, i) => {
	const cols = platforms.map((p) => (r.perPlatform[p] === undefined ? "missing" : r.perPlatform[p].toFixed(4)).padStart(16)).join("");
	process.stdout.write(String(i + 1).padStart(4) + "  " + r.seed.padEnd(10) + r.score.toFixed(4).padStart(7) + "  " + cols + "\n");
});
