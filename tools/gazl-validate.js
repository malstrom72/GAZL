#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");

const DEFAULT_NATIVE_MANIFEST = path.resolve(__dirname, "../docs/nativeCallbackSignatures.gazl");

function printUsage() {
	const script = path.basename(process.argv[1] || "gazl-validate.js");
	console.error(`Usage: ${script} [--warn-only] [--force] <file ...>`);
	console.error("Scan one or more .gazl files for signature metadata conflicts.");
}

function parseArgs(argv) {
	const options = { warnOnly: false, force: false };
	const files = [];
	let parsingOptions = true;

	for (let i = 0; i < argv.length; ++i) {
		const arg = argv[i];

		if (parsingOptions && arg === "--") {
			parsingOptions = false;
			continue;
		}

		if (parsingOptions && arg.startsWith("-")) {
			if (arg === "--warn-only") {
				options.warnOnly = true;
			} else if (arg === "--force") {
				options.force = true;
			} else if (arg === "--help" || arg === "-h") {
				printUsage();
				process.exit(0);
			} else {
				console.error(`Unknown option: ${arg}`);
				printUsage();
				process.exit(1);
			}
			continue;
		}

		files.push(arg);
	}

	if (files.length === 0) {
		printUsage();
		process.exit(1);
	}

	return { options, files };
}

function createContext(options) {
	return {
		options,
		exports: {
			functions: new Map(),
			globals: new Map(),
			consts: new Map(),
			arrays: new Map(),
		},
		externs: {
			functions: new Map(),
			globals: new Map(),
			consts: new Map(),
			arrays: new Map(),
		},
		calls: new Map(),
		definitions: new Map(),
		diagnostics: [],
		hadReadError: false,
	};
}

function readFileLines(filePath) {
	const data = fs.readFileSync(filePath, "utf8");
	const lines = data.split(/\r?\n/);
	return lines;
}

function classifyRole(role) {
	const parts = role.trim().split(/\s+/).filter(Boolean);
	let extern = false;
	if (parts[0] === "extern") {
		extern = true;
		parts.shift();
	}

	let native = false;
	if (parts[0] === "native") {
		native = true;
	}

	return {
		extern,
		native,
		role: parts.join(" "),
	};
}

function parseParamTypes(paramText) {
	const result = [];
	if (!paramText) {
		return result;
	}

	const pieces = paramText.split(",");
	for (let idx = 0; idx < pieces.length; ++idx) {
		const raw = pieces[idx].trim();
		if (!raw) {
			continue;
		}
		const match = raw.match(/^[A-Za-z?][A-Za-z0-9?]*/);
		const token = match ? match[0] : raw;
		result.push(token.toLowerCase());
	}
	return result;
}

function normalizeParamDisplay(paramText) {
	if (!paramText) {
		return "";
	}

	const pieces = paramText.split(",");
	const normalized = [];
	for (let idx = 0; idx < pieces.length; ++idx) {
		const trimmed = pieces[idx].trim();
		if (!trimmed) {
			continue;
		}
		normalized.push(trimmed.replace(/\s+/g, " "));
	}
	return normalized.join(", ");
}

function splitOrigin(text) {
	const trimmed = text.trim();
	if (trimmed.length === 0) {
		return { body: trimmed, origin: null };
	}

	const match = trimmed.match(/^(.*\S)\s+@\s+(.+)\s*$/);
	if (!match) {
		return { body: trimmed, origin: null };
	}

	return { body: match[1], origin: match[2].trim() };
}

function parseOriginMarker(originText) {
	if (!originText) {
		return null;
	}

	const trimmed = originText.trim();
	if (trimmed.length === 0) {
		return null;
	}

	let match = trimmed.match(/^(.*):([0-9]+):([0-9]+)$/);
	if (match) {
		return {
			raw: trimmed,
			file: match[1],
			line: parseInt(match[2], 10),
			column: parseInt(match[3], 10),
		};
	}

	match = trimmed.match(/^([0-9]+):([0-9]+)$/);
	if (match) {
		return {
			raw: trimmed,
			file: null,
			line: parseInt(match[1], 10),
			column: parseInt(match[2], 10),
		};
	}

	match = trimmed.match(/^(.*):([0-9]+)$/);
	if (match) {
		return {
			raw: trimmed,
			file: match[1],
			line: parseInt(match[2], 10),
			column: null,
		};
	}

	match = trimmed.match(/^([0-9]+)$/);
	if (match) {
		return {
			raw: trimmed,
			file: null,
			line: parseInt(match[1], 10),
			column: null,
		};
	}

	return { raw: trimmed };
}

function parseSignatureComment(comment) {
	if (!comment.startsWith("signature ")) {
		return null;
	}

	const payload = comment.substr("signature ".length).trim();
	const split = splitOrigin(payload);

	const funcMatch = split.body.match(/^(.*?)\s+([^\s(]+)\s*\(([^)]*)\)\s*->\s*(\S+)\s*$/);
	if (funcMatch) {
		const roleInfo = classifyRole(funcMatch[1]);
		const params = parseParamTypes(funcMatch[3]);
		const paramDisplay = normalizeParamDisplay(funcMatch[3]);
		const returns = funcMatch[4].toLowerCase();
		const wildcard = roleInfo.extern && !roleInfo.native && params.length === 0 && returns === "unknown";
		return {
			kind: "function",
			name: funcMatch[2],
			params,
			paramDisplay,
			returns,
			wildcard,
			extern: roleInfo.extern,
			native: roleInfo.native,
			origin: split.origin,
		};
	}

	const valueMatch = split.body.match(/^(.*?)\s+([^:]+?)\s*:\s*(\S+)\s*$/);
	if (valueMatch) {
		const roleInfo = classifyRole(valueMatch[1]);
		const type = valueMatch[3].toLowerCase();
		const nameSpec = valueMatch[2].trim();
		const arrayMatch = nameSpec.match(/^([^\[]+)\[(.*)\]$/);
		if (arrayMatch) {
			const sizeText = arrayMatch[2].trim();
			return {
				kind: "array",
				name: arrayMatch[1].trim(),
				size: sizeText === "" ? undefined : sizeText,
				category: type,
				extern: roleInfo.extern,
				role: roleInfo.role,
				origin: split.origin,
			};
		}

		const role = roleInfo.role || "global";
		const name = nameSpec;
		if (role === "const") {
			return {
				kind: "const",
				name,
				category: type,
				extern: roleInfo.extern,
				role,
				origin: split.origin,
			};
		}
		return {
			kind: "global",
			name,
			category: type,
			extern: roleInfo.extern,
			role,
			origin: split.origin,
		};
	}

	return null;
}

function parseExpectsComment(comment) {
	if (!comment.startsWith("expects ")) {
		return null;
	}

	const payload = comment.substr("expects ".length).trim();
	const split = splitOrigin(payload);
	const match = split.body.match(/^([^\s(]+)\s*\(([^)]*)\)\s*->\s*(\S+)\s*$/);
	if (!match) {
		return null;
	}

	return {
		name: match[1],
		params: parseParamTypes(match[2]),
		paramDisplay: normalizeParamDisplay(match[2]),
		returns: match[3].toLowerCase(),
		origin: split.origin,
	};
}

function detectCallInfo(line, commentIndex) {
	const prefix = commentIndex >= 0 ? line.slice(0, commentIndex) : line;
	const upper = prefix.toUpperCase();
	const callIndex = upper.lastIndexOf("CALL");
	if (callIndex === -1) {
		return { native: null };
	}

	let cursor = callIndex + 4;
	while (cursor < prefix.length && /\s/.test(prefix[cursor])) {
		cursor += 1;
	}
	if (cursor >= prefix.length) {
		return { native: null };
	}

	const remainder = prefix.slice(cursor);
	const match = remainder.match(/^([^\s]+)/);
	if (!match) {
		return { native: null };
	}

	const token = match[1];
	if (token.startsWith("^")) {
		return { native: true };
	}

	return { native: false };
}

function location(file, line, originText) {
	return { file, line, origin: parseOriginMarker(originText) };
}

function loadNativeManifest(ctx, manifestPath) {
	if (!manifestPath) {
		return;
	}

	let data;
	try {
		data = fs.readFileSync(manifestPath, "utf8");
	} catch (err) {
		if (err && err.code === "ENOENT") {
			return;
		}
		console.error(`Failed to read native manifest ${manifestPath}: ${err.message || err}`);
		ctx.hadReadError = true;
		return;
	}

	const lines = data.split(/\r?\n/);
	lines.forEach((line, index) => {
		const trimmed = line.trim();
		if (!trimmed.startsWith(";")) {
			return;
		}

		const comment = trimmed.slice(1).trim();
		if (!comment.toLowerCase().startsWith("signature ")) {
			return;
		}

		const parsed = parseSignatureComment(comment);
		if (!parsed || parsed.kind !== "function") {
			return;
		}

		parsed.native = true;
		const loc = location(manifestPath, index + 1, parsed.origin);
		addDefinitionRecord(ctx, parsed, loc, "native manifest", true);
	});
}

function addDefinitionRecord(ctx, parsed, loc, kind, nativeOverride) {
	if (!parsed.name) {
		return;
	}

	const native = nativeOverride != null ? nativeOverride : parsed.native;
	if (!ctx.definitions.has(parsed.name)) {
		ctx.definitions.set(parsed.name, []);
	}
	ctx.definitions.get(parsed.name).push({
		signature: {
			params: parsed.params,
			returns: parsed.returns,
			wildcard: parsed.wildcard,
			paramDisplay: parsed.paramDisplay,
		},
		location: loc,
		kind,
		native: !!native,
	});
}

function addFunctionExport(ctx, parsed, loc) {
	const signature = {
		params: parsed.params,
		returns: parsed.returns,
		wildcard: parsed.wildcard,
		paramDisplay: parsed.paramDisplay,
	};
	const existing = ctx.exports.functions.get(parsed.name);
	if (!existing) {
		ctx.exports.functions.set(parsed.name, {
			signature,
			locations: [loc],
			native: !!parsed.native,
		});
		addDefinitionRecord(ctx, parsed, loc, "definition");
		return;
	}

	if (!functionSignaturesCompatible(existing.signature, signature)) {
		ctx.diagnostics.push({
			severity: "error",
			message: `Conflicting definitions for function ${parsed.name}`,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin,
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin,
				},
			],
		});
		return;
	}

	if (existing.native !== undefined && existing.native !== !!parsed.native) {
		ctx.diagnostics.push({
			severity: "error",
			message: `Conflicting native declarations for function ${parsed.name}`,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin,
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin,
				},
			],
		});
		return;
	}

	existing.native = existing.native != null ? existing.native : !!parsed.native;
	existing.locations.push(loc);
	addDefinitionRecord(ctx, parsed, loc, "definition");
}

function addFunctionImport(ctx, parsed, loc) {
	const record = {
		signature: {
			params: parsed.params,
			returns: parsed.returns,
			wildcard: parsed.wildcard,
			paramDisplay: parsed.paramDisplay,
		},
		location: loc,
		native: parsed.native,
		kind: parsed.native ? "extern native" : "extern",
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
	const record = {
		category: parsed.category,
		role: parsed.role,
		location: loc,
	};
	const existing = ctx.exports.globals.get(parsed.name);
	if (!existing) {
		ctx.exports.globals.set(parsed.name, {
			signature: record,
			locations: [loc],
		});
		return;
	}

	if (!typesCompatible(existing.signature.category, record.category)) {
		ctx.diagnostics.push({
			severity: "error",
			message: `Conflicting definitions for global ${parsed.name}`,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin,
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin,
				},
			],
		});
		return;
	}

	existing.locations.push(loc);
}

function addGlobalImport(ctx, parsed, loc) {
	const record = { category: parsed.category, role: parsed.role };
	if (!ctx.externs.globals.has(parsed.name)) {
		ctx.externs.globals.set(parsed.name, []);
	}
	ctx.externs.globals.get(parsed.name).push({ signature: record, location: loc });
}

function addConstExport(ctx, parsed, loc) {
	const record = {
		category: parsed.category,
		role: parsed.role,
		location: loc,
	};
	const existing = ctx.exports.consts.get(parsed.name);
	if (!existing) {
		ctx.exports.consts.set(parsed.name, {
			signature: record,
			locations: [loc],
		});
		return;
	}

	if (!typesCompatible(existing.signature.category, record.category)) {
		ctx.diagnostics.push({
			severity: "error",
			message: `Conflicting definitions for const ${parsed.name}`,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin,
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin,
				},
			],
		});
		return;
	}

	existing.locations.push(loc);
}

function addConstImport(ctx, parsed, loc) {
	const record = { category: parsed.category, role: parsed.role };
	if (!ctx.externs.consts.has(parsed.name)) {
		ctx.externs.consts.set(parsed.name, []);
	}
	ctx.externs.consts.get(parsed.name).push({ signature: record, location: loc });
}

function addArrayExport(ctx, parsed, loc) {
	const record = {
		category: parsed.category,
		size: parsed.size,
		role: parsed.role,
		location: loc,
	};
	const existing = ctx.exports.arrays.get(parsed.name);
	if (!existing) {
		ctx.exports.arrays.set(parsed.name, {
			signature: record,
			locations: [loc],
		});
		return;
	}

	if (!arraySignaturesCompatible(existing.signature, record)) {
		ctx.diagnostics.push({
			severity: "error",
			message: `Conflicting definitions for array ${parsed.name}`,
			locations: [
				{
					label: "previous definition",
					file: existing.locations[0].file,
					line: existing.locations[0].line,
					origin: existing.locations[0].origin,
				},
				{
					label: "redefinition",
					file: loc.file,
					line: loc.line,
					origin: loc.origin,
				},
			],
		});
		return;
	}

	existing.locations.push(loc);
}

function addArrayImport(ctx, parsed, loc) {
	const record = {
		category: parsed.category,
		size: parsed.size,
		role: parsed.role,
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
		native: callInfo ? callInfo.native : null,
	});
}

function typesCompatible(a, b) {
	if (a == null || b == null) {
		return true;
	}
	if (a === "unknown" || b === "unknown") {
		return true;
	}
	return a === b;
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
	const paramCount = Math.max(a.params ? a.params.length : 0, b.params ? b.params.length : 0);
	for (let i = 0; i < paramCount; ++i) {
		const left = a.params ? a.params[i] : undefined;
		const right = b.params ? b.params[i] : undefined;
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
	let stats;
	try {
		stats = fs.statSync(filePath);
	} catch (err) {
		console.error(`Unable to read ${filePath}: ${err.message}`);
		ctx.hadReadError = true;
		return;
	}

	if (stats.isDirectory()) {
		const entries = fs.readdirSync(filePath);
		entries.forEach((entry) => {
			const child = path.join(filePath, entry);
			processFile(child, ctx);
		});
		return;
	}

	if (!stats.isFile()) {
		return;
	}

	let lines;
	try {
		lines = readFileLines(filePath);
	} catch (err) {
		console.error(`Unable to read ${filePath}: ${err.message}`);
		ctx.hadReadError = true;
		return;
	}

	for (let idx = 0; idx < lines.length; ++idx) {
		const line = lines[idx];
		const lineNumber = idx + 1;

		const sigIdx = line.indexOf("; signature");
		if (sigIdx !== -1) {
			const comment = line.substr(sigIdx + 1).trim();
			const parsed = parseSignatureComment(comment);
			if (parsed) {
				recordSignature(ctx, parsed, location(filePath, lineNumber, parsed.origin));
			}
		}

		const expectsIdx = line.indexOf("; expects");
		if (expectsIdx !== -1) {
			const comment = line.substr(expectsIdx + 1).trim();
			const parsed = parseExpectsComment(comment);
			if (parsed) {
				const callInfo = detectCallInfo(line, expectsIdx);
				addCallExpectation(ctx, parsed, location(filePath, lineNumber, parsed.origin), callInfo);
			}
		}
	}
}

function compareExternSets(kind, externMap, exportMap, ctx, comparator, mismatchMessage, missingMessage) {
	const missingFn =
		typeof missingMessage === "function"
			? missingMessage
			: typeof mismatchMessage === "function"
				? mismatchMessage
				: () => "Missing extern definition";

	for (const [name, entries] of externMap.entries()) {
		if (entries.length > 1) {
			for (let i = 0; i < entries.length; ++i) {
				for (let j = i + 1; j < entries.length; ++j) {
					const hasNativeA = Object.prototype.hasOwnProperty.call(entries[i], "native");
					const hasNativeB = Object.prototype.hasOwnProperty.call(entries[j], "native");
					if (hasNativeA && hasNativeB && entries[i].native !== entries[j].native) {
						ctx.diagnostics.push({
							severity: "error",
							message: `${kind} ${name} has conflicting native declarations`,
							locations: [
								{
									label: "first declaration",
									file: entries[i].location.file,
									line: entries[i].location.line,
									origin: entries[i].location.origin,
								},
								{
									label: "second declaration",
									file: entries[j].location.file,
									line: entries[j].location.line,
									origin: entries[j].location.origin,
								},
							],
						});
						continue;
					}
					if (!comparator(entries[i], entries[j])) {
						ctx.diagnostics.push({
							severity: "error",
							message: `${kind} ${name} has conflicting extern declarations`,
							locations: [
								{
									label: "first declaration",
									file: entries[i].location.file,
									line: entries[i].location.line,
									origin: entries[i].location.origin,
								},
								{
									label: "second declaration",
									file: entries[j].location.file,
									line: entries[j].location.line,
									origin: entries[j].location.origin,
								},
							],
						});
					}
				}
			}
		}

		const exportEntry = exportMap.get(name);
		if (exportEntry) {
			entries.forEach((entry) => {
				if (entry.native) {
					ctx.diagnostics.push({
						severity: "error",
						message: `${kind} ${name} is declared native but defined in Impala`,
						locations: [
							{
								label: "definition",
								file: exportEntry.locations[0].file,
								line: exportEntry.locations[0].line,
								origin: exportEntry.locations[0].origin,
							},
							{
								label: "extern declaration",
								file: entry.location.file,
								line: entry.location.line,
								origin: entry.location.origin,
							},
						],
					});
					return;
				}
				if (!comparator({ signature: exportEntry.signature }, { signature: entry.signature })) {
					ctx.diagnostics.push({
						severity: "error",
						message: `${kind} ${name} does not match its definition`,
						locations: [
							{
								label: "definition",
								file: exportEntry.locations[0].file,
								line: exportEntry.locations[0].line,
								origin: exportEntry.locations[0].origin,
							},
							{
								label: "extern declaration",
								file: entry.location.file,
								line: entry.location.line,
								origin: entry.location.origin,
							},
						],
					});
				}
			});
			continue;
		}

		const skipMissing = entries.every((entry) => entry.native);
		if (!skipMissing) {
			ctx.diagnostics.push({
				severity: "warning",
				message: missingFn(name),
				locations: entries.map((entry) => ({
					label: "extern declaration",
					file: entry.location.file,
					line: entry.location.line,
					origin: entry.location.origin,
				})),
			});
		}
	}
}

function comparatorWrapper(entryA, entryB, fn) {
	return fn(entryA.signature, entryB.signature);
}

function formatSignatureForMessage(name, signature) {
	const fnName = name || "unknown";
	if (!signature) {
		return `${fnName}() -> unknown`;
	}
	const returns = signature.returns || "unknown";
	let params = "";
	if (signature.paramDisplay) {
		params = signature.paramDisplay;
	} else if (signature.params && signature.params.length > 0) {
		params = signature.params.join(", ");
	}
	return `${fnName}(${params}) -> ${returns}`;
}

function isNativeCompatible(callNative, recordNative) {
	if (callNative == null) {
		return true;
	}
	return !!recordNative === callNative;
}

function validateCalls(ctx) {
	for (const [name, callEntries] of ctx.calls.entries()) {
		if (!callEntries || callEntries.length === 0) {
			continue;
		}

		for (let i = 0; i < callEntries.length; ++i) {
			for (let j = i + 1; j < callEntries.length; ++j) {
				if (!functionSignaturesCompatible(callEntries[i].signature, callEntries[j].signature)) {
					const leftReturn = callEntries[i].signature.returns || "unknown";
					const rightReturn = callEntries[j].signature.returns || "unknown";
					ctx.diagnostics.push({
						severity: "error",
						message: `Conflicting return type expectations for "${name}" (${leftReturn} vs ${rightReturn})`,
						locations: [
							{
								label: "call site",
								file: callEntries[i].location.file,
								line: callEntries[i].location.line,
								origin: callEntries[i].location.origin,
							},
							{
								label: "call site",
								file: callEntries[j].location.file,
								line: callEntries[j].location.line,
								origin: callEntries[j].location.origin,
							},
						],
					});
				}
			}
		}

		const definitionRecords = ctx.definitions.get(name) || [];
		const externRecords = (ctx.externs.functions.get(name) || []).filter((entry) => !entry.native);

		callEntries.forEach((call) => {
			const matchingDefinitions = definitionRecords.filter((def) => functionSignaturesCompatible(def.signature, call.signature));
			const matchingExterns = externRecords.filter((entry) => functionSignaturesCompatible(entry.signature, call.signature));

			if (matchingDefinitions.length > 0) {
				const nativeMatch = matchingDefinitions.find((def) => isNativeCompatible(call.native, def.native));
				if (!nativeMatch) {
					const mismatch = matchingDefinitions[0];
					const expected = call.native ? "native" : "non-native";
					const actual = mismatch.native ? "native" : "non-native";
					ctx.diagnostics.push({
						severity: "error",
						message: `Call to ${name} uses ${expected} calling convention but definition is ${actual}`,
						locations: [
							{
								label: "definition",
								file: mismatch.location.file,
								line: mismatch.location.line,
								origin: mismatch.location.origin,
							},
							{
								label: "call site",
								file: call.location.file,
								line: call.location.line,
								origin: call.location.origin,
							},
						],
					});
				}
				return;
			}

			if (definitionRecords.length > 0) {
				const mismatch = definitionRecords[0];
				ctx.diagnostics.push({
					severity: "error",
					message: `Signature mismatch for "${name}": call expects "${formatSignatureForMessage(name, call.signature)}" but definition provides "${formatSignatureForMessage(name, mismatch.signature)}"`,
					locations: [
						{
							label: "definition",
							file: mismatch.location.file,
							line: mismatch.location.line,
							origin: mismatch.location.origin,
						},
						{
							label: "call site",
							file: call.location.file,
							line: call.location.line,
							origin: call.location.origin,
						},
					],
				});
				return;
			}

			if (matchingExterns.length > 0) {
				const nativeMatch = matchingExterns.find((entry) => isNativeCompatible(call.native, entry.native));
				if (!nativeMatch) {
					const mismatch = matchingExterns[0];
					const expected = call.native ? "native" : "non-native";
					const actual = mismatch.native ? "native" : "non-native";
					ctx.diagnostics.push({
						severity: "error",
						message: `Call to ${name} uses ${expected} calling convention but extern declaration is ${actual}`,
						locations: [
							{
								label: "extern declaration",
								file: mismatch.location.file,
								line: mismatch.location.line,
								origin: mismatch.location.origin,
							},
							{
								label: "call site",
								file: call.location.file,
								line: call.location.line,
								origin: call.location.origin,
							},
						],
					});
					return;
				}
				return;
			}

			const externEntries = ctx.externs.functions.get(name);
			if (externEntries && externEntries.length > 0) {
				ctx.diagnostics.push({
					severity: "error",
					message: `Call to ${name} does not match extern declaration`,
					locations: [
						{
							label: "extern declaration",
							file: externEntries[0].location.file,
							line: externEntries[0].location.line,
							origin: externEntries[0].location.origin,
						},
						{
							label: "call site",
							file: call.location.file,
							line: call.location.line,
							origin: call.location.origin,
						},
					],
				});
				return;
			}

			ctx.diagnostics.push({
				severity: "warning",
				message: `No signature metadata found for function ${name}`,
				locations: [
					{
						label: "call site",
						file: call.location.file,
						line: call.location.line,
						origin: call.location.origin,
					},
				],
			});
		});
	}
}

function validateContext(ctx) {
	compareExternSets(
		"Function",
		ctx.externs.functions,
		ctx.exports.functions,
		ctx,
		(a, b) => comparatorWrapper(a, b, functionSignaturesCompatible),
		(name) => `No definition found for extern function ${name}`,
	);

	compareExternSets(
		"Global",
		ctx.externs.globals,
		ctx.exports.globals,
		ctx,
		(a, b) => comparatorWrapper(a, b, (left, right) => typesCompatible(left.category || "unknown", right.category || "unknown")),
		(name) => `No definition found for extern global ${name}`,
	);

	compareExternSets(
		"Const",
		ctx.externs.consts,
		ctx.exports.consts,
		ctx,
		(a, b) => comparatorWrapper(a, b, (left, right) => typesCompatible(left.category || "unknown", right.category || "unknown")),
		(name) => `No definition found for extern const ${name}`,
	);

	compareExternSets(
		"Array",
		ctx.externs.arrays,
		ctx.exports.arrays,
		ctx,
		(a, b) => comparatorWrapper(a, b, arraySignaturesCompatible),
		(name) => `No definition found for extern array ${name}`,
	);

	validateCalls(ctx);
}

function formatOriginText(origin) {
	if (!origin) {
		return null;
	}

	const parts = [];
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
	const base = `${loc.file}:${loc.line}`;
	const originText = formatOriginText(loc.origin);
	if (originText) {
		return `${base} (origin ${originText})`;
	}
	return base;
}

function reportDiagnostics(ctx) {
	if (ctx.hadReadError) {
		return 1;
	}

	let hadError = false;
	ctx.diagnostics.forEach((diag) => {
		let severity = diag.severity;
		if (severity === "warning" && ctx.options.force) {
			severity = "error";
		} else if (severity === "error" && ctx.options.warnOnly) {
			severity = "warning";
		}

		const label = severity.toUpperCase();
		console.error(`${label}: ${diag.message}`);
		if (diag.locations) {
			diag.locations.forEach((loc) => {
				const prefix = loc.label ? `  ${loc.label}:` : "  at";
				console.error(`${prefix} ${formatLocation(loc)}`);
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

(function main() {
	const { options, files } = parseArgs(process.argv.slice(2));
	const ctx = createContext(options);
	loadNativeManifest(ctx, DEFAULT_NATIVE_MANIFEST);
	files.forEach((file) => {
		processFile(file, ctx);
	});
	validateContext(ctx);
	const exitCode = reportDiagnostics(ctx);
	process.exit(exitCode);
})();
