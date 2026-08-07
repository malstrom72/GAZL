'use strict';

const fs = require('fs');
const path = require('path');

const { compileWithJsImpala } = require('./impalaJsCompilerRunner');
const { gazlCmd, haveGazlCmd, parseExpectedRun, runExpected, assembleOnly, NEEDS_HOST }
        = require('./gazlAssembleCheck');

const args = process.argv.slice(2);
const makeGold = args.some((arg) => arg === 'makegold' || arg === '--makegold');
const skipRun = args.some((arg) => arg === '--no-run');

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

/* Goldens that cannot load under GAZLCmd for reasons that are not the compiler's. Empty since E444
   made an out-of-range `case` a compile error: switchtest was the sole entry, and it was here because
   the compiler accepted case values that folded to labels the assembler then rejected. */
const KNOWN_UNLOADABLE = {};

function main() {
        let totalFiles = 0;
        let errorCount = 0;
        let ranCount = 0;
        let assembledCount = 0;
        let compileOnlyCount = 0;
        const gazlCmdBuilt = haveGazlCmd();
        if (!gazlCmdBuilt && !makeGold && !skipRun) {
                console.log(`(no ${path.relative(repoRoot, gazlCmd)} - skipping assemble+run checks)`);
        }

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
                        /* `sourceName` so a golden shows what a user actually gets - the CLI passes the
                           basename too. Without it the goldens were the one place rows carried no file
                           name, which is part of how the option stayed provably dead unnoticed. */
                        output = compileWithJsImpala(source, { randomId: RANDOM_ID, retabulate: false,
                                        sourceName: file });
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
                        totalFiles += 1;
                        continue;
                }

                const expectedRun = parseExpectedRun(source);
                if (skipRun || !gazlCmdBuilt) {
                        console.log('OK');
                        totalFiles += 1;
                        continue;
                }

                if (!expectedRun) {
                        const excuse = KNOWN_UNLOADABLE[name];
                        const verdict = (excuse ? NEEDS_HOST : assembleOnly(goldenPath));
                        if (excuse) {
                                console.log(`OK (not assembled - ${excuse})`);
                        } else if (verdict === NEEDS_HOST) {
                                console.log('OK (compile-only)');
                                compileOnlyCount += 1;
                        } else if (verdict !== undefined) {
                                console.error(`<<< ${verdict} >>>`);
                                errorCount += 1;
                        } else {
                                console.log('OK (assembled)');
                                assembledCount += 1;
                        }
                        totalFiles += 1;
                        continue;
                }

                const failure = runExpected(goldenPath, expectedRun);
                if (failure) {
                        console.error(`<<< ${failure} >>>`);
                        errorCount += 1;
                } else {
                        console.log(`OK (ran: ${expectedRun.want.join(' ')})`);
                        ranCount += 1;
                }

                totalFiles += 1;
        }

        console.log('');
        console.log(`Assembled and ran: ${ranCount}`);
        console.log(`Assembled only: ${assembledCount}`);
        console.log(`Compiled but NOT link-checked (needs a host): ${compileOnlyCount}`);
        console.log(`Total errors: ${errorCount} / ${totalFiles}`);

        if (errorCount !== 0) {
                process.exit(1);
        }
}

main();
