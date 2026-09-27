#!/usr/bin/env node
/*
 * Turns dist/web (the module, from scripts/build-web.py) into the @massif-maps/web package:
 *   node web/package/build.mjs [--version 6.1.0] [--website]
 * Needs bindings/js built first (npm run build there). --website also copies it to the docs site.
 */
import { copyFileSync, cpSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import * as path from 'node:path';
import { fileURLToPath } from 'node:url';
import { build } from 'esbuild';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const out = path.join(root, 'dist', 'web');
const args = process.argv.slice(2);
const version = args.includes('--version') ? args[args.indexOf('--version') + 1] : '0.0.0-devel';
const api = path.join(root, 'bindings', 'js', 'dist', 'index.js');

for (const required of [path.join(out, 'massif-web.wasm'), api]) {
  if (!existsSync(required)) {
    console.error(`missing ${path.relative(root, required)} - build the module and bindings/js first`);
    process.exit(1);
  }
}

for (const name of ['massif.mjs', 'bridge.mjs', 'index.mjs']) {
  copyFileSync(path.join(root, 'web', 'js', name), path.join(out, name));
}
copyFileSync(path.join(here, 'index.d.ts'), path.join(out, 'index.d.ts'));
copyFileSync(path.join(root, 'web', 'demo', 'coi-serviceworker.js'), path.join(out, 'coi-serviceworker.js'));

// The gallery's live examples: not in the npm package, which ships top-level files only.
cpSync(path.join(root, 'web', 'examples'), path.join(out, 'examples'), { recursive: true });

// For a page with no bundler: the API inlined, the module still loaded by URL beside it.
await build({
  entryPoints: [path.join(out, 'index.mjs')],
  outfile: path.join(out, 'massif-maps.bundle.mjs'),
  bundle: true,
  format: 'esm',
  target: 'es2020',
  alias: { '@massif-maps/api': api },
  logLevel: 'warning',
});

const apiVersion = JSON.parse(readFileSync(path.join(root, 'bindings', 'js', 'package.json'), 'utf8')).version;
writeFileSync(path.join(out, 'package.json'), JSON.stringify({
  name: '@massif-maps/web',
  version,
  description: 'Massif Maps in the browser: the SDK compiled to WebAssembly, with its typed API',
  license: 'BSD-3-Clause',
  type: 'module',
  main: 'index.mjs',
  types: 'index.d.ts',
  exports: {
    '.': { types: './index.d.ts', default: './index.mjs' },
    './bundle': './massif-maps.bundle.mjs',
    './massif.mjs': './massif.mjs',
    './coi-serviceworker.js': './coi-serviceworker.js',
    './massif-web.mjs': './massif-web.mjs',
  },
  files: ['*.mjs', '*.wasm', '*.data', '*.d.ts', 'coi-serviceworker.js'],
  sideEffects: false,
  dependencies: { '@massif-maps/api': version === '0.0.0-devel' ? apiVersion : version },
  repository: { type: 'git', url: 'git+https://github.com/massif-maps/MassifMaps.git', directory: 'web' },
  homepage: 'https://massif-maps.github.io/MassifMaps/docs/getting-started/web',
}, null, 2) + '\n');

if (args.includes('--website')) {
  const site = path.join(root, 'website', 'static', 'massif');
  mkdirSync(site, { recursive: true });
  for (const name of ['massif-web.mjs', 'massif-web.wasm', 'massif-web.data', 'massif.mjs', 'massif-maps.bundle.mjs', 'coi-serviceworker.js']) {
    copyFileSync(path.join(out, name), path.join(site, name));
  }
  cpSync(path.join(out, 'examples'), path.join(site, 'examples'), { recursive: true });
}
console.log(`@massif-maps/web ${version} in ${path.relative(root, out)}`);
