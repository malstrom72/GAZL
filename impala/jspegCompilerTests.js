const fs = require("fs");
const path = require("path");
const childProcess = require("child_process");
const vm = require("vm");

const { wrapCompilerSource, applyImpalaHardening } = require("./updateJSPEG.js");
const { compileWithJsImpala } = require("./impalaJsCompilerRunner");

const dir = __dirname;
const IMPALA_ENCODING = "latin1";
const validatorScript = path.join(dir, "..", "tools", "gazl-validate.js");
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

	const validatorOutput = result.stdout || "";
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

const legacyWarnings = [];
try {
	compileWithJsImpala(mixedBitwiseSource, {
		randomId: 42,
		legacy: true,
		onWarning: (formatted, message) => legacyWarnings.push(message),
	});
} catch (err) {
	console.error("impala.jspeg compiler rejected mixed bitwise operators under --legacy");
	console.error(err && err.message ? err.message : String(err));
	process.exit(1);
}
if (legacyWarnings.length !== 1 || !legacyWarnings[0].includes("Mixed bitwise operators")) {
	console.error("impala.jspeg compiler did not emit exactly one mixed-bitwise warning under --legacy");
	process.exit(1);
}
console.log("impala.jspeg compiler downgrades mixed bitwise operators to a warning under --legacy");

const strictParenthesized = compileWithJsImpala(parenthesizedBitwiseSource, { randomId: 42 });
const legacyParenthesized = compileWithJsImpala(parenthesizedBitwiseSource, { randomId: 42, legacy: true });
if (strictParenthesized !== legacyParenthesized) {
	console.error("parenthesized bitwise source must compile identically in strict and legacy modes");
	process.exit(1);
}
console.log("impala.jspeg compiler accepts parenthesized bitwise mixes identically in both modes");

compileWithJsImpala(sameOpChainSource, { randomId: 42 });
console.log("impala.jspeg compiler accepts same-operator bitwise chains without parentheses");

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

const comparisonWarnings = [];
try {
	compileWithJsImpala(comparisonMixSource, {
		randomId: 42,
		legacy: true,
		onWarning: (formatted, message) => comparisonWarnings.push(message),
	});
} catch (err) {
	console.error("impala.jspeg compiler rejected bitwise-vs-comparison mix under --legacy");
	console.error(err && err.message ? err.message : String(err));
	process.exit(1);
}
if (comparisonWarnings.length !== 1 || !comparisonWarnings[0].includes("Comparison mixed with bitwise")) {
	console.error("impala.jspeg compiler did not emit exactly one comparison-mix warning under --legacy");
	process.exit(1);
}
console.log("impala.jspeg compiler downgrades comparison mixes to a warning under --legacy");

compileWithJsImpala(comparisonParenthesizedSource, { randomId: 42 });
console.log("impala.jspeg compiler accepts parenthesized bitwise-vs-comparison conditions");

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
];

for (const testCase of typedPointerCases) {
	let observedError = null;
	try {
		compileWithJsImpala(testCase.source + "\n", { randomId: 42 });
	} catch (err) {
		observedError = err && err.message ? err.message : String(err);
	}
	if (testCase.expectError === null) {
		if (observedError !== null) {
			console.error(`typed pointers: ${testCase.label} unexpectedly failed`);
			console.error(observedError);
			process.exit(1);
		}
	} else if (observedError === null || !observedError.includes(testCase.expectError)) {
		console.error(`typed pointers: ${testCase.label} did not raise "${testCase.expectError}"`);
		if (observedError !== null) {
			console.error(observedError);
		}
		process.exit(1);
	}
}
console.log("impala.jspeg compiler enforces typed pointer/array element rules");

console.log("JSPEG regression suite completed successfully");
