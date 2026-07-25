/* GAZL signature validator for NuXJS. */

var hostScriptPath = arguments && arguments.length > 0 ? "" + arguments[0] : "gazl-validate.js";
var hostArgs = [];
for (var hostArgIndex = 1; arguments && hostArgIndex < arguments.length; ++hostArgIndex) {
	hostArgs.push("" + arguments[hostArgIndex]);
}

function hostError(message) {
	print("" + message);
}
function hostRead(filePath) {
	return read(filePath);
}
function hostBasename(path) {
	var slash = path.lastIndexOf("/");
	var backslash = path.lastIndexOf("\\");
	var index = Math.max(slash, backslash);
	return index >= 0 ? path.substr(index + 1) : path;
}
function dirname(path) {
	var slash = path.lastIndexOf("/");
	var backslash = path.lastIndexOf("\\");
	var index = Math.max(slash, backslash);
	return index >= 0 ? path.substr(0, index + 1) : "";
}
var DEFAULT_NATIVE_MANIFEST = dirname(hostScriptPath) + "../docs/nativeCallbackSignatures.gazl";

function startsWith(text, prefix) {
	return text.substr(0, prefix.length) === prefix;
}
if (!String.prototype.trim) {
	String.prototype.trim = function () { return this.replace(/^\s+|\s+$/g, ""); };
}
if (!Array.prototype.forEach) {
	Array.prototype.forEach = function (fn) { for (var i = 0; i < this.length; ++i) if (i in this) fn(this[i], i, this); };
}
if (!Array.prototype.map) {
	Array.prototype.map = function (fn) { var r = []; for (var i = 0; i < this.length; ++i) if (i in this) r.push(fn(this[i], i, this)); return r; };
}
if (!Array.prototype.filter) {
	Array.prototype.filter = function (fn) { var r = []; for (var i = 0; i < this.length; ++i) if (i in this && fn(this[i], i, this)) r.push(this[i]); return r; };
}
if (!Array.prototype.every) {
	Array.prototype.every = function (fn) { for (var i = 0; i < this.length; ++i) if (i in this && !fn(this[i], i, this)) return false; return true; };
}
if (!Array.prototype.find) {
	Array.prototype.find = function (fn) { for (var i = 0; i < this.length; ++i) if (i in this && fn(this[i], i, this)) return this[i]; return undefined; };
}
function SimpleMap() { this.items = []; }
SimpleMap.prototype._indexOf = function (key) { for (var i = 0; i < this.items.length; ++i) if (this.items[i][0] === key) return i; return -1; };
SimpleMap.prototype.has = function (key) { return this._indexOf(key) >= 0; };
SimpleMap.prototype.get = function (key) { var i = this._indexOf(key); return i >= 0 ? this.items[i][1] : undefined; };
SimpleMap.prototype.set = function (key, value) { var i = this._indexOf(key); if (i >= 0) this.items[i][1] = value; else this.items.push([key, value]); return this; };
SimpleMap.prototype.entries = function () { return this.items.slice(0); };

function printUsage() {
	var script = hostBasename(hostScriptPath);
	hostError("Usage: " + script + " [--warn-only] [--force] <file ...>");
	hostError("Scan one or more .gazl files for signature metadata conflicts.");
}

function parseArgs(argv) {
	var options = { warnOnly: false, force: false };
	var files = [];
	var parsingOptions = true;

	for (var i = 0; i < argv.length; ++i) {
		var arg = argv[i];

		if (parsingOptions && arg === "--") {
			parsingOptions = false;
			continue;
		}

		if (parsingOptions && startsWith(arg, "-")) {
			if (arg === "--warn-only") {
				options.warnOnly = true;
			} else if (arg === "--force") {
				options.force = true;
			} else if (arg === "--help" || arg === "-h") {
				printUsage();
				throw { exitCode: 0 };
			} else {
				hostError("Unknown option: " + arg);
				printUsage();
				throw { exitCode: 1 };
			}
			continue;
		}

		files.push(arg);
	}

	if (files.length === 0) {
		printUsage();
		throw { exitCode: 1 };
	}

	return { options: options, files: files };
}

function createContext(options) {
	return {
		options: options,
		exports: {
			functions: new SimpleMap(),
			globals: new SimpleMap(),
			consts: new SimpleMap(),
			arrays: new SimpleMap()
		},
		externs: {
			functions: new SimpleMap(),
			globals: new SimpleMap(),
			consts: new SimpleMap(),
			arrays: new SimpleMap()
		},
		externStructs: new SimpleMap(),          // name -> [{ fields, location }]
		structDefs: new SimpleMap(),             // name -> [{ fields, location }] from a real definition
		layoutOffsets: new SimpleMap(),          // struct name -> [field names defined as .o.N.f]
		layoutSizes: new SimpleMap(),            // struct name -> location of .z.N
		calls: new SimpleMap(),
		definitions: new SimpleMap(),
		diagnostics: [],
		hadReadError: false
	};
}

function readFileLines(filePath) {
	var data = hostRead(filePath);
	var lines = data.split(/\r?\n/);
	return lines;
}

function classifyRole(role) {
	var parts = role.trim().split(/\s+/).filter(Boolean);
	var extern = false;
	if (parts[0] === "extern") {
		extern = true;
		parts.shift();
	}

	var native = false;
	if (parts[0] === "native") {
		native = true;
	}

	return {
		extern: extern,
		native: native,
		role: parts.join(" ")
	};
}

function parseParamTypes(paramText) {
	var result = [];
	if (!paramText) {
		return result;
	}

	var pieces = paramText.split(",");
	for (var idx = 0; idx < pieces.length; ++idx) {
		var raw = pieces[idx].trim();
		if (!raw) {
			continue;
		}
		var match = raw.match(/^[A-Za-z?][A-Za-z0-9?-]*/);   // hyphens: element chains like "int-ptr"
		var token = match ? match[0] : raw;
		result.push(token.toLowerCase());
	}
	return result;
}

function normalizeParamDisplay(paramText) {
	if (!paramText) {
		return "";
	}

	var pieces = paramText.split(",");
	var normalized = [];
	for (var idx = 0; idx < pieces.length; ++idx) {
		var trimmed = pieces[idx].trim();
		if (!trimmed) {
			continue;
		}
		normalized.push(trimmed.replace(/\s+/g, " "));
	}
	return normalized.join(", ");
}

function splitOrigin(text) {
	var trimmed = text.trim();
	if (trimmed.length === 0) {
		return { body: trimmed, origin: null };
	}

	var match = trimmed.match(/^(.*\S)\s+@\s+(.+)\s*$/);
	if (!match) {
		return { body: trimmed, origin: null };
	}

	return { body: match[1], origin: match[2].trim() };
}

function parseOriginMarker(originText) {
	if (!originText) {
		return null;
	}

	var trimmed = originText.trim();
	if (trimmed.length === 0) {
		return null;
	}

	var match = trimmed.match(/^(.*):([0-9]+):([0-9]+)$/);
	if (match) {
		return {
			raw: trimmed,
			file: match[1],
			line: parseInt(match[2], 10),
			column: parseInt(match[3], 10)
		};
	}

	match = trimmed.match(/^([0-9]+):([0-9]+)$/);
	if (match) {
		return {
			raw: trimmed,
			file: null,
			line: parseInt(match[1], 10),
			column: parseInt(match[2], 10)
		};
	}

	match = trimmed.match(/^(.*):([0-9]+)$/);
	if (match) {
		return {
			raw: trimmed,
			file: match[1],
			line: parseInt(match[2], 10),
			column: null
		};
	}

	match = trimmed.match(/^([0-9]+)$/);
	if (match) {
		return {
			raw: trimmed,
			file: null,
			line: parseInt(match[1], 10),
			column: null
		};
	}

	return { raw: trimmed };
}

function parseSignatureComment(comment) {
	if (!startsWith(comment, "signature ")) {
		return null;
	}

	var payload = comment.substr("signature ".length).trim();
	var split = splitOrigin(payload);

	var funcMatch = split.body.match(/^(.*?)\s+([^\s(]+)\s*\(([^)]*)\)\s*->\s*(\S+)\s*$/);
	if (funcMatch) {
		var roleInfo = classifyRole(funcMatch[1]);
		var params = parseParamTypes(funcMatch[3]);
		var paramDisplay = normalizeParamDisplay(funcMatch[3]);
		var returns = funcMatch[4].toLowerCase();
		var wildcard = roleInfo.extern && params.length === 0 && returns === "unknown";
		return {
			kind: "function",
			name: funcMatch[2],
			params: params,
			paramDisplay: paramDisplay,
			returns: returns,
			wildcard: wildcard,
			extern: roleInfo.extern,
			native: roleInfo.native,
			origin: split.origin
		};
	}

	// `extern struct Name { field : int, other : float-ptr }` - the interface Impala compiled
	// against. Checked before the value branch, whose `name : type` regex would swallow it.
	var structMatch = split.body.match(/^(extern\s+)?struct\s+([^\s{]+)\s*\{\s*([^}]*)\}\s*$/);
	if (structMatch) {
		var structFields = [];
		var rawFields = structMatch[3].split(",");
		for (var __fi = 0; __fi < rawFields.length; ++__fi) {
			var piece = rawFields[__fi].trim();
			if (piece === "") { continue; }
			var fm = piece.match(/^(\S+)\s*:\s*(\S+)$/);
			structFields.push(fm ? { name: fm[1], type: fm[2].toLowerCase() } : { name: piece, type: "unknown" });
		}
		return {
			kind: "struct",
			name: structMatch[2],
			fields: structFields,
			extern: !!structMatch[1],
			origin: split.origin
		};
	}

	var valueMatch = split.body.match(/^(.*?)\s+([^:]+?)\s*:\s*(\S+)\s*$/);
	if (valueMatch) {
		var roleInfo = classifyRole(valueMatch[1]);
		var type = valueMatch[3].toLowerCase();
		var nameSpec = valueMatch[2].trim();
		// The lazy role group leaves the role word attached to the name in extern rows such
		// as "extern array name[] : unknown" or "extern global name : ptr"; strip it so
		// extern declarations match their definitions.
		var rolePrefix = nameSpec.match(/^(array|global|const)\s+(.+)$/);
		if (rolePrefix) {
			nameSpec = rolePrefix[2];
			if (rolePrefix[1] !== "array") {
				roleInfo = { extern: roleInfo.extern, native: roleInfo.native, role: rolePrefix[1] };
			}
		}
		var arrayMatch = nameSpec.match(/^([^\[]+)\[(.*)\]$/);
		if (arrayMatch) {
			var sizeText = arrayMatch[2].trim();
			return {
				kind: "array",
				name: arrayMatch[1].trim(),
				size: sizeText === "" ? undefined : sizeText,
				category: type,
				extern: roleInfo.extern,
				role: roleInfo.role,
				origin: split.origin
			};
		}

		var role = roleInfo.role || "global";
		var name = nameSpec;
		if (role === "const") {
			return {
				kind: "const",
				name: name,
				category: type,
				extern: roleInfo.extern,
				role: role,
				origin: split.origin
			};
		}
		return {
			kind: "global",
			name: name,
			category: type,
			extern: roleInfo.extern,
			role: role,
			origin: split.origin
		};
	}

	return null;
}

function parseExpectsComment(comment) {
	if (!startsWith(comment, "expects ")) {
		return null;
	}

	var payload = comment.substr("expects ".length).trim();
	var split = splitOrigin(payload);
	var match = split.body.match(/^([^\s(]+)\s*\(([^)]*)\)\s*->\s*(\S+)\s*$/);
	if (!match) {
		return null;
	}

	return {
		name: match[1],
		params: parseParamTypes(match[2]),
		paramDisplay: normalizeParamDisplay(match[2]),
		returns: match[3].toLowerCase(),
		origin: split.origin
	};
}

function detectCallInfo(line, commentIndex) {
	var prefix = commentIndex >= 0 ? line.slice(0, commentIndex) : line;
	var upper = prefix.toUpperCase();
	var callIndex = upper.lastIndexOf("CALL");
	if (callIndex === -1) {
		return { native: null };
	}

	var cursor = callIndex + 4;
	while (cursor < prefix.length && /\s/.test(prefix[cursor])) {
		cursor += 1;
	}
	if (cursor >= prefix.length) {
		return { native: null };
	}

	var remainder = prefix.slice(cursor);
	var match = remainder.match(/^([^\s]+)/);
	if (!match) {
		return { native: null };
	}

	var token = match[1];
	if (startsWith(token, "^")) {
		return { native: true };
	}

	return { native: false };
}

function location(file, line, originText) {
	return { file: file, line: line, origin: parseOriginMarker(originText) };
}

function loadNativeManifest(ctx, manifestPath) {
	if (!manifestPath) {
		return;
	}

	var data;
	try {
		data = hostRead(manifestPath);
	} catch (err) {
		if ((err && err.code === "ENOENT") || manifestPath === DEFAULT_NATIVE_MANIFEST) {
			return;
		}
		hostError("Failed to read native manifest " + manifestPath + ": " + (err.message || err));
		ctx.hadReadError = true;
		return;
	}

	var lines = data.split(/\r?\n/);
	lines.forEach(function (line, index) {
		var trimmed = line.trim();
		if (!startsWith(trimmed, ";")) {
			return;
		}

		var comment = trimmed.slice(1).trim();
		if (!startsWith(comment.toLowerCase(), "signature ")) {
			return;
		}

		var parsed = parseSignatureComment(comment);
		if (!parsed || parsed.kind !== "function") {
			return;
		}

		parsed.native = true;
		var loc = location(manifestPath, index + 1, parsed.origin);
		addDefinitionRecord(ctx, parsed, loc, "native manifest", true);
	});
}

function addDefinitionRecord(ctx, parsed, loc, kind, nativeOverride) {
	if (!parsed.name) {
		return;
	}

	var native = nativeOverride != null ? nativeOverride : parsed.native;
	if (!ctx.definitions.has(parsed.name)) {
		ctx.definitions.set(parsed.name, []);
	}
	ctx.definitions.get(parsed.name).push({
		signature: {
			params: parsed.params,
			returns: parsed.returns,
			wildcard: parsed.wildcard,
			paramDisplay: parsed.paramDisplay
		},
		location: loc,
		kind: kind,
		native: !!native
	});
}

function addFunctionExport(ctx, parsed, loc) {
	var signature = {
		params: parsed.params,
		returns: parsed.returns,
		wildcard: parsed.wildcard,
		paramDisplay: parsed.paramDisplay
	};
	var existing = ctx.exports.functions.get(parsed.name);
	if (!existing) {
		ctx.exports.functions.set(parsed.name, {
			signature: signature,
			locations: [loc],
			native: !!parsed.native
		});
		addDefinitionRecord(ctx, parsed, loc, "definition");
		return;
	}

	if (!functionSignaturesCompatible(existing.signature, signature)) {
		ctx.diagnostics.push({
			severity: "error",
			message: "Conflicting definitions for function " + parsed.name,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin
				}
			]
		});
		return;
	}

	if (existing.native !== undefined && existing.native !== !!parsed.native) {
		ctx.diagnostics.push({
			severity: "error",
			message: "Conflicting native declarations for function " + parsed.name,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin
				}
			]
		});
		return;
	}

	existing.native = existing.native != null ? existing.native : !!parsed.native;
	existing.locations.push(loc);
	addDefinitionRecord(ctx, parsed, loc, "definition");
}

function addFunctionImport(ctx, parsed, loc) {
	var record = {
		signature: {
			params: parsed.params,
			returns: parsed.returns,
			wildcard: parsed.wildcard,
			paramDisplay: parsed.paramDisplay
		},
		location: loc,
		native: parsed.native,
		kind: parsed.native ? "extern native" : "extern"
	};
	if (!ctx.externs.functions.has(parsed.name)) {
		ctx.externs.functions.set(parsed.name, []);
	}
	ctx.externs.functions.get(parsed.name).push(record);

	if (parsed.native) {
		addDefinitionRecord(ctx, parsed, loc, "extern native", true);
	}
}

function addGlobalExport(ctx, parsed, loc) {
	var record = {
		category: parsed.category,
		role: parsed.role,
		location: loc
	};
	var existing = ctx.exports.globals.get(parsed.name);
	if (!existing) {
		ctx.exports.globals.set(parsed.name, {
			signature: record,
			locations: [loc]
		});
		return;
	}

	if (!typesCompatible(existing.signature.category, record.category)) {
		ctx.diagnostics.push({
			severity: "error",
			message: "Conflicting definitions for global " + parsed.name,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin
				}
			]
		});
		return;
	}

	existing.locations.push(loc);
}

function addGlobalImport(ctx, parsed, loc) {
	var record = { category: parsed.category, role: parsed.role };
	if (!ctx.externs.globals.has(parsed.name)) {
		ctx.externs.globals.set(parsed.name, []);
	}
	ctx.externs.globals.get(parsed.name).push({ signature: record, location: loc });
}

function addConstExport(ctx, parsed, loc) {
	var record = {
		category: parsed.category,
		role: parsed.role,
		location: loc
	};
	var existing = ctx.exports.consts.get(parsed.name);
	if (!existing) {
		ctx.exports.consts.set(parsed.name, {
			signature: record,
			locations: [loc]
		});
		return;
	}

	if (!typesCompatible(existing.signature.category, record.category)) {
		ctx.diagnostics.push({
			severity: "error",
			message: "Conflicting definitions for const " + parsed.name,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin
				}
			]
		});
		return;
	}

	existing.locations.push(loc);
}

function addConstImport(ctx, parsed, loc) {
	var record = { category: parsed.category, role: parsed.role };
	if (!ctx.externs.consts.has(parsed.name)) {
		ctx.externs.consts.set(parsed.name, []);
	}
	ctx.externs.consts.get(parsed.name).push({ signature: record, location: loc });
}

function addArrayExport(ctx, parsed, loc) {
	var record = {
		category: parsed.category,
		size: parsed.size,
		role: parsed.role,
		location: loc
	};
	var existing = ctx.exports.arrays.get(parsed.name);
	if (!existing) {
		ctx.exports.arrays.set(parsed.name, {
			signature: record,
			locations: [loc]
		});
		return;
	}

	if (!arraySignaturesCompatible(existing.signature, record)) {
		ctx.diagnostics.push({
			severity: "error",
			message: "Conflicting definitions for array " + parsed.name,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin
				}
			]
		});
		return;
	}

	existing.locations.push(loc);
}

function addArrayImport(ctx, parsed, loc) {
	var record = {
		category: parsed.category,
		size: parsed.size,
		role: parsed.role
	};
	if (!ctx.externs.arrays.has(parsed.name)) {
		ctx.externs.arrays.set(parsed.name, []);
	}
	ctx.externs.arrays.get(parsed.name).push({ signature: record, location: loc });
}

function addCallExpectation(ctx, parsed, loc, callInfo) {
	if (!parsed.name || parsed.name === "function") {
		return;
	}
	if (!ctx.calls.has(parsed.name)) {
		ctx.calls.set(parsed.name, []);
	}
	ctx.calls.get(parsed.name).push({
		signature: { params: parsed.params, returns: parsed.returns, paramDisplay: parsed.paramDisplay },
		location: loc,
		native: callInfo ? callInfo.native : null
	});
}

function isPointerCategory(category) {
	return category === "ptr" || category.length > 4 && category.substr(category.length - 4) === "-ptr";
}

function typesCompatible(a, b) {
	if (a == null || b == null) {
		return true;
	}
	if (a === "unknown" || b === "unknown") {
		return true;
	}
	if (a === b) {
		return true;
	}
	// Element-typed pointer chains ("int-ptr", "int-ptr-ptr"): a bare "ptr" is an
	// element-unknown pointer and matches any chain; differing chains do not match.
	if (isPointerCategory(a) && isPointerCategory(b) && (a === "ptr" || b === "ptr")) {
		return true;
	}
	return false;
}

function functionSignaturesCompatible(a, b) {
	if (!a || !b) {
		return true;
	}
	if (a.wildcard || b.wildcard) {
		return true;
	}
	if (a.params && b.params && a.params.length !== b.params.length) {
		return false;
	}
	var paramCount = Math.max(a.params ? a.params.length : 0, b.params ? b.params.length : 0);
	for (var i = 0; i < paramCount; ++i) {
		var left = a.params ? a.params[i] : undefined;
		var right = b.params ? b.params[i] : undefined;
		if (!typesCompatible(left || "unknown", right || "unknown")) {
			return false;
		}
	}
	if (!typesCompatible(a.returns || "unknown", b.returns || "unknown")) {
		return false;
	}
	return true;
}

function arraySignaturesCompatible(a, b) {
	if (!typesCompatible(a.category || "unknown", b.category || "unknown")) {
		return false;
	}
	if (a.size && b.size && a.size !== b.size) {
		return false;
	}
	return true;
}

function recordSignature(ctx, parsed, loc) {
	switch (parsed.kind) {
		case "function":
			if (parsed.extern) {
				addFunctionImport(ctx, parsed, loc);
			} else {
				addFunctionExport(ctx, parsed, loc);
			}
			break;
		case "global":
			if (parsed.extern) {
				addGlobalImport(ctx, parsed, loc);
			} else {
				addGlobalExport(ctx, parsed, loc);
			}
			break;
		case "const":
			if (parsed.extern) {
				addConstImport(ctx, parsed, loc);
			} else {
				addConstExport(ctx, parsed, loc);
			}
			break;
		case "struct":
			if (parsed.extern) {                             // a declaration: what this unit compiled against
				if (!ctx.externStructs.has(parsed.name)) { ctx.externStructs.set(parsed.name, []); }
				ctx.externStructs.get(parsed.name).push({ fields: parsed.fields, location: loc });
			} else {                                         // a definition: the real, typed layout
				if (!ctx.structDefs.has(parsed.name)) { ctx.structDefs.set(parsed.name, []); }
				ctx.structDefs.get(parsed.name).push({ fields: parsed.fields, location: loc });
			}
			break;
		case "array":
			if (parsed.extern) {
				addArrayImport(ctx, parsed, loc);
			} else {
				addArrayExport(ctx, parsed, loc);
			}
			break;
		default:
			break;
	}
}

function processFile(filePath, ctx) {
	var lines;
	try {
		lines = readFileLines(filePath);
	} catch (err) {
		hostError("Unable to read " + filePath + ": " + err.message);
		ctx.hadReadError = true;
		return;
	}

	for (var idx = 0; idx < lines.length; ++idx) {
		var line = lines[idx];
		var lineNumber = idx + 1;

		var sigIdx = line.indexOf("; signature");
		if (sigIdx !== -1) {
			var comment = line.substr(sigIdx + 1).trim();
			var parsed = parseSignatureComment(comment);
			if (parsed) {
				recordSignature(ctx, parsed, location(filePath, lineNumber, parsed.origin));
			}
		}

		// Layout constants: `.o.Name.field: ! DEFi ...` and `.z.Name: ! DEFi ...`. Whoever supplies
		// them (an Impala unit, or a host header) is asserting a struct layout, so an extern struct
		// declaration can be checked against them.
		var offMatch = line.match(/^\s*\.o\.([A-Za-z_$][\w$]*)\.([A-Za-z_$][\w$]*)\s*:/);
		if (offMatch) {
			if (!ctx.layoutOffsets.has(offMatch[1])) { ctx.layoutOffsets.set(offMatch[1], []); }
			ctx.layoutOffsets.get(offMatch[1]).push(offMatch[2]);
		}
		var sizeMatch = line.match(/^\s*\.z\.([A-Za-z_$][\w$]*)\s*:/);
		if (sizeMatch && !ctx.layoutSizes.has(sizeMatch[1])) {
			ctx.layoutSizes.set(sizeMatch[1], location(filePath, lineNumber, undefined));
		}

		var expectsIdx = line.indexOf("; expects");
		if (expectsIdx !== -1) {
			var comment = line.substr(expectsIdx + 1).trim();
			var parsed = parseExpectsComment(comment);
			if (parsed) {
				var callInfo = detectCallInfo(line, expectsIdx);
				addCallExpectation(ctx, parsed, location(filePath, lineNumber, parsed.origin), callInfo);
			}
		}
	}
}

function compareExternSets(kind, externMap, exportMap, ctx, comparator, mismatchMessage, missingMessage) {
	var missingFn =
		typeof missingMessage === "function"
			? missingMessage
			: typeof mismatchMessage === "function"
				? mismatchMessage
				: function () { return "Missing extern definition"; };

	var __externEntries = externMap.entries();
	for (var __externIndex = 0; __externIndex < __externEntries.length; ++__externIndex) {
		var name = __externEntries[__externIndex][0];
		var entries = __externEntries[__externIndex][1];
		if (entries.length > 1) {
			for (var i = 0; i < entries.length; ++i) {
				for (var j = i + 1; j < entries.length; ++j) {
					var hasNativeA = Object.prototype.hasOwnProperty.call(entries[i], "native");
					var hasNativeB = Object.prototype.hasOwnProperty.call(entries[j], "native");
					if (hasNativeA && hasNativeB && entries[i].native !== entries[j].native) {
						ctx.diagnostics.push({
							severity: "error",
							message: kind + " " + name + " has conflicting native declarations",
							locations: [
								{
									label: "first declaration",
									file: entries[i].location.file,
									line: entries[i].location.line,
									origin: entries[i].location.origin
								},
								{
									label: "second declaration",
									file: entries[j].location.file,
									line: entries[j].location.line,
									origin: entries[j].location.origin
								}
							]
						});
						continue;
					}
					if (!comparator(entries[i], entries[j])) {
						ctx.diagnostics.push({
							severity: "error",
							message: kind + " " + name + " has conflicting extern declarations",
							locations: [
								{
									label: "first declaration",
									file: entries[i].location.file,
									line: entries[i].location.line,
									origin: entries[i].location.origin
								},
								{
									label: "second declaration",
									file: entries[j].location.file,
									line: entries[j].location.line,
									origin: entries[j].location.origin
								}
							]
						});
					}
				}
			}
		}

		var exportEntry = exportMap.get(name);
		if (exportEntry) {
			entries.forEach(function (entry) {
				if (entry.native) {
					ctx.diagnostics.push({
						severity: "error",
						message: kind + " " + name + " is declared native but defined in Impala",
						locations: [
							{
								label: "definition",
								file: exportEntry.locations[0].file,
								line: exportEntry.locations[0].line,
								origin: exportEntry.locations[0].origin
							},
							{
								label: "extern declaration",
								file: entry.location.file,
								line: entry.location.line,
								origin: entry.location.origin
							}
						]
					});
					return;
				}
				if (!comparator({ signature: exportEntry.signature }, { signature: entry.signature })) {
					ctx.diagnostics.push({
						severity: "error",
						message: kind + " " + name + " does not match its definition",
						locations: [
							{
								label: "definition",
								file: exportEntry.locations[0].file,
								line: exportEntry.locations[0].line,
								origin: exportEntry.locations[0].origin
							},
							{
								label: "extern declaration",
								file: entry.location.file,
								line: entry.location.line,
								origin: entry.location.origin
							}
						]
					});
				}
			});
			continue;
		}

		var skipMissing = entries.every(function (entry) { return entry.native; });
		if (!skipMissing) {
			ctx.diagnostics.push({
				severity: "warning",
				message: missingFn(name),
				locations: entries.map(function (entry) { return {
					label: "extern declaration",
					file: entry.location.file,
					line: entry.location.line,
					origin: entry.location.origin
				}; })
			});
		}
	}
}

function comparatorWrapper(entryA, entryB, fn) {
	return fn(entryA.signature, entryB.signature);
}

function formatSignatureForMessage(name, signature) {
	var fnName = name || "unknown";
	if (!signature) {
		return fnName + "() -> unknown";
	}
	var returns = signature.returns || "unknown";
	var params = "";
	if (signature.paramDisplay) {
		params = signature.paramDisplay;
	} else if (signature.params && signature.params.length > 0) {
		params = signature.params.join(", ");
	}
	return fnName + "(" + params + ") -> " + returns;
}

function isNativeCompatible(callNative, recordNative) {
	if (callNative == null) {
		return true;
	}
	return !!recordNative === callNative;
}

function validateCalls(ctx) {
	var __callEntries = ctx.calls.entries();
	for (var __callIndex = 0; __callIndex < __callEntries.length; ++__callIndex) {
		var name = __callEntries[__callIndex][0];
		var callEntries = __callEntries[__callIndex][1];
		if (!callEntries || callEntries.length === 0) {
			continue;
		}

		for (var i = 0; i < callEntries.length; ++i) {
			for (var j = i + 1; j < callEntries.length; ++j) {
				if (!functionSignaturesCompatible(callEntries[i].signature, callEntries[j].signature)) {
					var leftReturn = callEntries[i].signature.returns || "unknown";
					var rightReturn = callEntries[j].signature.returns || "unknown";
					ctx.diagnostics.push({
						severity: "error",
						message: "Conflicting return type expectations for \"" + name + "\" (" + leftReturn + " vs " + rightReturn + ")",
						locations: [
							{
								label: "call site",
								file: callEntries[i].location.file,
								line: callEntries[i].location.line,
								origin: callEntries[i].location.origin
							},
							{
								label: "call site",
								file: callEntries[j].location.file,
								line: callEntries[j].location.line,
								origin: callEntries[j].location.origin
							}
						]
					});
				}
			}
		}

		var definitionRecords = ctx.definitions.get(name) || [];
		var concreteDefinitionRecords = definitionRecords.filter(function (def) {
			return !(def.signature && def.signature.wildcard);
		});
		var checkedDefinitionRecords = concreteDefinitionRecords.length > 0 ? concreteDefinitionRecords : definitionRecords;
		var externRecords = (ctx.externs.functions.get(name) || []).filter(function (entry) { return !entry.native; });

		callEntries.forEach(function (call) {
			var matchingDefinitions = checkedDefinitionRecords.filter(function (def) { return functionSignaturesCompatible(def.signature, call.signature); });
			var matchingExterns = externRecords.filter(function (entry) { return functionSignaturesCompatible(entry.signature, call.signature); });

			if (matchingDefinitions.length > 0) {
				var nativeMatch = matchingDefinitions.find(function (def) { return isNativeCompatible(call.native, def.native); });
				if (!nativeMatch) {
					var mismatch = matchingDefinitions[0];
					var expected = call.native ? "native" : "non-native";
					var actual = mismatch.native ? "native" : "non-native";
					ctx.diagnostics.push({
						severity: "error",
						message: "Call to " + name + " uses " + expected + " calling convention but definition is " + actual,
						locations: [
							{
								label: "definition",
								file: mismatch.location.file,
								line: mismatch.location.line,
								origin: mismatch.location.origin
							},
							{
								label: "call site",
								file: call.location.file,
								line: call.location.line,
								origin: call.location.origin
							}
						]
					});
				}
				return;
			}

			if (checkedDefinitionRecords.length > 0) {
				var mismatch = checkedDefinitionRecords[0];
				ctx.diagnostics.push({
					severity: "error",
					message: "Signature mismatch for \"" + name + "\": call expects \"" + formatSignatureForMessage(name, call.signature) + "\" but definition provides \"" + formatSignatureForMessage(name, mismatch.signature) + "\"",
					locations: [
						{
							label: "definition",
							file: mismatch.location.file,
							line: mismatch.location.line,
							origin: mismatch.location.origin
						},
						{
							label: "call site",
							file: call.location.file,
							line: call.location.line,
							origin: call.location.origin
						}
					]
				});
				return;
			}

			if (matchingExterns.length > 0) {
				var nativeMatch = matchingExterns.find(function (entry) { return isNativeCompatible(call.native, entry.native); });
				if (!nativeMatch) {
					var mismatch = matchingExterns[0];
					var expected = call.native ? "native" : "non-native";
					var actual = mismatch.native ? "native" : "non-native";
					ctx.diagnostics.push({
						severity: "error",
						message: "Call to " + name + " uses " + expected + " calling convention but extern declaration is " + actual,
						locations: [
							{
								label: "extern declaration",
								file: mismatch.location.file,
								line: mismatch.location.line,
								origin: mismatch.location.origin
							},
							{
								label: "call site",
								file: call.location.file,
								line: call.location.line,
								origin: call.location.origin
							}
						]
					});
					return;
				}
				return;
			}

			var externEntries = ctx.externs.functions.get(name);
			if (externEntries && externEntries.length > 0) {
				ctx.diagnostics.push({
					severity: "error",
					message: "Call to " + name + " does not match extern declaration",
					locations: [
						{
							label: "extern declaration",
							file: externEntries[0].location.file,
							line: externEntries[0].location.line,
							origin: externEntries[0].location.origin
						},
						{
							label: "call site",
							file: call.location.file,
							line: call.location.line,
							origin: call.location.origin
						}
					]
				});
				return;
			}

			ctx.diagnostics.push({
				severity: "warning",
				message: "No signature metadata found for function " + name,
				locations: [
					{
						label: "call site",
						file: call.location.file,
						line: call.location.line,
						origin: call.location.origin
					}
				]
			});
		});
	}
}

// An `extern struct` says "the host owns this layout; here is the interface I compiled against".
// Two things are checkable, and neither was before: two units must not declare the same extern
// struct differently, and where a layout IS supplied in the scanned set (.o.Name.field / .z.Name),
// every declared field must have an offset constant and the size must exist. Without this a host
// that renames, drops or re-types a field links fine and corrupts memory at run time.
// An `extern` declaration is a CLAIM about a function implemented elsewhere; Impala compiles calls
// from it (argument checks, casts, result type). Linking is where the claim must be confirmed. An
// `extern native` declaration is also recorded as a fallback definition so calls to natives nobody
// describes still resolve - but it must NOT certify itself: where an authoritative definition exists
// (the native manifest, or a real definition in another scanned unit) the declaration has to match it.
function validateExternDeclarations(ctx) {
	function authoritative(def) {
		return def.kind !== "extern native" && !(def.signature && def.signature.wildcard);
	}
	var entries = ctx.externs.functions.entries();
	for (var ei = 0; ei < entries.length; ++ei) {
		var name = entries[ei][0];
		var decls = entries[ei][1];
		var defs = (ctx.definitions.get(name) || []).filter(authoritative);
		if (defs.length === 0) { continue; }          // nothing authoritative to check against
		for (var di = 0; di < decls.length; ++di) {
			var decl = decls[di];
			if (decl.signature && decl.signature.wildcard) { continue; }   // name-only asserts nothing
			for (var fi = 0; fi < defs.length; ++fi) {
				if (functionSignaturesCompatible(decl.signature, defs[fi].signature)) { continue; }
				ctx.diagnostics.push({
					severity: "error",
					message: "extern declaration of " + name + " does not match its definition: declared \""
						+ formatSignatureForMessage(name, decl.signature) + "\" but definition provides \""
						+ formatSignatureForMessage(name, defs[fi].signature) + "\"",
					locations: [
						{ label: "extern declaration", file: decl.location.file,
							line: decl.location.line, origin: decl.location.origin },
						{ label: "definition", file: defs[fi].location.file,
							line: defs[fi].location.line, origin: defs[fi].location.origin }
					]
				});
			}
		}
	}
}

function validateExternStructs(ctx) {
	function describe(fields) {
		var parts = [];
		for (var i = 0; i < fields.length; ++i) { parts.push(fields[i].name + " : " + fields[i].type); }
		return "{ " + parts.join(", ") + " }";
	}

	var entries = ctx.externStructs.entries();
	for (var ei = 0; ei < entries.length; ++ei) {
		var name = entries[ei][0];
		var decls = entries[ei][1];

		// (a) conflicting declarations of the same extern struct
		for (var a = 0; a < decls.length; ++a) {
			for (var b = a + 1; b < decls.length; ++b) {
				if (describe(decls[a].fields) !== describe(decls[b].fields)) {
					ctx.diagnostics.push({
						severity: "error",
						message: "extern struct " + name + " has conflicting declarations: "
							+ describe(decls[a].fields) + " vs " + describe(decls[b].fields),
						locations: [
							{ label: "first declaration", file: decls[a].location.file,
								line: decls[a].location.line, origin: decls[a].location.origin },
							{ label: "second declaration", file: decls[b].location.file,
								line: decls[b].location.line, origin: decls[b].location.origin }
						]
					});
				}
			}
		}

		// (b) a real definition of the same struct is authoritative: field names, order AND types
		//     must match what this unit compiled against.
		var realDefs = ctx.structDefs.get(name) || [];
		for (var ri = 0; ri < realDefs.length; ++ri) {
			for (var qi = 0; qi < decls.length; ++qi) {
				if (describe(decls[qi].fields) === describe(realDefs[ri].fields)) { continue; }
				ctx.diagnostics.push({
					severity: "error",
					message: "extern struct " + name + " does not match its definition: declared "
						+ describe(decls[qi].fields) + " but definition provides " + describe(realDefs[ri].fields),
					locations: [
						{ label: "extern declaration", file: decls[qi].location.file,
							line: decls[qi].location.line, origin: decls[qi].location.origin },
						{ label: "definition", file: realDefs[ri].location.file,
							line: realDefs[ri].location.line, origin: realDefs[ri].location.origin }
					]
				});
			}
		}

		// (b) check against a supplied layout, when one is in the scanned set
		var offsets = ctx.layoutOffsets.get(name);
		var hasSize = ctx.layoutSizes.has(name);
		if (!offsets && !hasSize) { continue; }        // nothing supplied here: nothing to check against
		var decl = decls[0];
		var supplied = offsets || [];
		for (var fi = 0; fi < decl.fields.length; ++fi) {
			var fieldName = decl.fields[fi].name;
			var found = false;
			for (var si = 0; si < supplied.length; ++si) { if (supplied[si] === fieldName) { found = true; break; } }
			if (!found) {
				ctx.diagnostics.push({
					severity: "error",
					message: "extern struct " + name + " declares field \"" + fieldName
						+ "\" but the supplied layout defines no .o." + name + "." + fieldName,
					locations: [ { label: "declared here", file: decl.location.file,
						line: decl.location.line, origin: decl.location.origin } ]
				});
			}
		}
		if (!hasSize) {
			ctx.diagnostics.push({
				severity: "error",
				message: "extern struct " + name + " has offset constants but no .z." + name + " size constant",
				locations: [ { label: "declared here", file: decl.location.file,
					line: decl.location.line, origin: decl.location.origin } ]
			});
		}
	}
}

function validateContext(ctx) {
	compareExternSets(
		"Function",
		ctx.externs.functions,
		ctx.exports.functions,
		ctx,
		function (a, b) { return comparatorWrapper(a, b, functionSignaturesCompatible); },
		function (name) { return "No definition found for extern function " + name; }
	);

	compareExternSets(
		"Global",
		ctx.externs.globals,
		ctx.exports.globals,
		ctx,
		function (a, b) { return comparatorWrapper(a, b, function (left, right) { return typesCompatible(left.category || "unknown", right.category || "unknown"); }); },
		function (name) { return "No definition found for extern global " + name; }
	);

	compareExternSets(
		"Const",
		ctx.externs.consts,
		ctx.exports.consts,
		ctx,
		function (a, b) { return comparatorWrapper(a, b, function (left, right) { return typesCompatible(left.category || "unknown", right.category || "unknown"); }); },
		function (name) { return "No definition found for extern const " + name; }
	);

	compareExternSets(
		"Array",
		ctx.externs.arrays,
		ctx.exports.arrays,
		ctx,
		function (a, b) { return comparatorWrapper(a, b, arraySignaturesCompatible); },
		function (name) { return "No definition found for extern array " + name; }
	);

	validateExternDeclarations(ctx);
	validateExternStructs(ctx);
	validateCalls(ctx);
}

function formatOriginText(origin) {
	if (!origin) {
		return null;
	}

	var parts = [];
	if (origin.file) {
		parts.push(origin.file);
	}
	if (origin.line != null) {
		parts.push(String(origin.line));
	}
	if (origin.column != null) {
		parts.push(String(origin.column));
	}

	if (parts.length > 0) {
		return parts.join(":");
	}

	return origin.raw || null;
}

function formatLocation(loc) {
	var base = loc.file + ":" + loc.line;
	var originText = formatOriginText(loc.origin);
	if (originText) {
		return base + " (origin " + originText + ")";
	}
	return base;
}

function reportDiagnostics(ctx) {
	if (ctx.hadReadError) {
		return 1;
	}

	var hadError = false;
	ctx.diagnostics.forEach(function (diag) {
		var severity = diag.severity;
		if (severity === "warning" && ctx.options.force) {
			severity = "error";
		} else if (severity === "error" && ctx.options.warnOnly) {
			severity = "warning";
		}

		var label = severity.toUpperCase();
		hostError(label + ": " + diag.message);
		if (diag.locations) {
			diag.locations.forEach(function (loc) {
				var prefix = loc.label ? "  " + loc.label + ":" : "  at";
				hostError(prefix + " " + formatLocation(loc));
			});
		}

		if (severity === "error") {
			hadError = true;
		}
	});

	if (hadError && !ctx.options.warnOnly) {
		return 1;
	}
	return 0;
}

function main() {
	var parsedArgs = parseArgs(hostArgs);
	var options = parsedArgs.options;
	var files = parsedArgs.files;
	var ctx = createContext(options);
	loadNativeManifest(ctx, DEFAULT_NATIVE_MANIFEST);
	files.forEach(function (file) {
		processFile(file, ctx);
	});
	validateContext(ctx);
	var exitCode = reportDiagnostics(ctx);
	return exitCode;
}

var __gazlValidateExitCode = 1;
try {
	__gazlValidateExitCode = main();
} catch (__gazlValidateError) {
	if (__gazlValidateError && typeof __gazlValidateError.exitCode === "number") {
		__gazlValidateExitCode = __gazlValidateError.exitCode;
	} else {
		hostError(__gazlValidateError && __gazlValidateError.message ? __gazlValidateError.message : __gazlValidateError);
		__gazlValidateExitCode = 1;
	}
}
if (__gazlValidateExitCode !== 0) {
	throw new Error("gazl-validate failed with exit code " + __gazlValidateExitCode);
}
