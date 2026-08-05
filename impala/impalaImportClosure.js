/* Import-closure resolution and `--dead-strip`, shared by both command-line front ends.

   `impala.node.js` requires this file; `impala.nuxjs.js` load()s it, so it is written in the ES5
   subset NuXJS supports - var only, no arrow functions, no Set/Map/for-of, no template literals -
   and touches no host API directly. The host passes its own primitives in an `io` object:

     io.read(path)        -> file contents, throws if unreadable
     io.canonical(path)   -> optional; a key that identifies one file however it is spelled

   Node supplies realpathSync as `canonical`, which also folds symlinks and Windows case; NuXJS has
   no such call, so it omits it and the textual normalization below is the whole identity rule. That
   is the only behavioural difference between the two runtimes, and it only shows up when the same
   file is reached through a symlink. */

function scanImports(source) {
	var re = /^[ \t]*import[ \t]+"([^"\r\n]*)"/gm;
	var paths = [];
	var m;
	while ((m = re.exec(source)) !== null) {
		paths[paths.length] = m[1];
	}
	return paths;
}

function dirName(filePath) {
	var index = Math.max(filePath.lastIndexOf("/"), filePath.lastIndexOf("\\"));
	return index >= 0 ? filePath.substr(0, index) : "";
}

/* Fold `\` to `/` and resolve `.` / `..` textually. A leading `/`, or a `X:` drive prefix, is kept
   so an absolute path stays absolute; everything else stays relative to wherever the process runs,
   which is what makes a bare `import "lib.impala"` on stdin resolve against the current directory. */
function normalizePath(filePath) {
	var text = filePath.split("\\").join("/");
	var prefix = "";
	if (/^[A-Za-z]:\//.test(text)) {
		prefix = text.substr(0, 3);
		text = text.substr(3);
	} else if (text.charAt(0) === "/") {
		prefix = "/";
		text = text.substr(1);
	}
	var parts = text.split("/");
	var out = [];
	for (var i = 0; i < parts.length; ++i) {
		var part = parts[i];
		if (part === "" || part === ".") {
			continue;
		}
		if (part === ".." && out.length > 0 && out[out.length - 1] !== "..") {
			out.length = out.length - 1;
		} else if (part !== ".." || prefix === "") {
			out[out.length] = part;
		}
	}
	return prefix + out.join("/");
}

function joinPath(dir, rel) {
	return normalizePath(dir === "" ? rel : dir + "/" + rel);
}

/* Walk the closure of `rootPath`, post-order so a dependency always precedes its dependent. The
   visited set keyed on the canonical path dedups diamonds and makes cycles harmless (each file is
   read and emitted exactly once); resolution across a cycle is a separate matter - see
   docs/impala/Impala2.md "Cycles". */
function resolveImportClosure(rootPath, io) {
	var visitedKeys = [];
	var order = [];

	function key(filePath) {
		if (io.canonical) {
			var resolved = io.canonical(filePath);
			if (resolved) {
				return normalizePath(resolved);
			}
		}
		return normalizePath(filePath);
	}

	function seen(k) {
		for (var i = 0; i < visitedKeys.length; ++i) {
			if (visitedKeys[i] === k) {
				return true;
			}
		}
		return false;
	}

	function visit(filePath, importedFrom) {
		var k = key(filePath);
		if (seen(k)) {
			return;
		}
		visitedKeys[visitedKeys.length] = k;

		var source;
		try {
			source = io.read(filePath);
		} catch (err) {
			var via = importedFrom ? " (imported from " + importedFrom + ")" : "";
			throw new Error("Error reading " + filePath + via + ": "
					+ (err && err.message ? err.message : String(err)));
		}
		if (source === undefined || source === null) {
			throw new Error("Error reading " + filePath + (importedFrom ? " (imported from " + importedFrom + ")" : ""));
		}

		var dir = dirName(filePath);
		var imports = scanImports(source);
		for (var i = 0; i < imports.length; ++i) {
			visit(joinPath(dir, imports[i]), filePath);
		}
		order[order.length] = { path: filePath, source: source };
	}

	visit(normalizePath(rootPath), undefined);
	return order;
}

/* Concatenate the closure into one compilation-ready source. `spans` records where each unit's text
   landed so a diagnostic can name the file it came from rather than a line number that only means
   something in the concatenation. A lone unit gets NO banner, so compiling a file that imports
   nothing is byte-for-byte what compiling that file alone always produced. */
function concatenateClosure(rootPath, io) {
	var units = resolveImportClosure(rootPath, io);
	var rootDir = dirName(normalizePath(rootPath));
	var spans = [];
	var combined = "";
	for (var i = 0; i < units.length; ++i) {
		var name = relativeTo(rootDir, units[i].path);
		if (units.length > 1) {
			combined += (combined.length > 0 ? "\n" : "") + "// ==== unit: " + name + " ====\n";
		}
		spans[spans.length] = { name: name, start: combined.length, end: combined.length + units[i].source.length };
		combined += units[i].source;
	}
	return { units: units, combined: combined, spans: spans };
}

function relativeTo(dir, filePath) {
	var prefix = dir === "" ? "" : dir + "/";
	return filePath.indexOf(prefix) === 0 ? filePath.substr(prefix.length) : filePath;
}

/* Map an offset in the concatenated source back to the unit that owns it. One combined source means
   a raw line number indexes the concatenation and names the root unit - the wrong file, on the wrong
   line, for everything past the first. Both front ends resolve diagnostics through this. */
var LINE_BREAK_PATTERN = /\r\n|\r|\n/;

function locateInUnit(spans, source, index) {
	for (var i = 0; spans && i < spans.length; ++i) {
		if (index >= spans[i].start && index <= spans[i].end) {
			return { name: spans[i].name, line: source.slice(spans[i].start, index).split(LINE_BREAK_PATTERN).length };
		}
	}
	return undefined;
}

/* --- --dead-strip ------------------------------------------------------------
   Link-time dead-code elimination on the finished .gazl. Roots are `export`ed symbols (read from
   the `; signature ... export ...` rows); any FUNC or labeled data/DEF block that is neither
   exported nor reachable from an export is dropped. Conservative: anything it cannot classify with
   confidence is kept, and it only runs when explicitly requested. */
var TOP_LABEL_RE = /^\s*([A-Za-z_]\w*):\s+(\S.*)$/;   // a named top-level definition line
var ANON_ALLOC_RE = /^\s*(?:GLOB|CNST|TEMP)\s+\*/;    // an unlabeled section allocation
var DATA_ROW_RE = /^\s*(?:DAT[ifps]?|DATA)\s/;        // an unlabeled initializer continuation row
// A compile-time line that computes or names a data definition's extent: the `.x.name: ! DEFi` itself,
// or an unlabeled `!` fold feeding it. Anything LABELED with an ordinary identifier is a definition in
// its own right (isDataDef sees it) and ends the run, which is what keeps a neighbouring const or
// struct-layout block from being swallowed. That a bare `!` line is never something else immediately
// above a data definition is a property of the emitter, not of this pattern - the assert guard
// `! EQUi #DEBUG #0 @.noAssertStrings` is always followed by a DOT-labeled block, never an ASCII one.
var EXTENT_LINE_RE = /^\s*(?:\.x\.[\w.]+:\s+)?!\s*\w+/;

function stripComment(line) {
	var at = line.indexOf(";");
	return at >= 0 ? line.slice(0, at) : line;
}

function isFuncDef(line) {
	var m = TOP_LABEL_RE.exec(line);
	return m && /^FUNC\b/.test(m[2]) ? m[1] : null;
}

function isDataDef(line) {
	var m = TOP_LABEL_RE.exec(line);
	if (!m) {
		return null;
	}
	return /^(?:DAT[ifps]?|DATA|GLOB|CNST|TEMP|!\s*DEF[ifp]?)\b/.test(m[2]) ? m[1] : null;
}

/* A function body ends only at the next named definition or a standalone `; signature` row (extern
   decls). Internal `;----` separators, `; expects` comments and blank lines stay in the body - the
   code section of every function is preceded by its own `;----` separator. */
function isBoundary(line) {
	return !!(isFuncDef(line) || isDataDef(line) || /^\s*;\s*signature\b/.test(line));
}

function collectRefs(text, into) {
	// &func/&global, ^native, #const, *size - names only (numbers skip). `*` is load-bearing: a const
	// used ONLY as an array extent (`GLOB *BUF_SIZE`) has no other reference shape, and dropping its
	// `! DEFi` row is how `global int array buf[N]` - the canonical firmware idiom - stopped assembling.
	var re = /[&^#*]([A-Za-z_]\w*)/g;
	var m;
	while ((m = re.exec(text)) !== null) {
		into[into.length] = m[1];
	}
}

function deadStrip(gazl) {
	var lines = gazl.split("\n");
	var blocks = [];   // { kind:'func'|'data'|'loose', name, start, end, refs:[] }
	var i = 0;
	var k;
	while (i < lines.length) {
		var fname = isFuncDef(lines[i]);
		if (fname) {
			var start = i++;
			while (i < lines.length && !isBoundary(lines[i])) {
				i++;                                          // body: indented decls/instructions/blanks
			}
			blocks[blocks.length] = { kind: "func", name: fname, start: start, end: i };
			continue;
		}
		var dname = isDataDef(lines[i]);
		if (dname) {
			// pull a preceding anonymous allocation line into this block so it strips together
			var dataStart = i;
			var prev = blocks.length > 0 ? blocks[blocks.length - 1] : undefined;
			if (prev && prev.kind === "loose" && ANON_ALLOC_RE.test(lines[prev.start]) && prev.end === i) {
				blocks.length = blocks.length - 1;
				dataStart = prev.start;
			}
			// ...and so does the run of compile-time lines that COMPUTES this block's extent: the
			// `.x.name: ! DEFi` naming it, and the unlabeled `! MULi`/`! ADDi` folding that feeds it.
			// They belong to the definition twice over. Left loose they are kept unconditionally, so
			// a dropped array leaves its extent behind - and, worse, the only reference to a const
			// used purely as an extent (`buf[BUF_SIZE]`, `grid[H * W]`) lives on those lines, so the
			// walk would never mark BUF_SIZE/H/W reachable and would strip the `! DEFi` out from
			// under them. A labeled definition ends the run, so an adjacent const or struct layout
			// is never swallowed.
			while (blocks.length > 0) {
				prev = blocks[blocks.length - 1];
				if (prev.kind !== "loose" || prev.end !== dataStart || !EXTENT_LINE_RE.test(lines[prev.start])) {
					break;
				}
				blocks.length = blocks.length - 1;
				dataStart = prev.start;
			}
			// ...and trailing unlabeled `DATA` rows are this block's initializer, not free-standing
			// lines. Left loose they were kept unconditionally, so stripping the header handed the
			// values to whichever block came before - silently, whenever they fit its zero-fill slack.
			i++;
			while (i < lines.length && DATA_ROW_RE.test(lines[i])) {
				i++;
			}
			blocks[blocks.length] = { kind: "data", name: dname, start: dataStart, end: i };
			continue;
		}
		if (DATA_ROW_RE.test(lines[i])) {
			throw new Error("dead-strip: initializer row belongs to no data block: " + lines[i].trim());
		}
		blocks[blocks.length] = { kind: "loose", start: i, end: i + 1 };
		i++;
	}

	var byName = {};
	var work = [];
	for (i = 0; i < blocks.length; ++i) {
		if (blocks[i].kind === "loose") {
			/* A loose line is KEPT unconditionally, so whatever it names has to survive too - otherwise
			   the strip leaves a reference with nothing behind it. That is the general form of the bug
			   the `.x.` absorption above fixes specifically: `! DEFi #BUF_SIZE` outliving BUF_SIZE. It
			   also covers every other compiler-minted dotted line, which `TOP_LABEL_RE` cannot match
			   and which therefore all land here. Roots only ever ADD reachability, never remove it. */
			collectRefs(stripComment(lines[blocks[i].start]), work);
			continue;
		}
		byName[blocks[i].name] = blocks[i];
		blocks[i].refs = [];
		for (k = blocks[i].start; k < blocks[i].end; ++k) {
			collectRefs(stripComment(lines[k]), blocks[i].refs);
		}
		if (/;\s*signature\s+export\b/.test(lines[blocks[i].start])) {
			work[work.length] = blocks[i].name;
		}
	}

	var reachable = {};
	while (work.length > 0) {
		var name = work[work.length - 1];
		work.length = work.length - 1;
		if (reachable[name] === true || !byName[name]) {
			continue;
		}
		reachable[name] = true;
		var refs = byName[name].refs;
		for (i = 0; i < refs.length; ++i) {
			if (byName[refs[i]]) {
				work[work.length] = refs[i];
			}
		}
	}

	var kept = [];
	for (i = 0; i < blocks.length; ++i) {
		if (blocks[i].kind === "loose" || reachable[blocks[i].name] === true) {
			for (k = blocks[i].start; k < blocks[i].end; ++k) {
				kept[kept.length] = lines[k];
			}
		}
	}
	return kept.join("\n");
}

if (typeof module !== "undefined" && module.exports) {
	module.exports = { scanImports: scanImports, normalizePath: normalizePath, joinPath: joinPath,
			dirName: dirName, resolveImportClosure: resolveImportClosure,
			concatenateClosure: concatenateClosure, locateInUnit: locateInUnit, deadStrip: deadStrip };
}
