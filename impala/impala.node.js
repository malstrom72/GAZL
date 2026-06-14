'use strict';

// Simple Impala CLI using the JSPEG-generated compiler.
// Usage:
//   node impala/impala.node.js compile [<input.impala>] [<output.gazl>|-] [<random id>]
//   node impala/impala.node.js run [<input.impala>]

const fs = require('fs');
const os = require('os');
const path = require('path');
const cp = require('child_process');

const { compileWithJsImpala } = require('./impalaJsCompilerRunner');

const IMPALA_ENCODING = 'latin1';

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
	console.error('  node impala/impala.node.js compile [<input.impala>] [<output.gazl>|-] [<random id>]');
	console.error('  node impala/impala.node.js run [<input.impala>]');
	process.exit(1);
}

function parseRandomId(arg) {
	if (arg == null) return undefined;
	if (/^0x[0-9a-fA-F]+$/.test(arg)) return parseInt(arg, 16);
	const n = Number(arg);
	return Number.isFinite(n) ? Math.trunc(n) : undefined;
}

function compileCommand(args) {
	let source;
	let inputPath;
	let outputPath;
	let randomId;

	if (args.length === 0) {
		// stdin -> stdout
		source = readStdinLatin1Sync();
		if (!source) {
			console.error('No input provided on stdin');
			process.exit(1);
		}
		outputPath = '-';
	} else {
		inputPath = args[0];
		try {
			source = readFileLatin1(inputPath);
		} catch (err) {
			console.error(`Error reading ${inputPath}: ${err && err.message ? err.message : String(err)}`);
			process.exit(1);
		}
		outputPath = args[1] || '-';
		randomId = parseRandomId(args[2]);
	}

	let output;
	try {
		output = compileWithJsImpala(source, { randomId, retabulate: true, trailingNewline: true, sourceName: inputPath || '<stdin>' });
	} catch (err) {
		const message = (err && err.message) ? err.message : String(err);
		if (inputPath) console.error(`Error compiling ${inputPath}: ${message}`);
		else console.error(`Error: ${message}`);
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
		if (inputPath) console.error(`Successfully compiled ${inputPath}`);
		else console.error('Successful');
	} catch (err) {
		console.error(`Error writing ${outputPath}: ${err && err.message ? err.message : String(err)}`);
		process.exit(1);
	}
}

function runCommand(args) {
	let source;
	let inputPath;
	if (args.length === 0) {
		source = readStdinLatin1Sync();
		if (!source) {
			console.error('No input provided on stdin');
			process.exit(1);
		}
	} else {
		inputPath = args[0];
		try {
			source = readFileLatin1(inputPath);
		} catch (err) {
			console.error(`Error reading ${inputPath}: ${err && err.message ? err.message : String(err)}`);
			process.exit(1);
		}
	}

	let gazl;
	try {
		gazl = compileWithJsImpala(source, { retabulate: true, trailingNewline: true, sourceName: inputPath || '<stdin>' });
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
	const [cmd, ...rest] = process.argv.slice(2);
	if (!cmd) return usageAndExit();
	switch (cmd) {
		case 'compile':
			return compileCommand(rest);
		case 'run':
			return runCommand(rest);
		default:
			return usageAndExit();
	}
}

main();
