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

/** One MapBox distance-field byte in the SDK's own encoding. */
function reencodeSdf(alpha: number): number {
    const texels = (alpha / 255 - SDF_EDGE) * MAPBOX_SDF_RADIUS;
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
        const index = JSON.parse((await fetchBuffer(withQuery(url, '.json'))).toString('utf8'));
        const image = PNG.sync.read(await fetchBuffer(withQuery(url, '.png')));
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
    width: number;
    height: number;
    sdf: boolean;
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

    if (entry.sdf) {
        for (let i = 0; i < scaled.data.length; i += 4) {
            const distance = scaled.data[i + 3];
            if (flatten) {
                const coverage = smoothstep(SDF_EDGE - SDF_GAMMA, SDF_EDGE + SDF_GAMMA, distance / 255);
                [scaled.data[i], scaled.data[i + 1], scaled.data[i + 2]] = colour;
                scaled.data[i + 3] = Math.round(coverage * 255);
            } else {
                // Alpha -> red, re-encoded, and opaque: the shader's `color.r` IS the field.
                const value = reencodeSdf(distance);
                scaled.data[i] = scaled.data[i + 1] = scaled.data[i + 2] = value;
                scaled.data[i + 3] = 255;
            }
        }
    }

    const iconsDir = join(outDir, 'icons');
    mkdirSync(iconsDir, { recursive: true });
    // Tint and scale are baked in, so one sprite drawn two ways is two files.
    const suffix = [
        tint ? tint.map((c) => c.toString(16).padStart(2, '0')).join('') : '',
        scale === 1 ? '' : `x${Math.round(scale * 100)}`,
    ].filter(Boolean).join('-');
    const file = `${safeFileName(name)}${suffix ? `-${suffix}` : ''}.png`;
    writeFileSync(join(iconsDir, file), PNG.sync.write(scaled));

    const ratio = entry.pixelRatio && entry.pixelRatio > 0 ? entry.pixelRatio : 1;
    return {
        file: `icons/${file}`,
        width: scaled.width / ratio,
        height: scaled.height / ratio,
        sdf: !!entry.sdf && !flatten,
    };
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
