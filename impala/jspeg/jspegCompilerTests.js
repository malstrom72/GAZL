const fs = require('fs');
const path = require('path');

const dir = __dirname;
const jspeg = fs.readFileSync(path.join(dir, 'jspegCompiler.js'), 'utf8');
eval(jspeg);

const jspegGrammar = fs.readFileSync(path.join(dir, 'jspeg.jspeg'), 'utf8');
const [, compilerGenerated] = compileJSPEG(jspegGrammar);
if (('compileJSPEG=' + compilerGenerated).trim() !== jspeg.trim()) {
	console.error('jspegCompiler.js is out of date with jspeg.jspeg');
	process.exit(1);
}

console.log('jspegCompiler.js matches jspeg.jspeg output');

const grammar = fs.readFileSync(path.join(dir, 'impala.jspeg'), 'utf8');
const [, generated] = compileJSPEG(grammar);
const existing = fs.readFileSync(path.join(dir, 'impalaCompiler.js'), 'utf8');

if (('impalaCompiler=' + generated).trim() !== existing.trim()) {
	console.error('Generated compiler differs from impalaCompiler.js');
	process.exit(1);
}

console.log('impalaCompiler.js matches generated output');

// Compile and test arithmetic grammar
const testGrammar = fs.readFileSync(path.join(dir, 'jspegTest.jspeg'), 'utf8');
const [, testGenerated] = compileJSPEG(testGrammar);
eval('jspegTestParser=' + testGenerated);
const [ok, value, idx] = jspegTestParser('1+2*3');
if (!ok || value !== 7 || idx !== '1+2*3'.length) {
        console.error('jspegTest.jspeg failed to parse expression correctly');
        process.exit(1);
}

console.log('jspegTest.jspeg arithmetic parser works');

const tagCaptureGrammar = fs.readFileSync(path.join(dir, 'tagCaptureTest.jspeg'), 'utf8');
const [, tagCaptureGenerated] = compileJSPEG(tagCaptureGrammar);
eval('tagCaptureParser=' + tagCaptureGenerated);
const recordInput = 'foo=1,\nbar=23, qux=7';
const [mapOk, mapValue, mapIdx] = tagCaptureParser(recordInput);
if (!mapOk || mapIdx !== recordInput.length || mapValue.foo !== 1 || mapValue.bar !== 23 || mapValue.qux !== 7) {
	console.error('tagCaptureTest.jspeg failed to build key/value map correctly');
	process.exit(1);
}

const [badOk] = tagCaptureParser('foo=oops');
if (badOk) {
	console.error('tagCaptureTest.jspeg accepted invalid input');
	process.exit(1);
}

console.log('tagCaptureTest.jspeg tag and capture parser works');
