'use strict';

const fs = require('fs');
const path = require('path');
const childProcess = require('child_process');

const { compileWithJsImpala } = require('./impalaJsCompilerRunner');

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

const gazlCmd = path.join(repoRoot, 'output',
        process.platform === 'win32' ? 'GAZLCmd.exe' : 'GAZLCmd');

/* A fixture opts in to being ASSEMBLED AND RUN by declaring, in its header comment:
        Expected (GAZLCmd <name>.gazl <entry> [<define> <value> ...]): <whitespace-separated output>
   The trailing defines feed GAZLCmd's own `<define symbol> <define value>` arguments, which is how an
   `extern struct` fixture supplies the host-owned layout it compiled against. Without such a line a
   fixture is compile-only, so the byte-diff above is the whole check for it.

   This exists because compiling clean does NOT mean the GAZL assembles: a struct field's folded extent
   used to be emitted after the layout block that read it, which every golden and a million fuzzed
   programs missed because nothing ever fed a golden to the assembler. */
function parseExpectedRun(source) {
        const match = source.match(/Expected \(GAZLCmd ([^)]*)\):[ \t]*(.*)/);
        if (!match) { return undefined; }
        const parts = match[1].trim().split(/\s+/);
        return { args: parts.slice(1), want: match[2].trim().split(/\s+/).filter(Boolean) };
}

function runGolden(goldenPath, expectedRun) {
        const result = childProcess.spawnSync(gazlCmd,
                [goldenPath].concat(expectedRun.args), { encoding: 'latin1', timeout: 30000 });
        if (result.error) {
                return `could not run GAZLCmd: ${result.error.message}`;
        }
        /* GAZLCmd puts the PROGRAM's output on stdout and its own report (sizes, Status) on stderr. */
        const report = result.stderr || '';
        if (!/Code size:/.test(report)) {             // never reached the entry point -> assembly failed
                const line = report.split('\n').find((l) => l.trim()) || '(no output)';
                return `did not assemble: ${line.trim()}`;
        }
        const got = (result.stdout || '').trim().split(/\s+/).filter(Boolean);
        if (got.join(' ') !== expectedRun.want.join(' ')) {
                return `printed "${got.join(' ')}" but the fixture expects "${expectedRun.want.join(' ')}"`;
        }
        if (!/Status: 0\b/.test(report)) {
                return `ran but exited non-zero: ${(report.match(/Status: .*/) || ['?'])[0]}`;
        }
        return undefined;
}

/* A fixture with no run line is still fed to the assembler, because "compiles clean" and "loads"
   are different claims and only the second one catches a label this compiler emitted but never
   defined. Most such fixtures are firmware that links against a host, so an unresolved name that
   is NOT module-local (a plain global, or the `.o.`/`.z.` layout symbols an extern struct leaves
   to the host) means "out of scope here", not "broken". */
const NEEDS_HOST = {};

/* Goldens that cannot load under GAZLCmd for reasons that are not the compiler's: */
const KNOWN_UNLOADABLE = {
        calc: 'defines log(), which GAZLCmd itself provides as a native',
        perfTest: 'defines log(), which GAZLCmd itself provides as a native',
        switchtest: 'grammar torture fixture - case values deliberately outside the switch range'
};

/* GAZLCmd has no assemble-only mode: it defaults to entering `main`, which for a fixture that has
   one means running a whole program nobody asked for (Priyome is an interactive chess game). Name
   an entry point that cannot exist and it assembles, prints its banner, then stops. */
const NO_ENTRY_POINT = '.no-entry-point';

function assembleGolden(goldenPath) {
        const result = childProcess.spawnSync(gazlCmd, [goldenPath, NO_ENTRY_POINT],
                { encoding: 'latin1', timeout: 30000 });
        if (result.error) {
                return `could not run GAZLCmd: ${result.error.message}`;
        }
        const report = (result.stderr || '') + (result.stdout || '');
        if (/Code size:/.test(report)) {               // assembled - it never reaches an entry point
                return undefined;
        }
        const line = (report.split('\n').find((l) => l.trim()) || '(no output)').trim();
        const symbol = (line.match(/:\s*(\S+)\s*$/) || ['', ''])[1];
        if (/Symbol not (found|previously defined)/.test(line)
                        && (symbol.charAt(0) !== '.' || /^\.[oz]\./.test(symbol))) {
                return NEEDS_HOST;
        }
        return `did not assemble: ${line}`;
}

function main() {
        let totalFiles = 0;
        let errorCount = 0;
        let ranCount = 0;
        let assembledCount = 0;
        const haveGazlCmd = fs.existsSync(gazlCmd);
        if (!haveGazlCmd && !makeGold && !skipRun) {
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
                        totalFiles += 1;
                        continue;
                }

                const expectedRun = parseExpectedRun(source);
                if (skipRun || !haveGazlCmd) {
                        console.log('OK');
                        totalFiles += 1;
                        continue;
                }

                if (!expectedRun) {
                        const verdict = (KNOWN_UNLOADABLE[name] !== undefined
                                        ? NEEDS_HOST : assembleGolden(goldenPath));
                        if (verdict === NEEDS_HOST) {
                                console.log('OK (compile-only)');
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

                const failure = runGolden(goldenPath, expectedRun);
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
        console.log(`Total errors: ${errorCount} / ${totalFiles}`);

        if (errorCount !== 0) {
                process.exit(1);
        }
}

main();
