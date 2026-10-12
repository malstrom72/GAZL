'use strict';

const fs = require('fs');
const path = require('path');

const { compileWithJsImpala } = require('./impalaJsCompilerRunner');
const { gazlCmd, haveGazlCmd, parseExpectedRun, runExpected, parseExpectedFailure, runExpectedFailure,
        assembleOnly, NEEDS_HOST }
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
/* The conformance suite: one small program per section of docs/impala/Impala.md, its golden beside it.
   See tests/impala/conformance/README.md. */
const conformanceDir = path.join(testsDir, 'conformance');
const referencePath = path.join(repoRoot, 'docs', 'impala', 'Impala.md');

/* A heading's anchor as GitHub renders it: lower case, punctuation other than `-` dropped, spaces to `-`. */
function headingAnchor(text) {
        return text.toLowerCase().replace(/[^a-z0-9 _-]/g, '').replace(/ /g, '-');
}

/* Every language section of the reference must be claimed by a conformance program (`Conformance:
   Impala.md#anchor` in its header) or listed as exempt in the suite's README with a reason. A section is
   every heading from "Lexical elements" up to "The Impala tools", except a `##` group heading whose
   subsections carry the content. Returns the number of problems, each one printed. */
function checkConformanceCoverage(sources) {
        const lines = fs.readFileSync(referencePath, 'utf8').split(/\r?\n/);
        const headings = [];
        let inLanguage = false;
        let inFence = false;
        for (const line of lines) {
                if (/^```/.test(line)) {
                        inFence = !inFence;
                        continue;
                }
                const m = !inFence && line.match(/^(#{2,4}) (.*)$/);
                if (!m) {
                        continue;
                }
                if (m[2] === 'Lexical elements') {
                        inLanguage = true;
                } else if (m[2] === 'The Impala tools') {
                        inLanguage = false;
                }
                if (inLanguage) {
                        headings.push({ level: m[1].length, anchor: headingAnchor(m[2]) });
                }
        }
        const wanted = headings.filter((h, i) => h.level > 2
                        || !(i + 1 < headings.length && headings[i + 1].level > h.level));
        const claimed = {};
        for (const source of sources) {
                const m = source.text.match(/Conformance: Impala\.md#([a-z0-9_-]+)/);
                if (m) {
                        claimed[m[1]] = source.file;
                }
        }
        const readme = fs.readFileSync(path.join(conformanceDir, 'README.md'), 'utf8');
        const exempt = {};
        const exemptPattern = /^- `Impala\.md#([a-z0-9_-]+)`/gm;
        for (let m = exemptPattern.exec(readme); m; m = exemptPattern.exec(readme)) {
                exempt[m[1]] = true;
        }
        let problems = 0;
        const known = {};
        for (const h of headings) {
                known[h.anchor] = true;
        }
        for (const h of wanted) {
                if (claimed[h.anchor] === undefined && !exempt[h.anchor]) {
                        console.error(`<<< Impala.md#${h.anchor} has no conformance program and is not exempt >>>`);
                        problems += 1;
                }
        }
        for (const anchor of Object.keys(claimed).concat(Object.keys(exempt))) {
                if (!known[anchor]) {
                        console.error(`<<< Impala.md#${anchor} names no section of the language reference >>>`);
                        problems += 1;
                }
        }
        console.log(`Conformance: ${wanted.length} reference sections, ${Object.keys(claimed).length} claimed, `
                        + `${Object.keys(exempt).length} exempt, ${problems} problems`);
        return problems;
}

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
        let errorCount = 0;
        let ranCount = 0;
        let assembledCount = 0;
        let compileOnlyCount = 0;
        const gazlCmdBuilt = haveGazlCmd();
        if (!gazlCmdBuilt && !makeGold && !skipRun) {
                console.log(`(no ${path.relative(repoRoot, gazlCmd)} - skipping assemble+run checks)`);
        }

        const sourceFiles = [];
        for (const suite of [ { dir: sourcesDir, golden: goldenDir }, { dir: conformanceDir, golden: conformanceDir } ]) {
                const files = fs.readdirSync(suite.dir).filter((file) => file.endsWith('.impala')).sort();
                for (const file of files) {
                        sourceFiles.push({ file: file, dir: suite.dir, golden: suite.golden });
                }
        }
        errorCount += checkConformanceCoverage(sourceFiles.filter((entry) => entry.dir === conformanceDir)
                        .map((entry) => ({ file: entry.file, text: readImpalaSource(path.join(entry.dir, entry.file)) })));

        for (const entry of sourceFiles) {
                const file = entry.file;
                const name = path.basename(file, '.impala');
                const sourcePath = path.join(entry.dir, file);
                const goldenPath = path.join(entry.golden, `${name}.gazl`);

                console.log(`Compiling ${file}`);

                let source;
                try {
                        source = readImpalaSource(sourcePath);
                } catch (err) {
                        console.error('<<< Error reading source >>>');
                        console.error(formatError(err));
                        errorCount += 1;
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
                        continue;
                }

                /* Writing the golden REPLACES the only record of what this compiler should emit, so it is
                   the one moment the assemble and expected-run checks below matter most - they are what
                   catches a golden that faithfully records corrupt output. Hence write and fall through
                   rather than `continue`. */
                if (makeGold) {
                        writeImpalaFile(goldenPath, output);
                        console.log(`Updated ${path.relative(repoRoot, goldenPath)}`);
                } else {
                        let expected;
                        try {
                                expected = readImpalaFile(goldenPath);
                        } catch (err) {
                                console.error('<<< Missing golden >>>');
                                console.error(formatError(err));
                                errorCount += 1;
                                continue;
                        }

                        /* `output` is the compiler's own \n-only text and `expected` was just read raw, so
                           only the golden side can carry CRLF. */
                        if (canonicalizeNewlines(expected) !== output) {
                                console.error('<<< Output differs! >>>');
                                ensureErroneousDir();
                                const erroneousPath = path.join(erroneousDir, `${name}.gazl`);
                                writeImpalaFile(erroneousPath, output);
                                console.error(`Wrote actual output to ${path.relative(repoRoot, erroneousPath)}`);
                                errorCount += 1;
                                continue;
                        }
                }

                const expectedRun = parseExpectedRun(source);
                if (skipRun || !gazlCmdBuilt) {
                        console.log('OK');
                        continue;
                }

                if (!expectedRun) {
                        const verdict = assembleOnly(goldenPath);
                        if (verdict === NEEDS_HOST) {
                                console.log('OK (compile-only)');
                                compileOnlyCount += 1;
                        } else if (verdict !== undefined) {
                                console.error(`<<< ${verdict} >>>`);
                                errorCount += 1;
                        } else {
                                console.log('OK (assembled)');
                                assembledCount += 1;
                        }
                        continue;
                }

                const expectedFailure = parseExpectedFailure(source);
                if (expectedFailure) {
                        const stopped = runExpectedFailure(goldenPath, expectedFailure);
                        if (stopped) {
                                console.error(`<<< ${stopped} >>>`);
                                errorCount += 1;
                                continue;
                        }
                }
                const failure = runExpected(goldenPath, expectedRun);
                if (failure) {
                        console.error(`<<< ${failure} >>>`);
                        errorCount += 1;
                } else {
                        console.log(`OK (ran: ${expectedRun.want.join(' ')})`);
                        ranCount += 1;
                }
        }

        console.log('');
        console.log(`Assembled and ran: ${ranCount}`);
        console.log(`Assembled only: ${assembledCount}`);
        console.log(`Compiled but NOT link-checked (needs a host): ${compileOnlyCount}`);
        console.log(`Total errors: ${errorCount} / ${sourceFiles.length}`);

        if (errorCount !== 0) {
                process.exit(1);
        }
}

main();
