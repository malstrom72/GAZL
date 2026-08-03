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

/* A source that must COMPILE clean and then be refused by the assembler. That is a real outcome for
   anything Impala defers rather than decides (docs/TwoStageConstants.md), and nothing else here can see
   it: the parity fixtures require assembly to SUCCEED, and a diagnostic table only ever runs the
   compiler. Skipped without GAZLCmd, like every other assembler-backed check. */
function assertLoadFails(label, source, expectedInMessage) {
	if (!haveGazlCmd()) {
		return;
	}
	const gazlPath = path.join(dir, "..", "tests", "impala", "erroneous", "loadFail.gazl");
	fs.mkdirSync(path.dirname(gazlPath), { recursive: true });
	fs.writeFileSync(gazlPath, compileWithJsImpala(source, { randomId: 42 }), IMPALA_ENCODING);
	const verdict = assembleOnly(gazlPath);
	assert(verdict !== undefined && verdict !== NEEDS_HOST && verdict.indexOf(expectedInMessage) >= 0,
		`${label} should have been refused at assembly time with "${expectedInMessage}"\n`
			+ `  got: ${verdict === undefined ? "assembled clean" : verdict}`);
	console.log(`${label} is refused at assembly time`);
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

// `inline` is PARKED for Impala 3.0 (docs/ParkedFeatures.md): an expansion needs GAZL 2 `SCOP` / `ENDS`
// to place its locals, and Impala 2 has to stay usable on GAZL 1.0 engines. The keyword stays reserved
// so the door says why rather than reading as an unknown identifier. One case is the whole surface: the
// rejection is the first action of FuncDecl, before anything else about the function is looked at.
expectCompileOutcome("inline", "an inline function",
	"inline function f(int a) returns int r { r = a * 2; }\n"
		+ "function main() locals int q { q = f(3); }\n", "not supported in Impala 2.0");

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
const SYM_RANGE = "const int LO = 5\nconst int HI = 9\n";   // named consts: constInt never folds these
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
	// A SYMBOLIC range disables the window check - `constInt` never folds a named const, by design - but
	// it must NOT disable the duplicate check, which never needed the range base. It did until
	// 2026-08-02, sharing one early return: both arms minted `.s0#K` and the build died at assembly on
	// `Symbol already defined: .s0.0`. Non-zero base included, since that is where the offset the old
	// code keyed on stops being the value.
	[
		"duplicate case under a symbolic range",
		SYM_RANGE + SW("LO to HI", "case 0: { i=1; } case 0: { i=2; }"), "Duplicate case value 0"],
	[
		"duplicate case under a symbolic range with a non-zero base",
		SYM_RANGE + SW("LO to HI", "case 6: { i=1; } case 6: { i=2; }"), "Duplicate case value 6"],
	[
		"distinct cases under a symbolic range still compile",
		SYM_RANGE + SW("LO to HI", "case 0: { i=1; } case 1: { i=2; }"), null],
	// ...and the window check stays OFF there: a configuration may legitimately narrow the range, so
	// erroring on a now-surplus arm would make that configuration unbuildable (docs/TwoStageConstants.md).
	[
		"a case outside a symbolic range is left to the configuration",
		SYM_RANGE + SW("LO to HI", "case 99: { i=1; }"), null],
	["goto an undefined label", "function f() { goto nowhere; }", "goto to undefined label nowhere"],
	["goto a defined label", "function f() locals int i { i = 0; if (i < 3) goto top; top: ; }", null],
	// A label written twice mints two identical GAZL labels; the assembler rejected "Symbol already
	// defined" against a name and line the user never wrote. The label map in processBranches decides it.
	["a label defined twice in one function", "function f() locals int i { i = 0; lbl: ; lbl: ; }",
		"Duplicate label lbl"],
	["the same label name in two functions", "function f() locals int i { i=0; lbl: ; }\nfunction g() locals int i { i=0; lbl: ; }", null],
	["write to a readonly array element",
		"readonly int array T[4] = { 1,2,3,4 }\nfunction f() { global T[0] = 9; }",
		"Cannot assign to a readonly value"],
	["read a readonly array element",
		"readonly int array T[4] = { 1,2,3,4 }\nfunction f() locals int x { x = global T[2]; }", null],
	["write to a writable array element",
		"global int array W[4]\nfunction f() { global W[0] = 9; }", null],
	// A string literal lives in a readonly section; the store used to compile and fail at GAZL load
	// naming `.s_abc_...`. Marked readonly so the same E404 element-write check catches it here.
	["write to a string literal element", `function f() { "abc"[0] = 1; }`,
		"Cannot assign to a readonly value"],
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

// A struct value is initialized BY FIELD NAME. The 1.x positional list silently changed meaning the moment
// a field was inserted, removed or reordered - nothing in the source had to change for it to start filling
// different fields - so it is E455 by default and only --legacy still maps by position. Array levels stay
// positional in both forms (a struct's array field, and an array OF structs): there the index does the
// naming, so a `field:` in one of those slots is E458.
const NAMED = "struct P { int x; int y }\nstruct Q { int n; P mid; int array v[2] }\n";
const namedInitCases = [
	["named fields in declaration order",
		NAMED + "global Q q = { n: 1, mid: { x: 2, y: 3 }, v: { 4, 5 } }\nfunction main(){ }", null],
	["named fields OUT of order",
		NAMED + "global Q q = { v: { 4, 5 }, n: 1, mid: { y: 3, x: 2 } }\nfunction main(){ }", null],
	["omitted fields zero-fill", NAMED + "global Q q = { n: 1 }\nfunction main(){ }", null],
	["an empty initializer is fine", NAMED + "global Q q = { }\nfunction main(){ }", null],
	["positional is rejected", NAMED + "global P p = { 1, 2 }\nfunction main(){ }", "must name its fields"],
	["mixing named and positional is rejected",
		NAMED + "global P p = { x: 1, 2 }\nfunction main(){ }", "mixes named and positional"],
	["an unknown field is rejected", NAMED + "global P p = { x: 1, q: 2 }\nfunction main(){ }", "has no field q"],
	["a repeated field is rejected", NAMED + "global P p = { x: 1, x: 2 }\nfunction main(){ }", "initialized twice"],
	["a name where an array INDEX belongs is rejected",
		NAMED + "global Q q = { v: { x: 4, y: 5 } }\nfunction main(){ }", "an array element is positional"],
	["a name on an array-of-structs SLOT is rejected",
		NAMED + "global P array bank[2] = { first: { x: 1, y: 2 }, { x: 3, y: 4 } }\nfunction main(){ }",
		"an array element is positional"],
	["array-of-structs slots stay positional, their fields named",
		NAMED + "global P array bank[2] = { { x: 1, y: 2 }, { x: 3, y: 4 } }\nfunction main(){ }", null],
];
for (const [label, source, expected] of namedInitCases) {
	expectCompileOutcome("named initializer", label, source, expected);
}

// Entry order must not affect layout: words come out in FIELD order however they were written.
const inOrder = compileWithJsImpala(
	NAMED + "global Q q = { n: 1, mid: { x: 2, y: 3 }, v: { 4, 5 } }\nfunction main(){ }\n", { randomId: 42 });
const outOfOrder = compileWithJsImpala(
	NAMED + "global Q q = { v: { 4, 5 }, mid: { y: 3, x: 2 }, n: 1 }\nfunction main(){ }\n", { randomId: 42 });
assert(/DATA #1 #2 #3 #4 #5/.test(inOrder), `a named initializer must emit in field order\n${inOrder}`);
assert(inOrder === outOfOrder, "entry order must not change the emitted layout");

// --legacy keeps the 1.x positional form compiling, so old sources still build.
expectSingleLegacyWarning(NAMED + "global P p = { 1, 2 }\nfunction main(){ }\n",
	"must name its fields", "a positional struct initializer");
console.log("impala.jspeg compiler initializes struct values by field name");

// A struct's real size is emitted as assemble-time arithmetic (`! ADDi <a> #<a> #N`), so a symbolic array
// extent lays out fine and every consumer below must keep working. What cannot be done is placing DATA at
// or after such a field - see the case table for why. `fieldWords` used to multiply the extent OPERAND by a
// number, hand back NaN, and let the initializer loop run zero times, so `{ 1, { 7, 8, 9 }, 2 }` emitted
// `DATA #1 #2` - z's 2 landing in v[0].
const SYM_STRUCT = "const int N = 3\nstruct S { int a; int array v[N]; int z }\n";
const SYM_TAIL = "const int N = 3\nstruct S { int a; int array v[N] }\n";
const SYM_ZERO_SRC = SYM_STRUCT + "global S s = { a: 1, v: { 0, 0 }, z: 0 }\nfunction main() { }\n";
const SYM_FILL_SRC = SYM_STRUCT + "global S s = { a: 1, v: { 7, 8, 9 } }\nfunction main() { }\n";
const symbolicExtentCases = [
	// The array ITSELF is fillable: its words start at a known position, so only the ones it did not fill
	// are unplaceable. What Impala cannot do is check the count against a symbolic extent - an over-filled
	// array spills into whatever follows and the assembler cannot see it either, because `v[N]` with N=2
	// given three values, plus a `z`, emits four words that fit `1+N+1` EXACTLY. So the count check is
	// DEFERRED to the assembler as `! LEQi`/`! FAIL` (assertFitsExtent), which by then knows the extent, and
	// it is the field AFTER the array that stays blocked.
	["a symbolic array may be given values", SYM_FILL_SRC, null],
	["also when it is the last field",
		SYM_TAIL + "global S s = { a: 1, v: { 7 } }\nfunction main() { }", null],
	["but a field after it is blocked",
		SYM_STRUCT + "global S s = { a: 1, z: 2 }\nfunction main() { }", "Cannot initialize"],
	["blocked even when the array itself was filled",
		SYM_STRUCT + "global S s = { a: 1, v: { 7 }, z: 2 }\nfunction main() { }", "Cannot initialize"],
	["zeros are fine - they are what the region fills anyway", SYM_ZERO_SRC, null],
	// Zero-ness is a VALUE, not a spelling. Comparing operands against the canonical `#0`/`#0.0`/`&NULL`
	// strings rejected these, which is erroring on safe code. A SYMBOL stays rejected - Impala does not
	// know its value and must not guess one (docs/TwoStageConstants.md rule 2).
	["hex zero is zero", SYM_STRUCT + "global S s = { a: 1, z: 0x0 }\nfunction main() { }", null],
	["negative zero is zero", SYM_STRUCT + "global S s = { a: 1, z: -0 }\nfunction main() { }", null],
	["float zero with an exponent is zero",
		"const int N = 3\nstruct F { int a; float array v[N]; float z }\n"
			+ "global F f = { a: 1, z: 0.0e0 }\nfunction main() { }", null],
	["a const that happens to be zero is NOT assumed zero",
		"const int Z = 0\n" + SYM_STRUCT + "global S s = { a: 1, z: Z }\nfunction main() { }",
		"Cannot initialize"],
	["omitting the symbolic field and everything after it is fine",
		SYM_STRUCT + "global S s = { a: 1 }\nfunction main() { }", null],
	["the block reaches out through a nested struct",
		"const int N = 3\nstruct Inner { int array v[N] }\nstruct Outer { Inner i; int z }\n"
			+ "global Outer o = { i: { v: { 7, 8 } }, z: 2 }\nfunction main() { }", "Cannot initialize"],
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
	"struct S { int a; int array v[3]; int z }\nglobal S s = { a: 1, v: { 7, 8, 9 }, z: 2 }\nfunction main() { }\n",
	{ randomId: 42 });
assert(/DATA #1 #7 #8 #9 #2/.test(litInit),
	`a literal-extent struct initializer must emit all five words\n${litInit}`);
// A symbolic field emits exactly the words it was GIVEN and the row stops there - the ones it did not
// fill are a symbolic count DATA cannot skip, and `z` lies past them.
const symInit = compileWithJsImpala(SYM_ZERO_SRC, { randomId: 42 });
assert(/DATA #1 #0 #0(\s|$)/.test(symInit),
	`a symbolic initializer must stop at the last placeable word\n${symInit}`);
console.log("impala.jspeg compiler lays out symbolic struct extents and refuses to guess the rest");

// The count check Impala cannot make is handed to the assembler, which by then knows the extent. Pin the
// OPERANDS, not just that a line appeared: reversed operands, or elements counted where words are meant,
// would still emit a plausible `! LEQi` and silently stop catching the spill it exists for. The guard must
// also sit ABOVE the DATA rows - below them GAZL's own whole-allocation check reports the coarser
// `Not enough space in data section` first, and for a field spill that fits the total it says nothing.
const symAssert = compileWithJsImpala(SYM_FILL_SRC, { randomId: 42 });
assert(/! LEQi #3 #\.z\.S\.v @\.g\d+\n\s*! FAIL too many initializer values for S\.v: 3 given, room for \.z\.S\.v\n\s*\.g\d+:\s*!\n\s*DATA /.test(symAssert),
	`a symbolic array fill must defer its count check to the assembler, above its DATA rows\n${symAssert}`);
// WORDS, not elements: a struct-element array contributes .z.Elem each, and the extent symbol is in words.
const symElemAssert = compileWithJsImpala(
	"const int N = 3\nstruct P { int lo; int hi }\nstruct S { P array p[N] }\n"
		+ "global S s = { p: { { lo: 1, hi: 2 }, { lo: 3, hi: 4 } } }\nfunction main() { }\n", { randomId: 42 });
assert(/! LEQi #4 #\.z\.S\.p @/.test(symElemAssert),
	`a struct-element fill must count WORDS against the extent, not elements\n${symElemAssert}`);
// The compile-time half above only proves the LINE is there; this proves it BITES, at assembly time, where
// nothing else in this suite would notice it going quiet. The shape is the original defect: `v[N]` with N=2
// given three values plus a `z` emits four words that fit `1+N+1` EXACTLY, so the allocation never
// overflows and GAZL's own check stays silent - only this assertion can tell.
assertLoadFails("an over-filled symbolic array field",
	"const int N = 2\nstruct S { int head; int array v[N]; int z }\n"
		+ "global S s = { head: 1, v: { 1, 2, 3 } }\nfunction main() { }\n",
	"too many initializer values for S.v: 3 given, room for .z.S.v");
console.log("impala.jspeg compiler defers the symbolic value-count check to GAZL assembly time");

// Surplus initializer values used to be read by nobody and vanish: the fill loops stop at the extent, so
// nothing was emitted for them and the assembler had nothing wrong to see. (A surplus FIELD is already
// E456 - naming the fields closed that one for free.) A flat `int array a[2] = { 7, 8, 9 }` is a different
// shape: it over-runs the section, and the ASSEMBLER reports it, so Impala does not duplicate that check.
const surplusCases = [
	["too many values for a struct array field",
		"struct S { int array v[2]; int z }\nglobal S s = { v: { 7, 8, 9 }, z: 5 }\nfunction main(){ }",
		"3 given, but it holds 2"],
	["too many elements for an array of structs",
		"struct S { int a }\nglobal S array k[1] = { { a: 1 }, { a: 2 } }\nfunction main(){ }",
		"2 given, but it holds 1"],
	["an exact fit is fine",
		"struct S { int array v[2]; int z }\nglobal S s = { v: { 7, 8 }, z: 5 }\nfunction main(){ }", null],
	["under-filling is fine (the rest zero-fills)",
		"struct S { int array v[2]; int z }\nglobal S s = { v: { 7 }, z: 5 }\nfunction main(){ }", null],
];
for (const [label, source, expected] of surplusCases) {
	expectCompileOutcome("surplus initializer", label, source, expected);
}
// The NAMED form reports a surplus as E456 (a name no field has). The POSITIONAL form has no name to
// report, so it needs the count rule - and it is reachable ONLY under --legacy, which is why it was
// missed at first: the strict dialect raises E455 and stops before ever mapping by index. So this one
// cannot go in the table above; it has to compile in legacy mode to get past E455.
{
	let observed = null;
	try {
		compileWithJsImpala("struct S { int a; int b }\nglobal S s = { 1, 2, 3 }\nfunction main(){ }\n",
			{ randomId: 42, legacy: true, onWarning: () => {} });
	} catch (err) {
		observed = err && err.message ? err.message : String(err);
	}
	assert(observed !== null && observed.includes("3 given, but it holds 2"),
		`a legacy positional list must not silently drop a surplus value\n${observed}`);
}
// The caret belongs on the surplus entry, matching the E454/E459 rule.
expectDiagnosticAt("E460 names the surplus entry, not the initializer",
	"struct S { int array v[2]; int z }\nglobal S s = { v: { 7, 8, 9 }, z: 5 }\nfunction main(){ }\n",
	"2:27: error[E460]");
console.log("impala.jspeg compiler rejects initializer values that do not fit");

// A flat array initializer checked each entry against ITS OWN type, which no value can fail, so the
// declared element type went unenforced. The scalar paths were always strict (`global float f = 1` is
// E407); only the array path was not. Two shapes it silently mis-compiled, neither of which the
// assembler can see - a word is a word: `{ 1, "s" }` on an int array stored a POINTER in an int slot,
// and `{ 1, 2 }` on a float array stored the INTEGER bit pattern, so F[0] read back as 1.4013e-45.
const arrayElemTypeCases = [
	["ints in an int array", "readonly int array A[2] = { 1, 2 }", null],
	["a named const in an int array", "const int N = 7\nreadonly int array A[2] = { N, 2 }", null],
	["a string in an int array", "readonly int array A[2] = { 1, \"nope\" }", "Expected constant int"],
	["a float literal in an int array", "readonly int array A[2] = { 1, 2.5 }", "Expected constant int"],
	["floats in a float array", "readonly float array A[2] = { 1.0, 2.5 }", null],
	["int literals in a float array", "readonly float array A[2] = { 1, 2 }", "Expected constant float"],
	["strings in a pointer array", "readonly int pointer array A[2] = { \"a\", \"b\" }", null],
	["ints in a pointer array", "readonly int pointer array A[2] = { 1, 2 }", "Expected constant pointer"],
	// An UNTYPED array states no element type, so there is nothing to check it against - Impala 1 wrote
	// these and they must keep compiling. Not knowing is not the same as being fine.
	["an untyped array takes anything", "readonly array A[2] = { 1, \"x\" }", null],
	// A struct-element array must keep its own friendlier message rather than falling out as a type
	// mismatch against the struct name, which is why the element check skips a struct head.
	["a struct-element array still asks for nested braces", "struct S { int a }\nglobal S array B[1] = { 1 }",
		"needs nested braces"],
];
for (const [label, source, expected] of arrayElemTypeCases) {
	expectCompileOutcome("array element type", label, `${source}\nfunction main() { }\n`, expected);
}
// ...and the caret names the offending ENTRY, not the `{` and not the next declaration.
expectDiagnosticAt("E407 names the array entry whose type is wrong",
	"readonly int array A[2] = { 1, \"nope\" }\nfunction main() { }\n", "1:32: error[E407]");
console.log("impala.jspeg compiler checks array initializer entries against the declared element type");

// ...and the ROW carries the type, so the assembler re-checks every operand independently of Impala.
// `DATi`/`DATf`/`DATp` apply their type to all operands on the line (src/GAZL.cpp:996-1019 - one loop
// for all four mnemonics), where `DATA` takes KONST and checks nothing. That matters most for what
// Impala cannot fold: `DATi #N` verifies N is `! DEFi`, not `! DEFf`. Verified against GAZLCmd by
// READBACK, not by acceptance - a short row just zero-fills its section and assembles clean either way.
const initRowCases = [
	["int rows are DATi", "readonly int array A[2] = { 1, 2 }", /\bDATi #1 #2\b/],
	["float rows are DATf", "readonly float array A[2] = { 1.0, 2.5 }", /\bDATf #1\.0 #2\.5\b/],
	["pointer rows are DATp", "readonly int pointer array A[2] = { \"a\", \"b\" }", /\bDATp &\S+ &\S+/],
	// The whole point: a symbolic const gets its type checked at assembly time, which Impala cannot do.
	["a symbolic const still rides a typed row", "const int N = 7\nreadonly int array A[2] = { N, 2 }",
		/\bDATi #N #2\b/],
	// An untyped array has no element type to check against, so its row must stay the permissive form.
	["an untyped array keeps DATA", "readonly array A[2] = { 1, \"x\" }", /\bDATA #1 &/],
	// A struct row spans fields of DIFFERENT types, which is the mixed case DATA exists for - typing it
	// is not merely unnecessary, it is impossible (see `consts.mixed` in src/UnitTest.gazl).
	["a struct initializer keeps DATA", "struct S { int a; float b }\nglobal S s = { a: 1, b: 2.5 }",
		/\bDATA #1 #2\.5\b/],
];
for (const [label, source, wanted] of initRowCases) {
	const out = compileWithJsImpala(`${source}\nfunction main() { }\n`, { randomId: 42 });
	assert(wanted.test(out), `${label}: expected ${wanted} in the emitted rows\n${out}`);
}
console.log("impala.jspeg compiler types its array initializer rows so the assembler rechecks them");

// `readonly` reaches an assignment as a `:=` operator, which no lvalue branch accepts - so a readonly
// SCALAR and a readonly STRUCT FIELD both fell out as the bare "Invalid lvalue" that a genuine mistake
// like `1 = q` gets. Only the array-element case had a real message. A struct field additionally reached
// the assignment as a writable `=*`, so its POKE was emitted and only the CNST region caught it, at load.
const readonlyWriteCases = [
	["a readonly scalar", "readonly int r\nfunction main(){ global r = 1; }", "readonly value"],
	["a readonly array element", "readonly int array t[2]\nfunction main(){ global t[0] = 1; }",
		"readonly value"],
	["a readonly struct field", "struct S { int a }\nreadonly S s\nfunction main(){ global s.a = 1; }",
		"readonly value"],
	// The test is the readonly FLAG, never the operator/operand spelling. Keying on `:=` plus an
	// `&`/`$` operand looked equivalent and was not: it also matched a function name, a whole global
	// array, `nullfunc`, `null`, `&f` and a parameter - none of them readonly - and told each of them
	// to "declare it `global` instead of `readonly`".
	["a function name is not readonly",
		"function foo(){}\nfunction bar(){}\nfunction main(){ foo = bar; }", "Invalid lvalue"],
	["a whole global array is not readonly",
		"global int array t[2]\nglobal int array u[2]\nfunction main(){ global t = global u; }",
		"Invalid lvalue"],
	["nullfunc is not readonly", "function f(){}\nfunction main(){ nullfunc = f; }", "Invalid lvalue"],
	["a parameter is not readonly",
		"function f(int a) returns int r { a = 1; r = a; }\nfunction main() locals int q { q = f(2); }",
		"Invalid lvalue"],
	// Whole-struct assignment returns before the scalar readonly check, so it emitted its COPY straight
	// into the const region - the field-level hole was closed while this one stayed open.
	["a whole readonly struct",
		"struct S { int a }\nreadonly S s\nglobal S t\nfunction main(){ global s = global t; }",
		"readonly value"],
	["a whole WRITABLE struct is fine",
		"struct S { int a }\nglobal S s\nglobal S t\nfunction main(){ global s = global t; }", null],
	["reading a readonly struct field is fine",
		"struct S { int a }\nreadonly S s\nfunction main() locals int q { q = global s.a; }", null],
	["writing a NON-readonly struct field is fine",
		"struct S { int a }\nglobal S s\nfunction main(){ global s.a = 1; }", null],
	// A literal carries `:=` too, so the readonly wording must not swallow a real syntax mistake.
	["a genuine non-lvalue stays 'Invalid lvalue'", "function main() locals int q { 1 = q; }",
		"Invalid lvalue"],
];
for (const [label, source, expected] of readonlyWriteCases) {
	expectCompileOutcome("readonly write", label, source, expected);
}
console.log("impala.jspeg compiler names what is readonly instead of saying `Invalid lvalue`");

// The HOST owns an extern struct's field offsets AND its size, so a positional DATA row guesses at field
// order, `.z.`, and whether there are fields Impala never saw. Reads already adapt (`POKE &g:.o.E.f`);
// static data was the only early-bound part, and therefore the only part that could be wrong - silently.
// Only an all-zero initializer is layout-independent, and that is what the region fills anyway.
const EXT = "extern struct E { int a; int b }\n";
const EXT_ZERO_SRC = EXT + "global E e = { a: 0, b: 0 }\nfunction main(){ }\n";
const externInitCases = [
	["a non-zero extern initializer is rejected", EXT + "global E e = { a: 1, b: 2 }\nfunction main(){ }",
		"the host owns the layout"],
	["an all-zero extern initializer is fine", EXT_ZERO_SRC, null],
	["empty braces are fine", EXT + "global E e = { }\nfunction main(){ }", null],
	["no initializer at all is fine", EXT + "global E e\nfunction main(){ }", null],
	["an array of extern structs is rejected too",
		EXT + "global E array k[2] = { { a: 1 }, { a: 2 } }\nfunction main(){ }", "the host owns the layout"],
	["readonly does not exempt it", EXT + "readonly E e = { a: 1 }\nfunction main(){ }",
		"the host owns the layout"],
];
for (const [label, source, expected] of externInitCases) {
	expectCompileOutcome("extern initializer", label, source, expected);
}
const externZero = compileWithJsImpala(EXT_ZERO_SRC, { randomId: 42 });
assert(!/DATA/.test(externZero), `an all-zero extern initializer must emit no DATA row\n${externZero}`);
console.log("impala.jspeg compiler refuses to guess a host-owned struct layout");

// GAZL has ONE flat symbol space; Impala's tables did not. `global int S` beside `function S()` cleared
// every per-table check and then would not assemble ("Symbol already defined: S"), and `struct S` +
// `functype S` was caught in one ORDER only - the same clash, legal written the other way round. One claim
// per top-level name replaces the piecemeal checks, so every pair rejects, symmetrically, naming whichever
// kind got there first. Both orders are listed on purpose: the asymmetry is what made this a bug rather
// than a policy.
const nameClashCases = [
	["struct then functype", "struct S { int a }\nfunctype S(int x) returns int", "already used by a struct"],
	["functype then struct", "functype S(int x) returns int\nstruct S { int a }", "already used by a functype"],
	["struct then function", "struct S { int a }\nfunction S() { }", "already used by a struct"],
	["struct then global", "struct S { int a }\nglobal int array S[2]", "already used by a struct"],
	["struct then const", "struct S { int a }\nconst int S = 3", "already used by a struct"],
	["functype then function", "functype S(int x) returns int\nfunction S() { }", "already used by a functype"],
	// The one that used to assemble-fail rather than compile-fail, in both directions.
	["global then function", "global int S\nfunction S() { }", "already used by a global"],
	["function then global", "function S() { }\nglobal int S", "already used by a function"],
	["const then function", "const int S = 1\nfunction S() { }", "already used by a const"],
	["readonly then struct", "readonly int array S[2] = { 1, 2 }\nstruct S { int a }", "already used by a global"],
	// Re-claiming the SAME kind is a declaration meeting its definition, or an import closure seeing one
	// unit twice. Those must stay legal or every `extern` pairing breaks.
	["extern function, name-only, then defined",
		"extern function f;\nfunction f(int a) returns int r { r = a; }", null],
	["extern function, prototyped, then defined",
		"extern function f(int a) returns int r;\nfunction f(int a) returns int r { r = a; }", null],
	["extern struct then defined", "extern struct E { int a }\nstruct E { int a }", null],
	["bodyless extern struct twice", "extern struct E\nextern struct E\nfunction main() { }", null],
	["valueless const then defined", "const int K;\nconst int K = 3", null],
	["one functype declared twice", "functype F(int x) returns int\nfunctype F(int x) returns int", null],
	// Only TOP-LEVEL names share the space. A local is `$name` in GAZL and a field is `.o.S.f`, so neither
	// can collide with anything here - rejecting those would be a new restriction nobody asked for.
	["a local may shadow a global", "global int v\nfunction main() locals int v { v = 1; }", null],
	["a field may share a struct's name",
		"struct P { int a }\nstruct Q { int P }\nfunction main() locals Q q { q.P = 1; }", null],
];
for (const [label, source, expected] of nameClashCases) {
	expectCompileOutcome("name clash", label, source, expected);
}
console.log("impala.jspeg compiler keeps one flat namespace for every top-level name");

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
	// E454/E459 point at the OFFENDING ENTRY, not at the `{` and not at the next declaration. The entry
	// position comes from `BracedEntry`'s `.at`, which is only reachable while the item is still an
	// object - hence the check living in pushInitScalar rather than in emitInitData.
	["E454 names the entry that cannot be placed",
		"const int N = 3\nstruct S { int a; int array v[N]; int z }\nglobal S s = { a: 1, z: 2 }\n"
			+ "function main() { }\n", "3:22: error[E454]"],
	["E459 names the entry, not the struct",
		"extern struct E { int a; int b }\nstruct O { int p; E e; int q }\nglobal O o = { p: 1, q: 7 }\n"
			+ "function main() { }\n", "3:22: error[E459]"],
	// The SCALAR initializer paths kept `$$i` while the brace path beside them moved to a saved start,
	// so `Expr` (and `']' _`) ate the trailing space and the caret landed on the NEXT declaration. When
	// the bad declaration was last in the file it landed past EOF and no source line printed at all -
	// which is why the last case here deliberately has nothing after it.
	["E407 names the global's initializer, not the next declaration",
		"global int x = \"nope\"\nfunction main() { }\n", "1:16: error[E407]"],
	["E407 names the const's initializer, not the next declaration",
		"const int X = \"nope\"\nfunction main() { }\n", "1:15: error[E407]"],
	["E407 names the array extent, not the next declaration",
		"global int array A[\"nope\"]\nfunction main() { }\n", "1:20: error[E407]"],
	["E421 names the initializer, not the next declaration",
		"struct S { int a }\nglobal S s = 5\nfunction main() { }\n", "2:14: error[E421]"],
	["a trailing bad initializer still renders a caret, not a position past EOF",
		"function main() { }\nglobal int x = \"nope\"\n", "2:16: error[E407]"],
	// E461: an array FIELD overrun stays inside the struct's allocation, so GAZL cannot see it -
	// `s.v[5]` on `int array v[2]` silently landed in `pad`. A PLAIN array is checked by the same rule,
	// on a different path (it decayed to a pointer at lookup), because `g[9]` and `s.v[9]` are the same
	// mistake and reporting one at Impala compile time and the other as a GAZL symbol would be arbitrary.
	// The negative case matters separately: it takes the DYNAMIC path (the folding branch's regex has no
	// minus sign) and writes BACKWARDS. The read and the bare argument are listed because neither goes
	// through makeRValue - they reuse the operand directly, so a check placed only there passes them.
	["E461 names the offending index on a struct array field",
		"struct S { int array v[2]; int array pad[8] }\nglobal S s\n"
			+ "function main() { global s.v[5] = 1; }\n", "3:30: error[E461]"],
	["E461 catches a negative index, which takes the dynamic path",
		"struct S { int array v[2]; int array pad[8] }\nglobal S s\n"
			+ "function main() { global s.pad[-1] = 1; }\n", "3:32: error[E461]"],
	["E461 reports a READ, which never reaches makeRValue",
		"struct S { int array v[2]; int array pad[8] }\nglobal S s\n"
			+ "function main() locals int x { x = global s.v[2]; }\n", "3:47: error[E461]"],
	["E461 reports a bare ARGUMENT",
		"extern native printInt\nstruct S { int array v[2]; int array pad[8] }\nglobal S s\n"
			+ "function main() { printInt(global s.v[2]); }\n", "4:39: error[E461]"],
	["E461 reports a `.field` reached through an element that is not there",
		"struct E { int a }\nstruct O { E array e[2]; int t }\nglobal O o\n"
			+ "function main() { global o.e[[2]].a = 1; }\n", "4:31: error[E461]"],
	["E461 covers a plain GLOBAL array, not just a struct field",
		"global int array g[4]\nfunction main() { global g[9] = 1; }\n", "2:28: error[E461]"],
	["E461 covers a plain LOCAL array",
		"function main() locals int array a[5] { a[9] = 1; }\n", "1:43: error[E461]"],
];
for (const [label, source, expected] of caretCases) {
	expectDiagnosticAt(label, source, expected);
}
console.log("impala.jspeg compiler points its carets at the offending token");

// ADDRESS FORMATION IS NEVER BOUNDS-CHECKED, at any index - the rule from docs/Impala2Review.md, and the
// reason E461 cannot fire at the subscript itself: whether an out-of-range constant is an error depends
// on what is done with it, and at the subscript the `&` has not been seen yet. It is also why Impala
// needs no one-past-the-end carve-out where C does; the end pointer is just an address like the rest.
// Without these cases a stricter rule looks green forever.
const addressCases = [
	["&field[extent] is a legal end pointer",
		"struct S { int array v[2]; int array pad[8] }\nglobal S s\n"
			+ "function main() locals int pointer p { p = &global s.v[2]; }"],
	["&field[extent] on the LAST field, one past the struct itself",
		"struct S { int array v[2]; int array pad[8] }\nglobal S s\n"
			+ "function main() locals int pointer p { p = &global s.pad[8]; }"],
	["&structField[[extent]] is a legal end pointer too",
		"struct E { int a }\nstruct O { E array e[2]; int t }\nglobal O o\n"
			+ "function main() locals E pointer p { p = &global o.e[[2]]; }"],
	["an address WELL past the end is legal too - the rule has no distance limit",
		"global int array g[4]\nfunction main() locals int pointer p { p = &global g[9]; }"],
	["...and the same on a plain local array",
		"function main() locals int array a[5], int pointer p { p = &a[40]; }"],
	["in-range reads and writes are untouched",
		"extern native printInt\nstruct S { int array v[2]; int array pad[8] }\nglobal S s\n"
			+ "function main() locals int x { global s.v[1] = 3; x = global s.v[1]; printInt(x); }"],
];
for (const [label, source] of addressCases) {
	expectCompileOutcome("address formation", label, source, null);
}
console.log("impala.jspeg compiler never bounds-checks ADDRESS formation");

// `--range-checks` is the third tier: a DYNAMIC index into a struct array field, which neither static
// tier nor GAZL can decide. Both halves are asserted, because a flag that silently does nothing and a
// flag that silently does it always look identical from one side. The OFF case is the load-bearing one:
// the guard lines stay in the .gazl text whatever `DEBUG` says, and that text is the shipped artifact.
{
	const src = "const int DEBUG = 1\nstruct S { int array v[2]; int array pad[8] }\nglobal S s\n"
		+ "export function main() locals int i { i = 5; global s.v[i] = 99; }\n";
	const off = compileWithJsImpala(src, { randomId: 42 });
	const on = compileWithJsImpala(src, { randomId: 42, rangeChecks: true });
	assert(!/index out of range/.test(off) && !/@\.r\d/.test(off),
		"range checks: emitted with the flag OFF\n" + off);
	assert(/! EQUi #DEBUG #0 @\.r\d/.test(on), "range checks: no DEBUG gate on the guard\n" + on);
	assert(/SUBi .* #\.z\.S\.v /.test(on), "range checks: bound is not the .z. extent symbol\n" + on);
	assert(/index out of range: S\.v/.test(on), "range checks: no message naming the field\n" + on);
	assert(/CALL \^assertFail/.test(on), "range checks: does not reuse the assertFail native\n" + on);
	console.log("impala.jspeg compiler emits DEBUG-gated range checks only under --range-checks");
}

// EVERY array shape, because a flag that covers one of them is worse than no flag - it reads as
// protection. A plain array decays to a pointer at lookup and so takes a different subscript path from
// a struct field; a local's extent lives under `.z.<func>.<name>`, a global's under `.z.<name>`. The
// last two entries are the boundary: a CONSTANT index is left to GAZL ("Offset out of bounds", a better
// diagnostic than a trap), and a bare pointer has no extent to check at all.
{
	const decls = "const int DEBUG = 1\nstruct E { int a }\nstruct S { int array v[3] }\nglobal S s\n"
		+ "global int array g[4]\nglobal E array ge[3]\n";
	const shapes = [
		["global scalar array", "global g[i] = 1;", "#.z.g "],
		["local scalar array", "a[i] = 1;", "#.z.main.a "],
		["struct array field", "global s.v[i] = 1;", "#.z.S.v "],
		["global array of structs", "global ge[[i]].a = 1;", "#.z.ge "],
		["local array of structs", "le[[i]].a = 1;", "#.z.main.le "],
	];
	for (const [label, stmt, bound] of shapes) {
		const on = compileWithJsImpala(decls
			+ "export function main() locals int i, int array a[5], E array le[2] { i = 1; " + stmt + " }\n",
			{ randomId: 42, rangeChecks: true });
		assert(on.includes("SUBi") && on.includes(bound),
			`range checks: ${label} is not bounded by ${bound.trim()}\n` + on);
	}
	const noCheck = [
		["a constant index (GAZL rejects it outright)", "global g[2] = 1;"],
		["a bare pointer (no extent exists)", "p = &a[0]; p[i] = 1;"],
	];
	for (const [label, stmt] of noCheck) {
		const on = compileWithJsImpala(decls
			+ "export function main() locals int i, int array a[5], int pointer p { i = 1; " + stmt + " }\n",
			{ randomId: 42, rangeChecks: true });
		assert(!/index out of range/.test(on), `range checks: fired on ${label}\n` + on);
	}
	console.log("impala.jspeg compiler range-checks every array shape, and only those");
}

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
			"global Outer g = { n: 1, mid: { a: 0.5, b: 0.7 }, g: 2.0 }",
			"readonly Outer preset = { n: 3, mid: { a: 0.1, b: 0.2 }, g: 0.9 }",
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
		source: ["struct S { int a; float b }", "global S g = { a: 1.0, b: 2.0 }"].join("\n"),
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
		source: ["struct V { int n }", "const int N = 2", "global V array bank[N] = { { n: 1 }, { n: 2 } }"].join("\n"),
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
