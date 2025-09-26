#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
const os = require("os");
const { spawnSync } = require("child_process");

const repoRoot = path.resolve(__dirname, "..");
const validatorScript = path.join(repoRoot, "tools", "gazl-validate.js");

function assertCondition(condition, message) {
	if (!condition) {
		console.error(message);
		process.exit(1);
	}
}

function writeFixture(dir, name, lines) {
	const filePath = path.join(dir, name);
	const text = lines.join("\n");
	fs.writeFileSync(filePath, text + (text.endsWith("\n") ? "" : "\n"));
	return filePath;
}

function runValidator(args) {
	const result = spawnSync(process.execPath, [validatorScript].concat(args), {
		encoding: "utf8",
	});
	if (result.error) {
		throw result.error;
	}
	return result;
}

function main() {
	const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "gazl-validator-tests-"));
	try {
		const exportFile = writeFixture(tempDir, "moduleA.gazl", [
			"; signatures version=1",
			"FUNC foo\t; signature func foo(int, ptr) -> int",
			"\tRETU",
		]);
		const importFile = writeFixture(tempDir, "moduleB.gazl", ["; signatures version=1", "CALL foo\t; expects foo(int, ptr) -> int"]);

		const success = runValidator([exportFile, importFile]);
		assertCondition(success.status === 0, "validator should exit cleanly for matching fixtures");
		assertCondition(success.stderr.trim().length === 0, "validator reported diagnostics for matching fixtures");

		const externOnly = writeFixture(tempDir, "externStub.gazl", ["; signatures version=1", "; signature extern func add() -> unknown"]);
		const externDefinition = writeFixture(tempDir, "externDef.gazl", [
			"; signatures version=1",
			"FUNC add\t; signature func add(int, int) -> int",
			"\tRETU",
		]);
		const externCall = writeFixture(tempDir, "externCall.gazl", ["; signatures version=1", "CALL add\t; expects add(int, int) -> int"]);
		const externResult = runValidator([externOnly, externDefinition, externCall]);
		assertCondition(externResult.status === 0, "bare extern metadata should merge with later definitions");
		assertCondition(externResult.stderr.trim().length === 0, "validator should remain silent when extern placeholders match definitions");

		const warningFile = writeFixture(tempDir, "moduleWarning.gazl", [
			"; signatures version=1",
			"CALL missing\t; expects missing(int) -> void",
		]);
		const warning = runValidator([warningFile]);
		assertCondition(warning.status === 0, "validator warnings should not trigger non-zero exit");
		assertCondition(/WARNING:/.test(warning.stderr), "validator did not emit expected warning for missing metadata");

		const mismatchFile = writeFixture(tempDir, "moduleMismatch.gazl", [
			"; signatures version=1",
			"CALL foo\t; expects foo(ptr, ptr) -> int",
		]);
		const failure = runValidator([exportFile, mismatchFile]);
		assertCondition(failure.status === 1, "validator should exit with failure for mismatched fixtures");
		assertCondition(/Signature mismatch for "foo"/.test(failure.stderr), "validator did not report expected mismatch error");

		console.log("gazl-validator unit tests passed");
	} finally {
		fs.rmSync(tempDir, { recursive: true, force: true });
	}
}

if (require.main === module) {
	try {
		main();
	} catch (err) {
		console.error(err && err.stack ? err.stack : String(err));
		process.exit(1);
	}
}
