#!/usr/bin/env node
/**
 * Gathers one SVG per name in sprites.txt into svg/, so an icon font can be built from a folder
 * whose file names ARE the style's `icon-image` names. Nothing here draws: it copies from maki,
 * from temaki, and from local/ - the hand-added artwork a set does not carry.
 *
 * Only the icon's CONTENT is wanted. A transit roundel's disc, its ring and all three colours are
 * style properties in the SDK (shield-icon-background-fill / -border-fill / shield-icon-fill), so a
 * glyph that carried its own badge would fight them. Where the content is just a character the
 * font needs no glyph at all - see `letter` in sources.json.
 *
 *   node tools/icon-font/collect.mjs [--maki DIR] [--temaki DIR]
 */
import { copyFileSync, existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));

function flag(name, fallback) {
    const i = process.argv.indexOf(`--${name}`);
    return i !== -1 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}

const SETS = {
    maki: flag('maki', '/Volumes/dev/carto/maki/icons'),
    temaki: flag('temaki', '/Volumes/dev/carto/temaki/icons'),
    local: join(HERE, 'local'),
};

const targets = readFileSync(join(HERE, 'sprites.txt'), 'utf8').split('\n').filter(Boolean);
const sources = JSON.parse(readFileSync(join(HERE, 'sources.json'), 'utf8'));
const alias = sources.alias ?? {};
const letters = Object.fromEntries(Object.entries(sources.letter ?? {}).filter(([k]) => !k.startsWith('$')));

const have = Object.fromEntries(Object.entries(SETS).map(([id, dir]) => [
    id, existsSync(dir) ? new Set(readdirSync(dir).filter((f) => f.endsWith('.svg')).map((f) => f.slice(0, -4))) : new Set(),
]));

const out = join(HERE, 'svg');
rmSync(out, { recursive: true, force: true });
mkdirSync(out, { recursive: true });

const taken = [];
const missing = [];
const resolve = (name) => (have.local.has(name) ? { set: 'local', icon: name }
    : alias[name] ?? (have.maki.has(name) ? { set: 'maki', icon: name } : null));

for (const name of targets) {
    if (letters[name] || name.includes('.')) continue; // a character, or a composite - see below
    const entry = resolve(name);
    if (!entry || !have[entry.set]?.has(entry.icon)) {
        missing.push(name);
        continue;
    }
    copyFileSync(join(SETS[entry.set], `${entry.icon}.svg`), join(out, `${name}.svg`));
    taken.push({ name, ...entry });
}

/**
 * `gb-national-rail.london-dlr.london-underground` is three roundels in a row, not one icon.
 * `shield-icon-name` is a STRING shaped into a glyph run, so the converter can split on `.` and
 * concatenate the parts' characters - nothing has to be drawn for it, as long as every part is
 * itself covered. Listed here so a part that is still missing shows what it costs.
 */
const drawable = new Set([...taken.map((t) => t.name), ...Object.keys(letters)]);
const composites = targets.filter((n) => n.includes('.')).map((name) => {
    const parts = name.split('.');
    return { name, parts, blockedBy: parts.filter((p) => !drawable.has(p)) };
});

const by = (set) => taken.filter((t) => t.set === set).length;
const blocked = composites.filter((c) => c.blockedBy.length > 0);
writeFileSync(join(HERE, 'MANIFEST.json'), `${JSON.stringify({
    targets: targets.length,
    svg: taken.length,
    letters: Object.keys(letters).length,
    composites: composites.length,
    missing: missing.length,
    icons: taken,
    lettersMap: letters,
    composed: composites,
    missingNames: missing,
}, null, 2)}\n`);

process.stdout.write(
    `${targets.length} targets: ${taken.length} svg (maki ${by('maki')}, temaki ${by('temaki')}, `
    + `local ${by('local')}), ${Object.keys(letters).length} letters, ${composites.length} composed, `
    + `${missing.length} missing\n`);
if (missing.length) process.stdout.write(`\nmissing (${missing.length}):\n  ${missing.join('\n  ')}\n`);
if (blocked.length) {
    const parts = [...new Set(blocked.flatMap((c) => c.blockedBy))].sort();
    process.stdout.write(`\n${blocked.length} composites blocked, by ${parts.length} parts: ${parts.join(' ')}\n`);
}
