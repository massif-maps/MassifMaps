import { fromHsla, toHsla } from './fold.js';
import type { Json } from './types.js';

/**
 * `*-emissive-strength`, approximated by darkening the colour.
 *
 * Mapbox lights its 2D layers with the same scene lights as its 3D ones: emissive-strength is how
 * much of a layer's colour is EMITTED rather than lit. At 1 the colour is drawn as authored; at 0 it
 * is entirely at the mercy of the ambient and directional lights, so it goes dark at night. Standard
 * sets it on 103 properties, and that - not different colours - is how its night preset gets dark.
 *
 * Our renderer has no such model for 2D geometry: it draws every colour as authored, which is
 * emissive-strength 1 everywhere. So the lighting is folded into the colour at conversion time:
 *
 *     shown = authored x (emissive + (1 - emissive) x brightness)
 *
 * with `brightness` the same scene value `measure-light` resolves to (see config.ts). A fully
 * emissive layer is unchanged at any preset - which is what keeps labels and lit windows bright at
 * night - and a non-emissive one follows the light down.
 *
 * The light has a COLOUR, and it is carried: Standard's night ambient is `hsl(217, 100%, 11%)`
 * with the directional light at intensity 0, so the only light in the scene is blue and a night
 * land drawn with the lightness alone came out brown. The multiply is per CHANNEL,
 *
 *     shown_c = authored_c x (emissive + (1 - emissive) x brightness x cast_c)
 *
 * where `cast` is this preset's ambient chroma over the DEFAULT preset's, both taken at equal
 * luminance - so a white light (day) leaves every channel at 1 and the default preset is still
 * drawn exactly as authored.
 *
 * This is an APPROXIMATION, not Mapbox's shading: no directional term and no per-vertex normal.
 * What it buys is a night preset that reads as night instead of as a slightly muted day.
 */

/** Which mapbox emissive property lights a given CartoCSS property. */
const EMISSIVE_SOURCE: Array<[RegExp, string]> = [
    [/^background-/, 'background-emissive-strength'],
    [/^shield-icon-/, 'icon-emissive-strength'],
    [/^shield-/, 'text-emissive-strength'],
    [/^marker-/, 'icon-emissive-strength'],
    [/^text-/, 'text-emissive-strength'],
    [/^line-/, 'line-emissive-strength'],
    [/^polygon-/, 'fill-emissive-strength'],
    [/^building-/, 'fill-extrusion-emissive-strength'],
];

/**
 * What MapBox uses when a layer states none, which is NOT one value for all of them.
 *
 * A LABEL is emissive by default and stays legible at night; GEOMETRY is not, and is lit like any
 * other surface. Reading "unstated" as fully emissive left Standard's `roads-case` - which states
 * nothing - drawing its white casing over a night map, and its trees a bright green.
 */
const EMISSIVE_DEFAULT: Record<string, number> = {
    'text-emissive-strength': 1,
    'icon-emissive-strength': 1,
    'background-emissive-strength': 0,
    'line-emissive-strength': 0,
    'fill-emissive-strength': 0,
    'fill-extrusion-emissive-strength': 0,
    'model-emissive-strength': 0,
};

export function emissiveDefault(source: string | null): number | null {
    return source === null ? null : EMISSIVE_DEFAULT[source] ?? null;
}

/** A colour literal in any spelling the translator emits. None of them nest a paren. */
const COLOUR = /#[0-9a-fA-F]{3,8}\b|\b(?:hsla?|rgba?)\([^()]*\)/g;

export function emissiveProperty(cartocss: string): string | null {
    return EMISSIVE_SOURCE.find(([pattern]) => pattern.test(cartocss))?.[1] ?? null;
}

/** sRGB channels 0-1 of an HSL triple, h in degrees and s/l in percent. */
function hslToRgb(h: number, s: number, l: number): [number, number, number] {
    const S = Math.max(0, Math.min(1, s / 100));
    const L = Math.max(0, Math.min(1, l / 100));
    const c = (1 - Math.abs(2 * L - 1)) * S;
    const hp = ((((h % 360) + 360) % 360) / 60);
    const x = c * (1 - Math.abs((hp % 2) - 1));
    const [r, g, b] = hp < 1 ? [c, x, 0] : hp < 2 ? [x, c, 0] : hp < 3 ? [0, c, x]
        : hp < 4 ? [0, x, c] : hp < 5 ? [x, 0, c] : [c, 0, x];
    const m = L - c / 2;
    return [r + m, g + m, b + m];
}

function rgbLuminance([r, g, b]: [number, number, number]): number {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

/** A light colour as channels at luminance 1 - its chroma with its brightness divided out. */
function chroma(colour: string | null): [number, number, number] | null {
    const hsla = colour === null ? null : toHsla(colour);
    if (!hsla) return null;
    const rgb = hslToRgb(hsla[0], hsla[1], hsla[2]);
    const luminance = rgbLuminance(rgb);
    return luminance > 0 ? [rgb[0] / luminance, rgb[1] / luminance, rgb[2] / luminance] : null;
}

/**
 * How this preset's light colours a surface, relative to the style's own default preset. [1, 1, 1]
 * where either is unknown or the two match, which is what leaves the default preset as authored.
 */
export function lightCast(colour: string | null, defaultColour: string | null): [number, number, number] {
    const here = chroma(colour);
    const base = chroma(defaultColour);
    if (!here || !base) return [1, 1, 1];
    const ratio = [0, 1, 2].map((i) => (base[i] > 0 ? here[i] / base[i] : 1));
    // A light TINTS, it does not subtract: mapbox's night ambient has no red at all, yet a night
    // render is not red-free - their shader carries a neutral base the style does not describe.
    // Measured on Standard at night (see CAST_GAMMA), the cast is a mild blue LIFT, so the ratio
    // is floored at neutral and pulled towards it.
    return ratio.map((c) => Math.pow(Math.max(1, c), CAST_GAMMA)) as [number, number, number];
}

/**
 * The two numbers that fit this approximation to a MEASURED mapbox-gl night render, sampled from
 * `wasm/mbref.html` over Les Halles (2.34580 / 48.86300, z16.78, lightPreset night):
 *
 *   surface   authored              gl-js draws        emissive
 *   land      hsl(20, 20%, 95%)     rgb 38, 40, 51     0
 *   park      hsl(115, 60%, 80%)    rgb 72, 91, 86     0.25
 *
 * Solving both for the light gives a lit term of about (0.18, 0.175, 0.27) - luminance 0.18 where
 * the ambient-only proxy says 0.075, and a blue lift of 1.5x where the raw chroma ratio says 2.9x.
 * A gamma on each keeps the DEFAULT preset exactly at 1 (1^g = 1) while lifting every darker one,
 * which no additive floor can do.
 */
const LIT_GAMMA = 2 / 3;
const CAST_GAMMA = 1 / 3;

/**
 * The per-channel factor a property's colours are multiplied by, or null when nothing has to
 * change - a fully emissive layer, or a scene with no light value to go by.
 *
 * The cast multiplies the LIT term only, never the emissive one, so a self-lit surface keeps its
 * authored colour under any light - which is what stops a road casing turning into saturated blue
 * at night.
 */
export function lightingFactor(
    emissive: Json,
    brightness: number | null,
    cast: [number, number, number] = [1, 1, 1],
): [number, number, number] | null {
    if (brightness === null || typeof emissive !== 'number') return null;
    const strength = Math.max(0, Math.min(1, emissive));
    if (strength >= 1) return null;
    const lit = (1 - strength) * Math.pow(Math.max(0, Math.min(1, brightness)), LIT_GAMMA);
    const factor = cast.map((c) => Math.max(0, Math.min(1, strength + lit * c))) as [number, number, number];
    return factor.every((f) => f >= 0.999) ? null : factor;
}

/**
 * Every colour in a declaration value, multiplied per CHANNEL by the light.
 *
 * A uniform factor is a plain darkening; a coloured one also moves the hue. Note that scaling every
 * channel by the same k does NOT keep the HSL saturation - the number drops as the colour darkens,
 * while the channel RATIOS, which are what the eye reads as hue and chroma, are untouched.
 */
export function applyLighting(value: string, factor: [number, number, number]): string {
    return value.replace(COLOUR, (raw) => {
        const hsla = toHsla(raw);
        if (!hsla) return raw;
        const [r, g, b] = hslToRgb(hsla[0], hsla[1], hsla[2]);
        const [h, s, l] = rgbToHslTriple([r * factor[0], g * factor[1], b * factor[2]]);
        return fromHsla(h, s, l, hsla[3]);
    });
}

/** Back to the HSL the palette is written in. */
function rgbToHslTriple([r, g, b]: [number, number, number]): [number, number, number] {
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    const l = (max + min) / 2;
    const d = max - min;
    if (d === 0) return [0, 0, l * 100];
    const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
    const h = max === r ? ((g - b) / d + (g < b ? 6 : 0))
        : max === g ? ((b - r) / d + 2)
        : ((r - g) / d + 4);
    return [h * 60, s * 100, l * 100];
}
