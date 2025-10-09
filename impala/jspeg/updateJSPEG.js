#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");
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
"\t\tvar holder = {};\n" +
"\t\tObject.defineProperty(holder, '__metaSlot', {\n" +
"\t\t\tvalue: { operator: undefined, type: undefined,\n" +
"\t\t\t\t\t operands: [ undefined, undefined, undefined ] },\n" +
"\t\t\twritable: true,\n" +
"\t\t\tconfigurable: true\n" +
"\t\t});\n" +
"\t\tObject.defineProperty(holder, '_', {\n" +
"\t\t\tconfigurable: true,\n" +
"\t\t\tget: function () {\n" +
"\t\t\t\tif (!Object.prototype.hasOwnProperty.call(this, '__metaSlot')) {\n" +
"\t\t\t\t\tObject.defineProperty(this, '__metaSlot', {\n" +
"\t\t\t\t\t\tvalue: { operator: undefined, type: undefined,\n" +
"\t\t\t\t\t\t\t operands: [ undefined, undefined, undefined ] },\n" +
"\t\t\t\t\t\twritable: true,\n" +
"\t\t\t\t\t\tconfigurable: true\n" +
"\t\t\t\t\t});\n" +
"\t\t\t\t}\n" +
"\t\t\t\treturn this.__metaSlot;\n" +
"\t\t\t},\n" +
"\t\t\tset: function (value) {\n" +
"\t\t\t\tObject.defineProperty(this, '__metaSlot', {\n" +
"\t\t\t\t\tvalue: value,\n" +
"\t\t\t\t\twritable: true,\n" +
"\t\t\t\t\tconfigurable: true\n" +
"\t\t\t\t});\n" +
"\t\t\t}\n" +
"\t\t});\n" +
"\t\treturn holder;\n" +
"\t};\n" +
"\tif (typeof globalThis !== 'undefined' && typeof globalThis.createParserContext !== 'function') {\n" +
"\t\tglobalThis.createParserContext = createParserContext;\n" +
"\t}\n\n";
if (!patched.includes("createParserContext = function ()")) {
patched = patched.replace(metaSectionHeader, createContextHelper + metaSectionHeader);
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

patched = patched.replace(/\$[A-Za-z0-9_]*=\{\}/g, (match) => match.replace('={}', '=createParserContext()'));

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
"\t\tvar snippetOffset = Number.isFinite(offset) ? offset : 0;\n" +
"\t\tvar before = oneLine(snippetSource.substr(snippetOffset - 8, 8));\n" +
"\t\tvar after = oneLine(snippetSource.substr(snippetOffset, 40));\n" +
"\t\tvar err = new Error(message + ' : ' + before + ' <!!!!> ' + after);\n" +
"\t\terr.impalaMessage = message;\n" +
"\t\tif (Number.isFinite(offset)) {\n" +
"\t\t\terr.impalaOffset = offset;\n" +
"\t\t}\n" +
"\t\terr.impalaSnippetBefore = before;\n" +
"\t\terr.impalaSnippetAfter = after;\n" +
"\t\tthrow err;\n" +
"\t};\n";
patched = patched.includes("err.impalaMessage = message;")
? patched
: patched.replace(failFunctionPattern, failFunctionReplacement);

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
? patched.replace(assignRegex, (match) => (match.includes('JSPEG meta missing') ? match : `${match}${assignGuard}`))
: patched;

const rootInitPattern = "var _i=0,_im=0,_o={_:void 0},";
const hardenedRootInit = "var _i=0,_im=0,_o=createParserContext(),";
if (!patched.includes(hardenedRootInit)) {
patched = patched.replace(rootInitPattern, hardenedRootInit);
}
if (!patched.includes("function createParserContext() {")) {
const globalHelper =
"function createParserContext() {\n" +
"        var holder = {};\n" +
"        Object.defineProperty(holder, '__metaSlot', {\n" +
"                value: { operator: undefined, type: undefined, operands: [ undefined, undefined, undefined ] },\n" +
"                writable: true,\n" +
"                configurable: true\n" +
"        });\n" +
"        Object.defineProperty(holder, '_', {\n" +
"                configurable: true,\n" +
"                get: function () {\n" +
"                        if (!Object.prototype.hasOwnProperty.call(this, '__metaSlot')) {\n" +
"                                Object.defineProperty(this, '__metaSlot', {\n" +
"                                        value: { operator: undefined, type: undefined, operands: [ undefined, undefined, undefined ] },\n" +
"                                        writable: true,\n" +
"                                        configurable: true\n" +
"                                });\n" +
"                        }\n" +
"                        return this.__metaSlot;\n" +
"                },\n" +
"                set: function (value) {\n" +
"                        Object.defineProperty(this, '__metaSlot', {\n" +
"                                value: value,\n" +
"                                writable: true,\n" +
"                                configurable: true\n" +
"                        });\n" +
"                }\n" +
"        });\n" +
"        return holder;\n" +
"}\n" +
"if (typeof globalThis !== 'undefined' && typeof globalThis.createParserContext !== 'function') {\n" +
"        globalThis.createParserContext = createParserContext;\n" +
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
			"\tvar sourceName;",
			"\tif (typeof options === 'string') {",
			"\t\tsourceName = options;",
			"\t} else if (options && Object.prototype.hasOwnProperty.call(options, 'sourceName')) {",
			"\t\tsourceName = options.sourceName;",
			"\t} else if (options && options.sourceName !== undefined) {",
			"\t\tsourceName = options.sourceName;",
			"\t} else {",
			"\t\tsourceName = undefined;",
			"\t}",
			"\t$$parser.sourceName = sourceName;",
			`\treturn ${implName}(source);`,
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

function loadCompiler(source, description) {
	const context = {
		console,
		module: { exports: {} },
		exports: {},
	};
	vm.createContext(context);
	const script = new vm.Script(source, { filename: description });
	script.runInContext(context);

	if (typeof context.module.exports === "function") {
		return context.module.exports;
	}
	if (typeof context.exports === "function") {
		return context.exports;
	}
	if (typeof context.compileJSPEG === "function") {
		return context.compileJSPEG;
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
