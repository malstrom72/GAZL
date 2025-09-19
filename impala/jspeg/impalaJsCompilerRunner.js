'use strict';

const fs = require('fs');
const path = require('path');
const vm = require('vm');

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

function compileWithJsImpala(source, options = {}) {
        const compilerPath = options.compilerPath || path.join(__dirname, 'impalaCompiler.js');
        const compilerSource = options.compilerSource || fs.readFileSync(compilerPath, 'utf8');

        const outputLines = [];
        const context = {
                module: { exports: {} },
                exports: {},
                console,
                output: (line) => outputLines.push(line),
                impalaRandomId: options.randomId ?? 12345678,
                $$parser: {}
        };
        vm.createContext(context);

        new vm.Script(compilerSource, { filename: path.basename(compilerPath) }).runInContext(context);

        const compilerFn = context.module.exports;
        const [ok, , index] = compilerFn(source);
        if (!ok) {
                throw new Error('JSPEG impala compiler failed to compile source');
        }
        if (index !== source.length) {
                throw new Error(`JSPEG impala compiler stopped at ${index} of ${source.length}`);
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
