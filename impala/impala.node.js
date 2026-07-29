'use strict';

// Simple Impala CLI using the JSPEG-generated compiler.
// Usage:
//   node impala/impala.node.js compile [<input.impala>] [<output.gazl>|-] [<random id>]
//   node impala/impala.node.js run [<input.impala>]
//
// `compile` always resolves the import closure of its input - a file that imports nothing is just a
// closure of one, and compiles to exactly what it always did. Reading from stdin resolves imports
// against the current directory, there being no source file to be relative to.

const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');

const { compileWithJsImpala } = require('./impalaJsCompilerRunner');
const { concatenateClosure, resolveImportClosure, scanImports, deadStrip } = require('./impalaImportClosure');

const IMPALA_ENCODING = 'latin1';
const STDIN_PATH = '<stdin>';   // a root name with no directory, so imports resolve from the cwd

function readFileLatin1(filePath) {
	return fs.readFileSync(filePath, IMPALA_ENCODING);
}

function writeFileLatin1(filePath, contents) {
	fs.writeFileSync(filePath, contents, IMPALA_ENCODING);
}

function readStdinLatin1Sync() {
	let data = '';
	const fd = 0; // stdin
	try {
		for (;;) {
			const chunk = Buffer.allocUnsafe(64 * 1024);
			const bytes = fs.readSync(fd, chunk, 0, chunk.length, null);
			if (bytes === 0) break;
			data += chunk.subarray(0, bytes).toString(IMPALA_ENCODING);
		}
	} catch (err) {
		// If stdin is not a pipe/tty with data, ignore
	}
	return data;
}

function usageAndExit() {
	console.error('Usage:');
	console.error('  node impala/impala.node.js compile [--legacy] [--dead-strip] [<input.impala>] [<output.gazl>|-] [<random id>]');
	console.error('  node impala/impala.node.js run [--legacy] [<input.impala>]');
	console.error('  --legacy downgrades Impala 2 strict-expression errors to warnings');
	console.error('  --dead-strip drops everything unreachable from an `export`');
	process.exit(1);
}

// --- Step 5: import-as-linking -------------------------------------------------
// `import "path"` names a unit for the link closure (path relative to the importing file). The
// closure walk, concatenation and `--dead-strip` all live in ./impalaImportClosure so the NuXJS
// front end runs the same code; this supplies the two host primitives it asks for. `realpathSync`
// as `canonical` is what folds symlinks and Windows case into one file identity.
function makeIo(stdinSource) {
	return {
		read(filePath) {
			return (stdinSource !== undefined && filePath === STDIN_PATH) ? stdinSource : readFileLatin1(filePath);
		},
		canonical(filePath) {
			try { return fs.realpathSync(filePath); } catch (_) { return path.resolve(filePath); }
		},
	};
}

// Compile a root unit and its import closure into one linked .gazl program. Exposed for tests.
function compileProgram(rootPath, options = {}) {
	const { combined, units, spans } = concatenateClosure(rootPath, makeIo(options.stdinSource));
	let output = compileWithJsImpala(combined, {
		randomId: options.randomId,
		retabulate: true,
		trailingNewline: true,
		sourceName: rootPath,
		units: spans,
		legacy: options.legacy,
	});
	if (options.deadStrip) {
		output = deadStrip(output);
	}
	return { output, unitCount: units.length };
}

function compileCommand(args, legacy, wantDeadStrip) {
	let stdinSource;
	let rootPath;
	if (args.length === 0) {
		stdinSource = readStdinLatin1Sync();
		if (!stdinSource) {
			console.error('No input provided on stdin');
			process.exit(1);
		}
		rootPath = STDIN_PATH;
	} else {
		rootPath = args[0];
	}
	const outputPath = args[1] || '-';
	const randomId = parseRandomId(args[2]);

	let output;
	let unitCount;
	try {
		const built = compileProgram(rootPath, { randomId, legacy, deadStrip: wantDeadStrip, stdinSource });
		output = built.output;
		unitCount = built.unitCount;
	} catch (err) {
		const message = (err && err.message) ? err.message : String(err);
		console.error(message.includes(': error[') || message.includes(': error:') ? message : `Error compiling ${rootPath}: ${message}`);
		// Overwrite any stale output so a failed compile cannot leave a previously-good .gazl behind.
		if (outputPath && outputPath !== '-') {
			try { writeFileLatin1(outputPath, 'Error: ' + message); } catch (_) {}
		}
		process.exit(1);
	}

	if (!outputPath || outputPath === '-') {
		process.stdout.write(output);
		return;
	}
	try {
		writeFileLatin1(outputPath, output);
		console.error(`Successfully compiled ${rootPath}${unitCount > 1 ? ` (${unitCount} units)` : ''}`);
	} catch (err) {
		console.error(`Error writing ${outputPath}: ${err && err.message ? err.message : String(err)}`);
		process.exit(1);
	}
}

function parseRandomId(arg) {
	if (arg == null) return undefined;
	if (/^0x[0-9a-fA-F]+$/.test(arg)) return parseInt(arg, 16);
	const n = Number(arg);
	return Number.isFinite(n) ? Math.trunc(n) : undefined;
}

function runCommand(args, legacy) {
	let stdinSource;
	let rootPath;
	if (args.length === 0) {
		stdinSource = readStdinLatin1Sync();
		if (!stdinSource) {
			console.error('No input provided on stdin');
			process.exit(1);
		}
		rootPath = STDIN_PATH;
	} else {
		rootPath = args[0];
	}

	let gazl;
	try {
		gazl = compileProgram(rootPath, { legacy, stdinSource }).output;
	} catch (err) {
		console.error((err && err.message) ? err.message : String(err));
		process.exit(1);
	}

	const repoRoot = path.resolve(__dirname, '..');
	const gazlCmd = process.platform === 'win32'
		? path.join(repoRoot, 'output', 'GAZLCmd.exe')
		: path.join(repoRoot, 'output', 'GAZLCmd');
	const tempGazl = path.join(os.tmpdir(), `impala-${process.pid}-${Date.now()}.gazl`);

	try { writeFileLatin1(tempGazl, gazl); } catch (err) {
		console.error(`Error writing temporary gazl: ${err && err.message ? err.message : String(err)}`);
		process.exit(1);
	}

	console.error('');
	const result = cp.spawnSync(gazlCmd, [tempGazl, 'main'], { stdio: 'inherit' });
	console.error('');
	if (result.error) {
		console.error(`Error launching ${path.relative(repoRoot, gazlCmd)}: ${result.error.message}`);
		process.exit(1);
	}
	process.exit(result.status || 0);
}

function main() {
	const argv = process.argv.slice(2);
	const legacy = argv.includes('--legacy');
	const wantDeadStrip = argv.includes('--dead-strip');
	const [cmd, ...rest] = argv.filter((arg) => arg !== '--legacy' && arg !== '--dead-strip');
	if (!cmd) return usageAndExit();
	switch (cmd) {
		case 'compile':
			return compileCommand(rest, legacy, wantDeadStrip);
		case 'run':
			return runCommand(rest, legacy);
		default:
			return usageAndExit();
	}
}

// Re-exported for tests, with the Node io already bound so callers need not know about it.
module.exports = {
	compileProgram,
	concatenateClosure: (rootPath) => concatenateClosure(rootPath, makeIo()),
	resolveImportClosure: (rootPath) => resolveImportClosure(rootPath, makeIo()),
	scanImports,
	deadStrip,
};

if (require.main === module) {
	main();
}
