#!/usr/bin/env node
/*
	layoutSearch.js - benchmark dispatch-switch case orders produced by tools/permuteDispatch.js, so the same seed list can
	run on every platform with no link between the machines. Called by tools/layoutSearch.sh / .cmd.

	Usage: node tools/layoutSearch.js <first seed> <last seed> <out.csv> [--drift=20] [--pin=N]

	The seed list is seed 0, then first..last with seed 0 again after every `drift` seeds and at the end (seed 0 is the
	unchanged source: the baseline, and a drift check over the run). For each entry: permute src/GAZL.cpp into
	output/layoutSearch/, build release GAZLCmd from it, check that it still computes the same results as the first
	seed 0 (every Permut8 firmware checksum in tests/impala/golden and every kernel's plain-run output), then time each
	kernel with `--bench=5 --warmup=2` and append `platform,seed,kernel,cpu_median,median_ms` rows to the CSV. A seed
	that fails the check gets one `#invalid` row instead. Windows passes `--pin=4` (override with --pin) and reports
	cpu_median_Mcyc; elsewhere there is no pinning and cpu_median_ms. Re-running with the same arguments resumes: list
	entries already complete in the CSV are skipped.
*/
"use strict";

const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");

const ROOT = path.resolve(__dirname, "..");
const WORK = path.join(ROOT, "output", "layoutSearch");
const WINDOWS = process.platform === "win32";
const PLATFORM = (WINDOWS ? "windows" : process.platform === "darwin" ? "macos" : process.platform) + "-" + process.arch;
const EXE = path.join(WORK, WINDOWS ? "GAZLCmd.exe" : "GAZLCmd");
const BENCH_ARGS = ["--bench=5", "--warmup=2"];

function fail(message) {
	process.stderr.write("layoutSearch: " + message + "\n");
	process.exit(1);
}

function run(command, args, options) {
	return childProcess.spawnSync(command, args, Object.assign({ encoding: "latin1", maxBuffer: 1 << 26 }, options));
}

function parseArgs(argv) {
	const positional = argv.filter((a) => !a.startsWith("--"));
	const option = (name, fallback) => {
		const a = argv.find((x) => x.startsWith("--" + name + "="));
		return a === undefined ? fallback : a.slice(name.length + 3);
	};
	if (positional.length !== 3 || !/^\d+$/.test(positional[0]) || !/^\d+$/.test(positional[1])) {
		fail("usage: node tools/layoutSearch.js <first seed> <last seed> <out.csv> [--drift=20] [--pin=N]");
	}
	const drift = Number(option("drift", "20"));
	if (!(drift >= 1)) fail("--drift must be at least 1");
	const pin = option("pin", WINDOWS ? "4" : "");
	const csv = path.resolve(process.env.LAYOUT_SEARCH_CWD || process.cwd(), positional[2]); // relative to the wrapper's caller
	return { first: Number(positional[0]), last: Number(positional[1]), csv, drift, pin };
}

function seedList(first, last, drift) {
	const list = [0];
	let sinceDrift = 0;
	for (let seed = first; seed <= last; ++seed) {
		if (seed === 0) continue;
		list.push(seed);
		if (++sinceDrift === drift) {
			list.push(0);
			sinceDrift = 0;
		}
	}
	if (list[list.length - 1] !== 0) list.push(0);
	return list;
}

function kernels() {
	const bench = path.join("tests", "bench", "golden");
	const ops = fs
		.readdirSync(path.join(ROOT, bench))
		.filter((f) => /^op_.*\.gazl$/.test(f))
		.sort();
	return ops
		.map((f) => path.join(bench, f))
		.concat([path.join("tests", "impala", "golden", "perfTest1.gazl"), path.join("tests", "impala", "golden", "perfTest2.gazl")]);
}

// Permut8 firmwares wrapped by the pure-GAZL host harness, as tools/runPermut8Firmware.sh does.
function firmwares() {
	const dir = path.join(ROOT, "tests", "impala", "golden");
	const out = path.join(WORK, "p8");
	fs.mkdirSync(out, { recursive: true });
	const list = [];
	for (const f of fs
		.readdirSync(dir)
		.filter((x) => x.endsWith(".gazl"))
		.sort()) {
		const source = fs.readFileSync(path.join(dir, f), "latin1");
		const wrapped = path.join(out, f);
		if (!fs.existsSync(wrapped)) {
			const r = run("node", [path.join(ROOT, "tools", "permut8Host.js"), path.join(dir, f), wrapped], { cwd: ROOT });
			if (r.status !== 0) continue; // not a firmware the harness can wrap
		}
		const args = [wrapped, "hostMain", "--forward=yield:yield_,read:read_,write:write_,trace:trace_"];
		if (/^\s*(sqrt|log|atan2):\s+FUNC/m.test(source)) args.push("--no-libm");
		for (const n of ["input", "print", "printInt", "printFloat", "printLF", "exit"]) {
			if (new RegExp("^" + n + ":", "m").test(source)) args.push("--no-native=" + n);
		}
		list.push({ name: f, args });
	}
	return list;
}

function build(seed) {
	const permuted = path.join(WORK, "GAZL.cpp");
	const p = run("node", [path.join(ROOT, "tools", "permuteDispatch.js"), String(seed), path.join(ROOT, "src", "GAZL.cpp"), permuted]);
	if (p.status !== 0) fail("permuteDispatch failed for seed " + seed + ":\n" + p.stderr);
	fs.rmSync(EXE, { force: true });
	const tools = path.join(ROOT, "tools");
	const args = ["release", WINDOWS ? "x64" : "native", EXE, "-I..", "-I" + path.join("..", "src"), "GAZLCmd.cpp", permuted];
	const b = WINDOWS
		? run("cmd.exe", ["/d", "/c", "BuildCpp.cmd"].concat(args), { cwd: tools })
		: run("bash", ["BuildCpp.sh"].concat(args), { cwd: tools });
	if (b.status !== 0 || !fs.existsSync(EXE)) fail("build failed for seed " + seed + ":\n" + b.stdout + b.stderr);
}

// Everything the build computes, with timings removed, so two builds can be compared for identical results.
function fingerprint(kernelList, firmwareList) {
	const strip = (text) => text.replace(/time: [-+0-9.eE]+s/g, "time: _");
	const result = {};
	for (const k of kernelList) result[k] = strip(run(EXE, [k, "main"], { cwd: ROOT }).stdout);
	for (const f of firmwareList) {
		const r = run(EXE, f.args, { cwd: ROOT });
		result["firmware " + f.name] = strip(r.stdout) + " exit " + r.status;
	}
	return result;
}

function bench(kernel, pin) {
	const args = [kernel, "main"].concat(BENCH_ARGS, pin === "" ? [] : ["--pin=" + pin]);
	const r = run(EXE, args, { cwd: ROOT });
	const line = r.stdout.split(/\r?\n/).find((l) => l.startsWith("bench\t"));
	if (!line) fail("no bench line for " + kernel + ":\n" + r.stdout + r.stderr);
	const fields = {};
	for (const f of line.split("\t")) {
		const eq = f.indexOf("=");
		if (eq > 0) fields[f.slice(0, eq)] = f.slice(eq + 1);
	}
	const cpu = fields.cpu_median_Mcyc !== undefined ? fields.cpu_median_Mcyc : fields.cpu_median_ms;
	if (cpu === undefined || fields.median_ms === undefined) fail("bench line lacks cpu_median / median_ms: " + line);
	return { cpu, wall: fields.median_ms };
}

function main() {
	const options = parseArgs(process.argv.slice(2));
	fs.mkdirSync(WORK, { recursive: true });
	const kernelList = kernels();
	const kernelNames = kernelList.map((k) => path.basename(k, ".gazl"));
	const firmwareList = firmwares();
	const list = seedList(options.first, options.last, options.drift);

	// Resume: count the complete entries per seed already in the CSV.
	const done = new Map();
	if (!fs.existsSync(options.csv)) fs.writeFileSync(options.csv, "platform,seed,kernel,cpu_median,median_ms\n");
	else {
		const rows = fs.readFileSync(options.csv, "latin1").split(/\r?\n/).slice(1).filter(Boolean);
		const perSeed = new Map();
		for (const row of rows) {
			const [platform, seed, kernel] = row.split(",");
			if (platform !== PLATFORM) continue;
			const n = kernel === "#invalid" ? kernelNames.length : 1;
			perSeed.set(seed, (perSeed.get(seed) || 0) + n);
		}
		for (const [seed, n] of perSeed) done.set(seed, Math.floor(n / kernelNames.length));
	}

	// The seed-0 results for this platform and this exact src/GAZL.cpp.
	const sourceHash = require("crypto")
		.createHash("sha1")
		.update(fs.readFileSync(path.join(ROOT, "src", "GAZL.cpp")))
		.digest("hex");
	const referencePath = path.join(WORK, "reference-" + PLATFORM + "-" + sourceHash.slice(0, 12) + ".json");
	process.stdout.write(
		PLATFORM +
			": " +
			list.length +
			" list entries (seeds " +
			options.first +
			".." +
			options.last +
			", seed 0 every " +
			options.drift +
			"), " +
			kernelNames.length +
			" kernels, " +
			firmwareList.length +
			" firmwares" +
			(options.pin === "" ? "" : ", --pin=" + options.pin) +
			"\n",
	);
	for (let i = 0; i < list.length; ++i) {
		const seed = String(list[i]);
		const left = done.get(seed) || 0;
		if (left > 0) {
			done.set(seed, left - 1);
			continue;
		}
		const started = Date.now();
		build(list[i]);
		const print = fingerprint(kernelList, firmwareList);
		if (!fs.existsSync(referencePath)) {
			if (list[i] !== 0) fail("no seed-0 reference yet: run a list that starts at seed 0");
			fs.writeFileSync(referencePath, JSON.stringify(print, null, "\t"));
		}
		const reference = JSON.parse(fs.readFileSync(referencePath, "latin1"));
		const wrong = Object.keys(reference).filter((k) => reference[k] !== print[k]);
		if (wrong.length) {
			fs.appendFileSync(options.csv, [PLATFORM, seed, "#invalid", "nan", "nan"].join(",") + "\n");
			process.stdout.write(
				"[" + (i + 1) + "/" + list.length + "] seed " + seed + ": INVALID, differs from seed 0 in " + wrong.join(", ") + "\n",
			);
			continue;
		}
		const rows = kernelList.map((k, j) => {
			const r = bench(k, options.pin);
			return [PLATFORM, seed, kernelNames[j], r.cpu, r.wall].join(",");
		});
		fs.appendFileSync(options.csv, rows.join("\n") + "\n");
		process.stdout.write("[" + (i + 1) + "/" + list.length + "] seed " + seed + ": " + ((Date.now() - started) / 1000).toFixed(1) + "s\n");
	}
}

main();
