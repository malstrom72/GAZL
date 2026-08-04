'use strict';

/* Feeding a .gazl to the REAL assembler, shared by the golden gate (runJspegTests.js) and the
   testdata parity gate (jspegCompilerTests.js).

   This exists because compiling clean does NOT mean the GAZL assembles: a struct field's folded extent
   used to be emitted after the layout block that read it, which every golden and a million fuzzed
   programs missed because nothing ever fed a golden to the assembler. `tools/gazl-validate.sh` does not
   cover this - it compares `; signature` metadata across units and is built to run on modules whose
   externs are deliberately unresolved, which is exactly what the assembler refuses to load. */

const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const repoRoot = path.resolve(__dirname, '..');
const gazlCmd = path.join(repoRoot, 'output',
	process.platform === 'win32' ? 'GAZLCmd.exe' : 'GAZLCmd');

function haveGazlCmd() {
	return fs.existsSync(gazlCmd);
}

function runGazlCmd(gazlPath, cmdArgs) {
	const result = childProcess.spawnSync(gazlCmd, [gazlPath].concat(cmdArgs),
		{ encoding: 'latin1', timeout: 30000 });
	/* GAZLCmd puts the PROGRAM's output on stdout and its own report (sizes, Status) on stderr. */
	const report = result.stderr || '';
	return {
		failure: (result.error ? `could not run GAZLCmd: ${result.error.message}` : undefined),
		assembled: /Code size:/.test(report),  // absent -> it never reached the entry point
		line: (report.split('\n').find((l) => l.trim()) || '(no output)').trim(),
		stdout: result.stdout || '',
		report
	};
}

/* A fixture opts in to being ASSEMBLED AND RUN by declaring, in its header comment:
	Expected (GAZLCmd <name>.gazl <entry> [<define> <value> ...]): <whitespace-separated output>
   The trailing defines feed GAZLCmd's own `<define symbol> <define value>` arguments, which is how an
   `extern struct` fixture supplies the host-owned layout it compiled against. Without such a line a
   fixture is compile-only, so the byte-diff against the golden is the whole check for it. */
function parseExpectedRun(source) {
	const match = source.match(/Expected \(GAZLCmd ([^)]*)\):[ \t]*(.*)/);
	if (!match) { return undefined; }
	const parts = match[1].trim().split(/\s+/);
	return { args: parts.slice(1), want: match[2].trim().split(/\s+/).filter(Boolean) };
}

function runExpected(gazlPath, expectedRun) {
	const run = runGazlCmd(gazlPath, expectedRun.args);
	if (run.failure) { return run.failure; }
	if (!run.assembled) { return `did not assemble: ${run.line}`; }
	const got = run.stdout.trim().split(/\s+/).filter(Boolean);
	if (got.join(' ') !== expectedRun.want.join(' ')) {
		return `printed "${got.join(' ')}" but the fixture expects "${expectedRun.want.join(' ')}"`;
	}
	if (!/Status: 0\b/.test(run.report)) {
		return `ran but exited non-zero: ${(run.report.match(/Status: .*/) || ['?'])[0]}`;
	}
	return undefined;
}

/* A fixture with no run line is still fed to the assembler, because "compiles clean" and "loads"
   are different claims and only the second one catches a label this compiler emitted but never
   defined. Most such fixtures are firmware that links against a host, so a symbol complaint about
   a name that is NOT module-local (a plain global, or the `.o.`/`.z.` layout symbols an extern
   struct leaves to the host) means "out of scope here", not "broken" - whether it is missing, or
   already taken by a native GAZLCmd itself registers. A module-local `.` name is ours to answer
   for, so `.l0` or a duplicate `.s4#6` still reds the build.

   Known blind spot: GAZLCmd stops at the FIRST unresolved symbol, so a host name near the top of a
   module waves the REST of it through unchecked - hence the separate summary count. Supplying the
   missing names as `<define> <value>` pairs does not fix it: about as many fixtures define
   `DEBUG`/`PARAM_COUNT` themselves and would then collide (measured: 22 linked -> 8).

   Measured AGAIN 2026-08-03, this time feeding back whatever symbol the assembler stops at, in a loop,
   so a collision cannot happen by construction. It still does not pay, and the reason is not fixable
   from here. Of the 50 modules that do not assemble today, the loop gains 6; the rest divide into
   21 `Offset out of bounds`, 14 `Exception` and 9 `Symbol already defined`. The first group is the
   point: these constants are host-owned SIZES and INDICES (`PARAM_COUNT`, `OPERATOR_1_PARAM_INDEX`,
   ...), declared valueless in the sources on purpose, and no manifest in this repo carries the real
   numbers - so any value invented here is wrong often enough to turn a blind spot into a FALSE red,
   which is strictly worse than an honest "not checked". Do not measure this a third time without the
   actual Permut8 constants in hand; with them it is worth revisiting, because assembly is the gate
   that catches what byte-comparison cannot. */
const NEEDS_HOST = {};
const HOST_SYMBOL = /Symbol (?:not found|not previously defined|already defined)[^:]*:\s*(\S+)$/;

/* GAZLCmd has no assemble-only mode: it defaults to entering `main`, which for a fixture that has
   one means running a whole program nobody asked for (Priyome is an interactive chess game). Name
   an entry point that cannot exist and it assembles, prints its banner, then stops. */
const NO_ENTRY_POINT = '.no-entry-point';

// undefined = assembled, NEEDS_HOST = stopped at a symbol only a host can supply, string = failure.
function assembleOnly(gazlPath) {
	const run = runGazlCmd(gazlPath, [NO_ENTRY_POINT]);
	if (run.failure) { return run.failure; }
	if (run.assembled) { return undefined; }
	const symbol = (run.line.match(HOST_SYMBOL) || [])[1];
	if (symbol && (symbol.charAt(0) !== '.' || /^\.[oz]\./.test(symbol))) {
		return NEEDS_HOST;
	}
	return `did not assemble: ${run.line}`;
}

module.exports = { gazlCmd, haveGazlCmd, parseExpectedRun, runExpected, assembleOnly, NEEDS_HOST };
