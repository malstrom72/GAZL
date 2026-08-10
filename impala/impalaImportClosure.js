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
var TOP_LABEL_RE = /^\s*([A-Za-z_]\w*):\s+(\S.*)$/;   // an ORDINARY named definition (FUNC/const/global - never dotted)
// A definition may also carry a COMPILER-MINTED dotted label: `.s_<text>_<id>` (a string constant) and
// `.a_<text>_<id>` (an assert message) are `CNST *n` blocks with `DATs`/`DATi` rows, just like an ordinary
// data def. `TOP_LABEL_RE`'s `[A-Za-z_]` cannot match the leading dot, so these were invisible as blocks -
// their rows were absorbed into a neighbouring FUNC (stripped with it -> a dangling `&.s_...` that will not
// assemble) or orphaned into the "belongs to no data block" throw. Match the dot here; `isDataDef` then
// decides which dotted lines are real blocks (`.s_`/`.a_` storage) and which stay absorbable extent folds
// (`.z.`/`.o.` `! DEF`), so the layout-swallowing the tag-only match caused does NOT return.
var DATA_LABEL_RE = /^\s*(\.[\w.]+|[A-Za-z_]\w*):\s+(\S.*)$/;
var STORAGE_HEAD_RE = /^(?:DAT[ifps]?|DATA|GLOB|CNST|TEMP)\b/;   // a header that ALLOCATES storage and owns the DATA rows under it
var DEF_FOLD_RE = /^!\s*DEF[ifp]?\b/;                            // a `! DEF` compile-time fold: a const value, or a `.z.`/`.o.` extent
var ANON_ALLOC_RE = /^\s*(?:GLOB|CNST|TEMP)\s+\*/;    // an unlabeled section allocation
var DATA_ROW_RE = /^\s*(?:DAT[ifps]?|DATA)\s/;        // an unlabeled initializer continuation row
// A compile-time line that computes or names a data definition's extent: an unlabeled `!` fold, or the
// `.z.<name>: ! DEFi` naming THIS definition's size. The name must match, because `.z.` also labels a
// struct's own size (`.z.Voice`) and a struct layout block sits immediately above the arrays that use
// it - matching the tag alone would swallow the layout into the first array and strip `.z.Voice` out
// from under every other user. (Extents used to carry a distinct `.x.` tag, which made the tag alone
// sufficient; `.z.` absorbed it on 2026-08-02, so the name is now what separates them.) Note the tag
// alone does NOT reproduce that today, because the `; signature struct ...` row a layout emits sits
// between it and the next array's fold and stops the walk anyway - so this is defence against an
// emitter change, not a live bug, and no fixture can witness it while that row is there.
// Anything LABELED with an ordinary identifier is a definition in its own right (isDataDef sees it) and
// ends the run. That a bare `!` line is never something else immediately above a data definition is a
// property of the emitter, not of this pattern - the assert guard `! EQUi #DEBUG #0 @.noAssertStrings`
// is always followed by a DOT-labeled block, never an ASCII one.
var CT_FOLD_RE = /^\s*!\s*\w+/;
var EXTENT_LABEL_RE = /^\s*\.z\.([\w.]+):\s+!\s*\w+/;
// Any compile-time line, with or without a compiler-minted dot label, INCLUDING a bare `!` no-op
// carrying nothing but a label (`.g0:  !`). Only used inside a data block's own initializer run,
// where the DATA row that must follow is what keeps it from over-reaching.
var CT_LINE_RE = /^\s*(?:\.[\w.]+:)?\s*!/;
function isExtentLineFor(line, name) {
	var m = EXTENT_LABEL_RE.exec(line);
	return CT_FOLD_RE.test(line) || (m !== null && m[1] === name);
}

function stripComment(line) {
	var at = line.indexOf(";");
	return at >= 0 ? line.slice(0, at) : line;
}

function isFuncDef(line) {
	var m = TOP_LABEL_RE.exec(line);
	return m && /^FUNC\b/.test(m[2]) ? m[1] : null;
}

function isDataDef(line) {
	var m = DATA_LABEL_RE.exec(line);
	if (!m) {
		return null;
	}
	// A storage header owns the DATA rows beneath it, so it is always a block of its own - whether it wears
	// an ordinary label or a minted `.s_`/`.a_` one. A `! DEF` fold is a definition ONLY under an ordinary
	// label (a const like `FALSE: ! DEFi #0`); a DOTTED fold is a neighbour's `.z.`/`.o.` extent and must
	// stay absorbable, not stand alone - which is what kept the tag-only match from swallowing struct layouts.
	if (STORAGE_HEAD_RE.test(m[2])) {
		return m[1];
	}
	return DEF_FOLD_RE.test(m[2]) && m[1].charAt(0) !== "." ? m[1] : null;
}

/* A function body ends only at the next named definition or a standalone `; signature` row (extern
   decls). Internal `;----` separators, `; expects` comments and blank lines stay in the body - the
   code section of every function is preceded by its own `;----` separator. */
function isBoundary(line) {
	return !!(isFuncDef(line) || isDataDef(line) || /^\s*;\s*signature\b/.test(line));
}

function collectRefs(text, into) {
	// &func/&global, ^native, #const, *size, :offset - names only (numbers skip). `*` is load-bearing: a
	// const used ONLY as an array extent (`GLOB *BUF_SIZE`) has no other reference shape, and dropping
	// its `! DEFi` row is how `global int array buf[N]` - the canonical firmware idiom - stopped
	// assembling. `:` is load-bearing one step further in: an index that only the assembler can resolve
	// rides the BASE operand (`ADRL %1 $arr:KZ *0`, `MOVi $j $x:.o.S.f`), and that is the sole place the
	// name appears.
	// The leading `[.]` matters just as much. Every compiler-minted symbol starts with one - `.z.E`,
	// `.o.S.f`, an assert's `.a_...` message block - so `[A-Za-z_]` made a reference to ANY of them
	// invisible, and the definition strippable out from under it.
	// Over-matching is SAFE here and under-matching is not: an extra name keeps a block that could have
	// gone, a missing one deletes a definition something still needs.
	var re = /[&^#*:]([.A-Za-z_][\w.]*)/g;
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
			// `.z.name: ! DEFi` naming it, and the unlabeled `! MULi`/`! ADDi` folding that feeds it.
			// They belong to the definition twice over. Left loose they are kept unconditionally, so
			// a dropped array leaves its extent behind - and, worse, the only reference to a const
			// used purely as an extent (`buf[BUF_SIZE]`, `grid[H * W]`) lives on those lines, so the
			// walk would never mark BUF_SIZE/H/W reachable and would strip the `! DEFi` out from
			// under them. A labeled definition ends the run, so an adjacent const or struct layout
			// is never swallowed.
			while (blocks.length > 0) {
				prev = blocks[blocks.length - 1];
				if (prev.kind !== "loose" || prev.end !== dataStart
						|| !isExtentLineFor(lines[prev.start], dname)) {
					break;
				}
				blocks.length = blocks.length - 1;
				dataStart = prev.start;
			}
			// ...and trailing unlabeled `DATA` rows are this block's initializer, not free-standing
			// lines. Left loose they were kept unconditionally, so stripping the header handed the
			// values to whichever block came before - silently, whenever they fit its zero-fill slack.
			// A run of compile-time lines counts as part of the initializer when the rows CONTINUE
			// after it: that is the `! LEQi`/`! FAIL`/skip-label guard assertFitsExtent emits between
			// the header and the rows it guards, which otherwise cut the block in two and left the
			// rows orphaned (a hard throw below). Requiring a DATA row to follow is what stops this
			// from swallowing the NEXT definition's extent fold, which looks identical from here but
			// is never followed by rows belonging to THIS block.
			i++;
			while (i < lines.length) {
				if (DATA_ROW_RE.test(lines[i])) {
					i++;
					continue;
				}
				var j = i;
				while (j < lines.length && CT_LINE_RE.test(lines[j])) {
					j++;
				}
				if (j === i || j >= lines.length || !DATA_ROW_RE.test(lines[j])) {
					break;
				}
				i = j;
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
			   the extent absorption above fixes specifically: `! DEFi #BUF_SIZE` outliving BUF_SIZE. It
			   also covers every other compiler-minted dotted line, which `TOP_LABEL_RE` cannot match
			   and which therefore all land here. Roots only ever ADD reachability, never remove it. */
			collectRefs(stripComment(lines[blocks[i].start]), work);
			continue;
		}
		byName[blocks[i].name] = blocks[i];
		blocks[i].refs = [];
		for (k = blocks[i].start; k < blocks[i].end; ++k) {
			collectRefs(stripComment(lines[k]), blocks[i].refs);
			// The export signature rides the DEFINITION row, and that is NOT blocks[i].start once a preceding
			// `.z.<name>: ! DEFi` extent (or an anonymous alloc) has been absorbed above it - the case of every
			// exported global, `.z.arr: ! DEFi` then `arr: GLOB ... ; signature export`. Reading start alone
			// therefore dropped exactly the roots the CLI promises to keep. Scan the whole block for it.
			if (/;\s*signature\s+export\b/.test(lines[k])) {
				work[work.length] = blocks[i].name;
			}
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
