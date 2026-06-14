function splitLines(text) {
	return ("" + text).split(/\r\n|\n|\r/);
}

function isSpace(ch) {
	return ch === " " || ch === "\t";
}

function trimRightSpaces(text) {
	var end = text.length;
	while (end > 0 && text.charAt(end - 1) === " ") {
		--end;
	}
	return text.substring(0, end);
}

function escapeCppString(text) {
	var out = '"';
	var i;
	var ch;
	var code;
	for (i = 0; i < text.length; ++i) {
		ch = text.charAt(i);
		code = text.charCodeAt(i);
		if (ch === "\\") {
			out += "\\\\";
		} else if (ch === '"') {
			out += '\\"';
		} else if (ch === "\n") {
			out += "\\n";
		} else if (ch === "\r") {
			out += "\\r";
		} else if (ch === "\t") {
			out += "\\t";
		} else if (code < 32 || code > 126) {
			out += "\\x" + (code < 16 ? "0" : "") + code.toString(16);
		} else {
			out += ch;
		}
	}
	return out + '"';
}

function compactGAZL(source) {
	var lines = splitLines(source);
	var output = "";
	var lineIndex;
	var s;
	var i;
	var l;
	var ch;
	var tokenStart;

	for (lineIndex = 0; lineIndex < lines.length; ++lineIndex) {
		s = lines[lineIndex];
		i = 0;
		l = "";
		while (i < s.length && s.charAt(i) !== ";") {
			ch = s.charAt(i);
			if (isSpace(ch)) {
				while (i < s.length && isSpace(s.charAt(i))) {
					++i;
				}
				if (l !== "") {
					l += " ";
				}
			} else if (s.substring(i, i + 4) === "DATs") {
				l += s.substring(i);
				i = s.length;
			} else {
				tokenStart = i;
				while (i < s.length && !isSpace(s.charAt(i)) && s.charAt(i) !== ";") {
					++i;
				}
				l += s.substring(tokenStart, i);
			}
		}
		l = trimRightSpaces(l);
		if (l !== "") {
			output += l + "\n";
		}
	}

	return output;
}

function textToCpp(text) {
	var lines = splitLines(text);
	var result = "";
	var i;
	for (i = 0; i < lines.length; ++i) {
		if (i === lines.length - 1 && lines[i] === "") {
			continue;
		}
		if (result !== "") {
			result += "\n";
		}
		result += "\t" + escapeCppString(lines[i] + "\n");
	}
	return result;
}
