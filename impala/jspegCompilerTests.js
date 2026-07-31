const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vm = require("vm");

const { wrapCompilerSource, applyImpalaHardening } = require("./updateJSPEG.js");
const { compileWithJsImpala } = require("./impalaJsCompilerRunner");
const { haveGazlCmd, assembleOnly, NEEDS_HOST } = require("./gazlAssembleCheck");

const dir = __dirname;
const IMPALA_ENCODING = "latin1";
const validatorScript = path.join(dir, "..", "tools", "gazl-validate.nuxjs.js");
const nuxjsExe = path.join(dir, "..", "output", process.platform === "win32" ? "NuXJS.exe" : "NuXJS");
const validatorFixturesDir = path.join(dir, "testdata", "validator");

function canonicalizeNewlines(source) {
	return source.replace(/\r\n?/g, "\n");
}

function canonicalizeTrimmed(source) {
	return canonicalizeNewlines(source).trim();
}

function canonicalizeTrimEnd(source) {
	return canonicalizeNewlines(source).trimEnd();
}

const jspegSource = fs.readFileSync(path.join(dir, "jspegCompiler.js"), "utf8");
const compileJSPEG = require(path.join(dir, "jspegCompiler.js"));
if (typeof compileJSPEG !== "function") {
	console.error("jspegCompiler.js did not export a compiler function");
	process.exit(1);
}

const jspegGrammar = fs.readFileSync(path.join(dir, "jspeg.jspeg"), "utf8");
const [compilerOk, compilerGenerated, compilerIndex] = compileJSPEG(jspegGrammar);
if (!compilerOk) {
	console.error("Failed to compile jspeg.jspeg with recorded compiler");
	process.exit(1);
}
if (compilerIndex !== jspegGrammar.length) {
	console.error(`jspeg.jspeg compile stopped at ${compilerIndex} of ${jspegGrammar.length}`);
	process.exit(1);
}
const expectedJspegSource = wrapCompilerSource("compileJSPEG", compilerGenerated);
if (canonicalizeTrimmed(expectedJspegSource) !== canonicalizeTrimmed(jspegSource)) {
	console.error("jspegCompiler.js is out of date with jspeg.jspeg");
	process.exit(1);
}
console.log("jspegCompiler.js matches jspeg.jspeg output");

const compileJSPEGSelfHosted = eval(compilerGenerated);
const [selfHostOk, selfHostGenerated, selfHostIndex] = compileJSPEGSelfHosted(jspegGrammar);
if (!selfHostOk) {
	console.error("Self-hosted compiler failed to compile jspeg.jspeg");
	process.exit(1);
}
if (selfHostIndex !== jspegGrammar.length) {
	console.error(`Self-hosted compile stopped at ${selfHostIndex} of ${jspegGrammar.length}`);
	process.exit(1);
}
if (canonicalizeTrimmed(wrapCompilerSource("compileJSPEG", selfHostGenerated)) !== canonicalizeTrimmed(jspegSource)) {
	console.error("Self-hosted compiler drifted from recorded jspegCompiler.js output");
	process.exit(1);
}
console.log("Self-hosted compile of jspeg.jspeg matches recorded compiler");

const impalaGrammar = fs.readFileSync(path.join(dir, "impala.jspeg"), "utf8");
const [impalaOk, impalaGenerated, impalaIndex] = compileJSPEG(impalaGrammar);
if (!impalaOk) {
	console.error("Failed to compile impala.jspeg with recorded compiler");
	process.exit(1);
}
if (impalaIndex !== impalaGrammar.length) {
	console.error(`impala.jspeg compile stopped at ${impalaIndex} of ${impalaGrammar.length}`);
	process.exit(1);
}
const impalaExisting = fs.readFileSync(path.join(dir, "impalaCompiler.js"), "utf8");
const impalaExpected = applyImpalaHardening(
	wrapCompilerSource("impalaCompiler", impalaGenerated, {
		prelude: "var $$parser = {};",
		exposeSourceNameOption: true,
	}),
).trim();
if (canonicalizeTrimmed(impalaExpected) !== canonicalizeTrimmed(impalaExisting)) {
	console.error("Generated compiler differs from impalaCompiler.js");
	process.exit(1);
}
console.log("impalaCompiler.js matches generated output");

const [impalaSelfOk, impalaSelfGenerated, impalaSelfIndex] = compileJSPEGSelfHosted(impalaGrammar);
if (!impalaSelfOk) {
	console.error("Self-hosted compiler failed to compile impala.jspeg");
	process.exit(1);
}
if (impalaSelfIndex !== impalaGrammar.length) {
	console.error(`Self-hosted impala.jspeg compile stopped at ${impalaSelfIndex} of ${impalaGrammar.length}`);
	process.exit(1);
}
if (canonicalizeTrimmed(impalaGenerated) !== canonicalizeTrimmed(impalaSelfGenerated)) {
	console.error("impala.jspeg output diverged between recorded and self-hosted compilers");
	process.exit(1);
}
const impalaSelfExpected = applyImpalaHardening(
	wrapCompilerSource("impalaCompiler", impalaSelfGenerated, {
		prelude: "var $$parser = {};",
		exposeSourceNameOption: true,
	}),
).trim();
if (canonicalizeTrimmed(impalaSelfExpected) !== canonicalizeTrimmed(impalaExisting)) {
	console.error("Self-hosted impalaCompiler.js differs from recorded output after hardening");
	process.exit(1);
}
console.log("impala.jspeg compiles identically under self-hosted compiler");

assert(!impalaExisting.includes("Object.defineProperty"), "impalaCompiler.js must not require descriptor support for parser context");

const compilerContext = loadImpalaCompilerForTests();
const makeMetaHelper = compilerContext.makeMeta;
const assignHelper = compilerContext.assign;
const failHelper = compilerContext.fail;

assert(
	!Object.prototype.hasOwnProperty.call(compilerContext, "createParserContext"),
	"impala compiler must keep createParserContext private",
);
assert(typeof makeMetaHelper === "function", "makeMeta helper must be callable");
assert(typeof assignHelper === "function", "assign helper must be callable");
assert(typeof failHelper === "function", "fail helper must be callable");

const primitiveMeta = makeMetaHelper(42, "test", "i", "#1", "#2", "#3");
assert(primitiveMeta && primitiveMeta.operator === "test", "makeMeta must return a record with assigned operator");
assert(Array.isArray(primitiveMeta.operands) && primitiveMeta.operands.length === 3, "makeMeta must normalise operand array length");

const nullMeta = makeMetaHelper(null, undefined, undefined, undefined, undefined, undefined);
assert(Array.isArray(nullMeta.operands) && nullMeta.operands.length === 3, "makeMeta must create placeholder records for null");

const malformedHolder = { operands: "oops", type: "f" };
makeMetaHelper(malformedHolder, "=", "f", "%0", "%1", undefined);
assert(
	Array.isArray(malformedHolder.operands) && malformedHolder.operands.length === 3,
	"metaSlot must coerce non-array operands to fixed arity",
);
assert(Object.prototype.hasOwnProperty.call(malformedHolder, "operator"), "metaSlot must stamp operator property on plain objects");

let observedMissingMetaGuard = false;
try {
	assignHelper(
		{},
		{ operands: [undefined, "%L0", undefined], type: "i" },
		{ operator: ":=", type: "i", operands: [undefined, "%R0", undefined] },
		"missing meta",
		0,
	);
} catch (err) {
	observedMissingMetaGuard = err && err.message && err.message.includes("JSPEG meta missing for assignment");
}
assert(observedMissingMetaGuard, "assign must reject l-values without operator metadata");

let capturedFailError;
try {
	failHelper("boom", "0123456789abcdefghij", 10);
} catch (err) {
	capturedFailError = err;
}
const isErrorObject =
	capturedFailError &&
	typeof capturedFailError === "object" &&
	(capturedFailError instanceof Error || (capturedFailError.constructor && capturedFailError.constructor.name === "Error"));
assert(isErrorObject, "fail must throw Error instances");
assert(capturedFailError.impalaMessage === "boom", "fail must record original error message");
assert(capturedFailError.impalaOffset === 10, "fail must capture numeric offsets");
assert(capturedFailError.impalaSnippetBefore === "23456789", "fail must store snippet before cursor");
assert(capturedFailError.impalaSnippetAfter.startsWith("abcdefgh"), "fail must store snippet after cursor");

testPlainHostImpalaCompiler(impalaExisting);
testCompilerRunnerOmitsDefaultRandomId();
testCompilerRunnerRandomIdSeeding();
testStringLabelFloatLiteralCollision();
testNuXJSCommandCompilerScript(impalaExisting);

function compileAndEval(compilerFn, source, label) {
	const [ok, generated, endIndex] = compilerFn(source);
	if (!ok) {
		console.error(`${label} failed to compile`);
		process.exit(1);
	}
	if (endIndex !== source.length) {
		console.error(`${label} stopped at ${endIndex} of ${source.length}`);
		process.exit(1);
	}
	let parser;
	try {
		parser = eval(generated);
	} catch (err) {
		console.error(`${label} generated invalid JavaScript`);
		console.error(err);
		process.exit(1);
	}
	return { code: generated, parser };
}

function jsonEqual(a, b) {
	return JSON.stringify(a) === JSON.stringify(b);
}

function assert(condition, message) {
	if (!condition) {
		console.error(message);
		process.exit(1);
	}
}

// Compile `source` and require it to fail with `expectError` in the message, or to succeed when
// `expectError` is null. Every "this construct must be rejected" table goes through here, so the
// convention (and the diagnostic on an unmet expectation) lives in one place.
function expectCompileOutcome(group, label, source, expectError) {
	let observed = null;
	try {
		compileWithJsImpala(source + "\n", { randomId: 42 });
	} catch (err) {
		observed = err && err.message ? err.message : String(err);
	}
	if (expectError === null || expectError === undefined) {
		assert(observed === null, `${group}: ${label} unexpectedly failed\n${observed}`);
	} else {
		assert(observed !== null && observed.includes(expectError),
			`${group}: ${label} did not raise "${expectError}"${observed === null ? "" : "\n" + observed}`);
		// The message alone is not the diagnostic. Three doors carried the right text while passing a
		// bogus source position, so they rendered with no code, no line and no caret - and every test
		// here still passed. Require the rendered shape, not just the wording.
		assert(/^[^\n]*:\d+:\d+: error\[E\d+\]: /m.test(observed),
			`${group}: ${label} raised an unrendered diagnostic (no file:line:col or error code)\n${observed}`);
	}
}

// `expectCompileOutcome` only proves SOME position rendered, which is what let a whole class of carets drift
// onto the next line or the next declaration unnoticed. Pin the exact `line:col: error[code]` instead.
function expectDiagnosticAt(label, source, expected) {
	let observed = null;
	try {
		compileWithJsImpala(source, { randomId: 42 });
	} catch (err) {
		observed = err && err.message ? err.message : String(err);
	}
	assert(observed !== null, `caret: ${label} unexpectedly compiled`);
	assert(observed.includes(expected),
		`caret: ${label} did not report at ${expected}\n${observed}`);
}

// A strict-expression error must downgrade to exactly one warning under --legacy, carrying the same wording.
// Three checks (mixed bitwise, comparison mix, `!` precedence) verify this identically; share the shape.
function expectSingleLegacyWarning(source, fragment, description) {
	const warnings = [];
	try {
		compileWithJsImpala(source, {
			randomId: 42,
			legacy: true,
			onWarning: (formatted, message) => warnings.push(message),
		});
	} catch (err) {
		console.error(`impala.jspeg compiler rejected ${description} under --legacy`);
		console.error(err && err.message ? err.message : String(err));
		process.exit(1);
	}
	if (warnings.length !== 1 || !warnings[0].includes(fragment)) {
		console.error(`impala.jspeg compiler did not emit exactly one ${description} warning under --legacy`);
		process.exit(1);
	}
	console.log(`impala.jspeg compiler downgrades ${description} to a warning under --legacy`);
}

function runParserCase(label, parser, input) {
	let result;
	try {
		result = parser(input);
	} catch (err) {
		console.error(`${label} threw while parsing ${JSON.stringify(input)}`);
		console.error(err);
		process.exit(1);
	}
	if (!Array.isArray(result) || result.length !== 3) {
		console.error(`${label} returned unexpected result ${JSON.stringify(result)}`);
		process.exit(1);
	}
	return { success: !!result[0], value: result[1], index: result[2] };
}

function compareParserOutputs(label, cases, baselineParser, selfHostedParser) {
	cases.forEach((test) => {
		const baseline = runParserCase(`${label} (baseline)`, baselineParser, test.input);
		const selfHosted = runParserCase(`${label} (self-hosted)`, selfHostedParser, test.input);

		if (baseline.success !== selfHosted.success || baseline.index !== selfHosted.index || !jsonEqual(baseline.value, selfHosted.value)) {
			console.error(
				`${label} produced different results between baseline and self-hosted compilers for input ${JSON.stringify(test.input)}`,
			);
			console.error("baseline:", baseline);
			console.error("selfHosted:", selfHosted);
			process.exit(1);
		}

		if (test.expectSuccess !== undefined && baseline.success !== test.expectSuccess) {
			console.error(`${label} unexpected success=${baseline.success} for input ${JSON.stringify(test.input)}`);
			process.exit(1);
		}

		if (test.expectIndex !== undefined && baseline.index !== test.expectIndex) {
			console.error(`${label} consumed ${baseline.index} characters for input ${JSON.stringify(test.input)}, expected ${test.expectIndex}`);
			process.exit(1);
		}

		if (test.expectValue !== undefined && !jsonEqual(baseline.value, test.expectValue)) {
			console.error(`${label} produced unexpected value ${JSON.stringify(baseline.value)} for input ${JSON.stringify(test.input)}`);
			process.exit(1);
		}
	});
}

function testGrammarEquivalence(filename, label, cases) {
	const source = fs.readFileSync(path.join(dir, filename), "utf8");
	const baseline = compileAndEval(compileJSPEG, source, `${label} via baseline compiler`);
	const selfHosted = compileAndEval(compileJSPEGSelfHosted, source, `${label} via self-hosted compiler`);

	if (canonicalizeTrimmed(baseline.code) !== canonicalizeTrimmed(selfHosted.code)) {
		console.error(`${label} generated code diverges between baseline and self-hosted compilers`);
		process.exit(1);
	}

	compareParserOutputs(label, cases, baseline.parser, selfHosted.parser);
	console.log(`${label} parser matches across baseline and self-hosted compilers`);
}

function loadImpalaCompilerForTests() {
	const compilerPath = path.join(dir, "impalaCompiler.js");
	const compilerSource = fs.readFileSync(compilerPath, "utf8");
	const context = {
		console,
		module: { exports: {} },
		exports: {},
	};

	vm.createContext(context);
	const script = new vm.Script(compilerSource, { filename: "impalaCompiler.js" });
	script.runInContext(context);

	if (typeof context.module.exports !== "function") {
		console.error("impalaCompiler.js did not export a compiler function");
		process.exit(1);
	}

	if (!context.$$parser || typeof context.$$parser !== "object") {
		console.error("impalaCompiler.js did not initialise $$parser helpers");
		process.exit(1);
	}

	try {
		context.module.exports("function main()\nlocals int x\n{\n}\n", {
			sourceName: "test",
			output: () => {},
			randomId: 0xabcdef,
		});
	} catch (err) {
		console.error("impalaCompiler.js self-test compile failed");
		console.error(err && err.stack ? err.stack : err);
		process.exit(1);
	}

	if (
		Object.prototype.hasOwnProperty.call(context, "output") ||
		Object.prototype.hasOwnProperty.call(context, "impalaRandomId") ||
		Object.prototype.hasOwnProperty.call(context, "createParserContext")
	) {
		console.error("impalaCompiler.js leaked host bindings into the global context");
		process.exit(1);
	}

	return context;
}

function testPlainHostImpalaCompiler(compilerSource) {
	const context = {};
	vm.createContext(context);
	const script = new vm.Script(compilerSource, { filename: "impalaCompiler.plain.js" });
	script.runInContext(context);

	assert(typeof context.impalaCompiler === "function", "plain host context must expose impalaCompiler as a script global");
	assert(!Object.prototype.hasOwnProperty.call(context, "module"), "plain host context must not need CommonJS module");
	assert(!Object.prototype.hasOwnProperty.call(context, "output"), "plain host context must not need ambient output");
	assert(!Object.prototype.hasOwnProperty.call(context, "impalaRandomId"), "plain host context must not need ambient impalaRandomId");
	assert(!Object.prototype.hasOwnProperty.call(context, "globalThis"), "plain host context must not need injected globalThis");

	const outputLines = [];
	try {
		context.impalaCompiler("function main()\nlocals int x\n{\n}\n", {
			sourceName: "plain-host-test.impala",
			output: (line) => outputLines.push(line),
			randomId: 0xabcdef,
		});
	} catch (err) {
		console.error("impalaCompiler.js failed in plain host context");
		console.error(err && err.stack ? err.stack : err);
		process.exit(1);
	}

	assert(outputLines.length > 0, "plain host compile must emit output through options.output");
	console.log("impalaCompiler.js runs in a plain host context without Node globals");
}

function testCompilerRunnerOmitsDefaultRandomId() {
	const output = compileWithJsImpala("", {
		compilerSource: `
			module.exports = function (source, options) {
				if (Object.prototype.hasOwnProperty.call(options, "randomId")) {
					throw new Error("randomId should be omitted when no seed is supplied");
				}
				options.output("OK");
				return [true, "", source.length];
			};
		`,
		retabulate: false,
		trailingNewline: false,
	});

	assert(output === "OK", "compileWithJsImpala must run without injecting a default randomId");
	console.log("compileWithJsImpala omits randomId when no seed is supplied");
}

function testCompilerRunnerRandomIdSeeding() {
	const source = fs.readFileSync(path.join(dir, "..", "tests", "impala", "sources", "calc.impala"), IMPALA_ENCODING);
	const originalRandom = Math.random;
	let randomValue = 0.125;

	try {
		Math.random = () => randomValue;
		const unseededA = compileWithJsImpala(source, { retabulate: false, trailingNewline: false });
		randomValue = 0.875;
		const unseededB = compileWithJsImpala(source, { retabulate: false, trailingNewline: false });
		assert(unseededA !== unseededB, "compileWithJsImpala must generate different labels when no randomId is supplied");

		randomValue = 0.125;
		const seededA = compileWithJsImpala(source, { randomId: 42, retabulate: false, trailingNewline: false });
		randomValue = 0.875;
		const seededB = compileWithJsImpala(source, { randomId: 42, retabulate: false, trailingNewline: false });
		assert(seededA === seededB, "compileWithJsImpala must generate deterministic labels when randomId is supplied");
	} finally {
		Math.random = originalRandom;
	}

	console.log("compileWithJsImpala randomizes omitted seeds and honors explicit seeds");
}

function testStringLabelFloatLiteralCollision() {
	const source = [
		"readonly array panelTextRows[1] = {",
		'\t"GRBLEN"',
		"}",
		"",
		"function main()",
		"{",
		"}",
		"",
	].join("\n");
	// A negative randomId is the real trigger: (negative).toString(16) starts with '-',
	// which is not a valid GAZL identifier character. Force the value unsigned first.
	const output = compileWithJsImpala(source, {
		randomId: -0x326982e7,
		sourceName: "evighet_code.impala",
		retabulate: false,
		trailingNewline: false,
	});

	assert(
		output.includes(".s_GRBLEN_cd967d19"),
		"string labels must emit the hex id as an unsigned 32-bit value",
	);
	assert(!/\.s_GRBLEN[^\s:]*-/.test(output), "string labels must never contain '-' (invalid GAZL identifier character)");
	console.log("compileWithJsImpala emits string labels with valid GAZL identifier characters");
}

function testNuXJSCommandCompilerScript(compilerSource) {
	const scriptSource = fs.readFileSync(path.join(dir, "impala.nuxjs.js"), "utf8");
	const closureSource = fs.readFileSync(path.join(dir, "impalaImportClosure.js"), "utf8");
	const sourcePath = "smoke.impala";
	const sourceText = fs.readFileSync(path.join(dir, "testdata", "smoke.impala"), IMPALA_ENCODING);

	function runCase(label, args, acceptedCompilerPaths, expectedHasRandomId) {
		const outputLines = [];
		const writtenFiles = {};
		let capturedOptions;
		let loaded = false;
		const context = {
			arguments: args,
			print: (line) => outputLines.push(String(line)),
			write: (file, contents) => {
				writtenFiles[file] = String(contents);
			},
			read: (file) => {
				if (file === sourcePath) {
					return sourceText;
				}
				throw new Error(`Unexpected read path for ${label}: ${file}`);
			},
			load: (file) => {
				// The script also load()s its closure helper, from the real file - it is staged
				// alongside impala.nuxjs.js and is what makes `import` work on the NuXJS path.
				if (/impalaImportClosure\.js$/.test(file)) {
					new vm.Script(closureSource, { filename: file }).runInContext(context);
					return;
				}
				if (!acceptedCompilerPaths[file]) {
					throw new Error(`Unexpected load path for ${label}: ${file}`);
				}
				if (loaded) {
					throw new Error(`Compiler loaded more than once for ${label}`);
				}
				loaded = true;
				new vm.Script(compilerSource, { filename: file }).runInContext(context);
				const loadedCompiler = context.impalaCompiler;
				context.impalaCompiler = (source, options) => {
					capturedOptions = options;
					return loadedCompiler(source, options);
				};
			},
		};
		vm.createContext(context);
		new vm.Script(scriptSource, { filename: "impala.nuxjs.js" }).runInContext(context);

		if (expectedHasRandomId !== undefined) {
			assert(
				Object.prototype.hasOwnProperty.call(capturedOptions, "randomId") === expectedHasRandomId,
				`NuXJS command compiler script randomId presence mismatch for ${label}`,
			);
		}
		if (args[2] && args[2] !== "-") {
			assert(writtenFiles[args[2]], `NuXJS command compiler script must write compiled GAZL for ${label}`);
			assert(
				writtenFiles[args[2]].indexOf("main:") !== -1 && writtenFiles[args[2]].indexOf("FUNC") !== -1,
				`NuXJS command compiler script file output must include compiled main function for ${label}`,
			);
		} else {
			assert(outputLines.length > 0, `NuXJS command compiler script must emit compiled GAZL for ${label}`);
			assert(
				outputLines.some((line) => line.indexOf("main:") !== -1 && line.indexOf("FUNC") !== -1),
				`NuXJS command compiler script output must include compiled main function for ${label}`,
			);
		}
	}

	runCase(
		"stdout with explicit compiler path",
		["impala.nuxjs.js", sourcePath, "-", "42", sourcePath, "customCompiler.js"],
		{
			customCompiler: "ok",
			"customCompiler.js": "ok",
		},
		true,
	);
	runCase(
		"repo-root script path",
		["impala/impala.nuxjs.js", sourcePath, "-", "42", sourcePath],
		{
			"impala/impalaCompiler.js": "ok",
		},
		true,
	);
	runCase("local script path with defaults", ["impala.nuxjs.js", sourcePath], { "impalaCompiler.js": "ok" }, false);
	runCase(
		"direct output path",
		["impala.nuxjs.js", sourcePath, "out.gazl", "42", sourcePath, "customCompiler.js"],
		{
			customCompiler: "ok",
			"customCompiler.js": "ok",
		},
		true,
	);
	runCase("numeric output path", ["impala.nuxjs.js", sourcePath, "42"], { "impalaCompiler.js": "ok" }, false);
	console.log("impala.nuxjs.js compiles an Impala source through NuXJS-style command arguments");
}

const arithmeticCases = [
	{
		input: "1+2*3",
		expectSuccess: true,
		expectValue: 7,
		expectIndex: "1+2*3".length,
	},
	{
		input: "4*(2+3)",
		expectSuccess: true,
		expectValue: 20,
		expectIndex: "4*(2+3)".length,
	},
	{ input: "1+", expectSuccess: false },
];

testGrammarEquivalence("jspegTest.jspeg", "jspegTest.jspeg", arithmeticCases);

const recordInput = "foo=1,\nbar=23, qux=7";
const tagCaptureCases = [
	{
		input: recordInput,
		expectSuccess: true,
		expectValue: { foo: 1, bar: 23, qux: 7 },
		expectIndex: recordInput.length,
	},
	{ input: "foo=oops", expectSuccess: false },
];

testGrammarEquivalence("tagCaptureTest.jspeg", "tagCaptureTest.jspeg", tagCaptureCases);

const parityFixtures = [
	{
		name: "smoke",
		source: "smoke.impala",
		expected: "smoke.expected.gazl",
		options: { randomId: 42, sourceName: "smoke.impala" },
	},
	{
		name: "bool",
		source: "bool.impala",
		expected: "bool.expected.gazl",
		options: { randomId: 42, sourceName: "bool.impala" },
	},
	{
		name: "control",
		source: "control.impala",
		expected: "control.expected.gazl",
		options: { randomId: 42, sourceName: "control.impala" },
	},
	{
		name: "perfTest2",
		source: "perfTest2.impala",
		expected: "perfTest2.expected.gazl",
		options: { randomId: 42, sourceName: "perfTest2.impala" },
	},
	{
		name: "inputTest",
		source: "inputTest.impala",
		expected: "inputTest.expected.gazl",
		options: { randomId: 42, sourceName: "inputTest.impala" },
	},
	{
		name: "derefCallContract",
		source: "derefCallContract.impala",
		expected: "derefCallContract.expected.gazl",
		options: { randomId: 42, sourceName: "derefCallContract.impala" },
	},
];

const legacySourceDir = path.join(dir, "..", "tests", "impala", "sources");
const legacyExpectedDir = path.join(dir, "..", "tests", "impala", "golden");
const LEGACY_RANDOM_ID = 0x4d2;
const legacyParityFixtures = fs
	.readdirSync(legacySourceDir)
	.filter((file) => file.endsWith(".impala"))
	.sort()
	.map((file) => {
		const name = path.basename(file, ".impala");
		return {
			name,
			source: file,
			expected: `${name}.gazl`,
			sourceDir: legacySourceDir,
			expectedDir: legacyExpectedDir,
			options: {
				randomId: LEGACY_RANDOM_ID,
				retabulate: false,
				sourceName: path.join(legacySourceDir, file),
			},
		};
	});

function resolveFixturePath(fixture, key, defaultDir) {
	if (fixture[`${key}Dir`]) {
		return path.join(fixture[`${key}Dir`], fixture[key]);
	}
	return path.join(defaultDir, fixture[key]);
}

function runParityFixture(fixture) {
	const sourcePath = resolveFixturePath(fixture, "source", path.join(dir, "testdata"));
	const expectedPath = resolveFixturePath(fixture, "expected", path.join(dir, "testdata"));
	const source = canonicalizeNewlines(fs.readFileSync(sourcePath, IMPALA_ENCODING));
	const expected = fs.readFileSync(expectedPath, IMPALA_ENCODING);
	let actual;
	try {
		actual = compileWithJsImpala(source, Object.assign({}, fixture.options));
	} catch (err) {
		const message = err && err.message ? err.message : String(err);
		if (fixture.expectFailure) {
			console.warn(`Skipping ${fixture.name} fixture until JSPEG supports this feature: ${message}`);
			return;
		}
		console.error(`impala.jspeg compiler threw on fixture ${fixture.name}`);
		console.error(message);
		process.exit(1);
	}

	if (fixture.expectFailure) {
		console.error(`impala.jspeg compiler unexpectedly handled ${fixture.name}; remove expectFailure flag to enforce parity.`);
		process.exit(1);
	}

	const normalizedActual = canonicalizeTrimEnd(actual);
	const normalizedExpected = canonicalizeTrimEnd(expected);

	if (normalizedActual !== normalizedExpected) {
		console.error(`impala.jspeg compiler output diverges from recorded fixture: ${fixture.name}`);
		process.exit(1);
	}
	console.log(`impala.jspeg compiler matches ${fixture.name} fixture output`);
	if (!fixture.expectedDir) {
		assembleFixture(fixture.name, expectedPath);
	}
}

/* The `impala/testdata` fixtures used to get `; signature` validation and nothing else, so a label
   this compiler emitted but never defined - a duplicate `.sN#K` from two identical `case` values,
   say - would sail through on the shapes only testdata covers (return contracts, extern assignment).
   Same gate the goldens get, same rule: a host or companion-unit symbol is out of scope here, a
   module-local `.` name is ours. Fixtures carrying an `expectedDir` are goldens, which runJspegTests
   already assembles - with its own exemption list - so they are skipped rather than checked twice. */
function assembleFixture(name, gazlPath) {
	if (!haveGazlCmd()) {
		return;
	}
	const verdict = assembleOnly(gazlPath);
	if (verdict === NEEDS_HOST) {
		console.log(`${name} compiled but NOT link-checked (needs a host)`);
	} else if (verdict !== undefined) {
		console.error(`${name} fixture ${verdict}`);
		process.exit(1);
	} else {
		console.log(`${name} fixture assembles`);
	}
}

function resolveValidatorFixture(name) {
	return path.join(validatorFixturesDir, name);
}

function runValidatorCase(label, fixtureNames, expectedExitCode, expectedMessageSubstring) {
	const files = fixtureNames.map(resolveValidatorFixture);
	const result = childProcess.spawnSync(nuxjsExe, [validatorScript].concat(files), {
		encoding: "utf8",
	});

	if (result.error) {
		console.error(`Failed to launch gazl-validate for ${label}`);
		console.error(result.error);
		process.exit(1);
	}

	if (result.status !== expectedExitCode) {
		console.error(`gazl-validate exited with ${result.status} for ${label}, expected ${expectedExitCode}`);
		if (result.stdout) {
			console.error("stdout:");
			console.error(result.stdout);
		}
		if (result.stderr) {
			console.error("stderr:");
			console.error(result.stderr);
		}
		process.exit(1);
	}

	const validatorOutput = result.stderr || "";
	if (expectedMessageSubstring) {
		if (!validatorOutput.includes(expectedMessageSubstring)) {
			console.error(`gazl-validate output for ${label} did not include expected message: ${expectedMessageSubstring}`);
			console.error("output:");
			console.error(validatorOutput);
			process.exit(1);
		}
	} else if (validatorOutput.trim().length !== 0) {
		console.error(`gazl-validate produced unexpected diagnostics for ${label}`);
		console.error("output:");
		console.error(validatorOutput);
		process.exit(1);
	}

	if (expectedExitCode === 0) {
		console.log(`gazl-validate ${label} fixture passed`);
	} else {
		console.log(`gazl-validate ${label} fixture produced expected failure`);
	}
}

parityFixtures.forEach(runParityFixture);
legacyParityFixtures.forEach(runParityFixture);

runValidatorCase("matching metadata fixtures", ["exports.gazl", "imports-valid.gazl"], 0);
runValidatorCase("mismatched metadata fixtures", ["exports.gazl", "imports-mismatch.gazl"], 1, 'Signature mismatch for "foo"');
runValidatorCase(
	"matching array element metadata fixtures",
	["elem-exports.gazl", "elem-imports-valid.gazl"],
	0,
);
runValidatorCase(
	"mismatched array element metadata fixtures",
	["elem-exports.gazl", "elem-imports-mismatch.gazl"],
	1,
	"Array sharedInts does not match its definition",
);

runValidatorCase(
	"call site passing the wrong pointer element to a name-only extern",
	["call-elem-def.gazl", "call-elem-mismatch.gazl"],
	1,
	'Signature mismatch for "takesIntPtr"',
);

runValidatorCase(
	"extern struct matching a supplied host layout",
	["struct-decl.gazl", "struct-layout-valid.gazl"],
	0,
);
runValidatorCase(
	"extern struct whose host layout drifted",
	["struct-decl.gazl", "struct-layout-drift.gazl"],
	1,
	"extern struct AudioBuffer declares field \"channels\"",
);
runValidatorCase(
	"extern struct declared differently in two units",
	["struct-decl.gazl", "struct-decl-conflict.gazl"],
	1,
	"extern struct AudioBuffer has conflicting declarations",
);

runValidatorCase(
	"extern native prototype matching the native manifest",
	["extern-native-good.gazl"],
	0,
);
runValidatorCase(
	"extern native prototype contradicting the native manifest",
	["extern-native-bad.gazl"],
	1,
	"extern declaration of printInt does not match its definition",
);

runValidatorCase(
	"extern struct contradicting the real struct definition",
	["struct-decl-typemismatch.gazl", "struct-def.gazl"],
	1,
	"extern struct Frame does not match its definition",
);

// An array extent in a signature row is compared only when BOTH sides state one. An extent that
// folded to a compile-time scratch cannot be stated, so it emits the empty wildcard and is skipped
// rather than compared as the (pool-recycled, meaningless) scratch name it used to print.
// A valueless `const int N;` is external by omission of a value. It now emits a row, so it is
// link-checked like every other extern kind - except for "no definition found", which cannot apply to a
// host/run-time-supplied constant and would otherwise fire on hundreds of them across the corpus.
runValidatorCase(
	"valueless extern const with no definition anywhere",
	["const-extern-decl.gazl"],
	0,
);
runValidatorCase(
	"valueless extern const declared with two different types",
	["const-extern-decl.gazl", "const-extern-conflict.gazl"],
	1,
	"Const WORD_SIZE has conflicting extern declarations",
);
runValidatorCase(
	"valueless extern const contradicting its real definition",
	["const-extern-decl.gazl", "const-extern-def.gazl"],
	1,
	"Const WORD_SIZE does not match its definition",
);

runValidatorCase(
	"extern struct whose array extents are unstated wildcards",
	["struct-extent-wildcard.gazl", "struct-extent-def.gazl"],
	0,
);
runValidatorCase(
	"extern struct stating an array extent that contradicts the definition",
	["struct-extent-mismatch.gazl", "struct-extent-def.gazl"],
	1,
	"extern struct Bank does not match its definition",
);
// A struct field goes through the same typesCompatible rule as a global or an array element, so a bare
// `ptr` matches any pointer chain. It used to be compared as a raw string and rejected here only.
runValidatorCase(
	"extern struct field typing a pointer the definition leaves untyped",
	["struct-field-ptrchain.gazl", "struct-field-ptrchain-def.gazl"],
	0,
);

const validatorUnitTestScript = path.join(dir, "..", "tests", "gazl-validator-tests.js");
const validatorUnitResult = childProcess.spawnSync(process.execPath, [validatorUnitTestScript], {
	encoding: "utf8",
});

if (validatorUnitResult.error) {
	console.error("Failed to run gazl-validator unit tests");
	console.error(validatorUnitResult.error);
	process.exit(1);
}

if (validatorUnitResult.stdout) {
	process.stdout.write(validatorUnitResult.stdout);
}

if (validatorUnitResult.stderr) {
	process.stderr.write(validatorUnitResult.stderr);
}

if (validatorUnitResult.status !== 0) {
	console.error("gazl-validator unit tests failed");
	process.exit(1);
}

const failureSource = ["function main()", "locals pointer p", "{", "        copy (1 from p to 1);", "}", ""].join("\n");

let observedFailure = false;
try {
	compileWithJsImpala(failureSource, { randomId: 42 });
} catch (err) {
	observedFailure = true;
}

if (!observedFailure) {
	console.error("impala.jspeg compiler unexpectedly succeeded on failureSource");
	process.exit(1);
}

const smokeSource = canonicalizeNewlines(fs.readFileSync(path.join(dir, "testdata", "smoke.impala"), IMPALA_ENCODING));
const smokeExpected = fs.readFileSync(path.join(dir, "testdata", "smoke.expected.gazl"), IMPALA_ENCODING);
const smokeOutputAfterFailure = compileWithJsImpala(smokeSource, {
	randomId: 42,
});

if (canonicalizeTrimEnd(smokeOutputAfterFailure) !== canonicalizeTrimEnd(smokeExpected)) {
	console.error("impala.jspeg compiler leaked state after aborted compile");
	process.exit(1);
}
console.log("impala.jspeg compiler recovers after aborted compile without leaking state");

const mismatchedReturnSource = [
	"extern function foreignFoo;",
	"function main()",
	"locals int value",
	"{",
	"        value = foreignFoo();",
	"}",
	"",
	"function foreignFoo()",
	"returns float result",
	"{",
	"        result = 0.0;",
	"}",
	"",
].join("\n");

let observedMismatch = false;
try {
	compileWithJsImpala(mismatchedReturnSource, { randomId: 42 });
} catch (err) {
	observedMismatch = err && err.message && err.message.includes("Return type for foreignFoo");
}

if (!observedMismatch) {
	console.error("impala.jspeg compiler failed to report mismatched inferred return type");
	process.exit(1);
}
console.log("impala.jspeg compiler enforces inferred return type expectations");

// --- Impala 2 strict expressions: mixed bitwise operators -------------------

const mixedBitwiseSource = ["function main()", "locals int a", "{", "\ta = 1 | 2 & 3;", "}", ""].join("\n");
const parenthesizedBitwiseSource = ["function main()", "locals int a", "{", "\ta = (1 | 2) & 3;", "}", ""].join("\n");
const sameOpChainSource = ["function main()", "locals int a", "{", "\ta = 1 | 2 | 4;", "}", ""].join("\n");
const comparisonMixSource = [
	"function main()",
	"locals int a, int ok",
	"{",
	"\ta = 12;",
	"\tok = 0;",
	"\tif (a & 3 == 0) ok = 1;",
	"}",
	"",
].join("\n");
const comparisonParenthesizedSource = comparisonMixSource.replace("(a & 3 == 0)", "((a & 3) == 0)");

let observedMixedBitwiseError = false;
try {
	compileWithJsImpala(mixedBitwiseSource, { randomId: 42 });
} catch (err) {
	observedMixedBitwiseError = err && err.message && err.message.includes("Mixed bitwise operators")
		&& err.message.includes("require parentheses");
}
if (!observedMixedBitwiseError) {
	console.error("impala.jspeg compiler failed to reject mixed bitwise operators");
	process.exit(1);
}
console.log("impala.jspeg compiler rejects unparenthesized mixed bitwise operators");

expectSingleLegacyWarning(mixedBitwiseSource, "Mixed bitwise operators", "mixed bitwise operators");

const strictParenthesized = compileWithJsImpala(parenthesizedBitwiseSource, { randomId: 42 });
const legacyParenthesized = compileWithJsImpala(parenthesizedBitwiseSource, { randomId: 42, legacy: true });
if (strictParenthesized !== legacyParenthesized) {
	console.error("parenthesized bitwise source must compile identically in strict and legacy modes");
	process.exit(1);
}
console.log("impala.jspeg compiler accepts parenthesized bitwise mixes identically in both modes");

compileWithJsImpala(sameOpChainSource, { randomId: 42 });
console.log("impala.jspeg compiler accepts same-operator bitwise chains without parentheses");

// `inline function` has no out-of-line copy, so every use that needs one is an error rather than a
// silent de-optimisation. The behavioural check lives in the inlineEquivalence fixture pair; these are
// the cases that must not compile at all, plus the shapes that must.
const inlineCases = [
	["a recursive inline function",
		"inline function f(int n) returns int r { if (n > 0) { r = f(n - 1); } else { r = 0; } }\n"
			+ "function main() locals int q { q = f(3); }\n", "cannot call itself"],
	["taking the address of an inline function",
		"inline function f(int a) returns int r { r = a; }\nfunctype Cb(int a) returns int r\n"
			+ "function main() locals Cb c, int q { c = f; q = c(1); }\n", "address of the inline function"],
	["exporting an inline function",
		"export inline function g(int a) returns int r { r = a; }\n"
			+ "function main() locals int q { q = g(1); }\n", "cannot be exported"],
	// Locals live in transients, one expansion at a time, so the WORD COUNT must be known while
	// compiling - a folded or symbolic extent does not resolve until assembly.
	["an inline function with a non-literal array size",
		"const int H = 2\nconst int N = 2\ninline function f(int a) returns int r\n"
			+ "locals int array t[H * N]\n{ t[0] = a; r = t[0]; }\n"
			+ "function main() locals int q { q = f(1); }\n", "compile-time size"],
	["an inline function that was forward declared",
		"extern function later\ninline function later(int a) returns int r { r = a; }\n"
			+ "function main() locals int q { q = later(1); }\n", "was already declared"],
	["calling an inline function before its definition",
		"function main() locals int q { q = f(1); }\ninline function f(int a) returns int r { r = a; }\n",
			"Undeclared identifier"],
	// and the shapes that must work
	["an inline body containing a call",
		"function helper(int a) returns int r { r = a * 2; }\n"
			+ "inline function wrap(int a) returns int r { r = helper(a) + 1; }\n"
			+ "function main() locals int q { q = wrap(5); }\n", null],
	["an inline body containing a switch",
		"inline function pick(int i) returns int r\n"
			+ "{ switch (i == 0 to 3) { case 0: r = 10; default: r = 99; } }\n"
			+ "function main() locals int q { q = pick(0); }\n", null],
	["an inline body containing an assert",
		"const int DEBUG = 1\ninline function chk(int a) returns int r { assert(a > 0); r = a; }\n"
			+ "function main() locals int q { q = chk(5); }\n", null],
	["an inline body containing a while loop",
		"inline function count(int n) returns int r\nlocals int i\n"
			+ "{ r = 0; i = 0; while (i < n) { r = r + i; i = i + 1; } }\n"
			+ "function main() locals int q { q = count(4); }\n", null],
	["an inline function with a pointer parameter",
		"inline function dbl(int pointer p) returns int r { r = *p * 2; }\n"
			+ "function main() locals int array c[1], int q { c[0] = 21; q = dbl(&c[0]); }\n", null],
	["an inline function with float parameters",
		"inline function scale(float v, float k) returns float r { r = v * k; }\n"
			+ "function main() locals float f { f = scale(2.5, 4.0); }\n", null],

	["an inline function declaring a struct local",
		"struct P { int x; int y }\ninline function f(int v) returns int r\nlocals P a, P b\n"
			+ "{ a.x = v; a.y = v; b.x = v; r = a.x + a.y + b.x; }\n"
			+ "function main() locals int q { q = f(1) + f(2); }\n", null],
	["an inline function declaring an array of structs",
		"struct P { int x }\ninline function f(int v) returns int r\nlocals P array a[2]\n"
			+ "{ a[[0]].x = v; a[[1]].x = v; r = a[[0]].x + a[[1]].x; }\n"
			+ "function main() locals int q { q = f(1); }\n", null],
	["an inline function declaring a scalar local",
		"inline function f(int a) returns int r\nlocals int t\n{ t = a * 2; r = t + 1; }\n"
			+ "function main() locals int q { q = f(1); }\n", null],
	["an inline function declaring an array local",
		"inline function f(int a) returns int r\nlocals int array t[3], int i\n"
			+ "{ t[0] = a; t[1] = a; t[2] = a; r = 0; for (i = 0 to 3) r = r + t[i]; }\n"
			+ "function main() locals int q { q = f(2); }\n", null],
	["a plain inline call", "inline function f(int a) returns int r { r = a * 2; }\n"
		+ "function main() locals int q { q = f(3); }\n", null],
	["nested inline expansion", "inline function f(int a) returns int r { r = a * 2; }\n"
		+ "inline function g(int a) returns int r { r = f(a) + 1; }\n"
		+ "function main() locals int q { q = g(2); }\n", null],
	["a void inline function", "global int s;\ninline function f(int a) { global s = a; }\n"
		+ "function main() { f(3); }\n", null],
];
for (const [label, source, expected] of inlineCases) {
	expectCompileOutcome("inline", label, source, expected);
}
console.log("impala.jspeg compiler enforces the inline-function rules");

// By-value structs are parked, and EVERY door that can introduce one must say so. `functype` was
// unguarded, so a by-value struct param/return reached the parked window machinery - and against an
// `extern struct` it baked a numeric COPY size next to a symbolic `LOCA *.z.Name`, i.e. a truncating
// copy whenever the host's layout is wider.
const byValueDoors = [
	["function parameter", "struct V { int a; int b }\nfunction f(V v) { }\n", "Passing a struct by value"],
	["function return", "struct V { int a; int b }\nfunction f() returns V v { }\n", "Returning a struct by value"],
	["extern prototype parameter", "struct V { int a; int b }\nextern native n(V v)\n", "Passing a struct by value"],
	["extern prototype return", "struct V { int a; int b }\nextern native n() returns V v\n", "Returning a struct by value"],
	["functype parameter", "struct V { int a; int b }\nfunctype Cb(V v)\n", "Passing a struct by value"],
	["functype return", "struct V { int a; int b }\nfunctype Cb() returns V\n", "Returning a struct by value"],
];
for (const [label, source, expected] of byValueDoors) {
	expectCompileOutcome("by-value struct", label, source, expected);
}
console.log("impala.jspeg compiler rejects by-value structs at every declaration door");

// An array extent belongs exactly where it is verifiable: an `extern struct` field must omit it (the
// host owns that layout, as with a standalone `extern array`), and every other array must state it.
const arrayExtentCases = [
	[
		"extern struct array field stating a size",
		"extern struct G { int array a[4] }\nfunction f(G pointer p) returns int r { r = p->a[0]; }\n",
		"extern struct array field must not state a size",
	],
	[
		"struct array field omitting its size",
		"struct S { int array a }\nfunction f(S pointer p) returns int r { r = p->a[0]; }\n",
		"Array a needs a size",
	],
	[
		"local array omitting its size",
		"function f() returns int r\nlocals int array a\n{ r = a[0]; }\n",
		"Array a needs a size",
	],
	[
		"global array omitting its size",
		"global int array g;\nfunction f() returns int r { r = 1; }\n",
		"Array g needs a size",
	],
];
// The legal counterparts live in the same table (expectError null), so the rule cannot be satisfied by
// rejecting everything.
arrayExtentCases.push(
	["sizeless extern struct field",
		"extern struct G { int n; int array a; float f }\n"
			+ "function f(G pointer p) returns int r { r = p->a[2] + p->n; }\n", null],
	["sized struct field",
		"struct S { int array a[4] }\nfunction f(S pointer p) returns int r { r = p->a[0]; }\n", null],
);
for (const [label, source, expected] of arrayExtentCases) {
	expectCompileOutcome("array extent", label, source, expected);
}
console.log("impala.jspeg compiler requires an array extent everywhere except an extern struct field");

// Shapes the compiler used to accept and hand to the assembler, which then failed the build naming a
// compiler-minted symbol (`.s0.0`, `.s0.-6`, `nowhere`) instead of the source line. Each is decidable
// here whenever the values are numeric; a SYMBOLIC range or extent stays unchecked on purpose, because
// not knowing is not the same as being fine. See docs/CompileTimeHardening.md.
const SW = (range, body) => `function f() locals int i { i = 1; switch (i == ${range}) { ${body} } }`;
const acceptedThenRejected = [
	["duplicate case value", SW("0 to 3", "case 0: { i=1; } case 0: { i=2; }"), "Duplicate case value 0"],
	["duplicate inside one list", SW("0 to 3", "case 1, 1: { i=1; }"), "Duplicate case value 1"],
	["case above the range", SW("0 to 3", "case 8: { i=1; }"), "outside the switch range 0 to 3"],
	["case at the exclusive bound", SW("0 to 3", "case 3: { i=1; }"), "outside the switch range 0 to 3"],
	// Below `from` is the one that produced an UNLOADABLE module: the offset folds to `.s0.-6`, which
	// the assembler rejects as an invalid identifier. `from` is non-zero here on purpose - the offset
	// is then an assemble-time `! SUBi <A>`, so the check cannot read it off the emitted operand.
	["case below the range", SW("5 to 9", "case -1: { i=1; }"), "outside the switch range 5 to 9"],
	["case just below from", SW("5 to 9", "case 4: { i=1; }"), "outside the switch range 5 to 9"],
	["in-range cases", SW("5 to 9", "case 5, 8: { i=1; } default: { i=2; }"), null],
	["goto an undefined label", "function f() { goto nowhere; }", "goto to undefined label nowhere"],
	["goto a defined label", "function f() locals int i { i = 0; if (i < 3) goto top; top: ; }", null],
	// A label written twice mints two identical GAZL labels; the assembler rejected "Symbol already
	// defined" against a name and line the user never wrote. The label map in processBranches decides it.
	["a label defined twice in one function", "function f() locals int i { i = 0; lbl: ; lbl: ; }",
		"Duplicate label lbl"],
	["the same label name in two functions", "function f() locals int i { i=0; lbl: ; }\nfunction g() locals int i { i=0; lbl: ; }", null],
	["write to a readonly array element",
		"readonly int array T[4] = { 1,2,3,4 }\nfunction f() { global T[0] = 9; }",
		"Cannot assign to an element of a readonly array"],
	["read a readonly array element",
		"readonly int array T[4] = { 1,2,3,4 }\nfunction f() locals int x { x = global T[2]; }", null],
	["write to a writable array element",
		"global int array W[4]\nfunction f() { global W[0] = 9; }", null],
	// A string literal lives in a readonly section; the store used to compile and fail at GAZL load
	// naming `.s_abc_...`. Marked readonly so the same E404 element-write check catches it here.
	["write to a string literal element", `function f() { "abc"[0] = 1; }`,
		"Cannot assign to an element of a readonly array"],
	["read a string literal element", `function f() locals int c { c = "abc"[0]; }`, null],
	// A pointer difference counts elements (DIFp, then a divide by the stride), so it only means anything
	// when both sides walk the same element type. `ip - fp` used to slip through and divide by the wrong size.
	["difference across element types",
		"function f() locals int pointer ip, float pointer fp, int n { n = ip - fp; }",
		"matching element types"],
	["difference within one element type",
		"function f() locals int pointer p, int pointer q, int n { n = p - q; }", null],
	// `copy` used not to consume its terminator, so `copy(...) i = 1;` was two statements with nothing between.
	["copy without its terminator",
		"function f() locals int array a[4], int array b[4], int i { copy (4 from &a[0] to &b[0]) i = 1; }",
		"syntax error"],
	["copy with its terminator",
		"function f() locals int array a[4], int array b[4], int i { copy (4 from &a[0] to &b[0]); i = 1; }", null],
];
for (const [label, source, expected] of acceptedThenRejected) {
	expectCompileOutcome("accepted-then-rejected", label, source, expected);
}
console.log("impala.jspeg compiler rejects the shapes that used to fail at assembly time");

// `global` is a mandatory prefix at every use site, so mixing it up is the first error most newcomers
// and code generators hit - and a bare "Undeclared identifier" points away from the one-word fix.
const namespaceHints = [
	["global read without the keyword", "global int G = 5\nfunction f() locals int x { x = G; }",
		"G is a global - write `global G`"],
	["local read with the keyword", "function f() locals int x { x = 1; global x = 2; }",
		"x is a local - drop the `global` keyword"],
];
// The note travels as a rendered `note:` line in the formatted message, not as a property - the runner
// re-throws a plain Error - so match the text a user actually sees.
for (const [label, source, expectedHint] of namespaceHints) {
	expectCompileOutcome("E403 hint", label, source, "note: " + expectedHint);
}
console.log("impala.jspeg compiler names the fix when global/local are confused");

// A `(` after a value is always a call, so the argument list must never backtrack: its prologue has
// already borrowed the call window, and a backtrack leaks that window into whatever comes next. It
// used to surface as `Assertion failed: transient %1 must exist in stock` for a nested call, or as a
// bogus `int = funcptr` type error for a top-level one, instead of naming the broken syntax.
const CALL_HDR = "extern native printInt\nfunction t(int a) returns int r { r = a * 3; }\nfunction v() { }\n";
const argListCases = [
	["a short-circuit boolean argument", "printInt(t((x > 0 || x > 1)));", "Malformed argument list"],
	["a comparison argument", "printInt(t(x > 0));", "Malformed argument list"],
	["a parenthesized comparison argument", "printInt(t((x > 0)));", "Malformed argument list"],
	["a missing comma", "printInt(t(1 2));", "Malformed argument list"],
	["a trailing comma", "printInt(t(1,));", "Malformed argument list"],
	["an unclosed argument list", "printInt(t(1);", "Malformed argument list"],
	["a malformed call at the top level", "x = t(1 2);", "Malformed argument list"],
	// the legal counterparts, so the rule cannot be satisfied by rejecting every call
	["a nested call", "printInt(t(t(1)));", null],
	["a call inside a boolean group", "if ((t(x) > 0 || t(x) > 1)) { x = 1; }", null],
	["a call with no arguments", "v();", null],
	["an argument that is itself parenthesized", "printInt(t((x + 1)));", null],
];
for (const [label, body, expected] of argListCases) {
	expectCompileOutcome("argument list", label,
		`${CALL_HDR}function main() locals int x { x = 5; ${body} }\n`, expected);
}
console.log("impala.jspeg compiler names a malformed argument list instead of backtracking out of the call");

let observedComparisonMixError = false;
try {
	compileWithJsImpala(comparisonMixSource, { randomId: 42 });
} catch (err) {
	observedComparisonMixError = err && err.message
		&& err.message.includes("Comparison mixed with bitwise operators requires parentheses");
}
if (!observedComparisonMixError) {
	console.error("impala.jspeg compiler failed to reject bitwise-vs-comparison mix in a condition");
	process.exit(1);
}
console.log("impala.jspeg compiler rejects unparenthesized bitwise operators against comparisons");

expectSingleLegacyWarning(comparisonMixSource, "Comparison mixed with bitwise", "comparison mixes");

compileWithJsImpala(comparisonParenthesizedSource, { randomId: 42 });
console.log("impala.jspeg compiler accepts parenthesized bitwise-vs-comparison conditions");

// `!` sits BELOW comparison, so `!x == 2` means `!(x == 2)` - the opposite of the C reading. It must be
// rejected unless its operand is parenthesised; like the bitwise mixes, --legacy keeps the old meaning
// with a warning. `--x` is not a decrement (it folds to `-(-x)`, a silent no-op) and must be rejected too.
const notPrecedenceCases = [
	["! on a bare comparison", "function f() locals int x { x = 1; if (!x == 2) { x = 3; } }",
		"'!' binds below comparison"],
	["! on a parenthesised comparison", "function f() locals int x { x = 1; if (!(x == 2)) { x = 3; } }", null],
	["nested ! on a group", "function f() locals int x { x = 1; if (!!(x == 2)) { x = 3; } }", null],
	["-- is not a decrement", "function f() locals int x, int y { x = 1; y = --x; }", "syntax error"],
	["- -x double negation is fine", "function f() locals int x, int y { x = 1; y = - -x; }", null],
];
for (const [label, source, expected] of notPrecedenceCases) {
	expectCompileOutcome("! precedence", label, source, expected);
}
console.log("impala.jspeg compiler rejects an unparenthesized `!` operand and the unspaced `--`");

// A const is an assembler-level constant, so it reads the same type grammar (TypeBase) as every other
// declarator: struct pointers and named functypes are addresses and so are constable. A struct VALUE is
// the one shape with no scalar/address form.
const CONST_HDR = "struct S { int a; int b }\nfunctype Fn(int a) returns int r\n";
const constTypeCases = [
	["a struct pointer const", "const S pointer SP;", null],
	["a named functype const", "const Fn CB;", null],
	["a plain int pointer const", "const int pointer CP;", null],
	["an untyped funcptr const", "const funcptr FC;", null],
	["a struct value const", "const S SV;", "A const cannot be a struct value"],
];
for (const [label, decl, expected] of constTypeCases) {
	expectCompileOutcome("const type", label, `${CONST_HDR}${decl}\nfunction main() { }`, expected);
}
console.log("impala.jspeg compiler accepts const struct pointers and named functypes");

// `return`/`break`/`continue` are reserved words. Bare `return;` is an early exit (RETU); `return expr;` is
// E448 (assign to the named return slot). `break;`/`continue;` are E450 - unsupported, with the `goto` idiom
// in the note. Naming a label with any of the three is E449 under strict, a warning under --legacy (below).
const reservedWordCases = [
	["bare return is an early exit", "function f() locals int x { x = 1; return; x = 2; }", null],
	["return in a returns function", "function g() returns int r { r = 1; return; }", null],
	["return with a value is rejected", "function g() returns int r { return 1; }", "assign to the return variable"],
	["return with an expression is rejected", "function g() returns int r locals int x { x = 1; return x + 1; }",
		"return does not take a value"],
	["break statement is unsupported", "function f() locals int x { x = 1; break; }", "'break' is not supported"],
	["continue statement is unsupported", "function f() locals int x { x = 1; continue; }", "'continue' is not supported"],
	["breakage stays an ordinary identifier", "function f() locals int breakage { breakage = 1; }", null],
	["return as a label is reserved", "function f() locals int x { x = 1; goto return; return: ; }",
		"'return' is a reserved word"],
	["break as a label is reserved", "function f() locals int x { x = 1; goto break; break: ; }",
		"'break' is a reserved word"],
];
for (const [label, source, expected] of reservedWordCases) {
	expectCompileOutcome("reserved words", label, source, expected);
}
console.log("impala.jspeg compiler reserves return/break/continue with dedicated diagnostics");

// The reserved-word LABEL rejection is the strict default; --legacy keeps the 1.x `goto break;` early-exit
// idiom, downgrading the E449 to a single warning so old code still compiles.
expectSingleLegacyWarning("function f() locals int x { x = 1; goto break; break: ; }\n",
	"'break' is a reserved word", "a reserved-word label");

// A struct's real size is emitted as assemble-time arithmetic (`! ADDi <a> #<a> #N`), so a symbolic array
// extent lays out fine and every consumer below must keep working. A brace initializer can still FILL one:
// a DATA row may define fewer words than its region holds and the rest zero-fills, so the values given land
// at the front either way. What has no compile-time answer is the offset of anything AFTER it - that alone
// is E454. `fieldWords` used to multiply the extent OPERAND by a number, hand back NaN, and let the
// initializer loop run zero times, so `{ 1, { 7, 8, 9 }, 2 }` emitted `DATA #1 #2` - z's 2 landing in v[0].
const SYM_STRUCT = "const int N = 3\nstruct S { int a; int array v[N]; int z }\n";
const SYM_TAIL = "const int N = 3\nstruct S { int a; int array v[N] }\n";
const symbolicExtentCases = [
	["filling a trailing symbolic array is fine",
		SYM_TAIL + "global S s = { 1, { 7, 8, 9 } }\nfunction main() { }", null],
	["under-filling a trailing symbolic array is fine",
		SYM_TAIL + "global S s = { 1, { 7 } }\nfunction main() { }", null],
	["a field after a symbolic extent may be omitted",
		SYM_STRUCT + "global S s = { 1, { 7, 8, 9 } }\nfunction main() { }", null],
	["a field after a symbolic extent may be given zero (it emits nothing)",
		SYM_STRUCT + "global S s = { 1, { 7, 8, 9 }, 0 }\nfunction main() { }", null],
	["a field after a symbolic extent may not be given a value",
		SYM_STRUCT + "global S s = { 1, { 7, 8, 9 }, 2 }\nfunction main() { }", "falls after v"],
	["the block reaches out through a nested struct",
		"const int N = 3\nstruct Inner { int array v[N] }\nstruct Outer { Inner i; int z }\n"
			+ "global Outer o = { { { 7, 8 } }, 2 }\nfunction main() { }", "falls after v"],
	["the same struct with no initializer is fine", SYM_STRUCT + "global S s\nfunction main() { }", null],
	["sizeof of a symbolically sized struct is fine",
		SYM_STRUCT + "function main() locals int q { q = sizeof(S); }", null],
	["nesting one by value is fine (not 'incomplete')",
		SYM_STRUCT + "struct Outer { S inner; int t }\nglobal Outer o\nfunction main() { }", null],
	["an array of them is fine", SYM_STRUCT + "global S array bank[2]\nfunction main() { }", null],
	["a local of that type is fine", SYM_STRUCT + "function main() locals S s { s.a = 1; }", null],
	["a genuinely undefined struct type is still E412",
		"struct Outer { Missing inner }\nfunction main() { }", "Unknown type Missing"],
];
for (const [label, source, expected] of symbolicExtentCases) {
	expectCompileOutcome("symbolic extent", label, source, expected);
}

// The corruption signature was a SHORT data row, so pin the exact words rather than just pass/fail.
const litInit = compileWithJsImpala(
	"struct S { int a; int array v[3]; int z }\nglobal S s = { 1, { 7, 8, 9 }, 2 }\nfunction main() { }\n",
	{ randomId: 42 });
assert(/DATA #1 #7 #8 #9 #2/.test(litInit),
	`a literal-extent struct initializer must emit all five words\n${litInit}`);
const symInit = compileWithJsImpala(
	SYM_TAIL + "global S s = { 1, { 7, 8, 9 } }\nfunction main() { }\n", { randomId: 42 });
assert(/DATA #1 #7 #8 #9(\s|$)/.test(symInit),
	`a trailing symbolic array must still take the words it was given\n${symInit}`);
console.log("impala.jspeg compiler lays out symbolic struct extents and rejects only what it cannot fill");

// `global` names a storage table. A function and a const are in neither, so the prefix used to be accepted
// and silently discarded there - a third, undiagnosed state next to "required" (globals) and E403 (locals).
const globalPrefixCases = [
	["global on a const is rejected", "const int C = 1\nfunction f() locals int x { x = global C; }",
		"C is a constant"],
	["global on a function is rejected", "function g() { }\nfunctype Fn()\n"
		+ "function f() locals Fn c { c = global g; }", "g is a function"],
	["global on a global variable stays required", "global int v\nfunction f() locals int x { x = global v; }", null],
	["a const without the prefix is fine", "const int C = 1\nfunction f() locals int x { x = C; }", null],
];
for (const [label, source, expected] of globalPrefixCases) {
	expectCompileOutcome("global prefix", label, source, expected);
}
console.log("impala.jspeg compiler rejects a `global` prefix on a function or a const");

// Old sources carry the no-op prefix, so --legacy keeps compiling them with a warning.
expectSingleLegacyWarning("const int C = 1\nfunction f() locals int x { x = global C; }\n",
	"C is a constant", "a `global` prefix on a const");

// `export` claims "this unit provides it"; a valueless `const` says "someone else does". The pair used to
// compile to output byte-identical to the un-exported form, silently dropping the keyword. A VALUED
// `export const` is meaningful - the signature row carries it for --dead-strip - so it stays legal.
const exportConstCases = [
	["export on a valueless const is rejected", "export const int C\nfunction main() { }",
		"contradicts a valueless `const`"],
	["export on a valued const is fine", "export const int C = 1\nfunction main() { }", null],
	["a valueless const without export is fine", "const int C\nfunction main() { }", null],
];
for (const [label, source, expected] of exportConstCases) {
	expectCompileOutcome("export const", label, source, expected);
}
console.log("impala.jspeg compiler rejects `export` on a valueless const");

// Caret placement, pinned per code. Each of these used to point at the token AFTER the mistake - the next
// line for E445, the next declaration for E422 - which is worse than useless when the construct spans lines.
const caretCases = [
	["E445 names the label, not the following line",
		"function f() locals int x {\n\tx = 1;\n\tgoto nowhere;\n\tx = 2;\n}\n", "3:10: error[E445]"],
	["E403 names the identifier, not the next token",
		"function f() locals int x {\n\tx = undeclaredName + 1;\n}\n", "2:9: error[E403]"],
	["E305 names the loop variable",
		"function f(int p) locals int x {\n\tfor (p = 0 to 3) { x = 1; }\n}\n", "2:10: error[E305]"],
	["E422 names the initializer, not the next declaration",
		"struct S { int a; int b }\nglobal S array bank[1] = { 1, 2 }\nglobal int later = 3\n",
		"2:26: error[E422]"],
	["E428 names the signature it belongs to",
		"function g() returns int a, int b {\n\ta = 1;\n}\nglobal int later = 3\n", "1:35: error[E428]"],
	["E451 names the stray `;`, not the `else` after it",
		"function f() locals int x {\n\tif (x == 1) { x = 2; };\n\telse { x = 3; }\n}\n", "2:27: error[E451]"],
	["E453 names the const, not the next declaration",
		"export const int C\nglobal int later = 3\nfunction main() { }\n", "1:18: error[E453]"],
	["E454 names the initializer, not the next declaration",
		"const int N = 3\nstruct S { int a; int array v[N]; int z }\nglobal S s = { 1, { 7, 8, 9 }, 2 }\n"
			+ "function main() { }\n", "3:14: error[E454]"],
];
for (const [label, source, expected] of caretCases) {
	expectDiagnosticAt(label, source, expected);
}
console.log("impala.jspeg compiler points its carets at the offending token");

// The `; else` detector must not fire on the shapes that legitimately put a `;` or an `else` nearby.
const caretNonCases = [
	["if/else with a semicolon-terminated then-branch",
		"function f() locals int x { if (x == 1) x = 2; else x = 3; }"],
	["if with no else, followed by an empty statement",
		"function f() locals int x { if (x == 1) { x = 2; }; x = 4; }"],
	["a local named elsewhere is not the `else` keyword",
		"function f() locals int elsewhere { if (elsewhere == 1) { elsewhere = 2; }; elsewhere = 3; }"],
];
for (const [label, source] of caretNonCases) {
	expectCompileOutcome("caret non-case", label, source, null);
}
console.log("impala.jspeg compiler does not mistake a legitimate `;` for a dangling else");

expectSingleLegacyWarning("function f() locals int x { x = 1; if (!x == 2) { x = 3; } }\n",
	"'!' binds below comparison", "the `!`-precedence error");

// --- Impala 2 Step 1: typed pointers and arrays -----------------------------

const typedPointerCases = [
	{
		label: "typed subscripts compile without casts",
		source: [
			"function findSmallest(int n, int pointer vector)",
			"returns int j",
			"locals int i",
			"{",
			"\tj = 0;",
			"\tfor (i = 1 to n)",
			"\t\tif (vector[i] < vector[j])",
			"\t\t\tj = i;",
			"}",
		].join("\n"),
		expectError: null,
	},
	{
		label: "typed array element assignment is typed",
		source: [
			"global float array gains[4]",
			"function main() locals float f { f = global gains[2]; global gains[3] = f * 2.0; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "null assigns into typed pointers",
		source: "function main() locals int pointer p { p = null; }",
		expectError: null,
	},
	{
		label: "erasing typed to untyped is implicit",
		source: "function main() locals int pointer p, pointer raw, int x { p = &x; raw = p; }",
		expectError: null,
	},
	{
		label: "cast admits untyped into typed",
		source: "function main() locals int pointer p, pointer raw { p = (int pointer) raw; }",
		expectError: null,
	},
	{
		label: "pointer-to-pointer declarations and derefs",
		source: "function main() locals int pointer p, int pointer pointer pp, int x { x = 1; p = &x; pp = &p; x = *(*pp); }",
		expectError: null,
	},
	{
		label: "untyped into typed pointer requires a cast",
		source: "function main() locals int pointer p, pointer raw { p = raw; }",
		expectError: "Pointer element type mismatch",
	},
	{
		label: "sideways pointer element types require a cast",
		source: "function main() locals int pointer p, float pointer fp, int x { p = &x; fp = p; }",
		expectError: "Pointer element type mismatch",
	},
	{
		label: "address-of mismatched element type is rejected",
		source: "function main() locals int pointer p, float f { p = &f; }",
		expectError: "Pointer element type mismatch",
	},
	{
		label: "untyped call results need a cast into typed pointers",
		source: [
			"function alloc() returns pointer r { r = null; }",
			"function main() locals int pointer p { p = alloc(); }",
		].join("\n"),
		expectError: "Pointer element type mismatch",
	},
	{
		label: "element type must match across declarations",
		source: ["extern int array shared", "global float array shared[4]"].join("\n"),
		expectError: "Element type mismatch with previous declaration",
	},
	{
		label: "string literals are int pointers",
		source: 'function main() locals int c, int pointer hex { c = ("0123456789abcdef")[11]; hex = "ABC"; c = *hex + hex[1]; }',
		expectError: null,
	},
	{
		label: "string literals do not assign into float pointers",
		source: 'function main() locals float pointer fp { fp = "abc"; }',
		expectError: "Pointer element type mismatch",
	},
	{
		label: "typed pointer arguments match typed parameters",
		source: [
			"function f(int pointer v) returns int r { r = *v; }",
			"function main() locals int x, int y { x = 1; y = f(&x); }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "null and erased arguments pass typed/untyped parameters",
		source: [
			"function f(int pointer v) locals int x { if (v != null) x = *v; }",
			"function g(pointer v) returns int r { r = (int) v[0]; }",
			"function main() locals int x, int y { x = 1; f(null); y = g(&x); }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "cast admits untyped arguments into typed parameters",
		source: [
			"function f(int pointer v) returns int r { r = *v; }",
			"function main() locals pointer raw, int y { y = f((int pointer) raw); }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "sideways pointer argument element types are rejected",
		source: [
			"function f(int pointer v) returns int r { r = *v; }",
			"function main() locals float g, int y { y = f(&g); }",
		].join("\n"),
		expectError: "Pointer element type mismatch for argument 1",
	},
	{
		label: "untyped pointer arguments need a cast into typed parameters",
		source: [
			"function f(int pointer v) returns int r { r = *v; }",
			"function main() locals pointer raw, int y { y = f(raw); }",
		].join("\n"),
		expectError: "Pointer element type mismatch for argument 1",
	},
	{
		label: "struct field access through a pointer compiles",
		source: [
			"struct S { int a; float b }",
			"function f(S pointer s) locals int x, float y { s->a = 3; x = s->a; y = s->b; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "sizeof reports struct word count",
		source: ["struct S { int a; float b; int c }", "const int N = sizeof(S)", "function main() { }"].join("\n"),
		expectError: null,
	},
	{
		label: "struct-pointer cast retypes a raw pointer",
		source: [
			"struct S { int a }",
			"global array store[2]",
			"function main() locals S pointer s { s = (S pointer) &global store[0]; s->a = 1; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "extern struct forward-declares mutual pointer types",
		source: ["extern struct B", "struct A { B pointer nb }", "struct B { A pointer na }", "function main() { }"].join("\n"),
		expectError: null,
	},
	{
		label: "-> requires a struct pointer",
		source: "function f(int pointer p) { p->x = 1; }",
		expectError: "Field access requires a struct",
	},
	{
		label: "unknown struct field is rejected",
		source: ["struct S { int a }", "function f(S pointer s) { s->b = 1; }"].join("\n"),
		expectError: "has no field b",
	},
	{
		label: "dot on a struct pointer suggests arrow",
		source: ["struct S { int a }", "function f(S pointer s) { s.a = 1; }"].join("\n"),
		expectError: "Use '->'",
	},
	{
		label: "duplicate struct definition is rejected",
		source: ["struct S { int a }", "struct S { int b }"].join("\n"),
		expectError: "Struct already defined",
	},
	{
		label: "duplicate struct field is rejected",
		source: "struct S { int a; int a }",
		expectError: "Duplicate field",
	},
	{
		label: "struct field assignment is type-checked",
		source: ["struct S { int a }", "function f(S pointer s, float x) { s->a = x; }"].join("\n"),
		expectError: "Incompatible types for assignment",
	},
	{
		label: "struct value locals with nested field access compile",
		source: [
			"struct Inner { float a; float b }",
			"struct Outer { int n; Inner mid; float g }",
			"function main() locals Outer o, int x, float y { o.n = 1; o.mid.a = 2.0; o.g = 3.0; x = o.n; y = o.mid.b; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "nested field access through a struct pointer compiles",
		source: [
			"struct Inner { float a }",
			"struct Outer { Inner mid }",
			"function f(Outer pointer o, float x) locals float y { o->mid.a = x; y = o->mid.a; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "arrow on a struct value suggests dot",
		source: [
			"struct Inner { float a }",
			"struct Outer { Inner mid }",
			"function main() locals Outer o { o.mid->a = 1.0; }",
		].join("\n"),
		expectError: "this is a struct value, not a pointer",
	},
	{
		label: "struct value field assignment is type-checked",
		source: ["struct S { int a }", "function main() locals S s, float x { x = 1.0; s.a = x; }"].join("\n"),
		expectError: "Incompatible types for assignment",
	},
	{
		label: "whole-struct assignment between locals compiles",
		source: ["struct S { int a; float b }", "function main() locals S x, S y { x.a = 1; y = x; }"].join("\n"),
		expectError: null,
	},
	{
		label: "whole-struct assignment through a pointer place compiles",
		source: [
			"struct S { int a }",
			"global array store[2]",
			"function f(S pointer p) locals S x { x.a = 1; *p = x; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "whole-struct assignment requires matching struct types",
		source: ["struct A { int x }", "struct B { int y }", "function main() locals A a, B b { a = b; }"].join("\n"),
		expectError: "Struct type mismatch",
	},
	{
		label: "struct cannot be assigned to a scalar",
		source: ["struct S { int a }", "function main() locals S s, int n { n = s; }"].join("\n"),
		expectError: "needs a struct value on both sides",
	},
	{
		label: "address-of a struct value yields a typed struct pointer",
		source: [
			"struct S { int a }",
			"function use(S pointer p) { p->a = 1; }",
			"function main() locals S s, S pointer q { use(&s); q = &s; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "address-of a struct value element yields a typed pointer",
		source: [
			"struct S { int a; float b }",
			"function take(float pointer f) { *f = 1.0; }",
			"function main() locals S s { take(&s.b); }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "address-of a struct value respects element type at the call",
		source: [
			"struct S { int a }",
			"function take(float pointer f) { *f = 1.0; }",
			"function main() locals S s { take(&s); }",
		].join("\n"),
		expectError: "Pointer element type mismatch",
	},
	{
		label: "struct globals: declaration, field access, address-of, copy",
		source: [
			"struct Inner { float a }",
			"struct Outer { int n; Inner mid }",
			"global Outer g",
			"function use(Outer pointer o) { o->n = 1; }",
			"function main() locals Outer local {",
			"\tglobal g.n = 5; global g.mid.a = 1.0;",
			"\tuse(&global g);",
			"\tlocal = global g;",
			"}",
		].join("\n"),
		expectError: null,
	},
	{
		label: "struct global takes a nested brace initializer",
		source: [
			"struct Inner { float a; float b }",
			"struct Outer { int n; Inner mid; float g }",
			"global Outer g = { 1, { 0.5, 0.7 }, 2.0 }",
			"readonly Outer preset = { 3, { 0.1, 0.2 }, 0.9 }",
			"function main() { }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "a struct value needs braces, not a bare initializer",
		source: ["struct S { int a }", "global S g = 0"].join("\n"),
		expectError: "needs a brace initializer",
	},
	{
		label: "struct initializer field type is checked",
		source: ["struct S { int a; float b }", "global S g = { 1.0, 2.0 }"].join("\n"),
		expectError: "Initializer type mismatch",
	},
	{
		label: "struct arrays index to a struct place (constant and dynamic)",
		source: [
			"struct V { int n; float g }",
			"global V array bank[4]",
			"function main() locals V array loc[2], int i, float f {",
			"\tglobal bank[[0]].n = 1; f = global bank[[2]].g;",
			"\tfor (i = 0 to 4) global bank[[i]].n = i;",
			"\tloc[[1]].g = 0.5;",
			"}",
		].join("\n"),
		expectError: null,
	},
	{
		label: "address-of a struct array element is a typed pointer",
		source: [
			"struct V { int n }",
			"global V array bank[3]",
			"function use(V pointer p) { p->n = 9; }",
			"function main() { use(&global bank[[1]]); }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "a struct array size may be a named constant",
		source: ["struct V { int n }", "const int N = 4", "global V array bank[N]"].join("\n"),
		expectError: null,
	},
	{
		label: "an initialized struct-element array still needs a literal size",
		source: ["struct V { int n }", "const int N = 2", "global V array bank[N] = { { 1 }, { 2 } }"].join("\n"),
		expectError: "literal size",
	},
	{
		label: "array fields inside a struct index correctly",
		source: [
			"struct F { float array state[4]; int taps }",
			"global F gf",
			"function p(F pointer f, int i, float x) { f->state[i] = x; }",
			"function main() locals F lf, int i, float y {",
			"\tlf.state[0] = 1.0; y = lf.state[2];",
			"\tglobal gf.state[1] = 2.0;",
			"}",
		].join("\n"),
		expectError: null,
	},
	{
		label: "struct array-of-struct field indexes to a nested place",
		source: [
			"struct Inner { float a }",
			"struct Outer { Inner array items[3] }",
			"function main() locals Outer o { o.items[[1]].a = 0.5; }",
		].join("\n"),
		expectError: null,
	},
	/* The `[[ ]]` rule: the spelling states the stride, so each bracket form is an error where the other
	   is correct, and a struct pointer moves by scaled subscript only. See docs/Impala2Review.md. */
	{
		label: "a plain subscript on a struct element is rejected",
		source: ["struct V { int n; int m }", "global V array bank[4]", "function main() locals int i { i = global bank[1].n; }"].join("\n"),
		expectError: "Plain subscript on a struct element",
	},
	{
		label: "a plain subscript through a struct pointer is rejected",
		source: ["struct V { int n; int m }", "function main() locals V pointer p, int i { i = p[1].n; }"].join("\n"),
		expectError: "Plain subscript on a struct element",
	},
	{
		label: "a scaled subscript on a one-word element is rejected",
		source: ["global int array w[4]", "function main() locals int i { i = global w[[1]]; }"].join("\n"),
		expectError: "Scaled subscript on a one-word element",
	},
	{
		label: "a scaled subscript on a scalar array field is rejected",
		source: ["struct F { float array state[4] }", "function main() locals F f, float x { x = f.state[[1]]; }"].join("\n"),
		expectError: "Scaled subscript on a one-word element",
	},
	{
		label: "arithmetic on a struct pointer is rejected",
		source: ["struct V { int n; int m }", "function main() locals V pointer p, int i { p = p + i; }"].join("\n"),
		expectError: "Arithmetic on a V pointer",
	},
	{
		label: "subtracting an int from a struct pointer is rejected",
		source: ["struct V { int n; int m }", "function main() locals V pointer p { p = p - 1; }"].join("\n"),
		expectError: "Arithmetic on a V pointer",
	},
	{
		label: "the difference of two struct pointers is rejected",
		source: ["struct V { int n; int m }", "function main() locals V pointer p, V pointer q, int n { n = q - p; }"].join("\n"),
		expectError: "Difference between V pointers",
	},
	{
		label: "for over a struct pointer is rejected (FORp cannot stride)",
		source: [
			"struct V { int n; int m }",
			"global V array bank[4]",
			"function main() locals V pointer p, int i { for (p = &global bank[[0]] to &global bank[[3]]) i = p->n; }",
		].join("\n"),
		expectError: "For variable must not be a struct pointer",
	},
	{
		label: "struct pointers still compare without scaling",
		source: [
			"struct V { int n; int m }",
			"global V array bank[4]",
			"function main() locals V pointer p, V pointer e, int i {",
			"\tp = &global bank[[0]]; e = &global bank[[4]];",
			"\twhile (p < e) { i = p->n; p = &p[[1]]; }",
			"}",
		].join("\n"),
		expectError: null,
	},
	{
		label: "an out-of-range constant index is legal when only an address is formed",
		source: ["struct V { int n }", "global V array bank[4]", "function main() locals V pointer e { e = &global bank[[9]]; }"].join("\n"),
		expectError: null,
	},
	{
		label: "unit-stride pointer arithmetic is untouched",
		source: ["global int array w[4]", "function main() locals int pointer v, int n { v = &global w[0]; v = v + 2; n = v - &global w[0]; }"].join("\n"),
		expectError: null,
	},
	{
		label: "dereferencing a returned struct pointer yields the struct",
		source: ["struct V { int a }", "function f(V pointer x) returns V pointer y { y = x; }", "function main() locals V s, V d { s.a = 1; d = *f(&s); }"].join("\n"),
		expectError: null,
	},
	{
		label: "a field can be read through a returned struct pointer",
		source: ["struct V { int a }", "function f(V pointer x) returns V pointer y { y = x; }", "function main() locals V s, int n { s.a = 1; n = f(&s)->a; }"].join("\n"),
		expectError: null,
	},
	{
		label: "dereferencing a returned int pointer yields an int",
		source: ["function f(int pointer x) returns int pointer y { y = x; }", "function main() locals int i, int n { i = 3; n = *f(&i); }"].join("\n"),
		expectError: null,
	},
	{
		label: "a returned pointer keeps its element type for assignment checks",
		source: ["struct V { int a }", "struct W { int b }", "function f(V pointer x) returns V pointer y { y = x; }", "function main() locals V s, W pointer w { w = f(&s); }"].join("\n"),
		expectError: "Pointer element type mismatch",
	},
	{
		label: "defining the same function twice is rejected",
		source: ["function shared(int n) returns int r { r = n + 1; }", "function shared(int n) returns int r { r = n + 99; }"].join("\n"),
		expectError: "Function shared is already defined",
	},
	{
		label: "an extern declaration may still be defined locally",
		source: ["extern function later", "function later() { }"].join("\n"),
		expectError: null,
	},
	{
		label: "an extern may declare a prototype",
		source: ["extern native dm(int a, int b, int pointer r) returns int q", "function main() locals int r, int q { q = dm(17, 5, &r); }"].join("\n"),
		expectError: null,
	},
	{
		label: "a prototyped extern checks argument count",
		source: ["extern native dm(int a, int b) returns int q", "function main() locals int q { q = dm(1); }"].join("\n"),
		expectError: "Invalid argument count when calling dm",
	},
	{
		label: "a prototyped extern checks argument types",
		source: ["extern native dm(int a, int pointer p) returns int q", "function main() locals int q { q = dm(1, 2); }"].join("\n"),
		expectError: "Argument type mismatch for argument 2",
	},
	{
		label: "a prototyped extern gives its call a return type",
		source: ["extern native dm(int a) returns int q", "function main() locals float f { f = dm(1); }"].join("\n"),
		expectError: "Incompatible types for assignment",
	},
	{
		label: "a name-only extern stays unchecked",
		source: ["extern native anything", "function main() { anything(1, 2.5, 3); }"].join("\n"),
		expectError: null,
	},
	{
		label: "an extern prototype cannot take a struct by value",
		source: ["struct V { int a }", "extern native f(V v)"].join("\n"),
		expectError: "Passing a struct by value is not supported",
	},
	{
		label: "an extern prototype cannot declare multiple returns",
		source: "extern native f(int a) returns int q, int r",
		expectError: "Multiple return values are not supported",
	},
	{
		label: "a struct value passed where a pointer is wanted names the struct",
		source: ["struct V { int a }", "function take(V pointer p) { p->a = 1; }", "function main() locals V v { take(v); }"].join("\n"),
		expectError: "struct V vs expected pointer",
	},
	{
		label: "multiple return values are parked for Impala 3.0",
		source: "function polar(float m, float p) returns float x, float y { x = m * p; y = m - p; }",
		expectError: "Multiple return values are not supported",
	},
	{
		label: "a multi-return funcptr type is parked for Impala 3.0",
		source: "functype SplitFn(int n) returns int, int",
		expectError: "Multiple return values are not supported",
	},
	{
		label: "a by-value struct parameter is parked for Impala 3.0",
		source: ["struct P { int x; int y }", "function sum(P p) returns int s { s = p.x + p.y; }"].join("\n"),
		expectError: "Passing a struct by value is not supported",
	},
	{
		label: "a by-value struct return is parked for Impala 3.0",
		source: ["struct S { int a }", "function make() returns S s { s.a = 1; }"].join("\n"),
		expectError: "Returning a struct by value is not supported",
	},
	{
		label: "destructuring assignment is parked for Impala 3.0",
		source: ["function one() returns int a { a = 1; }", "function main() locals int x, int y { x, y = one(); }"].join("\n"),
		expectError: "Destructuring assignment is not supported",
	},
	{
		label: "struct pointer params, out-params and whole-struct copy still work",
		source: ["struct P { int x; int y }", "function fill(P pointer p, int v) { p->x = v; p->y = v + 1; }", "function half(int n, int pointer rem) returns int q { q = n / 2; *rem = n - n / 2 * 2; }", "function main() locals P a, P b, int q, int r { fill(&a, 5); b = a; q = half(7, &r); }"].join("\n"),
		expectError: null,
	},
	{
		label: "a matching function assigns to a named funcptr type",
		source: [
			"functype BinOp(int a, int b) returns int",
			"function add(int a, int b) returns int r { r = a + b; }",
			"function main() locals BinOp cb, int n { cb = add; n = cb(2, 3); }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "a mismatched function is rejected by a named funcptr type",
		source: [
			"functype BinOp(int a, int b) returns int",
			"function wrong(float x) returns float r { r = x; }",
			"function main() locals BinOp cb { cb = wrong; }",
		].join("\n"),
		expectError: "does not match funcptr type",
	},
	{
		label: "an indirect call through a funcptr type checks argument types",
		source: [
			"functype UnaryFn(int x) returns int",
			"function id(int x) returns int r { r = x; }",
			"function main() locals UnaryFn cb, float y { cb = id; cb(y); }",
		].join("\n"),
		expectError: "Argument type mismatch",
	},
	{
		label: "nullfunc assigns to any funcptr type",
		source: [
			"functype BinOp(int a, int b) returns int",
			"function main() locals BinOp cb { cb = nullfunc; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "a funcptr type name may not collide with a struct",
		source: ["struct Foo { int a }", "functype Foo(int x) returns int"].join("\n"),
		expectError: "already used by a struct",
	},
	{
		label: "an array of funcptr types dispatches",
		source: [
			"functype BinOp(int a, int b) returns int",
			"function add(int a, int b) returns int r { r = a + b; }",
			"function main() locals BinOp array ops[1], int n { ops[0] = add; n = ops[0](1, 2); }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "export marks functions, globals, and consts",
		source: [
			"export const int GAIN = 4",
			"export global int state",
			"export global int array params[3]",
			"export function process(int x) returns int r { r = x * GAIN; }",
		].join("\n"),
		expectError: null,
	},
	{
		label: "a void function called as a statement is fine",
		source: ["function v() { }", "function main() { v(); }"].join("\n"),
		expectError: null,
	},
	{
		label: "a void result cannot be assigned to an int",
		source: ["function v() { }", "function main() locals int i { i = v(); }"].join("\n"),
		expectError: "Incompatible types for assignment",
	},
	{
		label: "a void result cannot be assigned to a float",
		source: ["function v() { }", "function main() locals float f { f = v(); }"].join("\n"),
		expectError: "Incompatible types for assignment",
	},
	{
		label: "a void result cannot feed an operator",
		source: ["function v() { }", "function main() locals int i { i = v() + 1; }"].join("\n"),
		expectError: "Invalid types",
	},
	{
		label: "a void result cannot be passed as an argument",
		source: ["extern native printInt", "function v() { }", "function main() { printInt(v()); }"].join("\n"),
		expectError: "Invalid type",
	},
	{
		label: "a void result cannot be cast and dereferenced into a value",
		source: ["function v() { }", "function main() locals int i { i = *(pointer)v(); }"].join("\n"),
		expectError: "Invalid type",
	},
	{
		label: "an extern prototype with no returns clause is void, not unknown",
		source: ["extern function ext()", "function main() locals int i { i = ext(); }"].join("\n"),
		expectError: "Incompatible types for assignment",
	},
	{
		label: "a parametrized extern prototype with no returns clause is void",
		source: ["extern native ext(int a)", "function main() locals int i { i = ext(1); }"].join("\n"),
		expectError: "Incompatible types for assignment",
	},
	{
		label: "a name-only extern stays an unknown-return wildcard",
		source: ["extern function ext;", "function main() locals int i { i = ext(); }"].join("\n"),
		expectError: null,
	},
	{
		label: "an extern prototype contradicting a definition above it is an error",
		source: ["function f() { }", "extern function f() returns int r"].join("\n"),
		expectError: "does not match its definition",
	},
	{
		label: "an extern prototype contradicting a definition below it is an error",
		source: ["extern function f() returns float x", "function f() returns int r { r = 1; }"].join("\n"),
		expectError: "does not match its definition",
	},
	{
		label: "an extern prototype contradicting a definition in a parameter is an error",
		source: ["extern function f(float a)", "function f(int a) { }"].join("\n"),
		expectError: "does not match its definition",
	},
	{
		label: "an extern prototype agreeing with a definition is silent, and names are not compared",
		source: ["extern function f(int a) returns int r", "function f(int b) returns int q { q = b; }"].join("\n"),
		expectError: null,
	},
	{
		label: "a bodyless extern struct claims no layout, so it cannot collide with a definition",
		source: ["struct S { int a }", "extern struct S", "function main() locals S s { s.a = 1; }"].join("\n"),
		expectError: null,
	},
	{
		label: "a bodyless extern struct before the definition is likewise silent",
		source: ["extern struct S", "struct S { int a }", "function main() locals S s { s.a = 1; }"].join("\n"),
		expectError: null,
	},
	{
		label: "an extern struct body agreeing with the definition is silent",
		source: ["struct S { int a; float b }", "extern struct S { int a; float b }"].join("\n"),
		expectError: null,
	},
	{
		label: "an extern struct body contradicting the definition is an error",
		source: ["struct S { int a; float b }", "extern struct S { int a; int b }"].join("\n"),
		expectError: "does not match its definition",
	},
	{
		label: "an extern struct body contradicting a LATER definition is the same error",
		source: ["extern struct S { int a; int b }", "struct S { int a; float b }"].join("\n"),
		expectError: "does not match its definition",
	},
	{
		label: "two real struct definitions still collide",
		source: ["struct S { int a }", "struct S { int a }"].join("\n"),
		expectError: "Struct already defined",
	},
	/* With no definition anywhere the declarations still have to agree with EACH OTHER - nothing
	   else can arbitrate, and the compiler generates calls and field offsets from whichever it kept. */
	{
		label: "two extern prototypes that disagree are an error even with no definition",
		source: ["extern function f(int a)", "extern function f(float a)"].join("\n"),
		expectError: "extern declarations of f disagree",
	},
	{
		label: "two extern struct bodies that disagree are an error even with no definition",
		source: ["extern struct S { int a }", "extern struct S { float a }"].join("\n"),
		expectError: "extern declarations of struct S disagree",
	},
	{
		label: "identical extern declarations are fine, however many",
		source: ["extern function f(int a)", "extern function f(int a)", "extern struct S { int a }",
			"extern struct S { int a }"].join("\n"),
		expectError: null,
	},
	/* A functype emits no symbol at all, so a second identical declaration collides with nothing -
	   which is what lets a unit declare the functypes it uses and still be imported next to a unit
	   that declares the same ones. Disagreeing ones are still an error, and this is the only place
	   that can be caught, since nothing about a functype reaches gazl-validate. */
	{
		label: "a functype may be re-declared identically",
		source: ["functype Cb(int a) returns int r", "functype Cb(int a) returns int r"].join("\n"),
		expectError: null,
	},
	{
		label: "a functype re-declared with a different shape is an error",
		source: ["functype Cb(int a) returns int r", "functype Cb(float a) returns int r"].join("\n"),
		expectError: "already declared with a different shape",
	},
	{
		label: "a functype still cannot take a struct's name",
		source: ["struct Cb { int a }", "functype Cb(int a)"].join("\n"),
		expectError: "already used by a struct",
	},
	/* An untyped funcptr is not promoted into a named one, exactly as a bare `pointer` is not
	   assignable to an `int pointer` (E201) - the named type exists to guarantee the shape of what
	   gets called. `(Cb)` is the cast that says "I checked"; a functype needs no `pointer` modifier. */
	{
		label: "an untyped funcptr does not assign into a named functype",
		source: ["functype Cb(int a) returns int r",
			"function main() locals funcptr f, Cb c { c = f; }"].join("\n"),
		expectError: "Funcptr type mismatch (expected 'Cb', got an untyped funcptr)",
	},
	{
		label: "...nor pass as one",
		source: ["functype Cb(int a) returns int r", "function g(Cb c) { }",
			"function main() locals funcptr f { g(f); }"].join("\n"),
		expectError: "Funcptr type mismatch for argument 1",
	},
	/* A GLOBAL read spells itself `&name`, exactly like a function reference, so testing the sigil
	   instead of the lookup sent globals down the function-reference branch to find no function and
	   fall out silently - past the very check that applied to them. */
	{
		label: "a global untyped funcptr is caught too, not just a local",
		source: ["functype Cb(int a) returns int r", "global funcptr fp;",
			"function main() locals Cb c { c = global fp; }"].join("\n"),
		expectError: "got an untyped funcptr",
	},
	{
		label: "a global of the named type still assigns freely",
		source: ["functype Cb(int a) returns int r", "global Cb gc;",
			"function main() locals Cb c { c = global gc; }"].join("\n"),
		expectError: null,
	},
	{
		label: "a (Cb) cast is what makes it explicit",
		source: ["functype Cb(int a) returns int r", "function g(Cb c) { }",
			"function main() locals funcptr f, Cb c { c = (Cb)f; g((Cb)f); }"].join("\n"),
		expectError: null,
	},
	{
		label: "a named funcptr type still widens to plain funcptr",
		source: ["functype Cb(int a) returns int r",
			"function main() locals funcptr f, Cb c { f = c; }"].join("\n"),
		expectError: null,
	},
	{
		label: "a functype is castable through a pointer too",
		source: ["functype Fn(int a) returns int r",
			"function main() locals Fn pointer p, pointer q { p = (Fn pointer)q; }"].join("\n"),
		expectError: null,
	},
	{
		label: "relaxing the cast rule left parenthesized expressions alone",
		source: ["function main() locals int a, int b, int i { i = (a) * (b) + (a); }"].join("\n"),
		expectError: null,
	},
];

/* Step 5: `export` rides the signature metadata as a role prefix, so --dead-strip can find roots. */
(function () {
	const out = compileWithJsImpala(
		["export function process() { }", "function helper() { }"].join("\n") + "\n",
		{ randomId: 42 }
	);
	if (!/signature export func process\b/.test(out)) {
		console.error("export: process should be marked `export func` in signature metadata");
		process.exit(1);
	}
	if (/signature export func helper\b/.test(out)) {
		console.error("export: helper must NOT be marked export");
		process.exit(1);
	}
})();

for (const testCase of typedPointerCases) {
	expectCompileOutcome("typed pointers", testCase.label, testCase.source, testCase.expectError);
}
console.log("impala.jspeg compiler enforces typed pointer/array element rules");

// --- Impala 2 diagnostics format --------------------------------------------

let diagnosticMessage = null;
try {
	compileWithJsImpala("function main() locals int pointer p, pointer raw { p = raw; }\n", {
		randomId: 42,
		sourceName: "diag.impala",
	});
} catch (err) {
	diagnosticMessage = err && err.message ? err.message : String(err);
}
if (
	diagnosticMessage === null ||
	!/^diag\.impala:1:\d+: error\[E201\]: /.test(diagnosticMessage) ||
	!diagnosticMessage.includes(": note: use a cast: (int pointer)")
) {
	console.error("diagnostics did not use the path:line:col error[code] + note format");
	console.error(diagnosticMessage);
	process.exit(1);
}
console.log("impala.jspeg compiler emits GCC-style coded diagnostics with fix-it notes");

const diagnosticWarnings = [];
compileWithJsImpala("function main() locals int a { a = 1 | 2 & 3; }\n", {
	randomId: 42,
	sourceName: "diag.impala",
	legacy: true,
	onWarning: (formatted) => diagnosticWarnings.push(formatted),
});
if (diagnosticWarnings.length !== 1 || !/^diag\.impala:1:\d+: warning\[E101\]: /.test(diagnosticWarnings[0])) {
	console.error("legacy warnings did not use the path:line:col warning[code] format");
	console.error(diagnosticWarnings[0]);
	process.exit(1);
}
console.log("impala.jspeg compiler renders --legacy warnings in the same diagnostic shape");

console.log("JSPEG regression suite completed successfully");
