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

/**
 * `mapbox://sprites/<user>/<style>[/<hash>]` -> the API URL that actually serves it. Mapbox styles
 * name their sheet with their own scheme, which is not a URL anything can fetch; the trailing hash
 * is a cache token and the path without it serves the same sheet.
 */
export function resolveSpriteUrl(url: string): string {
    const mapbox = /^mapbox:\/\/sprites\/([^/]+)\/([^/]+)/.exec(url);
    return mapbox ? `https://api.mapbox.com/styles/v1/${mapbox[1]}/${mapbox[2]}/sprite` : url;
}

function resolveSpriteUrls(style: MapboxStyle): Map<string, string> {
    const out = new Map<string, string>();
    const sprite = (style as { sprite?: Json }).sprite;
    if (typeof sprite === 'string') {
        out.set('default', resolveSpriteUrl(sprite));
    } else if (Array.isArray(sprite)) {
        for (const entry of sprite) {
            const record = entry as { id?: string; url?: string };
            if (typeof record.id === 'string' && typeof record.url === 'string') {
                out.set(record.id, resolveSpriteUrl(record.url));
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
        // The DENSEST sheet the provider serves, tried in order. A sprite is upscaled by the
        // display's pixel ratio before it reaches the screen - about 2.6x on the device this was
        // measured on - and a traffic light drawn from 19 texels reads as a smudge. pixelRatio in
        // the index is what puts it back to its logical size, so the only cost is size.
        //
        // MapBox serves up to @4x, and a POI icon at 80 texels rather than 40 is what carries the
        // notches between a fork's tines through the distance field: at @2x they are one texel of
        // partial coverage and close up at the size the icon is drawn. MapTiler stops at @2x, so
        // the loop just falls through for it.
        //
        // The variant belongs to the PATH, before the key: appended after it the URL reads
        // `sprite.json?access_token=pk...@2x`, which asks for the 1x sheet with a corrupt token,
        // fails, and falls back to 1x without a word. Every sprite in a keyed style was blurry.
        let index: Record<string, SpriteEntry> | undefined;
        let image: PNG | undefined;
        for (const variant of ['@4x', '@3x', '@2x', '']) {
            const url = base + variant + keySuffix;
            try {
                index = JSON.parse((await fetchBuffer(withQuery(url, '.json'))).toString('utf8'));
                image = PNG.sync.read(await fetchBuffer(withQuery(url, '.png')));
                break;
            } catch {
                index = undefined;
                image = undefined;
            }
        }
        if (!index || !image) throw new Error(`no sprite sheet at ${base}`);
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
    /** Set when the artwork was split into a glyph field and a plate - see extractIconPlate. */
    plate?: IconPlate;
    /** The CartoCSS test for the features the plate applies to, when it does not apply to all. */
    plateWhen?: string;
}

/**
 * The disc a MapBox vector icon is drawn on, measured off its flat render, in logical pixels. The
 * SDK draws it as the shield's icon PLATE, so it takes the style's `background` colour per feature
 * where the sheet's own is baked in.
 */
export interface IconPlate {
    /** Corner radius. Half the side is a circle, 0 a square. */
    radius: number;
    /** The ring around the disc, which the field is cropped by so the border lands on it. */
    borderWidth: number;
    /** MapBox's `icon-stroke`: the outline drawn under the glyph, 0 when the artwork has none. */
    strokeWidth: number;
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
    /** The file name to write under, when it is not the icon's own qualified one. */
    writeAs?: string,
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
    const file = `${safeFileName(writeAs ?? name)}${suffix ? `-${suffix}` : ''}.png`;
    writeFileSync(join(iconsDir, file), PNG.sync.write(out, ICON_PNG));

    const ratio = entry.pixelRatio && entry.pixelRatio > 0 ? entry.pixelRatio : 1;
    return {
        file: `icons/${file}`,
        width: out.width / ratio,
        height: out.height / ratio,
        sdf: !!entry.sdf && !flatten,
        pixelRatio: ratio,
    };
}

/** Alpha at which a texel counts as one of the icon's flat colours rather than an edge. */
const FLAT_ALPHA = 200;
/** How far in from the silhouette the ring is looked for, in texels. */
const RING_DEPTH = 3;
/** Where the plate fields are written, so they never collide with the raster cut of the same name. */
const GLYPH_DIR = 'icons-glyph';

/**
 * How the icons are encoded. Both are LOSSLESS - the pixels the SDK reads are identical - and
 * together they halve the sprite, which is most of what a converted style weighs (3.9 MB of PNG
 * for Mapbox Standard's 595 icons, 2.0 MB after).
 *
 * A distance FIELD carries its value in one channel: buildField writes r=g=b and a fully opaque
 * alpha, so three of the four bytes per pixel are a copy and a constant. `colorType: 0` writes the
 * red channel alone, which a decoder expands right back to r=g=b=v, a=255.
 */
const FIELD_PNG = { colorType: 0, deflateLevel: 9, filterType: -1 } as const;
/** A colour icon needs all four channels; only the deflate is worth tightening. */
const ICON_PNG = { deflateLevel: 9, filterType: -1 } as const;

type RGB = readonly [number, number, number];

function colourDistance(a: RGB, b: RGB): number {
    return Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
}

/**
 * A signed distance field over a coverage grid, in the SDK's encoding.
 *
 * A thresholded mask puts the edge half a texel out from the cell it measured, so the EDT only says
 * where the field is DEEP; anywhere the artwork carries partial coverage the coverage itself is the
 * answer, since it is the only record of where inside the texel the edge fell. The threshold is not
 * allowed to decide on its own either: a stroke thinner than a texel never reaches 1.0 anywhere and
 * thresholding it at 0.5 erased it outright - a bicycle's spokes came out as a handful of dots.
 */
function buildField(width: number, height: number, cell: (x: number, y: number) => number): PNG {
    const field = new PNG({ width, height });
    const ins = new Float64Array(width * height);
    const outs = new Float64Array(width * height);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const isInk = cell(x, y) >= 0.5;
            ins[y * width + x] = isInk ? 1e20 : 0;
            outs[y * width + x] = isInk ? 0 : 1e20;
        }
    }
    edt(ins, width, height);
    edt(outs, width, height);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const i = y * width + x;
            const partial = cell(x, y) > 0 && cell(x, y) < 1;
            const raw = Math.sqrt(ins[i]) - Math.sqrt(outs[i]);
            const base = raw - Math.sign(raw) * 0.5;
            const signed = partial || Math.abs(base) <= 0.5 ? cell(x, y) - 0.5 : base;
            const texels = Math.max(-FIELD_LIMIT, Math.min(FIELD_LIMIT, signed));
            const value = Math.max(0, Math.min(255, Math.round(MASSIF_SDF_EDGE + texels * MASSIF_SDF_UNIT)));
            const dst = i * 4;
            field.data[dst] = field.data[dst + 1] = field.data[dst + 2] = value;
            field.data[dst + 3] = 255;
        }
    }
    return field;
}

/**
 * The GLYPH of a composite vector icon as a distance field, and the disc it sat on.
 *
 * MapBox's sheet ships ONE flat render of an `["image", name, { params }]` icon, with the icon's own
 * default colours baked into it: a disc, a ring around it, the glyph, and their blends. Drawn as it
 * comes, every POI icon is grey where mapbox tints it by class, and a distance field taken from it
 * is nonsense - the red channel of a blue disc reads as "outside" and the white glyph as "inside",
 * which drew the icons inverted.
 *
 * Split in two, neither half is baked: the glyph becomes a field the style colours per feature, and
 * the disc becomes the shield's icon PLATE, whose fill, border and radius are style properties. The
 * ring is cropped off the field so the plate's border sits exactly where the artwork's did.
 *
 * Returns null for anything that is not one of these - a plain single-colour sprite, or a real SDF.
 */
export function extractIconPlate(
    sprites: SpriteSet,
    name: string,
    outDir: string,
    writeAs?: string,
): ExtractedIcon | null {
    const [sheetId, iconName] = splitIconName(name);
    const sheet = sprites.get(sheetId);
    const entry = sheet?.index[iconName];
    if (!sheet || !entry || entry.sdf || entry.width < 8 || entry.height < 8) return null;

    const width = entry.width;
    const height = entry.height;
    const at = (x: number, y: number): readonly [number, number, number, number] => {
        const i = ((entry.y + y) * sheet.image.width + (entry.x + x)) * 4;
        const d = sheet.image.data;
        return [d[i], d[i + 1], d[i + 2], d[i + 3]];
    };

    // Distance from the transparent surround, so the ring can be found whatever shape the icon is:
    // a POI disc reaches the cell edge on the axes and a transit roundel at its corners, and one
    // radius threshold cannot describe both. The grid is bordered so a silhouette touching the cell
    // still has an outside to measure from.
    const gw = width + 2;
    const gh = height + 2;
    const outward = new Float64Array(gw * gh);
    for (let y = 0; y < gh; y++) {
        for (let x = 0; x < gw; x++) {
            const inside = x > 0 && y > 0 && x <= width && y <= height && at(x - 1, y - 1)[3] >= FLAT_ALPHA;
            outward[y * gw + x] = inside ? 1e20 : 0;
        }
    }
    edt(outward, gw, gh);
    const depth = (x: number, y: number) => Math.sqrt(outward[(y + 1) * gw + (x + 1)]);

    // The flat colours, by how much of the icon they cover. Their blends are the rest.
    const counts = new Map<string, { colour: RGB; count: number }>();
    let flats = 0;
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const [r, g, b, a] = at(x, y);
            if (a < FLAT_ALPHA) continue;
            flats++;
            const key = `${r},${g},${b}`;
            const seen = counts.get(key);
            if (seen) seen.count++; else counts.set(key, { colour: [r, g, b], count: 1 });
        }
    }
    if (flats < 32) return null;
    const palette = [...counts.values()].filter((c) => c.count >= flats * 0.015).map((c) => c.colour);
    if (palette.length < 3) return null;

    const dominant = (accept: (x: number, y: number) => boolean, exclude: RGB[]): RGB | null => {
        const tally = new Map<string, { colour: RGB; count: number }>();
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const [r, g, b, a] = at(x, y);
                if (a < FLAT_ALPHA || !accept(x, y)) continue;
                const colour: RGB = [r, g, b];
                if (!palette.some((p) => colourDistance(p, colour) === 0)) continue;
                if (exclude.some((e) => colourDistance(e, colour) === 0)) continue;
                const key = `${r},${g},${b}`;
                const seen = tally.get(key);
                if (seen) seen.count++; else tally.set(key, { colour, count: 1 });
            }
        }
        let best: { colour: RGB; count: number } | null = null;
        for (const item of tally.values()) if (!best || item.count > best.count) best = item;
        return best ? best.colour : null;
    };

    // The OUTERMOST texel row, which is the ring wherever there is one: a wider band is already
    // mostly disc on an icon whose two flats meet without an antialiased row between them.
    const ring = dominant((x, y) => depth(x, y) <= 1, []);
    if (!ring) return null;
    // How thick the ring actually is: the band is followed inward while it still dominates.
    let ringTexels = 0;
    while (ringTexels < RING_DEPTH) {
        const band = dominant((x, y) => depth(x, y) > ringTexels && depth(x, y) <= ringTexels + 1, []);
        if (!band || colourDistance(band, ring) > 0) break;
        ringTexels++;
    }
    if (ringTexels === 0) return null;
    const inner = (x: number, y: number) => depth(x, y) > ringTexels;
    const disc = dominant(inner, [ring]);
    if (!disc) return null;

    // What is left inside the disc, ANTIALIASING discounted. A blend of the disc and the ring lies
    // on the segment between them, and a 32-texel roundel carries more of it than it does glyph -
    // taken for a flat it won the count, and the icon drew as a handful of specks.
    const discToRing = colourDistance(disc, ring);
    const isBlend = (c: RGB) => discToRing > 0
        && colourDistance(c, disc) + colourDistance(c, ring) <= discToRing * 1.15;
    const candidates = palette.filter((c) => colourDistance(c, disc) > 0
        && colourDistance(c, ring) > 0 && !isBlend(c));
    if (candidates.length === 0) return null;

    // MapBox composes an icon as `icon-stroke` under `icon`, so the glyph's own colour is the one
    // the other ENCLOSES - measured as the mean distance from the disc, since the two are parted by
    // an antialiased row and neither touches it. Taken by pixel count instead, an outlined glyph
    // gave its OUTLINE: the ⓘ drew as a white ring with the disc showing through the middle, which
    // is what "no white in the centre" was.
    const fromDisc = new Float64Array(width * height);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const [r, g, b, a] = at(x, y);
            const onDisc = a >= FLAT_ALPHA && colourDistance([r, g, b], disc) === 0;
            fromDisc[y * width + x] = onDisc ? 0 : 1e20;
        }
    }
    edt(fromDisc, width, height);
    let ink: RGB | null = null;
    let deepest = -1;
    for (const candidate of candidates) {
        let total = 0;
        let count = 0;
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const [r, g, b, a] = at(x, y);
                if (a < FLAT_ALPHA || !inner(x, y) || colourDistance([r, g, b], candidate) > 0) continue;
                total += Math.sqrt(fromDisc[y * width + x]);
                count++;
            }
        }
        if (count === 0) continue;
        const mean = total / count;
        if (mean > deepest) { deepest = mean; ink = candidate; }
    }
    if (!ink) return null;

    // MapBox's `icon-stroke` - the outline it draws UNDER the glyph - is the candidate the ink is
    // not, and how far it reaches past the ink is what the SDK's icon halo has to grow.
    let strokeWidth = 0;
    const stroke = candidates.find((c) => colourDistance(c, ink) > 0) ?? null;
    if (stroke) {
        const fromInk = new Float64Array(width * height);
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const [r, g, b, a] = at(x, y);
                const onInk = a >= FLAT_ALPHA && colourDistance([r, g, b], ink) === 0;
                fromInk[y * width + x] = onInk ? 0 : 1e20;
            }
        }
        edt(fromInk, width, height);
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const [r, g, b, a] = at(x, y);
                if (a < FLAT_ALPHA || !inner(x, y) || colourDistance([r, g, b], stroke) > 0) continue;
                strokeWidth = Math.max(strokeWidth, Math.min(3, Math.sqrt(fromInk[y * width + x])));
            }
        }
    }

    // How much INK a texel holds: its position along the disc -> ink axis. A partly covered texel
    // is a linear blend of the two, so the projection IS the coverage, and it needs no list of what
    // else the icon is painted with.
    //
    // Snapping to the nearest flat instead cost the thin strokes: a bicycle's spokes never reach
    // the ink colour anywhere, every one of their texels is a blend, and on a blue roundel that
    // blend is also a blend of the disc and the ring - so each was read as "not ink" and the wheels
    // drew as a ring of dots.
    const axis: RGB = [ink[0] - disc[0], ink[1] - disc[1], ink[2] - disc[2]];
    const axisLength2 = axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2];
    const coverage = (x: number, y: number): number => {
        const [r, g, b, a] = at(x, y);
        // The ring is not part of the glyph, and the disc's own box catches its corners. A texel of
        // margin past the ring, because a light ring projects high on this axis and a glyph never
        // reaches the disc's edge anyway - without it every roundel kept four white corner specks.
        if (a < FLAT_ALPHA || depth(x, y) <= ringTexels + 1 || axisLength2 === 0) return 0;
        const t = ((r - disc[0]) * axis[0] + (g - disc[1]) * axis[1] + (b - disc[2]) * axis[2]) / axisLength2;
        return Math.max(0, Math.min(1, t));
    };

    // The disc's own box and area: a rounded rect of side S with corner radius r covers
    // S^2 - (4 - pi) r^2, which is the radius without fitting an arc to a 40-texel shape.
    let x0 = width, y0 = height, x1 = -1, y1 = -1, area = 0;
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            if (!(at(x, y)[3] >= FLAT_ALPHA && inner(x, y))) continue;
            area++;
            x0 = Math.min(x0, x); y0 = Math.min(y0, y);
            x1 = Math.max(x1, x); y1 = Math.max(y1, y);
        }
    }
    if (x1 < x0 || y1 < y0) return null;
    const discW = x1 - x0 + 1;
    const discH = y1 - y0 + 1;
    // A plate is a rounded RECT fitted to the icon's box, so it stands in for a disc or a roundel
    // and for nothing else. MapBox's generic pin splits into flats just as cleanly and came out as
    // a coloured pill with its glyph knocked out.
    if (Math.abs(discW - discH) > 0.15 * Math.max(discW, discH)) return null;
    let inkArea = 0;
    for (let y = y0; y <= y1; y++) for (let x = x0; x <= x1; x++) if (coverage(x, y) >= 0.5) inkArea++;
    if (inkArea === 0 || inkArea > area * 0.6) return null;
    const radius = Math.min(Math.min(discW, discH) / 2,
        Math.sqrt(Math.max(0, discW * discH - area) / (4 - Math.PI)));

    // The field, on the disc's own box: the quad the shield draws IS the plate's box then, so the
    // plate needs no padding and its border lands on the ring the crop just removed.
    const field = buildField(discW, discH, (x, y) => coverage(x0 + x, y0 + y));

    const iconsDir = join(outDir, GLYPH_DIR);
    mkdirSync(iconsDir, { recursive: true });
    const file = `${safeFileName(writeAs ?? name)}.png`;
    writeFileSync(join(iconsDir, file), PNG.sync.write(field, FIELD_PNG));

    const ratio = entry.pixelRatio && entry.pixelRatio > 0 ? entry.pixelRatio : 1;
    return {
        file: `${GLYPH_DIR}/${file}`,
        width: discW / ratio,
        height: discH / ratio,
        sdf: true,
        pixelRatio: ratio,
        plate: { radius: radius / ratio, borderWidth: ringTexels / ratio, strokeWidth: strokeWidth / ratio },
    };
}

/**
 * A sprite that is not a disc with a glyph on it, as a field of its own SILHOUETTE.
 *
 * MapBox composes a POI icon as `background` + `icon`, and its sheet ships the two already merged;
 * the split above recovers them. What it cannot split - `marker`, MapBox's generic pin, first among
 * them - still has to draw SOMETHING, and the layer states the background separately anyway, so the
 * whole sprite becomes the glyph and the rule's own plate stands in for the disc. Left out, the
 * ~150 POIs that name the generic pin drew their label with no icon at all.
 *
 * It flattens a multi-colour sprite to one shape, which is what the icon-fill of a POI rule means.
 */
export function extractIconSilhouette(
    sprites: SpriteSet,
    name: string,
    outDir: string,
    writeAs?: string,
): ExtractedIcon | null {
    const [sheetId, iconName] = splitIconName(name);
    const sheet = sprites.get(sheetId);
    const entry = sheet?.index[iconName];
    if (!sheet || !entry || entry.sdf || entry.width < 1 || entry.height < 1) return null;

    const alpha = (x: number, y: number): number =>
        sheet.image.data[((entry.y + y) * sheet.image.width + (entry.x + x)) * 4 + 3] / 255;

    let x0 = entry.width, y0 = entry.height, x1 = -1, y1 = -1;
    for (let y = 0; y < entry.height; y++) {
        for (let x = 0; x < entry.width; x++) {
            if (alpha(x, y) <= 0) continue;
            x0 = Math.min(x0, x); y0 = Math.min(y0, y);
            x1 = Math.max(x1, x); y1 = Math.max(y1, y);
        }
    }
    if (x1 < x0 || y1 < y0) return null;

    const w = x1 - x0 + 1;
    const h = y1 - y0 + 1;
    const field = buildField(w, h, (x, y) => alpha(x0 + x, y0 + y));
    const iconsDir = join(outDir, GLYPH_DIR);
    mkdirSync(iconsDir, { recursive: true });
    const file = `${safeFileName(writeAs ?? iconName)}.png`;
    writeFileSync(join(iconsDir, file), PNG.sync.write(field, FIELD_PNG));

    const ratio = entry.pixelRatio && entry.pixelRatio > 0 ? entry.pixelRatio : 1;
    return { file: `${GLYPH_DIR}/${file}`, width: w / ratio, height: h / ratio, sdf: true, pixelRatio: ratio };
}

/** Every icon of every sheet that IS a composite, split into a glyph field and a plate. */
export function extractAllIconPlates(
    sprites: SpriteSet,
    outDir: string,
): { entries: Array<{ name: string; icon: ExtractedIcon }>; skipped: string[]; sample: ExtractedIcon | null } {
    const entries: Array<{ name: string; icon: ExtractedIcon }> = [];
    const skipped: string[] = [];
    const seen = new Set<string>();
    let sample: ExtractedIcon | null = null;
    for (const [sheetId, sheet] of sprites) {
        for (const name of Object.keys(sheet.index)) {
            if (!/^[A-Za-z0-9_-]+$/.test(name) || seen.has(name)) continue;
            const qualified = sheetId === 'default' ? name : `${sheetId}:${name}`;
            // A sprite that is not a composite still has to draw: its own silhouette, on the rule's
            // plate. See extractIconSilhouette.
            const icon = extractIconPlate(sprites, qualified, outDir, name)
                ?? extractIconSilhouette(sprites, qualified, outDir, name);
            if (!icon) { skipped.push(name); continue; }
            seen.add(name);
            entries.push({ name, icon });
            if (!sample) sample = icon;
        }
    }
    return { entries, skipped, sample };
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
            // EVERY sheet, written under its bare name: a style names a sprite per feature and
            // the file has to be the one a `[field]` interpolation lands on, which a qualified
            // 'transportation_road_3.png' never is. The default sheet wins a collision, since a
            // bare icon-image refers to it.
            if (names.includes(name)) continue;
            const icon = extractIcon(sprites, sheetId === 'default' ? name : `${sheetId}:${name}`,
                outDir, flattenSdf, undefined, 1, name);
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
