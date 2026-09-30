#!/usr/bin/env node
/*
 * The Node half of the release check: the installed packages, used as an app would.
 *   npm run check
 */
import { execFileSync } from 'node:child_process';
import { mkdirSync, readFileSync, readdirSync, statSync } from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const out = path.join(here, 'out');
const styles = path.join(here, 'node_modules', '@massif-maps', 'styles');
const cli = path.join(here, 'node_modules', '.bin', 'massif-style');
const pkgDir = (name) => path.join(here, 'node_modules', '@massif-maps', name);
const manifest = (name) => JSON.parse(readFileSync(path.join(pkgDir(name), 'package.json'), 'utf8'));
let failed = 0;

async function step(name, fn) {
  try {
    console.log(`ok    ${name}${(await fn()) ?? ''}`);
  } catch (e) {
    failed++;
    console.log(`FAIL  ${name}: ${e.message ?? e}`);
  }
}

function massifStyle(...args) {
  execFileSync(cli, args, { stdio: ['ignore', 'ignore', 'pipe'] });
}

function nonEmpty(file) {
  if (!statSync(file).size) throw new Error(`${file} is empty`);
  return file;
}

mkdirSync(out, { recursive: true });

await step('installed', () => ' ' + ['api', 'web', 'style-tools', 'styles']
  .map((p) => `${p}@${manifest(p).version}`).join(' '));

await step('@massif-maps/api imports', async () => {
  const api = await import('@massif-maps/api');
  if (typeof api.create !== 'function') throw new Error('no create()');
  return ` (${Object.keys(api).length} exports)`;
});

await step('@massif-maps/web pins @massif-maps/api and ships both modules', () => {
  const want = manifest('web').dependencies['@massif-maps/api'];
  const have = manifest('api').version;
  if (want !== have) throw new Error(`wants ${want}, installed ${have}`);
  const files = readdirSync(pkgDir('web'));
  for (const f of ['massif-web.wasm', 'massif-web-full.wasm', 'massif-maps.bundle.mjs']) {
    if (!files.includes(f)) throw new Error(`${f} missing`);
  }
});

await step('css2xml compiles the Massif CartoCSS project (wasm)', () => {
  massifStyle('css2xml', path.join(styles, 'cartocss', 'project.json'), path.join(out, 'massif.xml'));
  return ` -> ${path.relative(here, nonEmpty(path.join(out, 'massif.xml')))}`;
});

await step('mapbox2css converts the Massif MapLibre style and validates it (wasm)', () => {
  massifStyle('mapbox2css', path.join(styles, 'maplibre', 'streets.json'), path.join(out, 'streets'), '--validate', '--no-sprite');
  return ` -> ${path.relative(here, path.join(out, 'streets'))}`;
});

console.log(failed ? `\n${failed} failed` : '\nall passed - now `npm run serve` for the map');
process.exit(failed ? 1 : 0);
