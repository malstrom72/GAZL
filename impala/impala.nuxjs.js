/* Command-line Impala compiler for the NuXJS REPL.

   Usage:
     NuXJS impala/impala.nuxjs.js [--legacy] [--dead-strip] [--range-checks] source.impala [output.gazl|-] [randomId] [sourceName] [compiler.js]

   NuXJS exposes global `arguments` as [script.js, arguments...]. With no output
   path, or output path `-`, this script emits compiled GAZL to stdout.
   `--legacy` downgrades Impala 2 strict-expression errors to warnings (printed
   as `;`-prefixed comment lines so stdout remains a valid GAZL stream).

   The source's import closure is resolved and compiled as one program, by the same
   impalaImportClosure.js the Node front end uses. A source that imports nothing is a
   closure of one and compiles to exactly what it always did.
*/

var impalaNuxRawArgs = arguments;
/* ONE table, matching the FLAGS map in impala.node.js so the two front ends take the same set - they
   compile the same corpus and are byte-compared against each other, so a flag only one of them accepts
   is a hole in that comparison. Parsed below `fail`, which it needs.
   Every key starts with `--`, so none can collide with an Object.prototype member. */
var impalaNuxFlags = { "--legacy": false, "--dead-strip": false, "--range-checks": false };
var impalaNuxArgs = [];

function usage() {
	print("Usage: NuXJS impala/impala.nuxjs.js [--legacy] [--dead-strip] [--range-checks] source.impala [output.gazl|-] [randomId] [sourceName] [compiler.js]");
}

function fail(message) {
	usage();
	throw new Error(message);
}

/* An unrecognised `--flag` is REJECTED, not appended to the positional list. It used to fall through to
   the `else` below and land in `impalaNuxArgs`, where index 0 is the SCRIPT path and 1 the source - so a
   misspelling did not merely get ignored, it shifted every positional by one and the compile failed
   somewhere unrelated, or worse succeeded on the wrong file. impala.node.js hardened its own argv loop
   against exactly this ("`--range-cheks` used to be silently dropped and compile anyway"). */
for (var impalaNuxArgIndex = 0; impalaNuxArgIndex < impalaNuxRawArgs.length; ++impalaNuxArgIndex) {
	var impalaNuxArg = "" + impalaNuxRawArgs[impalaNuxArgIndex];
	if (impalaNuxArg.substr(0, 2) === "--") {
		if (impalaNuxFlags[impalaNuxArg] === undefined) {
			fail("Unknown option: " + impalaNuxArg);
		}
		impalaNuxFlags[impalaNuxArg] = true;
	} else {
		impalaNuxArgs[impalaNuxArgs.length] = impalaNuxArg;
	}
}
var impalaNuxLegacy = impalaNuxFlags["--legacy"];
var impalaNuxDeadStrip = impalaNuxFlags["--dead-strip"];

function loadCompilerPath(path) {
	var previous = typeof impalaCompiler === "function" ? impalaCompiler : undefined;
	load(path);
	if (typeof impalaCompiler !== "function" || impalaCompiler === previous) {
		throw new Error("Loaded compiler did not define impalaCompiler: " + path);
	}
	return path;
}

function dirname(path) {
	var slash = path.lastIndexOf("/");
	var backslash = path.lastIndexOf("\\");
	var index = Math.max(slash, backslash);
	return index >= 0 ? path.substr(0, index + 1) : "";
}

function repeatSpaces(count) {
	var text = "";
	while (count-- > 0) {
		text += " ";
	}
	return text;
}

function emitCompiledOutput(lines, outputPath) {
	var retabulated = [];
	var i;

	for (i = 0; i < lines.length; ++i) {
		retabulated[i] = retabulate(lines[i]);
	}
	// Strip after retabulation, exactly as the Node front end does, so both produce the same bytes.
	if (impalaNuxDeadStrip) {
		retabulated = deadStrip(retabulated.join("\n")).split("\n");
	}

	if (outputPath && outputPath !== "-") {
		write(outputPath, retabulated.length > 0 ? retabulated.join("\n") + "\n" : "");
		return;
	}
	for (i = 0; i < retabulated.length; ++i) {
		print(retabulated[i]);
	}
}

function retabulate(line) {
	var OUTPUT_TAB_WIDTH = 4;
	var INPUT_TAB_STOPS = [0, 20, 32, 64];
	var result = "";
	var column = 0;
	var tabIndex = 0;
	var segments = line.split("\t");
	var segment;
	var stop;
	var target;
	var remainder;
	var next;
	var i;

	if (line.length === 0) {
		return "";
	}

	for (i = 0; i < segments.length; ++i) {
		stop = tabIndex < INPUT_TAB_STOPS.length ? INPUT_TAB_STOPS[tabIndex] : -1000000000;
		target = Math.max(stop, column + 1);
		while (column < target) {
			remainder = column % OUTPUT_TAB_WIDTH;
			next = column + (remainder === 0 ? OUTPUT_TAB_WIDTH : OUTPUT_TAB_WIDTH - remainder);
			if (next > target) {
				break;
			}
			result += "\t";
			column = next;
		}
		if (column < target) {
			result += repeatSpaces(target - column);
			column = target;
		}
		segment = segments[i];
		result += segment;
		column += segment.length;
		++tabIndex;
	}

	return result;
}

if (!impalaNuxArgs || impalaNuxArgs.length < 2) {
	fail("Missing source file");
}

var impalaNuxScriptPath = "" + impalaNuxArgs[0];
var impalaNuxSourcePath = "" + impalaNuxArgs[1];
var impalaNuxOutputPath = impalaNuxArgs.length >= 3 ? "" + impalaNuxArgs[2] : undefined;
var impalaNuxHasRandomId = impalaNuxArgs.length >= 4;
var impalaNuxRandomId = impalaNuxHasRandomId ? +impalaNuxArgs[3] : undefined;
var impalaNuxSourceName = impalaNuxArgs.length >= 5 ? "" + impalaNuxArgs[4] : impalaNuxSourcePath;
var impalaNuxCompilerPath = impalaNuxArgs.length >= 6 ? "" + impalaNuxArgs[5] : dirname(impalaNuxScriptPath) + "impalaCompiler.js";

loadCompilerPath(impalaNuxCompilerPath);
load(dirname(impalaNuxScriptPath) + "impalaImportClosure.js");

var impalaNuxClosure = concatenateClosure(impalaNuxSourcePath, { read: function (path) { return read(path); } });
var impalaNuxSource = impalaNuxClosure.combined;
var impalaNuxSpans = impalaNuxClosure.spans;
var impalaNuxLines = [];
function impalaNuxLineColumn(source, offset) {
	var line = 1;
	var column = 1;
	var end = offset < source.length ? offset : source.length;
	for (var i = 0; i < end; ++i) {
		var ch = source[i];
		if (ch === "\n") {
			++line;
			column = 1;
		} else if (ch !== "\r") {
			++column;
		}
	}
	return line + ":" + column;
}

/* Past the first unit a raw offset names the root file on a line that only indexes the
   concatenation, so with a real closure the span decides the file and line instead. */
function impalaNuxDiagnostic(source, offset, severity, code, message) {
	var at = isFinite(offset) ? offset : 0;
	var where = impalaNuxSpans.length > 1 ? locateInUnit(impalaNuxSpans, source, at) : undefined;
	var position = where
			? where.name + ":" + where.line
			: impalaNuxSourceName + ":" + impalaNuxLineColumn(source, at);
	return position + ": " + severity + (code ? "[" + code + "]" : "") + ": " + message;
}

var impalaNuxCompilerOptions = {
	output: function (line) {
		impalaNuxLines[impalaNuxLines.length] = line;
	},
	sourceName: impalaNuxSourceName,
	units: impalaNuxSpans,
	warn: function (message, offset, code, hint) {
		print("; " + impalaNuxDiagnostic(impalaNuxSource, offset, "warning", code, message));
		if (hint) {
			print("; " + impalaNuxDiagnostic(impalaNuxSource, offset, "note", undefined, hint));
		}
	},
};
if (impalaNuxHasRandomId) {
	impalaNuxCompilerOptions.randomId = impalaNuxRandomId;
}
if (impalaNuxLegacy) {
	impalaNuxCompilerOptions.legacy = true;
}
if (impalaNuxFlags["--range-checks"]) {
	impalaNuxCompilerOptions.rangeChecks = true;
}
var impalaNuxResult;
try {
	impalaNuxResult = impalaCompiler(impalaNuxSource, impalaNuxCompilerOptions);
} catch (impalaNuxError) {
	if (impalaNuxError && isFinite(impalaNuxError.impalaOffset)) {
		print(impalaNuxDiagnostic(impalaNuxSource, impalaNuxError.impalaOffset, "error",
				impalaNuxError.impalaCode, impalaNuxError.impalaMessage || "compile error"));
		if (impalaNuxError.impalaHint) {
			print(impalaNuxDiagnostic(impalaNuxSource, impalaNuxError.impalaOffset, "note",
					undefined, impalaNuxError.impalaHint));
		}
		throw new Error("Impala compilation failed");
	}
	throw impalaNuxError;
}

if (!impalaNuxResult || !impalaNuxResult[0]) {
	print(impalaNuxDiagnostic(impalaNuxSource, impalaNuxResult ? impalaNuxResult[2] : 0,
			"error", "E001", "syntax error"));
	throw new Error("Impala compilation failed");
}

emitCompiledOutput(impalaNuxLines, impalaNuxOutputPath);
