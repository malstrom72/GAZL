'use strict';

const fs = require('fs');
const path = require('path');

const { compileWithJsImpala } = require('./impalaJsCompilerRunner');

const args = process.argv.slice(2);
const makeGold = args.some((arg) => arg === 'makegold' || arg === '--makegold');

const IMPALA_ENCODING = 'latin1';
const RANDOM_ID = 0x4d2;

const repoRoot = path.resolve(__dirname, '..');
const testsDir = path.join(repoRoot, 'tests', 'impala');
const sourcesDir = path.join(testsDir, 'sources');
const goldenDir = path.join(testsDir, 'golden');
const erroneousDir = path.join(testsDir, 'erroneous');

function readImpalaFile(filePath) {
        return fs.readFileSync(filePath, IMPALA_ENCODING);
}

function canonicalizeNewlines(contents) {
        return contents.replace(/\r\n?/g, '\n');
}

function readImpalaSource(filePath) {
        return canonicalizeNewlines(readImpalaFile(filePath));
}

function writeImpalaFile(filePath, contents) {
        fs.writeFileSync(filePath, contents, IMPALA_ENCODING);
}

function ensureErroneousDir() {
        if (!fs.existsSync(erroneousDir)) {
                fs.mkdirSync(erroneousDir, { recursive: true });
        }
}

function formatError(err) {
        if (!err) {
                return 'Unknown error';
        }
        if (err.stack) {
                return err.stack;
        }
        if (err.message) {
                return err.message;
        }
        return String(err);
}

function main() {
        let totalFiles = 0;
        let errorCount = 0;

        const sourceFiles = fs
                .readdirSync(sourcesDir)
                .filter((file) => file.endsWith('.impala'))
                .sort();

        for (const file of sourceFiles) {
                const name = path.basename(file, '.impala');
                const sourcePath = path.join(sourcesDir, file);
                const goldenPath = path.join(goldenDir, `${name}.gazl`);

                console.log(`Compiling ${file}`);

                let source;
                try {
                        source = readImpalaSource(sourcePath);
                } catch (err) {
                        console.error('<<< Error reading source >>>');
                        console.error(formatError(err));
                        errorCount += 1;
                        totalFiles += 1;
                        continue;
                }

                let output;
                try {
                        output = compileWithJsImpala(source, { randomId: RANDOM_ID, retabulate: false });
                } catch (err) {
                        console.error('<<< Error compiling >>>');
                        console.error(formatError(err));
                        errorCount += 1;
                        totalFiles += 1;
                        continue;
                }

                if (makeGold) {
                        writeImpalaFile(goldenPath, output);
                        console.log(`Updated ${path.relative(repoRoot, goldenPath)}`);
                        totalFiles += 1;
                        continue;
                }

                let expected;
                try {
                        expected = readImpalaFile(goldenPath);
                } catch (err) {
                        console.error('<<< Missing golden >>>');
                        console.error(formatError(err));
                        errorCount += 1;
                        totalFiles += 1;
                        continue;
                }

                if (canonicalizeNewlines(expected) !== canonicalizeNewlines(output)) {
                        console.error('<<< Output differs! >>>');
                        ensureErroneousDir();
                        const erroneousPath = path.join(erroneousDir, `${name}.gazl`);
                        writeImpalaFile(erroneousPath, output);
                        console.error(`Wrote actual output to ${path.relative(repoRoot, erroneousPath)}`);
                        errorCount += 1;
                } else {
                        console.log('OK');
                }

                totalFiles += 1;
        }

        console.log('');
        console.log(`Total errors: ${errorCount} / ${totalFiles}`);

        if (errorCount !== 0) {
                process.exit(1);
        }
}

main();
