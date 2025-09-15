#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

const root = __dirname;

function resolve(file) {
	return path.join(root, file);
}

function read(file) {
	return fs.readFileSync(resolve(file), 'utf8');
}

function write(file, contents) {
	fs.writeFileSync(resolve(file), contents);
}

function loadCompiler(source, description) {
	const context = { console };
	vm.createContext(context);
	vm.runInContext(source, context, { filename: description });
	if (typeof context.compileJSPEG !== 'function') {
		throw new Error(`${description} did not define compileJSPEG`);
	}
	return context.compileJSPEG;
}

function compileWith(fn, source, label) {
	const result = fn(source);
	if (!result || !Array.isArray(result) || result.length < 2) {
		throw new Error(`${label} did not return a [success, code, index] tuple`);
	}
	const [ok, generated, index] = result;
	if (!ok) {
		const location = (typeof index === 'number' ? ` at index ${index}` : '');
		throw new Error(`${label} failed to compile${location}`);
	}
	return generated;
}

function canonicalize(str) {
	return str.replace(/\r\n/g, '\n');
}

function canonicalizeTrimmed(str) {
	return canonicalize(str).trim();
}

function regenerate() {
	const compilerSource = read('jspegCompiler.js');
	const compileJSPEG = loadCompiler(compilerSource, 'jspegCompiler.js');

	const grammarSource = read('jspeg.jspeg');
	const generatedCompiler = compileWith(compileJSPEG, grammarSource, 'jspeg.jspeg');

	const updatedCompileJSPEG = loadCompiler('compileJSPEG=' + generatedCompiler, 'generated jspeg compiler');
	const regenerated = compileWith(updatedCompileJSPEG, grammarSource, 'jspeg.jspeg (self-host)');
	if (canonicalize(regenerated) !== canonicalize(generatedCompiler)) {
		throw new Error('Self-hosted compile produced different output for jspeg.jspeg');
	}

	const impalaGrammar = read('impala.jspeg');
	const generatedImpala = compileWith(updatedCompileJSPEG, impalaGrammar, 'impala.jspeg');

	return {
		jspegCompiler: 'compileJSPEG=' + generatedCompiler + '\n',
		impalaCompiler: 'impalaCompiler=' + generatedImpala + '\n'
	};
}

function writeOutputs(outputs) {
	write('jspegCompiler.js', outputs.jspegCompiler);
	write('impalaCompiler.js', outputs.impalaCompiler);
}

function checkOutputs(outputs) {
	const currentJspeg = canonicalizeTrimmed(read('jspegCompiler.js'));
	const currentImpala = canonicalizeTrimmed(read('impalaCompiler.js'));
	const expectedJspeg = canonicalizeTrimmed(outputs.jspegCompiler);
	const expectedImpala = canonicalizeTrimmed(outputs.impalaCompiler);

	return {
		jspegMatches: currentJspeg === expectedJspeg,
		impalaMatches: currentImpala === expectedImpala
	};
}

function main(args) {
	if (args.length > 1 || (args.length === 1 && args[0] !== '--check')) {
		console.error('Usage: node updateJSPEG.js [--check]');
		process.exit(1);
	}

	const outputs = regenerate();
	if (args[0] === '--check') {
		const { jspegMatches, impalaMatches } = checkOutputs(outputs);
		if (!jspegMatches || !impalaMatches) {
			console.error('JSPEG outputs are stale. Run "node updateJSPEG.js" to regenerate them.');
			if (!jspegMatches) {
				console.error(' - jspegCompiler.js');
			}
			if (!impalaMatches) {
				console.error(' - impalaCompiler.js');
			}
			process.exit(1);
		}
		console.log('JSPEG compilers are up to date.');
		return;
	}

	writeOutputs(outputs);
	console.log('Regenerated jspegCompiler.js and impalaCompiler.js');
}

if (require.main === module) {
	main(process.argv.slice(2));
} else {
	module.exports = {
		regenerate,
		writeOutputs,
		checkOutputs
	};
}
