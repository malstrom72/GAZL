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

function clampIndex(value, min, max) {
if (!Number.isFinite(value)) {
return min;
}
if (value < min) {
return min;
}
if (value > max) {
return max;
}
return Math.floor(value);
}

function findLineBounds(source, index) {
const length = source.length;
let line = 1;
let position = 0;
let lineStart = 0;
while (position < index) {
const code = source.charCodeAt(position);
if (code === 0x0d) {
const next = position + 1;
position = (next < length && source.charCodeAt(next) === 0x0a) ? next + 1 : next;
line += 1;
lineStart = position;
continue;
}
if (code === 0x0a) {
position += 1;
line += 1;
lineStart = position;
continue;
}
position += 1;
}

let lineEnd = length;
for (let pos = index; pos < length; ++pos) {
const code = source.charCodeAt(pos);
if (code === 0x0a || code === 0x0d) {
lineEnd = pos;
break;
}
}

return { line, lineStart, lineEnd };
}

function renderErrorContext(source, lineStart, lineEnd, pointerIndex) {
let displayLine = '';
let pointerLine = '';
let runningColumn = 1;
let pointerColumn = 1;

for (let pos = lineStart; pos < lineEnd; ++pos) {
const ch = source[pos];
if (ch === '\t') {
const spaces = OUTPUT_TAB_WIDTH - ((runningColumn - 1) % OUTPUT_TAB_WIDTH);
displayLine += ' '.repeat(spaces);
runningColumn += spaces;
if (pos < pointerIndex) {
pointerLine += ' '.repeat(spaces);
pointerColumn += spaces;
}
continue;
}
const code = ch.charCodeAt(0);
const safeChar = (code >= 0x20 && code !== 0x7f) ? ch : ' ';
displayLine += safeChar;
runningColumn += 1;
if (pos < pointerIndex) {
pointerLine += ' ';
pointerColumn += 1;
}
}

if (pointerIndex >= lineEnd) {
pointerColumn = runningColumn;
}

pointerLine += '^';

return {
displayLine,
pointerLine,
column: pointerColumn
};
}

function formatParseError(source, options, rawIndex) {
const index = clampIndex(rawIndex, 0, source.length);
const { line, lineStart, lineEnd } = findLineBounds(source, index);
const context = renderErrorContext(source, lineStart, lineEnd, index);
const locationLabel = options && options.sourceName ? ` ${options.sourceName}` : ' source';
const locationDetails = `line ${line}, column ${context.column}, offset ${index}`;
let message = `JSPEG impala compiler failed to compile${locationLabel} at ${locationDetails}.`;
if (context.displayLine.length > 0 || lineEnd > lineStart) {
message += `\n${context.displayLine}\n${context.pointerLine}`;
} else {
message += '\n^';
}
return message;
}

function patchCompilerSourceForMeta(source) {
const metaSlotReplacement = `    function metaSlot(node) {\n        if (node == null || (typeof node !== 'object' && typeof node !== 'function')) {\n            return { operator: undefined, type: undefined,\n                     operands: [ undefined, undefined, undefined ] };\n        }\n        if (node.operands !== undefined) {\n            if (!Array.isArray(node.operands)) {\n                node.operands = [ undefined, undefined, undefined ];\n            } else {\n                while (node.operands.length < 3) {\n                    node.operands.push(undefined);\n                }\n            }\n            if (!Object.prototype.hasOwnProperty.call(node, 'operator')) {\n                node.operator = undefined;\n            }\n            if (!Object.prototype.hasOwnProperty.call(node, 'type')) {\n                node.type = undefined;\n            }\n            return node;\n        }\n\n        if (!Object.prototype.hasOwnProperty.call(node, '_')) {\n            if (node.operands === undefined) {\n                node.operands = [ undefined, undefined, undefined ];\n            }\n            if (!Object.prototype.hasOwnProperty.call(node, 'operator')) {\n                node.operator = undefined;\n            }\n            if (!Object.prototype.hasOwnProperty.call(node, 'type')) {\n                node.type = undefined;\n            }\n            return node;\n        }\n\n        var slot = node._;\n        if (!slot || slot.operands === undefined) {\n            slot = { operator: undefined, type: undefined,\n                     operands: [ undefined, undefined, undefined ] };\n            node._ = slot;\n        }\n        return slot;\n    }\n`;
const metaSectionHeader = '    /* --------------------------------------------------------- *\n     *  Debug helpers & meta-record construction / destruction   *\n     * --------------------------------------------------------- */\n\n';
const createContextHelper = `${metaSectionHeader}    function __jspegCreateContext() {\n        var holder = {};\n        Object.defineProperty(holder, '__metaSlot', {\n            value: { operator: undefined, type: undefined,\n                     operands: [ undefined, undefined, undefined ] },\n            writable: true,\n            configurable: true\n        });\n        Object.defineProperty(holder, '_', {\n            configurable: true,\n            get: function () {\n                if (!Object.prototype.hasOwnProperty.call(this, '__metaSlot')) {\n                    Object.defineProperty(this, '__metaSlot', {\n                        value: { operator: undefined, type: undefined,\n                                 operands: [ undefined, undefined, undefined ] },\n                        writable: true,\n                        configurable: true\n                    });\n                }\n                return this.__metaSlot;\n            },\n            set: function (value) {\n                Object.defineProperty(this, '__metaSlot', {\n                    value: value,\n                    writable: true,\n                    configurable: true\n                });\n            }\n        });\n        return holder;\n    }\n\n`;
let patched = source.replace(metaSectionHeader, createContextHelper);
const metaSlotRegex = /    function metaSlot\(node\) \{[\s\S]*?^    }\n/m;
patched = patched.replace(metaSlotRegex, metaSlotReplacement);
patched = patched.replace(/\$[A-Za-z0-9_]*={}/g, (match) => match.replace('={}', '=__jspegCreateContext()'));

const marker = '    makeMeta = function (rec, op, type, op0, op1, op2) {';
const guard = `\n        if (rec == null) {\n                rec = { operator: undefined, type: undefined, operands: [undefined, undefined, undefined] };\n        }`;
if (patched.includes(marker)) {
patched = patched.replace(marker, `${marker}${guard}`);
}

const assignRegex = /    assign = function \(x, leftx, rightx,\n[ \t]+sourceCode, sourceOffset\) \{/;
const assignGuard = `\n        if (!leftx || leftx.operator === undefined) {\n                throw new Error('JSPEG meta missing for assignment: ' + JSON.stringify(leftx));\n        }`;
patched = patched.replace(assignRegex, (match) => `${match}${assignGuard}`);

const rootInitPattern = 'var _i=0,_im=0,_o={_:void 0},';
const rootMeta = 'var _i=0,_im=0,_o=__jspegCreateContext(),';
patched = patched.replace(rootInitPattern, rootMeta);

return patched;
}

function resolveCompilerExport(candidate) {
if (typeof candidate === 'function') {
return candidate;
}
if (candidate && typeof candidate === 'object') {
return (
candidate.impalaCompiler ||
candidate.default ||
candidate.compile ||
candidate.compiler ||
candidate
);
}
return undefined;
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

new vm.Script(patchedCompilerSource, { filename: path.basename(compilerPath) }).runInContext(context);

const compilerFn = resolveCompilerExport(context.module.exports);
if (typeof compilerFn !== 'function') {
throw new Error('JSPEG impala compiler did not export a function');
}

const compilerOptions = (options && Object.prototype.hasOwnProperty.call(options, 'sourceName'))
? { sourceName: options.sourceName }
: undefined;
const [ok, , index] = compilerFn(source, compilerOptions);
if (!ok) {
throw new Error(formatParseError(source, options, index));
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
