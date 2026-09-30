#!/usr/bin/env node
// Build a TTF icon font from a folder of POI SVGs, and the name -> codepoint map mapbox2css's
// --icon-font-map reads. A POI SVG is a badge: its plate (the first <circle>) is dropped, the glyph
// group kept, and the glyph's own box becomes the em - the SDK draws the plate itself.
//
//   node iconfont.mjs <svg-dir> <out-dir> <face-name> [--box x,y,w,h] [--alias suffix]
//
// Writes <out-dir>/<face>.ttf and <out-dir>/<face>.json. Codepoints start at U+E001 in name
// order, so an icon added later moves the ones after it: the map, not the number, is the contract.
import { mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { basename, join } from 'node:path';
import { Readable } from 'node:stream';
import { SVGIcons2SVGFontStream } from 'svgicons2svgfont';
import svg2ttf from 'svg2ttf';

const [srcDir, outDir, face, ...rest] = process.argv.slice(2);
if (!srcDir || !outDir || !face) {
    process.stderr.write('usage: iconfont.mjs <svg-dir> <out-dir> <face-name> [--box x,y,w,h] [--alias suffix]...\n');
    process.exit(2);
}
let box = '10.5 10.5 27 27'; // Massif's badge: a 15 px maki glyph scaled 1.8 into a 48 px disc
const aliases = [];
for (let i = 0; i < rest.length; i++) {
    if (rest[i] === '--box') box = rest[++i].split(',').join(' ');
    else if (rest[i] === '--alias') aliases.push(rest[++i]);
}

function glyphSVG(text) {
    const plateless = text.replace(/<circle\b[^>]*\/>/, '');
    return plateless.replace(/viewBox="[^"]*"/, `viewBox="${box}"`)
        .replace(/\swidth="[^"]*"/, '').replace(/\sheight="[^"]*"/, '');
}

const names = readdirSync(srcDir).filter((f) => f.endsWith('.svg')).sort();
const fontStream = new SVGIcons2SVGFontStream({ fontName: face, fontHeight: 1000, normalize: true, log: () => {} });
const chunks = [];
fontStream.on('data', (chunk) => chunks.push(chunk));
const done = new Promise((resolve, reject) => { fontStream.on('end', resolve); fontStream.on('error', reject); });

const map = {};
names.forEach((file, index) => {
    const name = basename(file, '.svg');
    const codepoint = 0xe001 + index;
    const glyph = Readable.from([glyphSVG(readFileSync(join(srcDir, file), 'utf8'))]);
    glyph.metadata = { name, unicode: [String.fromCodePoint(codepoint)] };
    fontStream.write(glyph);
    const hex = 'U+' + codepoint.toString(16).toUpperCase();
    map[name] = hex;
    for (const suffix of aliases) map[name + suffix] = hex;
});
fontStream.end();
await done;

const ttf = svg2ttf(chunks.join(''), { version: '1.0', description: `${face} POI icons` });
mkdirSync(outDir, { recursive: true });
writeFileSync(join(outDir, `${face}.ttf`), Buffer.from(ttf.buffer));
writeFileSync(join(outDir, `${face}.json`), JSON.stringify(map, null, 2) + '\n');
process.stdout.write(`${face}: ${names.length} glyphs -> ${join(outDir, face + '.ttf')}\n`);
