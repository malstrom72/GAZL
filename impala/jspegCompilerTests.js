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
			+ "{ a[0].x = v; a[1].x = v; r = a[0].x + a[1].x; }\n"
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
			"\tglobal bank[0].n = 1; f = global bank[2].g;",
			"\tfor (i = 0 to 4) global bank[i].n = i;",
			"\tloc[1].g = 0.5;",
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
			"function main() { use(&global bank[1]); }",
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
			"function main() locals Outer o { o.items[1].a = 0.5; }",
		].join("\n"),
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
