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
	console.error('  node impala/impala.node.js compile [--legacy] [<input.impala>] [<output.gazl>|-] [<random id>]');
	console.error('  node impala/impala.node.js build [--legacy] <root.impala> [<output.gazl>|-] [<random id>]');
	console.error('  node impala/impala.node.js run [--legacy] [<input.impala>]');
	console.error('  --legacy downgrades Impala 2 strict-expression errors to warnings');
	process.exit(1);
}

// --- Step 5: import-as-linking -------------------------------------------------
// `import "path"` names a unit for the link closure (path relative to the importing file).
// The builder walks the closure (visited-set by real path, cycles harmless), concatenates the
// unit sources in dependency-first order, and compiles the whole program in one pass — so
// cross-unit calls (including struct/multi-value returns) codegen correctly with no header drift.
function scanImports(source) {
	const re = /^[ \t]*import[ \t]+"([^"\r\n]*)"/gm;
	const paths = [];
	let m;
	while ((m = re.exec(source)) !== null) {
		paths.push(m[1]);
	}
	return paths;
}

function resolveImportClosure(rootPath) {
	const visited = new Set();
	const order = [];

	function canonical(p) {
		try { return fs.realpathSync(p); } catch (_) { return path.resolve(p); }
	}

	function visit(absPath, importChain) {
		const key = canonical(absPath);
		if (visited.has(key)) return;             // dedup diamonds; break import cycles
		visited.add(key);

		let source;
		try {
			source = readFileLatin1(absPath);
		} catch (err) {
			const via = importChain.length ? ` (imported from ${importChain[importChain.length - 1]})` : '';
			console.error(`Error reading ${absPath}${via}: ${err && err.message ? err.message : String(err)}`);
			process.exit(1);
		}

		const dir = path.dirname(absPath);
		for (const rel of scanImports(source)) {
			visit(path.resolve(dir, rel), importChain.concat(absPath));
		}
		order.push({ path: absPath, source });    // post-order → dependencies precede dependents
	}

	visit(path.resolve(rootPath), []);
	return order;
}

// Resolve + concatenate the import closure of `rootPath` into one compilation-ready source.
function concatenateClosure(rootPath) {
	const units = resolveImportClosure(rootPath);
	const rootDir = path.dirname(path.resolve(rootPath));
	const combined = units
		.map((u) => `// ==== unit: ${path.relative(rootDir, u.path).split(path.sep).join('/')} ====\n${u.source}`)
		.join('\n');
	return { units, combined };
}

// Build a linked .gazl program from a root unit. Exposed for tests.
function buildProgram(rootPath, options = {}) {
	const { combined, units } = concatenateClosure(rootPath);
	const output = compileWithJsImpala(combined, {
		randomId: options.randomId,
		retabulate: true,
		trailingNewline: true,
		sourceName: rootPath,
		legacy: options.legacy,
	});
	return { output, unitCount: units.length };
}

function buildCommand(args, legacy) {
	if (args.length === 0) {
		console.error('build requires a root .impala file');
		process.exit(1);
	}
	const rootPath = args[0];
	const outputPath = args[1] || '-';
	const randomId = parseRandomId(args[2]);

	let output;
	let unitCount;
	try {
		const built = buildProgram(rootPath, { randomId, legacy });
		output = built.output;
		unitCount = built.unitCount;
	} catch (err) {
		const message = (err && err.message) ? err.message : String(err);
		console.error(message.includes(': error[') || message.includes(': error:') ? message : `Error building ${rootPath}: ${message}`);
		process.exit(1);
	}

	if (!outputPath || outputPath === '-') {
		process.stdout.write(output);
		return;
	}
	try {
		writeFileLatin1(outputPath, output);
		console.error(`Successfully built ${rootPath} (${unitCount} unit${unitCount === 1 ? '' : 's'})`);
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

function compileCommand(args, legacy) {
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
		output = compileWithJsImpala(source, { randomId, retabulate: true, trailingNewline: true, sourceName: inputPath || '<stdin>', legacy });
	} catch (err) {
		const message = (err && err.message) ? err.message : String(err);
		if (message.includes(': error[') || message.includes(': error:')) console.error(message);
		else if (inputPath) console.error(`Error compiling ${inputPath}: ${message}`);
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

function runCommand(args, legacy) {
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
		gazl = compileWithJsImpala(source, { retabulate: true, trailingNewline: true, sourceName: inputPath || '<stdin>', legacy });
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
	const [cmd, ...rest] = argv.filter((arg) => arg !== '--legacy');
	if (!cmd) return usageAndExit();
	switch (cmd) {
		case 'compile':
			return compileCommand(rest, legacy);
		case 'build':
			return buildCommand(rest, legacy);
		case 'run':
			return runCommand(rest, legacy);
		default:
			return usageAndExit();
	}
}

module.exports = { buildProgram, concatenateClosure, resolveImportClosure, scanImports };

if (require.main === module) {
	main();
}
