import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { PNG } from 'pngjs';

import type { Json, MapboxStyle } from './types.js';

export interface SpriteEntry {
    x: number;
    y: number;
    width: number;
    height: number;
    pixelRatio?: number;
    sdf?: boolean;
}

interface SpriteSheet {
    index: Record<string, SpriteEntry>;
    image: PNG;
}

/** id -> sheet. The unprefixed sheet is 'default', which is what a bare icon-image refers to. */
export type SpriteSet = Map<string, SpriteSheet>;

/**
 * MapBox stores an SDF sprite's distance field in the ALPHA channel; the SDK's label shader reads
 * it from RED. Same field, different channel AND a different encoding, so it is re-encoded rather
 * than copied - the SDK then scales it like a glyph, which is what keeps a zoom-driven size crisp
 * and lets marker-halo-radius grow a halo from it.
 *
 * The two conventions:
 *
 * | | edge at | per texel |
 * |---|---|---|
 * | MapBox (tiny-sdf, cutoff 0.25, radius 8) | 0.75 | 1/8 |
 * | Massif (`BitmapCanvas::drawSDFPixel`, `128 / BITMAP_SDF_SCALE`) | 0.5 | 1/16 |
 *
 * Copied straight across, the SDK read MapBox's 0.75 edge as four texels INSIDE the shape: the
 * hole in `circle-dot` filled in and every icon came out fat and solid, with a halo squeezed to a
 * quarter of its width. Signed distance in texels is `(v - 0.75) * 8` and the SDK wants
 * `d * 16 + 127.5`, which is the one line below.
 *
 * Flattening instead (--sdf-flatten) is the fallback for an SDK without marker-sdf: a smoothstep
 * around the same edge, written WHITE so marker-color still tints it. That loses the scaling and
 * the halo, which is the whole reason not to do it by default.
 */
const SDF_EDGE = 0.75;
const SDF_GAMMA = 0.08;
const MAPBOX_SDF_RADIUS = 8;
const MASSIF_SDF_UNIT = 128 / 8; // BITMAP_SDF_SCALE
const MASSIF_SDF_EDGE = 127.5;
// What the SDK's own encoding can hold: 127.5 / 16 texels either side of the edge.
const FIELD_LIMIT = 127.5 / 16;

/**
 * One MapBox distance-field byte in the SDK's own encoding.
 *
 * `scale` is the resample the icon has already been through: MAPBOX_SDF_RADIUS counts texels of the
 * ORIGINAL sprite, and one texel of a 0.4-scaled icon covers 2.5 of them. Left out, the field
 * claimed a spread 1/scale times wider than it had, so the smaller the icon the shallower its
 * gradient - blurred edges, and a halo that ran past the field into a square patch.
 */
function reencodeSdf(alpha: number, scale: number): number {
    const texels = (alpha / 255 - SDF_EDGE) * MAPBOX_SDF_RADIUS * scale;
    return Math.max(0, Math.min(255, Math.round(texels * MASSIF_SDF_UNIT + MASSIF_SDF_EDGE)));
}

function resolveSpriteUrls(style: MapboxStyle): Map<string, string> {
    const out = new Map<string, string>();
    const sprite = (style as { sprite?: Json }).sprite;
    if (typeof sprite === 'string') {
        out.set('default', sprite);
    } else if (Array.isArray(sprite)) {
        for (const entry of sprite) {
            const record = entry as { id?: string; url?: string };
            if (typeof record.id === 'string' && typeof record.url === 'string') {
                out.set(record.id, record.url);
            }
        }
    }
    return out;
}

async function fetchBuffer(url: string): Promise<Buffer> {
    if (!/^https?:/.test(url)) return readFileSync(url);
    const response = await fetch(url);
    if (!response.ok) throw new Error(`${response.status} ${response.statusText} for ${url}`);
    return Buffer.from(await response.arrayBuffer());
}

/** Appends the style's own query string (MapTiler carries its key there) to the sprite URLs. */
function withQuery(base: string, suffix: string): string {
    const q = base.indexOf('?');
    return q === -1 ? base + suffix : base.slice(0, q) + suffix + base.slice(q);
}

export async function loadSprites(style: MapboxStyle, keySuffix: string): Promise<SpriteSet> {
    const sheets: SpriteSet = new Map();
    for (const [id, base] of resolveSpriteUrls(style)) {
        const url = base + keySuffix;
        // The @2x sheet first. A 1x sprite is upscaled by the display's pixel ratio before it
        // reaches the screen - about 2.6x on the device this was measured on - and a traffic light
        // drawn from 19 texels reads as a smudge. pixelRatio in the index is what puts it back to
        // its logical size, so the only cost is the download.
        let index: Record<string, SpriteEntry> | undefined;
        let image: PNG | undefined;
        for (const variant of ['@2x', '']) {
            try {
                index = JSON.parse((await fetchBuffer(withQuery(url + variant, '.json'))).toString('utf8'));
                image = PNG.sync.read(await fetchBuffer(withQuery(url + variant, '.png')));
                break;
            } catch {
                index = undefined;
                image = undefined;
            }
        }
        if (!index || !image) throw new Error(`no sprite sheet at ${url}`);
        sheets.set(id, { index, image });
    }
    return sheets;
}

/** 'misc:foo' picks the 'misc' sheet; a bare name is the default one. */
function splitIconName(name: string): [string, string] {
    const colon = name.indexOf(':');
    return colon === -1 ? ['default', name] : [name.slice(0, colon), name.slice(colon + 1)];
}

export interface ExtractedIcon {
    file: string;
    /** Logical pixels - the sheet's own texels divided by its pixelRatio. */
    width: number;
    height: number;
    sdf: boolean;
    /** Texels per logical pixel in the written file, so a drawn size can divide it back out. */
    pixelRatio: number;
}

/**
 * Cuts one icon out of its sheet and writes it as its own PNG. Returns null when the sheet has no
 * such icon, which a style referencing an image it never ships does produce.
 */
export function extractIcon(
    sprites: SpriteSet,
    name: string,
    outDir: string,
    flattenSdf = false,
    tint?: [number, number, number],
    scale = 1,
): ExtractedIcon | null {
    const [sheetId, iconName] = splitIconName(name);
    const sheet = sprites.get(sheetId);
    const entry = sheet?.index[iconName];
    if (!sheet || !entry || entry.width < 1 || entry.height < 1) return null;

    // A shield's image is a plain bitmap - ShieldSymbolizer has no `sdf` - so an SDF sprite going
    // into one is resolved here AND given the colour the style would have tinted it with.
    const flatten = flattenSdf || tint !== undefined;
    const colour = tint ?? [255, 255, 255];

    // The DISTANCE FIELD is carried through the resample and only turned into pixels afterwards.
    // A field is a smooth signal and scales cleanly; flattening first and scaling the result
    // resamples an already-antialiased edge, which is what made a shrunk icon look soft.
    const icon = new PNG({ width: entry.width, height: entry.height });
    for (let y = 0; y < entry.height; y++) {
        for (let x = 0; x < entry.width; x++) {
            const src = ((entry.y + y) * sheet.image.width + (entry.x + x)) * 4;
            const dst = (y * entry.width + x) * 4;
            if (entry.sdf) {
                const distance = sheet.image.data[src + 3];
                icon.data[dst] = icon.data[dst + 1] = icon.data[dst + 2] = distance;
                icon.data[dst + 3] = distance;
            } else {
                icon.data.set(sheet.image.data.subarray(src, src + 4), dst);
            }
        }
    }

    // A shield draws its image at the bitmap's own size, so icon-size is baked in here instead.
    const scaled = scale === 1 ? icon : resample(icon, scale);

    let out = scaled;
    if (entry.sdf) {
        for (let i = 0; i < scaled.data.length; i += 4) {
            const distance = scaled.data[i + 3];
            if (flatten) {
                const coverage = smoothstep(SDF_EDGE - SDF_GAMMA, SDF_EDGE + SDF_GAMMA, distance / 255);
                [scaled.data[i], scaled.data[i + 1], scaled.data[i + 2]] = colour;
                scaled.data[i + 3] = Math.round(coverage * 255);
            } else {
                // Alpha -> red, re-encoded, and opaque: the shader's `color.r` IS the field.
                const value = reencodeSdf(distance, scale);
                scaled.data[i] = scaled.data[i + 1] = scaled.data[i + 2] = value;
                scaled.data[i + 3] = 255;
            }
        }
        if (!flatten) out = padField(scaled, entry.pixelRatio && entry.pixelRatio > 0 ? entry.pixelRatio : 1);
    }

    const iconsDir = join(outDir, 'icons');
    mkdirSync(iconsDir, { recursive: true });
    // Tint and scale are baked in, so one sprite drawn two ways is two files.
    const suffix = [
        tint ? tint.map((c) => c.toString(16).padStart(2, '0')).join('') : '',
        scale === 1 ? '' : `x${Math.round(scale * 100)}`,
    ].filter(Boolean).join('-');
    const file = `${safeFileName(name)}${suffix ? `-${suffix}` : ''}.png`;
    writeFileSync(join(iconsDir, file), PNG.sync.write(out));

    const ratio = entry.pixelRatio && entry.pixelRatio > 0 ? entry.pixelRatio : 1;
    return {
        file: `icons/${file}`,
        width: out.width / ratio,
        height: out.height / ratio,
        sdf: !!entry.sdf && !flatten,
        pixelRatio: ratio,
    };
}

/**
 * Room for the halo to fade in, outside the shape.
 *
 * MapBox's field only describes what its tiny-sdf radius reached - 2 texels outside the ink at
 * cutoff 0.25 - and a MapTiler sprite cell is cut tight around its artwork, so the field is still
 * well above "fully outside" where the bitmap ends. The renderer draws a halo wherever the field is
 * within the halo width of the edge, so it drew one along the QUAD BORDER: a white rectangle round
 * every POI icon, and a white outline round every tree.
 *
 * The padding continues the ramp outward at the field's own rate (one texel = MASSIF_SDF_UNIT)
 * from the nearest border pixel, rather than filling a constant - a constant just moves the same
 * step outward and the halo follows it there.
 *
 * It grows the quad, and with it the label's collision box, so it is kept to what a typical
 * `icon-halo-width` of 2 needs rather than the widest a style could ask for.
 */
const SDF_PADDING = 6;

function padField(source: PNG, pixelRatio: number): PNG {
    const p = Math.round(SDF_PADDING * pixelRatio);
    const width = source.width + 2 * p;
    const height = source.height + 2 * p;
    const out = new PNG({ width, height });

    // The distance is re-derived from the INK by the same exact Euclidean transform MapBox's own
    // tiny-sdf uses (Felzenszwalb & Huttenlocher). Two reasons it cannot just be read back:
    // MapBox's field spans only -6..+2 texels around the shape, so everything further out carries
    // one saturated value and a wider halo lit the whole sprite square; and a cheap chamfer sweep
    // is ~7% out along the diagonals, which showed as a scalloped halo edge and patchy holes.
    //
    // SIGNED - outward from the ink and inward from its complement. One-sided leaves a step where
    // the kept MapBox value meets the derived one, and the halo threshold crosses it twice.
    const INF = 1e20;
    const at = (x: number, y: number): number | null => {
        const sx = x - p;
        const sy = y - p;
        if (sx < 0 || sy < 0 || sx >= source.width || sy >= source.height) return null;
        return source.data[(sy * source.width + sx) * 4];
    };

    const outward = new Float64Array(width * height);
    const inward = new Float64Array(width * height);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const i = y * width + x;
            const value = at(x, y);
            const ink = value !== null && value >= MASSIF_SDF_EDGE;
            outward[i] = ink ? 0 : INF;
            inward[i] = ink ? INF : 0;
        }
    }
    edt(outward, width, height);
    edt(inward, width, height);

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const i = y * width + x;
            // Signed distance in texels, positive inside. MapBox's own value is kept within a texel
            // and a half of the edge - it carries sub-texel coverage a thresholded mask cannot -
            // and the two agree there, so the field stays continuous across the handover.
            //
            // NOT clamped to tiny-sdf's -6..+2: MapBox can afford that span because it draws a
            // sprite at about one texel per screen pixel, where the SDK draws the same sprite at
            // ~1.4 and six texels would fund barely three pixels of halo.
            const derived = Math.sqrt(inward[i]) - Math.sqrt(outward[i]);
            const own = at(x, y);
            const ownTexels = own === null ? null : (own - MASSIF_SDF_EDGE) / MASSIF_SDF_UNIT;
            const signed = ownTexels !== null && Math.abs(derived) <= 1.5 ? ownTexels : derived;
            const texels = Math.max(-FIELD_LIMIT, Math.min(FIELD_LIMIT, signed));
            const value = Math.max(0, Math.min(255, Math.round(MASSIF_SDF_EDGE + texels * MASSIF_SDF_UNIT)));
            const dst = i * 4;
            out.data[dst] = out.data[dst + 1] = out.data[dst + 2] = value;
            out.data[dst + 3] = 255;
        }
    }
    return out;
}

/** Bilinear resample. Sprites are tens of pixels, so the simplest correct thing is fast enough. */
function resample(source: PNG, scale: number): PNG {
    const width = Math.max(1, Math.round(source.width * scale));
    const height = Math.max(1, Math.round(source.height * scale));
    const out = new PNG({ width, height });

    for (let y = 0; y < height; y++) {
        const sy = Math.min(source.height - 1, (y + 0.5) / scale - 0.5);
        const y0 = Math.max(0, Math.floor(sy));
        const y1 = Math.min(source.height - 1, y0 + 1);
        const fy = sy - y0;
        for (let x = 0; x < width; x++) {
            const sx = Math.min(source.width - 1, (x + 0.5) / scale - 0.5);
            const x0 = Math.max(0, Math.floor(sx));
            const x1 = Math.min(source.width - 1, x0 + 1);
            const fx = sx - x0;
            for (let c = 0; c < 4; c++) {
                const at = (px: number, py: number) => source.data[(py * source.width + px) * 4 + c];
                const top = at(x0, y0) * (1 - fx) + at(x1, y0) * fx;
                const bottom = at(x0, y1) * (1 - fx) + at(x1, y1) * fx;
                out.data[(y * width + x) * 4 + c] = Math.round(top * (1 - fy) + bottom * fy);
            }
        }
    }
    return out;
}

function smoothstep(edge0: number, edge1: number, x: number): number {
    const t = Math.min(1, Math.max(0, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}

function safeFileName(name: string): string {
    return name.replace(/[^A-Za-z0-9_.-]/g, '_');
}

/**
 * Every icon of every sheet, written out under its own name.
 *
 * MapTiler picks a POI's icon from the FEATURE - `coalesce(image(subclass), image(class),
 * image('dot'))` - so there is no one name to slice out. The SDK resolves `shield-file` per feature
 * and mapnik interpolates `[field]` inside a string, so the whole set is written and the style
 * names the file with the field in it. Only names that survive a file name unchanged are written,
 * because the interpolation has to land on the file exactly.
 */
export function extractAllIcons(
    sprites: SpriteSet,
    outDir: string,
    flattenSdf = false,
): { names: string[]; skipped: string[]; sample: ExtractedIcon | null } {
    const names: string[] = [];
    const skipped: string[] = [];
    let sample: ExtractedIcon | null = null;
    for (const [sheetId, sheet] of sprites) {
        for (const name of Object.keys(sheet.index)) {
            if (!/^[A-Za-z0-9_-]+$/.test(name)) {
                skipped.push(name);
                continue;
            }
            // Only the default sheet: a qualified name writes 'misc_foo.png', which no
            // interpolation of a bare field value can ever land on.
            if (sheetId !== 'default') continue;
            const icon = extractIcon(sprites, name, outDir, flattenSdf, undefined, 1);
            if (!icon) continue;
            names.push(name);
            if (!sample || name === 'dot') sample = icon;
        }
    }
    return { names, skipped, sample };
}

/**
 * Exact Euclidean distance transform, Felzenszwalb & Huttenlocher - the one MapBox's tiny-sdf uses.
 * `grid` holds SQUARED distances in place: 0 on the shape, a large number off it.
 *
 * Each pass is the lower envelope of the parabolas rooted at every cell of one row or column, which
 * is what makes it exact and linear where a chamfer sweep is neither.
 */
function edt(grid: Float64Array, width: number, height: number): void {
    const size = Math.max(width, height);
    const f = new Float64Array(size);
    const d = new Float64Array(size);
    const v = new Int32Array(size);
    const z = new Float64Array(size + 1);
    const FAR = 1e20;

    const pass = (n: number, read: (i: number) => number, write: (i: number, value: number) => void) => {
        for (let i = 0; i < n; i++) f[i] = read(i);
        let k = 0;
        v[0] = 0;
        z[0] = -FAR;
        z[1] = FAR;
        for (let q = 1; q < n; q++) {
            let s = 0;
            while (k >= 0) {
                s = ((f[q] + q * q) - (f[v[k]] + v[k] * v[k])) / (2 * q - 2 * v[k]);
                if (s > z[k]) break;
                k--;
            }
            k++;
            v[k] = q;
            z[k] = s;
            z[k + 1] = FAR;
        }
        k = 0;
        for (let q = 0; q < n; q++) {
            while (z[k + 1] < q) k++;
            const dx = q - v[k];
            d[q] = dx * dx + f[v[k]];
        }
        for (let i = 0; i < n; i++) write(i, d[i]);
    };

    for (let x = 0; x < width; x++) {
        pass(height, (y) => grid[y * width + x], (y, value) => { grid[y * width + x] = value; });
    }
    for (let y = 0; y < height; y++) {
        const row = y * width;
        pass(width, (x) => grid[row + x], (x, value) => { grid[row + x] = value; });
    }
}
