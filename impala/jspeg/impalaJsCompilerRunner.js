'use strict';

const path = require('path');
const Module = require('module');

const OUTPUT_TAB_WIDTH = 4;
const INPUT_TAB_STOPS = [0, 20, 32, 64];

function retabulate(line) {
        if (line.length === 0) {
                return '';
        }

        let out = '';
        let tabIndex = 0;
        let outPosition = 0;
        const segments = line.split('\t');

        for (const segment of segments) {
                const stop = INPUT_TAB_STOPS[tabIndex];
                const reach = Math.max(stop !== undefined ? stop : -Infinity, outPosition + 1);

                let next;
                while ((next = outPosition + OUTPUT_TAB_WIDTH - (outPosition % OUTPUT_TAB_WIDTH)) <= reach) {
                        out += '\t';
                        outPosition = next;
                }

                const spaces = reach - outPosition;
                if (spaces > 0) {
                        out += ' '.repeat(spaces);
                        outPosition = reach;
                }

                out += segment;
                outPosition += segment.length;
                tabIndex += 1;
        }

        return out;
}

function loadCompiler(compilerPath, compilerSource) {
        if (compilerSource !== undefined) {
                const filename = path.resolve(compilerPath);
                const mod = new Module(filename, module);
                mod.filename = filename;
                mod.paths = Module._nodeModulePaths(path.dirname(filename));
                mod._compile(compilerSource, filename);
                return mod.exports;
        }

        const resolvedPath = require.resolve(compilerPath);
        delete require.cache[resolvedPath];
        return require(resolvedPath);
}

function compileWithJsImpala(source, options = {}) {
        const compilerPath = options.compilerPath || path.join(__dirname, 'impalaCompiler.js');
        const compilerSource = options.compilerSource;

        const outputLines = [];
        const compilerFn = loadCompiler(compilerPath, compilerSource);

        const previousOutput = globalThis.output;
        const previousRandomId = globalThis.impalaRandomId;
        const hadOutput = Object.prototype.hasOwnProperty.call(globalThis, 'output');
        const hadRandomId = Object.prototype.hasOwnProperty.call(globalThis, 'impalaRandomId');
        globalThis.output = (line) => outputLines.push(line);
        globalThis.impalaRandomId = options.randomId ?? 12345678;

        try {
                const [ok, , index] = compilerFn(source);
                if (!ok) {
                        throw new Error('JSPEG impala compiler failed to compile source');
                }
                if (index !== source.length) {
                        throw new Error(`JSPEG impala compiler stopped at ${index} of ${source.length}`);
                }
        } finally {
                if (hadOutput) {
                        globalThis.output = previousOutput;
                } else {
                        delete globalThis.output;
                }
                if (hadRandomId) {
                        globalThis.impalaRandomId = previousRandomId;
                } else {
                        delete globalThis.impalaRandomId;
                }
        }

        if (outputLines.length === 0) {
                return '';
        }

        const shouldRetabulate = options.retabulate !== false;
        const formatted = shouldRetabulate ? outputLines.map(retabulate) : outputLines;
        let outputText = formatted.join('\n');

        const trailingNewlineOption = options.trailingNewline;
        if (trailingNewlineOption === true || (trailingNewlineOption === undefined && shouldRetabulate)) {
                outputText += '\n';
        } else if (!shouldRetabulate && trailingNewlineOption !== true) {
                while (outputText.endsWith('\n')) {
                        outputText = outputText.slice(0, -1);
                }
        }

        return outputText;
}

module.exports = {
        compileWithJsImpala
};
