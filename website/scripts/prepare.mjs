/*
 * What the site needs from the rest of the repo before it can build.
 *
 * Wired to prestart/prebuild, so `npm start` and `npm run build` both just work from a fresh
 * checkout instead of failing on a missing import.
 *
 *   1. tools/style-cli -> its dist/, which the /preview page imports the MapBox converter from.
 *      The converter is the CLI's own TypeScript and dist/ is not tracked.
 *   2. web/demo/coi-serviceworker.js -> static/, so there is ONE copy of it. Both the bench and
 *      the published preview need it, and a second copy would be a second thing to keep in step.
 */

import {execFileSync} from 'node:child_process';
import {copyFileSync, existsSync, mkdirSync} from 'node:fs';
import {dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';

const SITE = join(dirname(fileURLToPath(import.meta.url)), '..');
const REPO = join(SITE, '..');
const TOOLS = join(REPO, 'tools', 'style-cli');
const npm = process.platform === 'win32' ? 'npm.cmd' : 'npm';

const run = (...args) => execFileSync(npm, args, {cwd: TOOLS, stdio: 'inherit'});

// `npm ci` needs a clean tree; once node_modules is there, install is the incremental one.
run(existsSync(join(TOOLS, 'node_modules')) ? 'install' : 'ci', '--no-audit', '--no-fund');
run('run', 'build');

mkdirSync(join(SITE, 'static'), {recursive: true});
copyFileSync(join(REPO, 'web', 'demo', 'coi-serviceworker.js'),
             join(SITE, 'static', 'coi-serviceworker.js'));
