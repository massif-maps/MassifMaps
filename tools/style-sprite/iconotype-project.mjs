#!/usr/bin/env node
//   node iconotype-project.mjs <svg-dir> <out.iconotype.json> [--name FACE] [--badge x,y,w,h]
//   npx @iconotype/cli build --input <out.iconotype.json>     (docs/contributing/massif-style-release.md)
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

export const PUA_FIRST = 0xe000;
export const PUA_SIZE = 0xf8ff - 0xe000 + 1;
const ICONOTYPE = '@iconotype/cli@0.3.0';
const SCHEMA = 'https://iconotype.github.io/iconotype/schema/iconfont-1.json';

/** FNV-1a over the name, into the BMP Private Use Area. */
export function hashCodepoint(name) {
    let h = 0x811c9dc5;
    for (const byte of Buffer.from(name, 'utf8')) {
        h ^= byte;
        h = Math.imul(h, 0x01000193) >>> 0;
    }
    return PUA_FIRST + (h % PUA_SIZE);
}

/** name -> codepoint: `kept` (a previous project's) first, then each new name its hash or the next free slot. */
export function codepointOf(names, kept = new Map()) {
    const out = new Map();
    const taken = new Set();
    for (const [name, code] of kept) {
        out.set(name, code);
        taken.add(code);
    }
    for (const name of [...names].sort()) {
        if (out.has(name)) continue;
        let code = hashCodepoint(name);
        while (taken.has(code)) code = PUA_FIRST + ((code - PUA_FIRST + 1) % PUA_SIZE);
        out.set(name, code);
        taken.add(code);
    }
    return out;
}

/** `badge`: a Massif badge loses its plate (the first <circle>) and is cropped to its glyph box. */
function glyphSVG(text, badge) {
    // iconotype's fixer drops a path whose data starts on an encoded newline (Maki's zoo, volcano)
    let svg = text.replace(/&#x[9aAdD];/g, ' ');
    if (badge) {
        svg = svg.replace(/<circle\b[^>]*\/>/, '').replace(/viewBox="[^"]*"/, `viewBox="${badge.join(' ')}"`)
            .replace(/\swidth="[^"]*"/, '').replace(/\sheight="[^"]*"/, '');
    }
    return svg;
}

/** Paths for every SVG of `dir`, through iconotype's own fixer (`init`): font-ready, scaled to 1024. */
function fixedIcons(dir, badge) {
    const work = mkdtempSync(join(tmpdir(), 'iconotype-'));
    try {
        const glyphs = join(work, 'glyphs');
        mkdirSync(glyphs);
        for (const file of readdirSync(dir).filter((f) => f.endsWith('.svg'))) {
            writeFileSync(join(glyphs, file), glyphSVG(readFileSync(join(dir, file), 'utf8'), badge));
        }
        const project = join(work, 'init.iconotype.json');
        const run = spawnSync('npx', ['--yes', ICONOTYPE, 'init', '--input', glyphs, '--name', 'init', '--out', project],
            { cwd: dirname(fileURLToPath(import.meta.url)), encoding: 'utf8' });
        if (run.status !== 0) throw new Error(`iconotype init failed:\n${run.stderr}`);
        for (const line of run.stderr.split('\n')) if (/EMPTY|error/.test(line)) process.stderr.write(line + '\n');
        return JSON.parse(readFileSync(project, 'utf8')).icons;
    } finally {
        rmSync(work, { recursive: true, force: true });
    }
}

/** An icon whose SVG is gone stays, unselected, so its code is never handed to another. */
export function project(face, icons, previous) {
    const kept = new Map((previous?.icons ?? []).map((icon) => [icon.name, parseInt(icon.code, 16)]));
    const codes = codepointOf(icons.map((icon) => icon.name), kept);
    const current = new Set(icons.map((icon) => icon.name));
    const retired = (previous?.icons ?? []).filter((icon) => !current.has(icon.name))
        .map(({ name, paths }) => ({ name, selected: false, paths }));
    const all = [...icons.map(({ name, paths }) => ({ name, paths })), ...retired]
        .map((icon) => ({ name: icon.name, code: codes.get(icon.name).toString(16), ...icon }))
        .sort((a, b) => parseInt(a.code, 16) - parseInt(b.code, 16) || a.name.localeCompare(b.name));
    return {
        $schema: SCHEMA,
        schemaVersion: 1,
        name: face,
        font: { family: face, prefix: `${face.toLowerCase()}-`, emSize: 1024, baseline: 6.25, whitespace: 50, version: '1.0' },
        height: 1024,
        output: { fonts: { dir: '.', formats: ['ttf'] }, styles: [{ kind: 'json', path: `${face}.json` }] },
        icons: all,
    };
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
    const [srcDir, out, ...rest] = process.argv.slice(2);
    if (!srcDir || !out) {
        process.stderr.write('usage: iconotype-project.mjs <svg-dir> <out.iconotype.json> [--name FACE] [--badge x,y,w,h]\n');
        process.exit(2);
    }
    let face = basename(out).replace(/\.iconotype\.json$/, '');
    let badge = null;
    for (let i = 0; i < rest.length; i++) {
        if (rest[i] === '--name') face = rest[++i];
        else if (rest[i] === '--badge') badge = rest[++i].split(',').map(Number);
    }
    const previous = existsSync(out) ? JSON.parse(readFileSync(out, 'utf8')) : null;
    const doc = project(face, fixedIcons(srcDir, badge), previous);
    writeFileSync(out, JSON.stringify(doc, null, 2) + '\n');
    process.stdout.write(`${face}: ${doc.icons.filter((i) => i.selected !== false).length} icons -> ${out}\n`);
}
