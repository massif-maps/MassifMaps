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
 * The light has a COLOUR and a DIRECTION, and both are carried: the multiply is per CHANNEL and
 * the factor is MapBox's own ground radiance (`groundRadiance` below),
 *
 *     shown_c = authored_c x (emissive + (1 - emissive) x radiance_c)
 *
 * which is `mix(apply_lighting_ground(color), color, emissive_strength)` written out.
 *
 * It is still an approximation of the whole model - a 2D layer has no normal, so every surface is
 * lit as if it faced up - but the light itself is theirs rather than a fitted proxy.
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
 * The mapbox LAYER TYPE decides the emissive property, not the CartoCSS name it was translated to.
 * A `circle` becomes a marker, whose `icon-emissive-strength` a circle layer never states - so it
 * read as fully emissive and stayed identical at every preset, where gl-js lights it
 * (circle.fragment.glsl goes through apply_lighting_with_emission_ground). A fill's OUTLINE becomes
 * a `line-color` and was looked up against `line-emissive-strength`, which a fill layer does not
 * state either; mapbox lights the outline with the fill's own.
 */
const EMISSIVE_BY_LAYER_TYPE: Record<string, string> = {
    circle: 'circle-emissive-strength',
    fill: 'fill-emissive-strength',
};

/** The emissive property a layer of this TYPE uses, when the property name alone would mislead. */
export function emissiveForLayerType(type: string | undefined): string | null {
    return type ? EMISSIVE_BY_LAYER_TYPE[type] ?? null : null;
}

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
    'circle-emissive-strength': 0,
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

/** The lights block, resolved for one preset - everything the radiance below is a function of. */
export interface SceneLights {
    ambientColour: string;
    ambientIntensity: number;
    directionalColour: string;
    directionalIntensity: number;
    /** MapBox's polar angle, degrees from straight up. */
    directionalPolar: number;
}

const toLinear = (c: number) => Math.pow(c, 2.2);
const toSRGB = (c: number) => Math.pow(Math.max(0, Math.min(1, c)), 1 / 2.2);

/**
 * What MapBox's own light does to a FLAT, upward-facing surface - `calculateGroundRadiance`
 * (3d-style/render/lights.ts) with the ground normal, which is the value `apply_lighting_ground`
 * multiplies every unlit 2D colour by.
 *
 * Ported rather than approximated. The proxy this replaces raised a scalar brightness to a fitted
 * gamma and floored the colour ratio at neutral, which cost the two things that separate the
 * presets: dawn came out 8% darker and NEUTRAL where MapBox draws it at full brightness with a
 * fifth of its blue removed - so day and dawn looked alike - and every preset's tint was a lift
 * only, never a subtraction.
 */
export function groundRadiance(lights: SceneLights | null): [number, number, number] | null {
    if (!lights) return null;
    const ambient = toHsla(lights.ambientColour);
    const directional = toHsla(lights.directionalColour);
    if (!ambient || !directional) return null;
    const scale = (colour: [number, number, number], intensity: number) =>
        hslToRgb(colour[0], colour[1], colour[2]).map((c) => toLinear(c) * intensity) as [number, number, number];
    const amb = scale([ambient[0], ambient[1], ambient[2]], lights.ambientIntensity);
    const dir = scale([directional[0], directional[1], directional[2]], lights.directionalIntensity);
    const dirZ = Math.cos((lights.directionalPolar * Math.PI) / 180);
    // The sky is brighter near the sun, and a face turned away from it loses up to 30% of the
    // ambient. A ground normal is never turned away, so this is 1 whenever the sun is up.
    const minFactor = 1 - 0.3 * Math.min(rgbLuminance(dir), 1);
    const ambientDirectional = minFactor + (1 - minFactor) * Math.min(dirZ + 1, 1);
    return [0, 1, 2].map((i) => toSRGB(amb[i] * ambientDirectional + dir[i] * Math.max(0, dirZ))) as
        [number, number, number];
}

/**
 * The per-channel factor a property's colours are multiplied by, or null when nothing has to
 * change - a fully emissive layer, or a scene with no lights to go by.
 *
 * MapBox's `mix(apply_lighting_ground(color), color, emissive_strength)`, exactly: a self-lit
 * surface keeps its authored colour under any light, which is what stops a road casing turning
 * blue at night, and an unlit one is the radiance.
 */
export function lightingFactor(
    emissive: Json,
    radiance: [number, number, number] | null,
): [number, number, number] | null {
    if (radiance === null || typeof emissive !== 'number') return null;
    const strength = Math.max(0, Math.min(1, emissive));
    if (strength >= 1) return null;
    const factor = radiance.map((c) => Math.max(0, Math.min(1, strength + (1 - strength) * c))) as
        [number, number, number];
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
