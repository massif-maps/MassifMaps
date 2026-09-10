#!/usr/bin/env node
/*
 * Pack a folder of SVGs into a MapLibre sprite sheet.
 *
 * The stretch boxes matter as much as the artwork: a road shield holds a ref of one to three
 * characters, and MapLibre grows one image to fit rather than picking between per-length sprites.
 * `stretchX` names the columns it may repeat, `content` the box the text is placed in.
 *
 * `variants` writes one sprite per colour from a single drawing, substituting `__TOKEN__` in its
 * source. A road shield outside the US is that plate in the country's colour, and a style picks it
 * by name like any other icon - no SDF, no tint, so the sprite stays a nine-patch that stretches.
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

// Every POI drawing is the same white disc with a grey ring, and the glyph one flat grey on it.
// Matched exactly, so a drawing that stops being that shape fails loudly instead of shipping a
// sprite with two discs on it.
const POI_DISC = /<circle cx="24" cy="24" r="22\.5" fill="#ffffff" stroke="#9a9a9a" stroke-width="3"\/>\n\s*/;
const POI_GLYPH_FILL = 'fill="#333333"';

/**
 * Draw the disc a POI class stands on into its sprite, in the colour and shape the style would
 * give it.
 *
 * The SDK colours it per feature through `["image", …, {params}]` and never needs this; MapLibre
 * has no such expression, so without it the reference pane draws a neutral disc under everything
 * and a disc under a park bench. The palette is written by a style's own poi-palette.py, which is
 * also what states those params - one table, so the two rows cannot drift.
 *
 * It is a SECOND sprite, `<class>-poi`, and the neutral drawing stays. mapbox2css splits the
 * neutral one into a glyph field and the plate the SDK recolours, and that split needs a flat
 * neutral disc to measure: baking the colour in place cost every POI its plate.
 */
function bakePoi(id, svg, palette) {
    const p = palette.classes[id] ?? palette.default;
    if (!POI_DISC.test(svg)) throw new Error(`${id}: no disc to bake - has the artwork changed?`);
    // Radius 21 is the full circle the drawing already is, and less is a rounded square of the same
    // box: one number spells every badge, exactly as it does on the SDK side.
    const rx = ((p.radius / 21) * 22.5).toFixed(2).replace(/\.?0+$/, '');
    const disc = p.disc === null ? '' : `<rect x="1.5" y="1.5" width="45" height="45" rx="${rx}" `
        + `fill="${p.disc}" stroke="${palette.ring}" stroke-width="${p.border}"/>\n  `;
    return svg.replace(POI_DISC, disc).replace(POI_GLYPH_FILL, `fill="${p.glyph}"`);
}

/** One drawing, one sprite per colour: `<id>-<variant>`, with `__TOKEN__` replaced in the SVG. */
function expand(id, svg, manifest) {
    const variants = (manifest.variants || {})[id];
    if (!variants) return [{ id, svg }];
    return Object.entries(variants).map(([suffix, tokens]) => {
        let out = svg;
        for (const [token, value] of Object.entries(tokens)) {
            out = out.replaceAll(`__${token}__`, value);
        }
        if (out.includes('__')) throw new Error(`${id}-${suffix}: a __TOKEN__ was left unreplaced`);
        return { id: `${id}-${suffix}`, svg: out, from: id };
    });
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
    const at = (v) => v * ratio;
    const out = {};
    if (meta.stretchX) out.stretchX = meta.stretchX.map(([a, b]) => [at(a), at(b)]);
    if (meta.stretchY) out.stretchY = meta.stretchY.map(([a, b]) => [at(a), at(b)]);
    if (meta.content) out.content = meta.content.map(at);
    return out;
}

function build(srcDir, outDir, name) {
    const manifest = JSON.parse(readFileSync(join(srcDir, 'manifest.json'), 'utf8'));
    // One level of folders, so a sheet of a hundred POI glyphs does not sit loose beside the
    // shields. The sprite NAME is still the bare filename - the folder groups the sources only.
    const files = readdirSync(srcDir, { withFileTypes: true }).flatMap((entry) => (entry.isDirectory()
        ? readdirSync(join(srcDir, entry.name)).filter((f) => f.endsWith('.svg'))
            .map((f) => join(entry.name, f))
        : entry.name.endsWith('.svg') ? [entry.name] : [])).sort();
    if (!files.length) throw new Error(`no SVG in ${srcDir}`);
    mkdirSync(outDir, { recursive: true });

    let palette = null;
    try {
        palette = JSON.parse(readFileSync(join(srcDir, 'poi-palette.json'), 'utf8'));
    } catch { /* a sheet without POIs needs none */ }

    for (const ratio of RATIOS) {
        const images = files.flatMap((f) => {
            const source = basename(f, '.svg');
            const svg = readFileSync(join(srcDir, f), 'utf8');
            const drawings = palette && f.startsWith('poi/')
                ? [{ id: source, svg }, { id: `${source}-poi`, svg: bakePoi(source, svg, palette), from: source }]
                : expand(source, svg, manifest);
            return drawings.map((v) => ({
                id: v.id,
                meta: manifest.icons[v.from || v.id] || {},
                png: render(Buffer.from(v.svg), ratio),
            }));
        });
        const { width, height } = pack(images, 512 * ratio);
        const sheet = new PNG({ width, height });
        const index = {};
        for (const img of images) {
            blit(img.png, sheet, img.x, img.y);
            index[img.id] = {
                x: img.x, y: img.y, width: img.png.width, height: img.png.height,
                pixelRatio: ratio, visible: true,
                ...scaleBoxes(img.meta, ratio),
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
