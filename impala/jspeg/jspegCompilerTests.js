const fs = require('fs');
const path = require('path');

const dir = __dirname;
const jspeg = fs.readFileSync(path.join(dir, 'jspegCompiler.js'), 'utf8');
eval(jspeg);

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
