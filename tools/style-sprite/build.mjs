#!/usr/bin/env node
/*
 * Pack a folder of SVGs into a MapLibre sprite sheet.
 *
 * The stretch boxes matter as much as the artwork: a road shield holds a ref of one to three
 * characters, and MapLibre grows one image to fit rather than picking between per-length sprites.
 * `stretchX` names the columns it may repeat, `content` the box the text is placed in.
 */

import { readFileSync, writeFileSync, readdirSync, mkdirSync } from 'node:fs';
import { basename, join } from 'node:path';
import { Resvg } from '@resvg/resvg-js';
import { PNG } from 'pngjs';

const RATIOS = [1, 2];
const PAD = 2;

function render(svg, ratio) {
    const r = new Resvg(svg, { fitTo: { mode: 'zoom', value: ratio } });
    return PNG.sync.read(Buffer.from(r.render().asPng()));
}

/** shelf packing, tallest first - a sprite sheet of a few dozen icons needs nothing cleverer. */
function pack(images, maxWidth) {
    const order = [...images].sort((a, b) => b.png.height - a.png.height);
    let x = 0, y = 0, shelf = 0, width = 0;
    for (const img of order) {
        if (x + img.png.width > maxWidth && x > 0) {
            x = 0;
            y += shelf + PAD;
            shelf = 0;
        }
        img.x = x;
        img.y = y;
        x += img.png.width + PAD;
        shelf = Math.max(shelf, img.png.height);
        width = Math.max(width, x - PAD);
    }
    return { width, height: y + shelf };
}

function blit(src, dst, dx, dy) {
    for (let row = 0; row < src.height; row++) {
        const from = row * src.width * 4;
        src.data.copy(dst.data, ((dy + row) * dst.width + dx) * 4, from, from + src.width * 4);
    }
}

function scaleBoxes(meta, ratio) {
    const out = {};
    if (meta.stretchX) out.stretchX = meta.stretchX.map(([a, b]) => [a * ratio, b * ratio]);
    if (meta.stretchY) out.stretchY = meta.stretchY.map(([a, b]) => [a * ratio, b * ratio]);
    if (meta.content) out.content = meta.content.map((v) => v * ratio);
    return out;
}

function build(srcDir, outDir, name) {
    const manifest = JSON.parse(readFileSync(join(srcDir, 'manifest.json'), 'utf8'));
    const files = readdirSync(srcDir).filter((f) => f.endsWith('.svg')).sort();
    if (!files.length) throw new Error(`no SVG in ${srcDir}`);
    mkdirSync(outDir, { recursive: true });

    for (const ratio of RATIOS) {
        const images = files.map((f) => ({
            id: basename(f, '.svg'),
            png: render(readFileSync(join(srcDir, f)), ratio),
        }));
        const { width, height } = pack(images, 512 * ratio);
        const sheet = new PNG({ width, height });
        const index = {};
        for (const img of images) {
            blit(img.png, sheet, img.x, img.y);
            index[img.id] = {
                x: img.x, y: img.y, width: img.png.width, height: img.png.height,
                pixelRatio: ratio, visible: true,
                ...scaleBoxes(manifest.icons[img.id] || {}, ratio),
            };
        }
        const suffix = ratio === 1 ? '' : `@${ratio}x`;
        writeFileSync(join(outDir, `${name}${suffix}.png`), PNG.sync.write(sheet));
        writeFileSync(join(outDir, `${name}${suffix}.json`), JSON.stringify(index, null, 2) + '\n');
        console.log(`${name}${suffix}  ${images.length} icons  ${width}x${height}`);
    }
}

const [src, out, name = 'sprite'] = process.argv.slice(2);
if (!src || !out) {
    console.error('usage: build.mjs <sprite-src-dir> <out-dir> [name]');
    process.exit(2);
}
build(src, out, name);
