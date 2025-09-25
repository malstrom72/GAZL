const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const { wrapCompilerSource } = require('./updateJSPEG.js');
const { compileWithJsImpala } = require('./impalaJsCompilerRunner');

const dir = __dirname;
const IMPALA_ENCODING = 'latin1';
const validatorScript = path.join(dir, '..', '..', 'tools', 'gazl-validate.js');
const validatorFixturesDir = path.join(dir, 'testdata', 'validator');

const jspegSource = fs.readFileSync(path.join(dir, 'jspegCompiler.js'), 'utf8');
const compileJSPEG = require(path.join(dir, 'jspegCompiler.js'));
if (typeof compileJSPEG !== 'function') {
        console.error('jspegCompiler.js did not export a compiler function');
        process.exit(1);
}

const jspegGrammar = fs.readFileSync(path.join(dir, 'jspeg.jspeg'), 'utf8');
const [compilerOk, compilerGenerated, compilerIndex] = compileJSPEG(jspegGrammar);
if (!compilerOk) {
	console.error('Failed to compile jspeg.jspeg with recorded compiler');
	process.exit(1);
}
if (compilerIndex !== jspegGrammar.length) {
	console.error(`jspeg.jspeg compile stopped at ${compilerIndex} of ${jspegGrammar.length}`);
	process.exit(1);
}
const expectedJspegSource = wrapCompilerSource('compileJSPEG', compilerGenerated);
if (expectedJspegSource.trim() !== jspegSource.trim()) {
        console.error('jspegCompiler.js is out of date with jspeg.jspeg');
        process.exit(1);
}
console.log('jspegCompiler.js matches jspeg.jspeg output');

const compileJSPEGSelfHosted = eval(compilerGenerated);
const [selfHostOk, selfHostGenerated, selfHostIndex] = compileJSPEGSelfHosted(jspegGrammar);
if (!selfHostOk) {
	console.error('Self-hosted compiler failed to compile jspeg.jspeg');
	process.exit(1);
}
if (selfHostIndex !== jspegGrammar.length) {
	console.error(`Self-hosted compile stopped at ${selfHostIndex} of ${jspegGrammar.length}`);
	process.exit(1);
}
if (wrapCompilerSource('compileJSPEG', selfHostGenerated).trim() !== jspegSource.trim()) {
        console.error('Self-hosted compiler drifted from recorded jspegCompiler.js output');
        process.exit(1);
}
console.log('Self-hosted compile of jspeg.jspeg matches recorded compiler');

const impalaGrammar = fs.readFileSync(path.join(dir, 'impala.jspeg'), 'utf8');
const [impalaOk, impalaGenerated, impalaIndex] = compileJSPEG(impalaGrammar);
if (!impalaOk) {
	console.error('Failed to compile impala.jspeg with recorded compiler');
	process.exit(1);
}
if (impalaIndex !== impalaGrammar.length) {
	console.error(`impala.jspeg compile stopped at ${impalaIndex} of ${impalaGrammar.length}`);
	process.exit(1);
}
const impalaExisting = fs.readFileSync(path.join(dir, 'impalaCompiler.js'), 'utf8');
if (wrapCompilerSource('impalaCompiler', impalaGenerated, { prelude: 'var $$parser = {};', exposeSourceNameOption: true }).trim() !== impalaExisting.trim()) {
        console.error('Generated compiler differs from impalaCompiler.js');
        process.exit(1);
}
console.log('impalaCompiler.js matches generated output');

const [impalaSelfOk, impalaSelfGenerated, impalaSelfIndex] = compileJSPEGSelfHosted(impalaGrammar);
if (!impalaSelfOk) {
	console.error('Self-hosted compiler failed to compile impala.jspeg');
	process.exit(1);
}
if (impalaSelfIndex !== impalaGrammar.length) {
	console.error(`Self-hosted impala.jspeg compile stopped at ${impalaSelfIndex} of ${impalaGrammar.length}`);
	process.exit(1);
}
if (impalaGenerated.trim() !== impalaSelfGenerated.trim()) {
	console.error('impala.jspeg output diverged between recorded and self-hosted compilers');
	process.exit(1);
}
console.log('impala.jspeg compiles identically under self-hosted compiler');

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
			console.error(`${label} produced different results between baseline and self-hosted compilers for input ${JSON.stringify(test.input)}`);
			console.error('baseline:', baseline);
			console.error('selfHosted:', selfHosted);
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
	const source = fs.readFileSync(path.join(dir, filename), 'utf8');
	const baseline = compileAndEval(compileJSPEG, source, `${label} via baseline compiler`);
	const selfHosted = compileAndEval(compileJSPEGSelfHosted, source, `${label} via self-hosted compiler`);

	if (baseline.code.trim() !== selfHosted.code.trim()) {
		console.error(`${label} generated code diverges between baseline and self-hosted compilers`);
		process.exit(1);
	}

	compareParserOutputs(label, cases, baseline.parser, selfHosted.parser);
	console.log(`${label} parser matches across baseline and self-hosted compilers`);
}

const arithmeticCases = [
	{ input: '1+2*3', expectSuccess: true, expectValue: 7, expectIndex: '1+2*3'.length },
	{ input: '4*(2+3)', expectSuccess: true, expectValue: 20, expectIndex: '4*(2+3)'.length },
	{ input: '1+', expectSuccess: false }
];

testGrammarEquivalence('jspegTest.jspeg', 'jspegTest.jspeg', arithmeticCases);

const recordInput = 'foo=1,\nbar=23, qux=7';
const tagCaptureCases = [
	{ input: recordInput, expectSuccess: true, expectValue: { foo: 1, bar: 23, qux: 7 }, expectIndex: recordInput.length },
	{ input: 'foo=oops', expectSuccess: false }
];

testGrammarEquivalence('tagCaptureTest.jspeg', 'tagCaptureTest.jspeg', tagCaptureCases);

const parityFixtures = [
        { name: 'smoke', source: 'smoke.impala', expected: 'smoke.expected.gazl', options: { randomId: 42, sourceName: 'smoke.impala' } },
        { name: 'bool', source: 'bool.impala', expected: 'bool.expected.gazl', options: { randomId: 42, sourceName: 'bool.impala' } },
        { name: 'control', source: 'control.impala', expected: 'control.expected.gazl', options: { randomId: 42, sourceName: 'control.impala' } },
        { name: 'perfTest2', source: 'perfTest2.impala', expected: 'perfTest2.expected.gazl', options: { randomId: 42, sourceName: 'perfTest2.impala' } },
        { name: 'inputTest', source: 'inputTest.impala', expected: 'inputTest.expected.gazl', options: { randomId: 42, sourceName: 'inputTest.impala' } }
];

const legacySourceDir = path.join(dir, '..', '..', 'tests', 'impala', 'sources');
const legacyExpectedDir = path.join(dir, '..', '..', 'tests', 'impala', 'golden');
const LEGACY_RANDOM_ID = 0x4d2;
const legacyParityFixtures = fs
        .readdirSync(legacySourceDir)
        .filter((file) => file.endsWith('.impala'))
        .sort()
        .map((file) => {
                const name = path.basename(file, '.impala');
                return {
                        name,
                        source: file,
                        expected: `${name}.gazl`,
                        sourceDir: legacySourceDir,
                        expectedDir: legacyExpectedDir,
                        options: { randomId: LEGACY_RANDOM_ID, retabulate: false, sourceName: path.join(legacySourceDir, file) }
                };
        });

function resolveFixturePath(fixture, key, defaultDir) {
        if (fixture[`${key}Dir`]) {
                return path.join(fixture[`${key}Dir`], fixture[key]);
        }
        return path.join(defaultDir, fixture[key]);
}

function runParityFixture(fixture) {
	const sourcePath = resolveFixturePath(fixture, 'source', path.join(dir, 'testdata'));
	const expectedPath = resolveFixturePath(fixture, 'expected', path.join(dir, 'testdata'));
	const source = fs.readFileSync(sourcePath, IMPALA_ENCODING);
	const expected = fs.readFileSync(expectedPath, IMPALA_ENCODING);
        let actual;
        try {
                actual = compileWithJsImpala(source, Object.assign({}, fixture.options));
        } catch (err) {
                const message = (err && err.message) ? err.message : String(err);
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

        const normalizedActual = actual.trimEnd();
        const normalizedExpected = expected.trimEnd();

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
        const result = childProcess.spawnSync(process.execPath, [validatorScript].concat(files), {
                encoding: 'utf8'
        });

        if (result.error) {
		console.error(`Failed to launch gazl-validate for ${label}`);
		console.error(result.error);
                process.exit(1);
        }

        if (result.status !== expectedExitCode) {
		console.error(`gazl-validate exited with ${result.status} for ${label}, expected ${expectedExitCode}`);
                if (result.stdout) {
		console.error('stdout:');
		console.error(result.stdout);
                }
                if (result.stderr) {
		console.error('stderr:');
		console.error(result.stderr);
                }
                process.exit(1);
        }

        const stderr = result.stderr || '';
        if (expectedMessageSubstring) {
                if (!stderr.includes(expectedMessageSubstring)) {
		console.error(`gazl-validate output for ${label} did not include expected message: ${expectedMessageSubstring}`);
		console.error('stderr:');
		console.error(stderr);
                        process.exit(1);
                }
        } else if (stderr.trim().length !== 0) {
		console.error(`gazl-validate produced unexpected diagnostics for ${label}`);
		console.error('stderr:');
		console.error(stderr);
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

runValidatorCase('matching metadata fixtures', ['exports.gazl', 'imports-valid.gazl'], 0);
runValidatorCase(
'mismatched metadata fixtures',
['exports.gazl', 'imports-mismatch.gazl'],
1,
'Call to foo does not match its definition'
);

	const validatorUnitTestScript = path.join(dir, '..', '..', 'tests', 'gazl-validator-tests.js');
	const validatorUnitResult = childProcess.spawnSync(process.execPath, [validatorUnitTestScript], {
		encoding: 'utf8'
});

	if (validatorUnitResult.error) {
		console.error('Failed to run gazl-validator unit tests');
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
		console.error('gazl-validator unit tests failed');
		process.exit(1);
}

const failureSource = [
        'function main()',
        'locals pointer p',
        '{',
        '        copy (1 from p to 1);',
        '}',
        ''
].join('\n');

let observedFailure = false;
try {
        compileWithJsImpala(failureSource, { randomId: 42 });
} catch (err) {
        observedFailure = true;
}

if (!observedFailure) {
        console.error('impala.jspeg compiler unexpectedly succeeded on failureSource');
        process.exit(1);
}

const smokeSource = fs.readFileSync(path.join(dir, 'testdata', 'smoke.impala'), IMPALA_ENCODING);
const smokeExpected = fs.readFileSync(path.join(dir, 'testdata', 'smoke.expected.gazl'), IMPALA_ENCODING);
const smokeOutputAfterFailure = compileWithJsImpala(smokeSource, { randomId: 42 });

if (smokeOutputAfterFailure !== smokeExpected) {
        console.error('impala.jspeg compiler leaked state after aborted compile');
        process.exit(1);
}
console.log('impala.jspeg compiler recovers after aborted compile without leaking state');

const mismatchedReturnSource = [
        'extern function foreignFoo;',
        'function main()',
        'locals int value',
        '{',
        '        value = foreignFoo();',
        '}',
        '',
        'function foreignFoo()',
        'returns float result',
        '{',
        '        result = 0.0;',
        '}',
        ''
].join('\n');

let observedMismatch = false;
try {
        compileWithJsImpala(mismatchedReturnSource, { randomId: 42 });
} catch (err) {
        observedMismatch = (err && err.message && err.message.includes('Return type for foreignFoo'));
}

if (!observedMismatch) {
        console.error('impala.jspeg compiler failed to report mismatched inferred return type');
        process.exit(1);
}
console.log('impala.jspeg compiler enforces inferred return type expectations');

console.log('JSPEG regression suite completed successfully');
