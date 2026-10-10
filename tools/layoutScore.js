#!/usr/bin/env node
/*
	layoutScore.js - rank the dispatch case orders measured by tools/layoutSearch.js.

	Usage: node tools/layoutScore.js <results.csv> [more.csv ...] [--top=10]

	For each platform and kernel the baseline is the median cpu_median of all seed-0 entries. A seed's speedup on a
	(platform, kernel) is baseline / its own median cpu_median there, and its score is the geometric mean of those
	speedups over every kernel and platform it was measured on (> 1 is faster than seed 0). Seeds missing from some
	platform are scored on what they have and flagged. Seed 0's own entries show the drift over the run.
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

const median = (xs) => {
	const s = xs.slice().sort((a, b) => a - b);
	const m = s.length >> 1;
	return s.length % 2 ? s[m] : (s[m - 1] + s[m]) / 2;
};
const geomean = (xs) => Math.exp(xs.reduce((sum, x) => sum + Math.log(x), 0) / xs.length);

// samples[platform][seed][kernel] = [cpu_median, ...]; invalid[seed] = [platform, ...]
const samples = {};
const invalid = {};
for (const file of files) {
	const rows = fs.readFileSync(file, "latin1").split(/\r?\n/).slice(1).filter(Boolean);
	for (const row of rows) {
		const [platform, seed, kernel, cpu] = row.split(",");
		if (kernel === "#invalid") {
			(invalid[seed] = invalid[seed] || []).push(platform);
			continue;
		}
		const bySeed = (samples[platform] = samples[platform] || {});
		const byKernel = (bySeed[seed] = bySeed[seed] || {});
		(byKernel[kernel] = byKernel[kernel] || []).push(Number(cpu));
	}
}

const platforms = Object.keys(samples).sort();
for (const p of platforms) {
	if (!samples[p]["0"]) {
		process.stderr.write("layoutScore: no seed-0 baseline for " + p + "\n");
		process.exit(1);
	}
}

// Speedups per seed, per platform.
const scores = new Map();
for (const p of platforms) {
	const base = samples[p]["0"];
	for (const seed of Object.keys(samples[p])) {
		const speedups = [];
		for (const kernel of Object.keys(base)) {
			const own = samples[p][seed][kernel];
			if (own) speedups.push(median(base[kernel]) / median(own));
		}
		if (!scores.has(seed)) scores.set(seed, { all: [], perPlatform: {} });
		const s = scores.get(seed);
		s.all.push(...speedups);
		s.perPlatform[p] = geomean(speedups);
	}
}

// Drift: each seed-0 entry against the seed-0 baseline, per platform.
for (const p of platforms) {
	const base = samples[p]["0"];
	const kernels = Object.keys(base);
	const entries = Math.min(...kernels.map((k) => base[k].length));
	const drift = [];
	for (let e = 0; e < entries; ++e) drift.push(geomean(kernels.map((k) => median(base[k]) / base[k][e])));
	const pct = (x) => ((x - 1) * 100).toFixed(2) + "%";
	process.stdout.write(
		p +
			": " +
			(Object.keys(samples[p]).length - 1) +
			" seeds; seed-0 drift over " +
			entries +
			" entries: " +
			pct(Math.min(...drift)) +
			" .. " +
			pct(Math.max(...drift)) +
			"\n",
	);
}
const badSeeds = Object.keys(invalid);
if (badSeeds.length) process.stdout.write("INVALID (results differ from seed 0): seeds " + badSeeds.join(", ") + "\n");

const ranked = [...scores.entries()]
	.filter(([seed]) => seed !== "0")
	.map(([seed, s]) => ({ seed, score: geomean(s.all), perPlatform: s.perPlatform }))
	.sort((a, b) => b.score - a.score);
process.stdout.write("\nrank  seed        score  " + platforms.map((p) => p.padStart(16)).join("") + "\n");
ranked.slice(0, top).forEach((r, i) => {
	const cols = platforms.map((p) => (r.perPlatform[p] === undefined ? "missing" : r.perPlatform[p].toFixed(4)).padStart(16)).join("");
	process.stdout.write(String(i + 1).padStart(4) + "  " + r.seed.padEnd(10) + r.score.toFixed(4).padStart(7) + "  " + cols + "\n");
});
