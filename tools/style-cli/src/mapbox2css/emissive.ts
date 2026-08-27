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
 * This is an APPROXIMATION, not Mapbox's shading. It has no directional term, no per-vertex normal
 * and no colour cast from the light: only the lightness moves. What it buys is a night preset that
 * reads as night instead of as a slightly muted day.
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

/** A colour literal in any spelling the translator emits. None of them nest a paren. */
const COLOUR = /#[0-9a-fA-F]{3,8}\b|\b(?:hsla?|rgba?)\([^()]*\)/g;

export function emissiveProperty(cartocss: string): string | null {
    return EMISSIVE_SOURCE.find(([pattern]) => pattern.test(cartocss))?.[1] ?? null;
}

/**
 * The factor a property's colours are multiplied by, or null when nothing has to change - no
 * emissive strength stated (mapbox's own default is 1, fully emissive, which is what we already
 * draw), a fully emissive layer, or a scene with no light value to go by.
 */
export function lightingFactor(emissive: Json, brightness: number | null): number | null {
    if (brightness === null || typeof emissive !== 'number') return null;
    const strength = Math.max(0, Math.min(1, emissive));
    if (strength >= 1) return null;
    const factor = strength + (1 - strength) * Math.max(0, Math.min(1, brightness));
    return factor >= 0.999 ? null : factor;
}

/** Every colour in a declaration value, with its LIGHTNESS scaled. Hue and saturation are kept. */
export function applyLighting(value: string, factor: number): string {
    return value.replace(COLOUR, (raw) => {
        const hsla = toHsla(raw);
        if (!hsla) return raw;
        return fromHsla(hsla[0], hsla[1], hsla[2] * factor, hsla[3]);
    });
}
