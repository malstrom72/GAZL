#!/usr/bin/env node
/*
	permuteDispatch.js - reorder the `case` groups of the dispatch switch in `Processor::run` (src/GAZL.cpp) with a seeded
	PRNG, for the dispatch-layout search (tools/layoutSearch.js). Output is byte-identical on every platform for a given
	seed and input, and seed 0 writes the input unchanged.

	Usage: node tools/permuteDispatch.js <seed> <in GAZL.cpp> <out GAZL.cpp>

	The switch body is split into items, one per `case`/`default`/label line at the switch's case indentation, each
	owning its deeper-indented continuation lines and any comment lines just above it. Items are then merged into groups
	that must stay together, in their original order: an item that can fall through joins the item after it, and a
	labelled block (such as `call:` or `copy:`) joins every item that jumps into it. `default:` stays last. Only the
	order of whole groups changes, so the permuted switch holds exactly the same lines.
*/
"use strict";

const fs = require("fs");

function fail(message) {
	process.stderr.write("permuteDispatch: " + message + "\n");
	process.exit(1);
}

// mulberry32: small, well-mixed 32-bit PRNG; uses only 32-bit integer ops, so it is identical on every platform.
function makeRandom(seed) {
	let a = seed >>> 0;
	return function () {
		a = (a + 0x6d2b79f5) >>> 0;
		let t = a;
		t = Math.imul(t ^ (t >>> 15), t | 1);
		t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

const leadingTabs = (line) => /^\t*/.exec(line)[0].length;
const stripComments = (text) => text.replace(/\/\/[^\n]*/g, "").replace(/\/\*[\s\S]*?\*\//g, "");

// True if control can leave the item's last statement and run into the next item.
function canFallThrough(item) {
	const code = stripComments(item.lines.join("\n")).replace(/^\s*(case\s+\w+\s*:|default\s*:|\w+\s*:)/, "");
	const tail = code.replace(/[\s;{}]+$/, "");
	return !/(\bbreak|\bcontinue|\bgoto\s+\w+|\breturn\b[^;]*)$/.test(tail);
}

function permute(source, seed) {
	const newline = source.includes("\r\n") ? "\r\n" : "\n";
	const lines = source.split(newline);
	const runAt = lines.findIndex((l) => /^Int Processor::run\(\)\s*\{/.test(l));
	if (runAt < 0) fail("Processor::run() not found");
	const switchAt = lines.findIndex((l, i) => i > runAt && /switch\s*\(ip->opcode\)\s*\{/.test(l));
	if (switchAt < 0) fail("dispatch switch not found");
	const switchIndent = leadingTabs(lines[switchAt]);
	const caseIndent = switchIndent + 1;
	const endAt = lines.findIndex((l, i) => i > switchAt && leadingTabs(l) === switchIndent && /^\t*\}/.test(l));
	if (endAt < 0) fail("end of dispatch switch not found");

	// Split the body into items.
	const items = [];
	let pending = [];
	for (let i = switchAt + 1; i < endAt; ++i) {
		const line = lines[i];
		const atCaseLevel = leadingTabs(line) === caseIndent;
		const body = line.slice(caseIndent);
		if (atCaseLevel && /^(case\s+\w+\s*:|default\s*:|[A-Za-z_]\w*\s*:(?!:))/.test(body)) {
			const label = /^([A-Za-z_]\w*)\s*:/.exec(body);
			items.push({
				lines: pending.concat([line]),
				label: label && label[1] !== "case" && label[1] !== "default" ? label[1] : null,
				isDefault: /^default\s*:/.test(body),
			});
			pending = [];
		} else if (atCaseLevel && /^\/\//.test(body)) {
			pending.push(line); // comment above the next item belongs to it
		} else if (items.length === 0) {
			fail("unexpected line before the first case: " + JSON.stringify(line));
		} else {
			items[items.length - 1].lines.push(...pending, line);
			pending = [];
		}
	}
	if (pending.length) fail("trailing comment lines after the last case");
	if (!items.length || !items[items.length - 1].isDefault) fail("expected `default:` as the last item");

	// Union items that must stay together.
	const parent = items.map((_, i) => i);
	const find = (i) => (parent[i] === i ? i : (parent[i] = find(parent[i])));
	const unite = (a, b) => {
		parent[find(b)] = find(a);
	};
	const labelAt = new Map();
	items.forEach((item, i) => item.label && labelAt.set(item.label, i));
	items.forEach((item, i) => {
		if (i + 1 < items.length && !item.isDefault && canFallThrough(item)) unite(i, i + 1);
		const code = stripComments(item.lines.join("\n"));
		for (const m of code.matchAll(/\bgoto\s+(\w+)\s*;/g)) {
			if (labelAt.has(m[1])) unite(labelAt.get(m[1]), i);
			else if (m[1] !== "ret") fail("goto to unknown label `" + m[1] + "`");
		}
	});

	// Groups keep their items' original order; a group is placed where its first item was.
	const groupOf = new Map();
	const groups = [];
	items.forEach((item, i) => {
		const root = find(i);
		if (!groupOf.has(root)) {
			groupOf.set(root, groups.length);
			groups.push([]);
		}
		groups[groupOf.get(root)].push(i);
	});
	for (const g of groups) {
		for (let k = 1; k < g.length; ++k) {
			if (g[k] !== g[k - 1] + 1) fail("a group is not contiguous in the source: items " + g.join(","));
		}
	}
	const defaultGroup = groups.findIndex((g) => g.some((i) => items[i].isDefault));
	if (defaultGroup !== groups.length - 1) fail("`default:` must be in the last group");

	const movable = groups.slice(0, -1);
	if (seed !== 0) {
		const random = makeRandom(seed);
		for (let i = movable.length - 1; i > 0; --i) {
			const j = Math.floor(random() * (i + 1));
			[movable[i], movable[j]] = [movable[j], movable[i]];
		}
	}
	const ordered = movable.concat([groups[groups.length - 1]]);
	const body = [];
	for (const g of ordered) for (const i of g) body.push(...items[i].lines);
	return {
		text: lines
			.slice(0, switchAt + 1)
			.concat(body, lines.slice(endAt))
			.join(newline),
		groupCount: groups.length,
	};
}

const args = process.argv.slice(2);
if (args.length !== 3 || !/^\d+$/.test(args[0])) {
	fail("usage: node tools/permuteDispatch.js <seed> <in GAZL.cpp> <out GAZL.cpp>");
}
const seed = Number(args[0]);
if (!Number.isSafeInteger(seed) || seed > 0xffffffff) fail("seed must be 0 to 4294967295");
const source = fs.readFileSync(args[1], "latin1");
const result = permute(source, seed);
if (seed === 0 && result.text !== source) fail("internal error: seed 0 did not reproduce the input");
fs.writeFileSync(args[2], result.text, "latin1");
