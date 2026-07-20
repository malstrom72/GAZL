#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
const Module = require("module");
const child_process = require("child_process");

const root = __dirname;

function applyImpalaHardening(source) {
	let patched = source;
	const metaSectionHeader =
		"\t/* --------------------------------------------------------- *\n" +
		"\t *  Debug helpers & meta-record construction / destruction   *\n" +
		"\t * --------------------------------------------------------- */\n\n";
	const createContextHelper =
		"\tcreateParserContext = function () {\n" +
		"\t\treturn {\n" +
		"\t\t\t_: { operator: undefined, type: undefined,\n" +
		"\t\t\t\t operands: [ undefined, undefined, undefined ] }\n" +
		"\t\t};\n" +
		"\t};\n\n";
	if (!patched.includes("createParserContext = function ()")) {
		patched = patched.replace(metaSectionHeader, createContextHelper + metaSectionHeader);
	}

	const impalaImplSignature = "var impalaCompilerImpl = (function(_s) {";
	if (patched.includes(impalaImplSignature)) {
		patched = patched.replace(
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
		);
	}

	const metaSlotRegex = /\tfunction metaSlot\(node\) \{[\s\S]*?\t\}\n\n/;
	const metaSlotReplacement =
		"\tfunction metaSlot(node) {\n" +
		"\t\tif (node == null || (typeof node !== 'object' && typeof node !== 'function')) {\n" +
		"\t\t\treturn { operator: undefined, type: undefined,\n" +
		"\t\t\t\t\t operands: [ undefined, undefined, undefined ] };\n" +
		"\t\t}\n" +
		"\t\tif (node.operands !== undefined) {\n" +
		"\t\t\tif (!Array.isArray(node.operands)) {\n" +
		"\t\t\t\tnode.operands = [ undefined, undefined, undefined ];\n" +
		"\t\t\t} else {\n" +
		"\t\t\t\twhile (node.operands.length < 3) {\n" +
		"\t\t\t\t\tnode.operands.push(undefined);\n" +
		"\t\t\t\t}\n" +
		"\t\t\t}\n" +
		"\t\t\tif (!Object.prototype.hasOwnProperty.call(node, 'operator')) {\n" +
		"\t\t\t\tnode.operator = undefined;\n" +
		"\t\t\t}\n" +
		"\t\t\tif (!Object.prototype.hasOwnProperty.call(node, 'type')) {\n" +
		"\t\t\t\tnode.type = undefined;\n" +
		"\t\t\t}\n" +
		"\t\t\treturn node;\n" +
		"\t\t}\n\n" +
		"\t\tif (!Object.prototype.hasOwnProperty.call(node, '_')) {\n" +
		"\t\t\tif (node.operands === undefined) {\n" +
		"\t\t\t\tnode.operands = [ undefined, undefined, undefined ];\n" +
		"\t\t\t}\n" +
		"\t\t\tif (!Object.prototype.hasOwnProperty.call(node, 'operator')) {\n" +
		"\t\t\t\tnode.operator = undefined;\n" +
		"\t\t\t}\n" +
		"\t\t\tif (!Object.prototype.hasOwnProperty.call(node, 'type')) {\n" +
		"\t\t\t\tnode.type = undefined;\n" +
		"\t\t\t}\n" +
		"\t\t\treturn node;\n" +
		"\t\t}\n\n" +
		"\t\tvar slot = node._;\n" +
		"\t\tif (!slot || slot.operands === undefined) {\n" +
		"\t\t\tslot = { operator: undefined, type: undefined,\n" +
		"\t\t\t\t\t operands: [ undefined, undefined, undefined ] };\n" +
		"\t\t\tnode._ = slot;\n" +
		"\t\t}\n" +
		"\t\treturn slot;\n" +
		"\t}\n\n";
	patched = patched.replace(metaSlotRegex, metaSlotReplacement);

	patched = patched.replace(/\$[A-Za-z0-9_]*=\{\}/g, (match) => match.replace("={}", "=createParserContext()"));

	const keywordFunctionRegex = /function KEYWORD\(\$\)\{[^\n]*\n/;
	const keywordFunctionReplacement =
		"function KEYWORD($){var _b=_i,_words=KEYWORD_WORDS,_word,_end,_x;" +
		"for(var _k=0;_k<_words.length;++_k){" +
		"_word=_words[_k];" +
		"if(_s.substr(_i,_word.length)===_word){" +
		"_i+=_word.length;" +
		"_end=_i;" +
		"_x=SYMBOL_CHAR($);" +
		"_i=_end;" +
		"if(!_x)return true;" +
		"_i=_b;" +
		"}}_im=(_i>_im?_i:_im);_i=_b;return false}\n";
	if (keywordFunctionRegex.test(patched) && !patched.includes("KEYWORD_WORDS")) {
		patched = patched.replace(
			"var _hostOptions = _options || {};",
			[
				"var _hostOptions = _options || {};",
				"var KEYWORD_WORDS = [",
				"\t'abs', 'array', 'assert', 'case', 'const', 'copy', 'default', 'do', 'else', 'export', 'extern',",
				"\t'float', 'floor', 'for', 'from', 'ftoi', 'funcptr', 'functype', 'function', 'global', 'goto', 'if',",
				"\t'import', 'int', 'itof', 'locals', 'loop', 'native', 'null', 'nullfunc', 'pointer', 'readonly',",
				"\t'returns', 'sizeof', 'struct', 'switch', 'temporary', 'to', 'while'",
				"];",
			].join("\n"),
		);
		patched = patched.replace(keywordFunctionRegex, keywordFunctionReplacement);
	}

	const failFunctionPattern =
		"\tfail = function (error, source, offset) {\n" +
		"\t\tfunction oneLine(s) { return replace(replace(replace(s,\"\\t\",' '),\"\\r\",' '),\"\\n\",' '); }\n" +
		"\t\tthrow bake(error) + ' : ' +\n" +
		"\t\t      oneLine(source.substr(offset - 8, 8)) + ' <!!!!> ' +\n" +
		"\t\t      oneLine(source.substr(offset, 40));\n" +
		"\t};\n";
	const failFunctionReplacement =
		"\tfail = function (error, source, offset) {\n" +
		"\t\tfunction oneLine(s) { return replace(replace(replace(s,\"\\t\",' '),\"\\r\",' '),\"\\n\",' '); }\n" +
		"\t\tvar message = bake(error);\n" +
		"\t\tvar hasSource = typeof source === 'string';\n" +
		"\t\tvar snippetSource = hasSource ? source : '';\n" +
		"\t\tvar snippetOffset = isFinite(offset) ? offset : 0;\n" +
		"\t\tvar before = oneLine(snippetSource.substr(snippetOffset - 8, 8));\n" +
		"\t\tvar after = oneLine(snippetSource.substr(snippetOffset, 40));\n" +
		"\t\tvar err = new Error(message + ' : ' + before + ' <!!!!> ' + after);\n" +
		"\t\terr.impalaMessage = message;\n" +
		"\t\tif (isFinite(offset)) {\n" +
		"\t\t\terr.impalaOffset = offset;\n" +
		"\t\t}\n" +
		"\t\terr.impalaSnippetBefore = before;\n" +
		"\t\terr.impalaSnippetAfter = after;\n" +
		"\t\tthrow err;\n" +
		"\t};\n";
	patched = patched.includes("err.impalaMessage = message;") ? patched : patched.replace(failFunctionPattern, failFunctionReplacement);

	const makeMetaMarker = "\tmakeMeta = function (rec, op, type, op0, op1, op2) {";
	if (!patched.includes("rec = metaSlot(rec);")) {
		patched = patched.replace(makeMetaMarker, `${makeMetaMarker}\n\t\trec = metaSlot(rec);`);
	}

	const assignRegex = /\tassign = function \(x, leftx, rightx,\n[ \t]+sourceCode, sourceOffset\) \{/;
	const assignGuard =
		"\n\t\tif (!leftx || leftx.operator === undefined) {\n" +
		"\t\t\tthrow new Error('JSPEG meta missing for assignment: ' + JSON.stringify(leftx));\n" +
		"\t\t}";
	patched = assignRegex.test(patched)
		? patched.replace(assignRegex, (match) => (match.includes("JSPEG meta missing") ? match : `${match}${assignGuard}`))
		: patched;

	const rootInitPattern = "var _i=0,_im=0,_o={_:void 0},_b=root(_o);";
	const hardenedRootInit = "var _i=0,_im=0,_o=createParserContext();\n_o.options=_hostOptions;\nvar _b=root(_o);";
	if (!patched.includes(hardenedRootInit)) {
		patched = patched.replace(rootInitPattern, hardenedRootInit);
	}
	if (!patched.includes("function createParserContext() {")) {
		const globalHelper =
			"function createParserContext() {\n" +
			"        return {\n" +
			"                _: { operator: undefined, type: undefined, operands: [ undefined, undefined, undefined ] }\n" +
			"        };\n" +
			"}\n";
		patched = patched.replace(hardenedRootInit, `${globalHelper}${hardenedRootInit}`);
	}

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

function wrapCompilerSource(exportName, generated, options = {}) {
	const body = generated.trimEnd();
	const { prelude, exposeSourceNameOption } = options;
	const lines = [];
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
