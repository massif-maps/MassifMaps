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
 * it from RED (`color.r - offset`). Same field, different channel, so the icon is copied across
 * rather than resolved - the SDK then scales it like a glyph, which is what keeps a zoom-driven
 * size crisp and lets marker-halo-radius grow a halo from it.
 *
 * Flattening it here instead (--sdf-flatten) is the fallback for an SDK without marker-sdf: a
 * smoothstep around the 0.75 edge, written WHITE so marker-color still tints it. That loses the
 * scaling and the halo, which is the whole reason not to do it by default.
 */
const SDF_EDGE = 0.75;
const SDF_GAMMA = 0.08;

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
export function extractIcon(sprites: SpriteSet, name: string, outDir: string, flattenSdf = false): ExtractedIcon | null {
    const [sheetId, iconName] = splitIconName(name);
    const sheet = sprites.get(sheetId);
    const entry = sheet?.index[iconName];
    if (!sheet || !entry || entry.width < 1 || entry.height < 1) return null;

    const icon = new PNG({ width: entry.width, height: entry.height });
    for (let y = 0; y < entry.height; y++) {
        for (let x = 0; x < entry.width; x++) {
            const src = ((entry.y + y) * sheet.image.width + (entry.x + x)) * 4;
            const dst = (y * entry.width + x) * 4;
            if (entry.sdf && flattenSdf) {
                const distance = sheet.image.data[src + 3] / 255;
                const coverage = smoothstep(SDF_EDGE - SDF_GAMMA, SDF_EDGE + SDF_GAMMA, distance);
                icon.data[dst] = 255;
                icon.data[dst + 1] = 255;
                icon.data[dst + 2] = 255;
                icon.data[dst + 3] = Math.round(coverage * 255);
            } else if (entry.sdf) {
                // Alpha -> red, and opaque, so the shader's `color.r` is the field itself.
                const distance = sheet.image.data[src + 3];
                icon.data[dst] = distance;
                icon.data[dst + 1] = distance;
                icon.data[dst + 2] = distance;
                icon.data[dst + 3] = 255;
            } else {
                icon.data.set(sheet.image.data.subarray(src, src + 4), dst);
            }
        }
    }

    const iconsDir = join(outDir, 'icons');
    mkdirSync(iconsDir, { recursive: true });
    const file = `${safeFileName(name)}.png`;
    writeFileSync(join(iconsDir, file), PNG.sync.write(icon));

    const ratio = entry.pixelRatio && entry.pixelRatio > 0 ? entry.pixelRatio : 1;
    return { file: `icons/${file}`, width: entry.width / ratio, height: entry.height / ratio, sdf: !!entry.sdf };
}

function smoothstep(edge0: number, edge1: number, x: number): number {
    const t = Math.min(1, Math.max(0, (x - edge0) / (edge1 - edge0)));
    return t * t * (3 - 2 * t);
}

function safeFileName(name: string): string {
    return name.replace(/[^A-Za-z0-9_.-]/g, '_');
}
