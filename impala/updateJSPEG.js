#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
const Module = require("module");
const child_process = require("child_process");

const root = __dirname;

// The word list KEYWORD becomes, read OUT OF THE GRAMMAR rather than restated here. A hardcoded copy
// drifts the first time someone adds a keyword to impala.jspeg alone, and it drifts SILENTLY: the new
// word simply never matches, so it parses as an identifier and the failure surfaces far from the cause.
// Order does not matter - the loop retries on a SYMBOL_CHAR mismatch, so `for` cannot shadow `from`.
function keywordWordsFrom(grammar) {
	const rule = grammar.match(/^KEYWORD[ \t]*<-([\s\S]*?)\n[ \t\r]*\n/m);
	if (!rule) throw new Error("KEYWORD rule not found in impala.jspeg");
	return (rule[1].match(/[A-Z][A-Z0-9_]*/g) || []).map((name) => {
		const literal = grammar.match(new RegExp("\\b" + name + "\\s*<-\\s*'([^']*)'"));
		if (!literal) throw new Error(`KEYWORD alternative ${name} has no literal in impala.jspeg`);
		return literal[1];
	});
}

function mustReplace(text, pattern, replacement, label) {
	const out = text.replace(pattern, replacement);
	if (out === text) {
		throw new Error("impala hardening: " + label + " did not apply - the generated shape moved");
	}
	return out;
}

function applyImpalaHardening(source, grammar) {
	let patched = source;
	// First-declarator tripwire only: matching the whole statement false-positives on reserved
	// names inside right-hand-side strings (GAZL2 has `var tag = '_i' + ...`).
	const reservedLocal = grammar.match(/\bvar\s+(_val|_s|_im|_i)\b/);
	if (reservedLocal) {
		throw new Error("impala.jspeg action declares a reserved parser local: " + reservedLocal[0].trim());
	}
	const impalaImplSignature = "var impalaCompilerImpl = (function(_s) {";
	patched = mustReplace(
		patched,
			impalaImplSignature,
			() => [
				"var impalaCompilerImpl = (function(_s, _options) {",
				"var _hostOptions = _options || {};",
				"var output = (typeof _hostOptions.output === 'function') ? _hostOptions.output : function () {};",
				"var hostRandomId = Object.prototype.hasOwnProperty.call(_hostOptions, 'randomId')",
				"\t? _hostOptions.randomId",
				"\t: undefined;",
				"$$parser.sourceName = Object.prototype.hasOwnProperty.call(_hostOptions, 'sourceName')",
				"\t? _hostOptions.sourceName",
				"\t: undefined;",
			].join("\n"),
		"options prelude");
	patched = mustReplace(patched, /\$[A-Za-z0-9_]*=\{\}/g,
		(match) => match.replace("={}", "=newMetaSlot()"), "capture-slot init");

	const keywordFunctionRegex = /function KEYWORD\(\)\{[^\n]*\n/;
	const keywordFunctionReplacement =
		"function KEYWORD(){var _b=_i,_words=KEYWORD_WORDS,_word,_end,_x;" +
		"for(var _k=0;_k<_words.length;++_k){" +
		"_word=_words[_k];" +
		"if(_s.substr(_i,_word.length)===_word){" +
		"_i+=_word.length;" +
		"_end=_i;" +
		"_x=SYMBOL_CHAR();" +
		"_i=_end;" +
		"if(!_x)return true;" +
		"_i=_b;" +
		"}}_im=(_i>_im?_i:_im);_i=_b;return false}\n";
	{
		const words = keywordWordsFrom(grammar);
		const rows = [];
		for (let i = 0; i < words.length; i += 11) {
			rows.push("\t" + words.slice(i, i + 11).map((w) => `'${w}'`).join(", "));
		}
		// Join the rows with the separator rather than computing a last-row test per row: the boundary
		// condition has to be right for the GENERATED file to parse, in a script whose whole job is to
		// produce a file nobody hand-edits.
		patched = mustReplace(patched,
			"var _hostOptions = _options || {};",
			["var _hostOptions = _options || {};", "var KEYWORD_WORDS = [",
					rows.join(",\n"), "];"].join("\n"),
			"KEYWORD word list");
		patched = mustReplace(patched, keywordFunctionRegex, keywordFunctionReplacement, "KEYWORD scanner");
	}
	const rootInitPattern = "var _i=0,_im=0,_val,_b=root();";
	const hardenedRootInit = "var _i=0,_im=0,_val=newMetaSlot(),_b=root();";
	patched = mustReplace(patched, rootInitPattern, hardenedRootInit, "root init");

	return patched;
}

function resolve(file) {
	return path.join(root, file);
}

function read(file) {
	return fs.readFileSync(resolve(file), "utf8");
}

function write(file, contents) {
	fs.writeFileSync(resolve(file), contents);
}

/* Both outputs are git-tracked, so without a banner they read as hand-maintained sources. It goes here
   rather than at the write, because jspegCompilerTests.js byte-compares its own wrapCompilerSource() call
   against the file on disk - one copy is the only way the gate and the generator cannot disagree. Keep it
   deterministic: a timestamp would fail --check on every run. */
const GRAMMAR_OF = { compileJSPEG: "jspeg.jspeg", impalaCompiler: "impala.jspeg" };

function wrapCompilerSource(exportName, generated, options = {}) {
	const body = generated.trimEnd();
	const { prelude, exposeSourceNameOption } = options;
	const lines = [`/* GENERATED from impala/${GRAMMAR_OF[exportName]} by \`node impala/updateJSPEG.js\``
		+ " -- do not edit by hand. */"];
	if (prelude) {
		const entries = Array.isArray(prelude) ? prelude : [prelude];
		entries.forEach((line) => {
			lines.push(line);
		});
	}

	if (exposeSourceNameOption) {
		const implName = `${exportName}Impl`;
		lines.push(`var ${implName} = ${body};`);
		lines.push(
			`function ${exportName}(source, options) {`,
			"\tvar compilerOptions;",
			"\tif (typeof options === 'string') {",
			"\t\tcompilerOptions = { sourceName: options };",
			"\t} else if (options) {",
			"\t\tcompilerOptions = options;",
			"\t} else {",
			"\t\tcompilerOptions = {};",
			"\t}",
			`\treturn ${implName}(source, compilerOptions);`,
			"}",
		);
		lines.push(
			"if (typeof module !== 'undefined' && module.exports) {",
			`\tmodule.exports = ${exportName};`,
			`\tmodule.exports.${exportName} = ${exportName};`,
			`\tmodule.exports.default = ${exportName};`,
			`\tmodule.exports.raw = ${implName};`,
			"}",
		);
	} else {
		lines.push(
			`var ${exportName} = ${body};`,
			"if (typeof module !== 'undefined' && module.exports) {",
			`\tmodule.exports = ${exportName};`,
			`\tmodule.exports.${exportName} = ${exportName};`,
			`\tmodule.exports.default = ${exportName};`,
			"}",
		);
	}

	lines.push("");
	return lines.join("\n");
}

function sanitizeFilename(label) {
	const safe = label.replace(/[^A-Za-z0-9_.-]+/g, "_");
	return safe.endsWith(".js") ? safe : `${safe}.js`;
}

function loadCompiler(source, description) {
	const filename = path.join(root, sanitizeFilename(description));
	const compilerModule = new Module(filename, module);
	compilerModule.filename = filename;
	compilerModule.paths = Module._nodeModulePaths(path.dirname(filename));
	compilerModule.require = Module.createRequire(filename);
	compilerModule._compile(source, filename);
	const exports = compilerModule.exports;

	if (typeof exports === "function") {
		return exports;
	}
	if (exports && typeof exports === "object") {
		if (typeof exports.compileJSPEG === "function") {
			return exports.compileJSPEG;
		}
		if (typeof exports.default === "function") {
			return exports.default;
		}
	}

	throw new Error(`${description} did not define a compiler function`);
}

function compileWith(fn, source, label) {
	const result = fn(source);
	if (!result || !Array.isArray(result) || result.length < 2) {
		throw new Error(`${label} did not return a [success, code, index] tuple`);
	}
	const [ok, generated, index] = result;
	if (!ok) {
		const location = typeof index === "number" ? ` at index ${index}` : "";
		throw new Error(`${label} failed to compile${location}`);
	}
	return generated;
}

function canonicalize(str) {
	return str.replace(/\r\n/g, "\n");
}

function canonicalizeTrimmed(str) {
	return canonicalize(str).trim();
}

function runRegressionTests() {
	const result = child_process.spawnSync(process.execPath, [resolve("jspegCompilerTests.js")], {
		stdio: "inherit",
	});
	if (result.status !== 0) {
		throw new Error("JSPEG regression tests failed");
	}
}

function regenerate() {
	const compilerSource = read("jspegCompiler.js");
	const compileJSPEG = loadCompiler(compilerSource, "jspegCompiler.js");

	const grammarSource = read("jspeg.jspeg");
	const generatedCompiler = compileWith(compileJSPEG, grammarSource, "jspeg.jspeg");

	const wrappedGeneratedCompiler = wrapCompilerSource("compileJSPEG", generatedCompiler);
	const updatedCompileJSPEG = loadCompiler(wrappedGeneratedCompiler, "generated jspeg compiler");
	const regenerated = compileWith(updatedCompileJSPEG, grammarSource, "jspeg.jspeg (self-host)");
	if (canonicalize(regenerated) !== canonicalize(generatedCompiler)) {
		throw new Error("Self-hosted compile produced different output for jspeg.jspeg");
	}

	const impalaGrammar = read("impala.jspeg");
	const generatedImpala = compileWith(updatedCompileJSPEG, impalaGrammar, "impala.jspeg");

	return {
		jspegCompiler: wrappedGeneratedCompiler,
		impalaCompiler: applyImpalaHardening(
			wrapCompilerSource("impalaCompiler", generatedImpala, {
				prelude: "var $$parser = {};",
				exposeSourceNameOption: true,
			}),
			impalaGrammar,
		),
	};
}

function writeOutputs(outputs) {
	write("jspegCompiler.js", outputs.jspegCompiler);
	write("impalaCompiler.js", outputs.impalaCompiler);
}

function checkOutputs(outputs) {
	const currentJspeg = canonicalizeTrimmed(read("jspegCompiler.js"));
	const currentImpala = canonicalizeTrimmed(read("impalaCompiler.js"));
	const expectedJspeg = canonicalizeTrimmed(outputs.jspegCompiler);
	const expectedImpala = canonicalizeTrimmed(outputs.impalaCompiler);

	return {
		jspegMatches: currentJspeg === expectedJspeg,
		impalaMatches: currentImpala === expectedImpala,
	};
}

function main(args) {
	if (args.length > 1 || (args.length === 1 && args[0] !== "--check")) {
		console.error("Usage: node updateJSPEG.js [--check]");
		process.exit(1);
	}

	const outputs = regenerate();
	if (args[0] === "--check") {
		const { jspegMatches, impalaMatches } = checkOutputs(outputs);
		if (!jspegMatches || !impalaMatches) {
			console.error('JSPEG outputs are stale. Run "node updateJSPEG.js" to regenerate them.');
			if (!jspegMatches) {
				console.error(" - jspegCompiler.js");
			}
			if (!impalaMatches) {
				console.error(" - impalaCompiler.js");
			}
			process.exit(1);
		}
		console.log("JSPEG compilers are up to date.");
		console.log("Running JSPEG regression tests...");
		runRegressionTests();
		console.log("JSPEG regression tests passed.");
		return;
	}

	writeOutputs(outputs);
	console.log("Regenerated jspegCompiler.js and impalaCompiler.js");
	console.log("Running JSPEG regression tests...");
	runRegressionTests();
	console.log("JSPEG regression tests passed.");
}

if (require.main === module) {
	main(process.argv.slice(2));
} else {
	module.exports = {
		regenerate,
		writeOutputs,
		checkOutputs,
		wrapCompilerSource,
		applyImpalaHardening,
	};
}
