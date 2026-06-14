/* Command-line Impala compiler for the NuXJS REPL.

   Usage:
     NuXJS impala/impala.nuxjs.js source.impala [[output.gazl|-] [randomId] [sourceName] [compiler.js]]

   NuXJS exposes global `arguments` as [script.js, arguments...]. With no output
   path, or output path `-`, this script emits compiled GAZL to stdout.
*/

var impalaNuxArgs = arguments;

function usage() {
	print("Usage: NuXJS impala/impala.nuxjs.js source.impala [[output.gazl|-] [randomId] [sourceName] [compiler.js]]");
}

function fail(message) {
	usage();
	throw new Error(message);
}

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

function isRandomIdArg(arg) {
	var text = "" + arg;
	return /^0x[0-9a-fA-F]+$/.test(text) || /^[0-9]+$/.test(text);
}

function emitCompiledOutput(lines, outputPath) {
	var text = "";
	var i;
	var line;

	for (i = 0; i < lines.length; ++i) {
		line = retabulate(lines[i]);
		if (outputPath && outputPath !== "-") {
			text += line + "\n";
		} else {
			print(line);
		}
	}

	if (outputPath && outputPath !== "-") {
		write(outputPath, text);
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
var impalaNuxOutputPath;
var impalaNuxRandomId = 0x4d2;
var impalaNuxSourceName = impalaNuxSourcePath;
var impalaNuxCompilerPath;
var impalaNuxArgIndex = 2;

if (impalaNuxArgs.length > impalaNuxArgIndex && !isRandomIdArg(impalaNuxArgs[impalaNuxArgIndex])) {
	impalaNuxOutputPath = "" + impalaNuxArgs[impalaNuxArgIndex++];
}
if (impalaNuxArgs.length > impalaNuxArgIndex) {
	impalaNuxRandomId = +impalaNuxArgs[impalaNuxArgIndex++];
}
if (impalaNuxArgs.length > impalaNuxArgIndex) {
	impalaNuxSourceName = "" + impalaNuxArgs[impalaNuxArgIndex++];
}
impalaNuxCompilerPath =
	impalaNuxArgs.length > impalaNuxArgIndex ? "" + impalaNuxArgs[impalaNuxArgIndex] : dirname(impalaNuxScriptPath) + "impalaCompiler.js";

loadCompilerPath(impalaNuxCompilerPath);

var impalaNuxSource = read(impalaNuxSourcePath);
var impalaNuxLines = [];
var impalaNuxResult = impalaCompiler(impalaNuxSource, {
	output: function (line) {
		impalaNuxLines[impalaNuxLines.length] = line;
	},
	randomId: impalaNuxRandomId,
	sourceName: impalaNuxSourceName,
});

if (!impalaNuxResult || !impalaNuxResult[0]) {
	throw new Error("Impala compilation failed");
}

emitCompiledOutput(impalaNuxLines, impalaNuxOutputPath);
