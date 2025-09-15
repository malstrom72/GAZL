const fs = require('fs');

const jspeg = fs.readFileSync('jspegCompiler.js', 'utf8');
eval(jspeg);

const grammar = fs.readFileSync('impala.jspeg', 'utf8');
const [, generated] = compileJSPEG(grammar);
const existing = fs.readFileSync('impalaCompiler.js', 'utf8');

if (('impalaCompiler=' + generated).trim() !== existing.trim()) {
	console.error('Generated compiler differs from impalaCompiler.js');
	process.exit(1);
}

console.log('impalaCompiler.js matches generated output');
