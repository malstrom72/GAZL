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

function createMetaSlotInitializerScript() {
        return new vm.Script(`
                Object.defineProperty(Object.prototype, '_', {
                        get: function () {
                                globalThis.__jspegMetaOwner = this;
                                if (!this.hasOwnProperty('__metaSlot')) {
                                        Object.defineProperty(this, '__metaSlot', {
                                                value: { operator: undefined, type: undefined, operands: [undefined, undefined, undefined] },
                                                writable: true,
                                                configurable: true
                                        });
                                }
                                return this.__metaSlot;
                        },
                        set: function (value) {
                                globalThis.__jspegMetaOwner = this;
                                Object.defineProperty(this, '__metaSlot', {
                                        value,
                                        writable: true,
                                        configurable: true
                                });
                        },
                        configurable: true
                });
        `);
}

function patchCompilerSourceForMeta(source) {
        const marker = '    makeMeta = function (rec, op, type, op0, op1, op2) {';
        const guard = `\n        if (rec == null) {\n                var owner = (typeof __jspegMetaOwner !== 'undefined' ? __jspegMetaOwner : null);\n                if (owner) {\n                        if (!owner.hasOwnProperty('__metaSlot')) {\n                                owner.__metaSlot = { operator: undefined, type: undefined, operands: [undefined, undefined, undefined] };\n                        }\n                        rec = owner.__metaSlot;\n                } else {\n                        rec = { operator: undefined, type: undefined, operands: [undefined, undefined, undefined] };\n                }\n        }`;
        if (!source.includes(marker)) {
                return source;
        }
        let patched = source.replace(marker, `${marker}${guard}`);
        const assignRegex = /    assign = function \(x, leftx, rightx,\n[ \t]+sourceCode, sourceOffset\) \{/;
        const assignGuard = `\n        if (!leftx || leftx.operator === undefined) {\n                throw new Error('JSPEG meta missing for assignment: ' + JSON.stringify(leftx));\n        }`;
        patched = patched.replace(assignRegex, (match) => `${match}${assignGuard}`);
        const rootInitPattern = 'var _i=0,_im=0,_o={_:void 0},';
        const rootMeta = 'var _i=0,_im=0,_o={_: { operator: undefined, type: undefined, operands: [undefined, undefined, undefined] }},';
        patched = patched.replace(rootInitPattern, rootMeta);
        return patched;
}

function compileWithJsImpala(source, options = {}) {
        const compilerPath = options.compilerPath || path.join(__dirname, 'impalaCompiler.js');
        const compilerSource = options.compilerSource || fs.readFileSync(compilerPath, 'utf8');
        const patchedCompilerSource = patchCompilerSourceForMeta(compilerSource);

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

        createMetaSlotInitializerScript().runInContext(context);
        new vm.Script(patchedCompilerSource, { filename: path.basename(compilerPath) }).runInContext(context);

        const compilerFn = context.module.exports;
        const compilerOptions = (options && Object.prototype.hasOwnProperty.call(options, 'sourceName'))
                ? { sourceName: options.sourceName }
                : undefined;
        const [ok, , index] = compilerFn(source, compilerOptions);
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
