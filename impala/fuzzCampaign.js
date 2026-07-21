// Long-running chunked driver for fuzzImpala.js. Each chunk is a FRESH `node fuzzImpala.js` process:
// the Impala compiler module reloads per compile, so a single process exhausts memory around ~20k
// iterations (a Node crash, not a compiler bug) - chunking across processes sidesteps that and lets a
// campaign run for days. Compile-only chunks run at high volume; every `vmEvery`-th chunk is a slower
// `--vm` chunk (which spawns GAZLCmd per program) to catch load-time miscompiles. Seeds advance
// monotonically so no program is retested, and any CRASH/FAULT block is copied verbatim to the log.
//
// Usage: node fuzzCampaign.js [hours=48] [startSeed=1] [chunk=8000] [vmEvery=4] [logFile=fuzzCampaign.log]
//   Any reported CRASH/FAULT reproduces standalone from its printed seed:
//     node fuzzImpala.js 1 <seed>          (compile crash)
//     node fuzzImpala.js --vm 1 <seed>     (vm load fault)

const cp = require('child_process');
const fs = require('fs');
const path = require('path');

const hours    = parseFloat(process.argv[2] || '48');
let   seed     = parseInt(process.argv[3] || '1', 10);
const chunk    = parseInt(process.argv[4] || '8000', 10);   // stay well under the ~20k per-process ceiling
const vmEvery  = parseInt(process.argv[5] || '4', 10);      // every Nth chunk runs --vm instead of compile-only
const logFile  = path.join(__dirname, process.argv[6] || 'fuzzCampaign.log');
const fuzzer   = path.join(__dirname, 'fuzzImpala.js');

const deadline = Date.now() + hours * 3600 * 1000;
let chunks = 0, programs = 0, bugs = 0;

function stamp() { return new Date().toISOString().replace('T', ' ').slice(0, 19); }
function log(line) { fs.appendFileSync(logFile, line + '\n'); process.stdout.write(line + '\n'); }

log(`\n=== fuzz campaign start ${stamp()} : ${hours}h, chunk=${chunk}, vmEvery=${vmEvery}, seed0=${seed} ===`);

while (Date.now() < deadline) {
	const vm = (chunks % vmEvery === vmEvery - 1);
	const iters = vm ? Math.max(1, Math.floor(chunk / 8)) : chunk;   // --vm is far slower (spawns a VM per program)
	const args = vm ? ['--vm', String(iters), String(seed)] : [String(iters), String(seed)];
	const res = cp.spawnSync('node', [fuzzer].concat(args), { encoding: 'latin1', maxBuffer: 64 * 1024 * 1024 });
	const out = (res.stderr || '') + (res.stdout || '');

	// the fuzzer copies each CRASH/FAULT block between `=== ... seed=N ===` fences - keep them all
	const blocks = out.match(/=== (CRASH|VM FAULT) seed=[\s\S]*?=== end seed=\d+ ===/g) || [];
	for (const b of blocks) { log(`\n${b}\n`); bugs += 1; }

	const summary = (out.match(/^fuzz: .*$/m) || [`(no summary; exit ${res.status})`])[0];
	if (res.status !== 0 && blocks.length === 0) {
		// non-zero without a parsed bug block = the driver itself hit trouble (e.g. OOM) - note and continue
		log(`[${stamp()}] chunk ${chunks} ${vm ? 'vm ' : ''}seed=${seed}: exit ${res.status} :: ${summary}`);
	} else {
		log(`[${stamp()}] chunk ${chunks} ${vm ? 'vm ' : ''}seed=${seed}: ${summary}`);
	}

	programs += iters;
	seed     += iters;
	chunks   += 1;
}

log(`=== fuzz campaign end ${stamp()} : ${chunks} chunks, ~${programs} programs, ${bugs} bug blocks logged ===`);
process.exit(bugs > 0 ? 1 : 0);
