function splitLines(text) {
	return ("" + text).split(/\r\n|\n|\r/);
}

function trim(text) {
	return ("" + text).replace(/^[ \t]+|[ \t]+$/g, "");
}

function firstToken(text) {
	var i;
	for (i = 0; i < text.length; ++i) {
		if (text.charAt(i) === " " || text.charAt(i) === "\t") {
			return text.substring(0, i);
		}
	}
	return text;
}

function isInstructionName(text) {
	return /^[A-Z][A-Z][A-Z][A-Za-z]$/.test(text) && text !== "GAZL";
}

var docs = {};
var source = {};
var docsLines = splitLines(read("docs/InstructionSet.md"));
var sourceLines = splitLines(read("src/UnitTest.gazl"));
var i;
var line;
var name;
var missing = "";

for (i = 0; i < docsLines.length; ++i) {
	line = trim(docsLines[i]);
	if (line.length >= 3 && line.charAt(0) === "#" && line.charAt(1) === "#") {
		name = firstToken(trim(line.substring(2)));
		if (name !== "") {
			docs[name] = true;
		}
	}
}

for (i = 0; i < sourceLines.length; ++i) {
	line = trim(sourceLines[i]);
	if (line.charAt(0) === ";") {
		line = trim(line.substring(1));
		if (line.charAt(0) === "!") {
			line = trim(line.substring(1));
			name = firstToken(line);
			if (isInstructionName(name)) {
				source[name] = true;
			}
		}
	}
}

for (name in source) {
	if (source.hasOwnProperty(name) && !docs[name]) {
		missing += (missing !== "" ? ", " : "") + name;
	}
}

if (missing !== "") {
	print("Missing instructions in docs: " + missing);
	throw new Error("Documentation out of sync");
}

print("All instructions accounted for.");
