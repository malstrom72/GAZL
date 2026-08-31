#!/usr/bin/env node
/*
	Permut8 firmware harness generator: wraps an UNMODIFIED firmware .gazl (tests/impala/golden/*.gazl) into a
	runnable, self-checking program by concatenating pure GAZL around it - no firmware edits, no engine-side
	firmware logic:

	  [host constants]  the documented Permut8 host constants as DEFi lines (PARAM_COUNT, operator enums, ...)
	  [host core]       delay line + deterministic pseudo-audio RNG + read_/write_/trace_ (compiled Impala)
	  [yield_]          the per-sample host callback: checksum the output, feed the next input, count frames,
	                    print the checksum and exit() at the frame budget (variant per patch kind)
	  [firmware]        verbatim
	  [hostMain]        driver: set params, call init/update/reset (when present), then drive process()
	                    (full patches - the firmware's own yield loop runs it) or loop operate1/operate2()
	                    (mod patches)

	The firmware's `^yield/^read/^write/^trace` native calls are satisfied by the GAZL implementations through
	GAZLCmd's --forward mechanism (pushCall). Run the result with:

	  GAZLCmd <out.gazl> hostMain --forward=yield:yield_,read:read_,write:write_,trace:trace_

	(see tools/runPermut8Firmware.sh / .cmd). Works on both engines: add --jit for the JIT, and
	checkPermut8Firmwares.sh --jit runs the whole set as an interp-vs-JIT differential.

	Usage: node tools/permut8Host.js <firmware.gazl> <out.gazl>
*/
'use strict';

const fs = require('fs');

const HOST_CONSTS = [
	['DEBUG', 0],
	['OPERATOR_1_NOP', 0], ['OPERATOR_1_AND', 1], ['OPERATOR_1_MUL', 2], ['OPERATOR_1_OSC', 3], ['OPERATOR_1_RND', 4], ['OPERATOR_1_COUNT', 5],
	['OPERATOR_2_NOP', 0], ['OPERATOR_2_OR', 1], ['OPERATOR_2_XOR', 2], ['OPERATOR_2_MSK', 3], ['OPERATOR_2_SUB', 4], ['OPERATOR_2_COUNT', 5],
	['SWITCHES_SYNC_MASK', 1], ['SWITCHES_TRIPLET_MASK', 2], ['SWITCHES_DOTTED_MASK', 4], ['SWITCHES_WRITE_PROTECT_MASK', 8], ['SWITCHES_REVERSE_MASK', 16],
	['CLOCK_FREQ_PARAM_INDEX', 0], ['SWITCHES_PARAM_INDEX', 1], ['OPERATOR_1_PARAM_INDEX', 2], ['OPERAND_1_HIGH_PARAM_INDEX', 3],
	['OPERAND_1_LOW_PARAM_INDEX', 4], ['OPERATOR_2_PARAM_INDEX', 5], ['OPERAND_2_HIGH_PARAM_INDEX', 6], ['OPERAND_2_LOW_PARAM_INDEX', 7],
	['PARAM_COUNT', 8], ['POSITION_INT_BITS', 16], ['POSITION_FRACT_BITS', 4], ['HOST_POSITION_PPQ', 1920]
].map(function (c) { return ' ' + c[0] + ':\t! DEFi #' + c[1]; }).join('\n');

// Compiled from Impala (see the header comment): hostDelay/hostFrames/hostBudget/hostChecksum/hostRand +
// hostNext() (LCG, 12-bit signed samples) + trace_ (no-op) + write_/read_ (wrapped interleaved-stereo delay line).
const HOST_CORE = [
	' HOST_DELAY_MASK:\t! DEFi #8191',
	' hostDelay:\t\t\tGLOB *16384',
	' \t\t\t\t\tGLOB *1',
	' hostFrames:\t\tDATi #0',
	' \t\t\t\t\tGLOB *1',
	' hostBudget:\t\tDATi #0',
	' \t\t\t\t\tGLOB *1',
	' hostChecksum:\t\tDATi #0',
	' \t\t\t\t\tGLOB *1',
	' hostRand:\t\t\tDATi #0',
	' hostNext:\t\t\tFUNC',
	' \t\t\t\t\t$r:\t\t\tOUTi',
	' \t\t\t\t\t\t\t\tPEEK %0 &hostRand',
	' \t\t\t\t\t\t\t\tMULi %0 %0 #1103515245',
	' \t\t\t\t\t\t\t\tADDi %0 %0 #12345',
	' \t\t\t\t\t\t\t\tPOKE &hostRand %0',
	' \t\t\t\t\t\t\t\tPEEK %0 &hostRand',
	' \t\t\t\t\t\t\t\tSHRu %0 %0 #16',
	' \t\t\t\t\t\t\t\tANDi %0 %0 #0xFFF',
	' \t\t\t\t\t\t\t\tSUBi $r %0 #2048',
	' \t\t\t\t\t\t\t\tRETU',
	' trace_:\t\t\tFUNC',
	' \t\t\t\t\t\t\t\tPARA *1',
	' \t\t\t\t\t$s:\t\t\tINPp',
	' \t\t\t\t\t\t\t\tRETU',
	' write_:\t\t\tFUNC',
	' \t\t\t\t\t\t\t\tPARA *1',
	' \t\t\t\t\t$offset:\tINPi',
	' \t\t\t\t\t$frameCount: INPi',
	' \t\t\t\t\t$values:\tINPp',
	' \t\t\t\t\t$i:\t\t\tLOCi',
	' \t\t\t\t\t$idx:\t\tLOCi',
	' \t\t\t\t\t$v:\t\t\tLOCp',
	' \t\t\t\t\t\t\t\tMOVp $v $values',
	' \t\t\t\t\t\t\t\tMOVi $i #0',
	' \t\t\t\t\t\t\t\tGEQi #0 $frameCount @.e0',
	' \t\t\t\t\t.l1:\t\tADDi %0 $offset $i',
	' \t\t\t\t\t\t\t\tANDi %0 %0 #HOST_DELAY_MASK',
	' \t\t\t\t\t\t\t\tSHLi $idx %0 #1',
	' \t\t\t\t\t\t\t\tPEEK %0 $v',
	' \t\t\t\t\t\t\t\tPOKE &hostDelay $idx %0',
	' \t\t\t\t\t\t\t\tADDi %0 $idx #1',
	' \t\t\t\t\t\t\t\tPEEK %1 $v #1',
	' \t\t\t\t\t\t\t\tPOKE &hostDelay %0 %1',
	' \t\t\t\t\t\t\t\tADDp $v $v #2',
	' \t\t\t\t\t\t\t\tFORi $i $frameCount @.l1',
	' \t\t\t\t\t.e0:\t\tRETU',
	' read_:\t\t\t\tFUNC',
	' \t\t\t\t\t\t\t\tPARA *1',
	' \t\t\t\t\t$offset:\tINPi',
	' \t\t\t\t\t$frameCount: INPi',
	' \t\t\t\t\t$values:\tINPp',
	' \t\t\t\t\t$i:\t\t\tLOCi',
	' \t\t\t\t\t$idx:\t\tLOCi',
	' \t\t\t\t\t$v:\t\t\tLOCp',
	' \t\t\t\t\t\t\t\tMOVp $v $values',
	' \t\t\t\t\t\t\t\tMOVi $i #0',
	' \t\t\t\t\t\t\t\tGEQi #0 $frameCount @.e0',
	' \t\t\t\t\t.l1:\t\tADDi %1 $offset $i',
	' \t\t\t\t\t\t\t\tANDi %1 %1 #HOST_DELAY_MASK',
	' \t\t\t\t\t\t\t\tSHLi $idx %1 #1',
	' \t\t\t\t\t\t\t\tPEEK %1 &hostDelay $idx',
	' \t\t\t\t\t\t\t\tPOKE $v %1',
	' \t\t\t\t\t\t\t\tADDi %1 $idx #1',
	' \t\t\t\t\t\t\t\tPEEK %1 &hostDelay %1',
	' \t\t\t\t\t\t\t\tPOKE $v #1 %1',
	' \t\t\t\t\t\t\t\tADDp $v $v #2',
	' \t\t\t\t\t\t\t\tFORi $i $frameCount @.l1',
	' \t\t\t\t\t.e0:\t\tRETU'
].join('\n');

// Per-sample host callback for FULL patches (references the firmware's `signal` - and `clock`, when the
// firmware declares it): fold the output sample into the checksum, count the frame (print + exit at the
// budget), advance the clock, feed the next pseudo-audio input.
function fullYield(has) {
	const L = [
		' yield_:\t\t\tFUNC',
		' \t\t\t\t\t\t\t\tPARA *2',
		' \t\t\t\t\t\t\t\tPEEK %0 &hostChecksum',
		' \t\t\t\t\t\t\t\tMULi %0 %0 #33',
		' \t\t\t\t\t\t\t\tPEEK %1 &signal',
		' \t\t\t\t\t\t\t\tADDi %0 %0 %1',
		' \t\t\t\t\t\t\t\tPEEK %1 &signal:1',
		' \t\t\t\t\t\t\t\tMULi %1 %1 #7',
		' \t\t\t\t\t\t\t\tADDi %0 %0 %1',
		' \t\t\t\t\t\t\t\tPOKE &hostChecksum %0',
		' \t\t\t\t\t\t\t\tPEEK %0 &hostFrames',
		' \t\t\t\t\t\t\t\tADDi %0 %0 #1',
		' \t\t\t\t\t\t\t\tPOKE &hostFrames %0',
		' \t\t\t\t\t\t\t\tPEEK %1 &hostBudget',
		' \t\t\t\t\t\t\t\tGEQi %0 %1 @.done'
	];
	if (has.clock) {
		L.push(' \t\t\t\t\t\t\t\tPEEK %0 &clock');
		L.push(' \t\t\t\t\t\t\t\tADDi %0 %0 #1');
		L.push(' \t\t\t\t\t\t\t\tANDi %0 %0 #65535');
		L.push(' \t\t\t\t\t\t\t\tPOKE &clock %0');
	}
	L.push(' \t\t\t\t\t\t\t\tCALL &hostNext %0 *1');
	L.push(' \t\t\t\t\t\t\t\tPOKE &signal %0');
	L.push(' \t\t\t\t\t\t\t\tCALL &hostNext %0 *1');
	L.push(' \t\t\t\t\t\t\t\tPOKE &signal:1 %0');
	L.push(' \t\t\t\t\t\t\t\tGOTO @.end');
	L.push(' \t\t\t\t\t.done:\t\tPEEK %1 &hostChecksum');	// exit() never returns; ONE trailing RETU: the JIT
	L.push(' \t\t\t\t\t\t\t\tCALL ^printInt %0 *2');		// delimits a function by its FIRST RETU
	L.push(' \t\t\t\t\t\t\t\tCALL ^exit %0 *1');
	L.push(' \t\t\t\t\t.end:\t\tRETU');
	return L.join('\n');
}

// MOD patches are driven per sample from the driver loop instead; yield_ only counts frames (some mod
// firmwares reference ^yield without depending on signal/clock, which they do not declare).
const MOD_YIELD = [
	' yield_:\t\t\tFUNC',
	' \t\t\t\t\t\t\t\tPARA *2',
	' \t\t\t\t\t\t\t\tPEEK %0 &hostFrames',
	' \t\t\t\t\t\t\t\tADDi %0 %0 #1',
	' \t\t\t\t\t\t\t\tPOKE &hostFrames %0',
	' \t\t\t\t\t\t\t\tRETU'
].join('\n');

function detect(firmware) {
	function has(name) { return new RegExp('^\\s*' + name + ':\\s+FUNC', 'm').test(firmware); }
	return {
		process: has('process'), operate1: has('operate1'), operate2: has('operate2'),
		init: has('init'), update: has('update'), reset: has('reset'),
		clock: /^\s*clock:\s/m.test(firmware),				// optional API globals; only reference what the firmware declares
		params: /^\s*params:\s/m.test(firmware),
		signal: /^\s*signal:\s/m.test(firmware),
		config: /^\s*config:\s/m.test(firmware)				// the "firmware tape" string (e.g. sam's speech phrase)
	};
}

function driver(has, budget) {
	const L = [];
	function emit(s) { L.push(' \t\t\t\t\t\t\t\t' + s); }
	function emitAt(label, s) { L.push(' \t\t\t\t\t' + label + '\t\t' + s); }
	L.push(' hostMain:\t\t\tFUNC');
	emit('PARA *2');
	L.push(' \t\t\t\t\t$f:\t\t\tLOCi');
	L.push(' \t\t\t\t\t$b:\t\t\tLOCi');
	emit('POKE &hostBudget #' + budget);
	emit('POKE &hostRand #305419896');
	emit('POKE &hostFrames #0');							// reset host state so --bench re-runs are well-defined
	emit('POKE &hostChecksum #0');							// (the delay line intentionally carries over: same work per pass)
	if (has.clock) { emit('POKE &clock #0'); }
	if (has.params) {
		const params = [44100, 0, 1, 64, 32, 1, 64, 32];	// clockFreq, switches, op1, hi1, lo1, op2, hi2, lo2
		for (let i = 0; i < params.length; ++i) {
			emit('POKE &params' + (i === 0 ? '' : ':' + i) + ' #' + params[i]);
		}
	}
	if (has.config) {										// fill the "firmware tape" before init() (e.g. sam's speech phrase)
		const text = 'HELLO';
		for (let i = 0; i < text.length; ++i) {
			emit('POKE &config' + (i === 0 ? '' : ':' + i) + ' #' + text.charCodeAt(i));
		}
		emit('POKE &config:' + text.length + ' #0');
	}
	if (has.init) { emit('CALL &init %0 *1'); }
	if (has.update) { emit('CALL &update %0 *1'); }
	if (has.reset) { emit('CALL &reset %0 *1'); }
	if (has.process) {
		emit('CALL &hostNext %0 *1');
		emit('POKE &signal %0');
		emit('CALL &hostNext %0 *1');
		emit('POKE &signal:1 %0');
		emitAt('.pl:', 'CALL &process %0 *1');				// self-looping firmwares: their own yield loop runs (yield_
		emit('CALL &yield_ %0 *1');							// prints + exits at the budget); per-sample-RETURN firmwares:
		emit('GOTO @.pl');									// the driver performs the sample boundary and re-enters
		emit('RETU');										// unreachable, but the JIT delimits functions by their RETU
	} else {
		emit('MOVi $f #0');
		emit('PEEK $b &hostBudget');
		emit('GEQi #0 $b @.e0');
		emitAt('.l1:', 'CALL &hostNext %0 *1');
		emit('ANDi %0 %0 #1048575');
		emit('POKE &positions %0');
		emit('CALL &hostNext %0 *1');
		emit('ANDi %0 %0 #1048575');
		emit('POKE &positions:1 %0');
		if (has.operate1) { emit('CALL &operate1 %0 *1'); }
		if (has.operate2) { emit('CALL &operate2 %0 *1'); }
		emit('PEEK %0 &hostChecksum');
		emit('MULi %0 %0 #33');
		emit('PEEK %1 &positions');
		emit('ADDi %0 %0 %1');
		emit('PEEK %1 &positions:1');
		emit('MULi %1 %1 #7');
		emit('ADDi %0 %0 %1');
		emit('POKE &hostChecksum %0');
		if (has.clock) {
			emit('PEEK %0 &clock');
			emit('ADDi %0 %0 #1');
			emit('ANDi %0 %0 #65535');
			emit('POKE &clock %0');
		}
		emit('FORi $f $b @.l1');
		emitAt('.e0:', 'PEEK %1 &hostChecksum');
		emit('CALL ^printInt %0 *2');
		emit('RETU');
	}
	return L.join('\n');
}

function main() {
	if (process.argv.length < 4) {
		console.error('Usage: node tools/permut8Host.js <firmware.gazl> <out.gazl>');
		process.exit(1);
	}
	const firmware = fs.readFileSync(process.argv[2], 'latin1');
	const has = detect(firmware);
	if (!has.process && !has.operate1 && !has.operate2) {
		console.error('permut8Host: no process()/operate1()/operate2() in ' + process.argv[2]);
		process.exit(1);
	}
	if (has.process && !has.signal) {
		console.error('permut8Host: full patch without a `signal` global - not the standard API, unsupported: ' + process.argv[2]);
		process.exit(1);
	}
	const budget = 100000;
	const out = [
		'; Generated by tools/permut8Host.js from ' + process.argv[2] + ' - host prelude + verbatim firmware + driver.',
		HOST_CONSTS,
		HOST_CORE,
		has.process ? fullYield(has) : MOD_YIELD,
		firmware,
		driver(has, budget)
	].join('\n') + '\n';
	fs.writeFileSync(process.argv[3], out, 'latin1');
	console.error('permut8Host: ' + (has.process ? 'full' : 'mod') + ' patch, budget=' + budget
			+ (has.init ? ' +init' : '') + (has.update ? ' +update' : '') + (has.reset ? ' +reset' : '')
			+ ' -> ' + process.argv[3]);
}

main();
