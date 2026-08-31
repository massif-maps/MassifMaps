import type { Json, MapboxStyle } from './types.js';

/**
 * Mapbox Standard's `config`, `schema` and `imports` - the three things that make it not a plain
 * style document.
 *
 * Standard ships as a ROOT style with no layers of its own: one `imports` entry pointing at the
 * basemap fragment, and a `config` block overriding the fragment's defaults. The fragment declares
 * what may be configured in `schema`, and its layers read those values with `["config", name]`.
 * `lightPreset` is the one that matters here - it is how Standard does day/dusk/dawn/night, and it
 * is a per-value recolour of most of the style rather than a second stylesheet.
 *
 * A style parameter is the same shape: named, defaulted in project.json, enumerated, and settable
 * at runtime. So the mapping is 1:1 and the converted style keeps switching without a reload.
 */

/** One configurable value, as project.json declares it. */
export interface StyleParameterSpec {
    default: Json;
    values?: Record<string, Json>;
}

export interface ConfigResult {
    /** Parameter name -> its declaration, for project.json's `styleparameters`. */
    parameters: Map<string, StyleParameterSpec>;
    /** Names read by a layer but declared nowhere - reported, and defaulted to what was read. */
    undeclared: string[];
}

/** The preset names Standard's `lightPreset` takes, in the order its schema lists them. */
export const LIGHT_PRESETS = ['day', 'dawn', 'dusk', 'night'] as const;

/** Standard's own name for the knob that recolours the whole basemap. */
export const LIGHT_PRESET = 'lightPreset';

/** Every `["config", name]` in the style, in first-seen order. */
export function configNames(style: MapboxStyle): string[] {
    const found: string[] = [];
    const seen = new Set<string>();
    const walk = (node: Json): void => {
        if (Array.isArray(node)) {
            if (node[0] === 'config' && typeof node[1] === 'string' && !seen.has(node[1])) {
                seen.add(node[1]);
                found.push(node[1]);
            }
            for (const child of node) walk(child as Json);
        } else if (node && typeof node === 'object') {
            for (const child of Object.values(node)) walk(child);
        }
    };
    walk((style.layers ?? []) as unknown as Json);
    return found;
}

/**
 * What the style says each configurable value is: the fragment's `schema` for the default and the
 * allowed set, then every `imports[].config` on top - that is the override the root document is
 * for, and it is what a style URL's `?config=` ends up as.
 */
export function resolveConfig(style: MapboxStyle): ConfigResult {
    const parameters = new Map<string, StyleParameterSpec>();

    const schema = (style as Record<string, Json>).schema;
    if (schema && typeof schema === 'object' && !Array.isArray(schema)) {
        for (const [name, entry] of Object.entries(schema)) {
            if (!entry || typeof entry !== 'object' || Array.isArray(entry)) continue;
            const spec: StyleParameterSpec = { default: (entry as Record<string, Json>).default ?? '' };
            const values = (entry as Record<string, Json>).values;
            // The SDK's enum is a map, so a value can be renamed without touching the rules; the
            // spec's is a plain list and every entry stands for itself.
            if (Array.isArray(values) && values.every((v) => typeof v === 'string')) {
                spec.values = Object.fromEntries((values as string[]).map((v) => [v, v]));
            }
            parameters.set(name, spec);
        }
    }

    for (const entry of imports(style)) {
        const config = entry.config;
        if (!config || typeof config !== 'object' || Array.isArray(config)) continue;
        for (const [name, value] of Object.entries(config)) {
            const existing = parameters.get(name);
            parameters.set(name, { ...existing, default: value });
        }
    }

    // A layer may read a value the schema never declared - a fragment converted on its own has no
    // root document to carry one. Declaring it anyway is what keeps `[param::x]` resolvable.
    const undeclared: string[] = [];
    for (const name of configNames(style)) {
        if (parameters.has(name)) continue;
        undeclared.push(name);
        parameters.set(name, name === LIGHT_PRESET
            ? { default: 'day', values: Object.fromEntries(LIGHT_PRESETS.map((p) => [p, p])) }
            : { default: '' });
    }

    return { parameters, undeclared };
}

/** The style's `imports` entries, whatever shape the document is in. */
export function imports(style: MapboxStyle): Array<{ id?: string; url?: string; config?: Json }> {
    const raw = (style as Record<string, Json>).imports;
    if (!Array.isArray(raw)) return [];
    return raw.filter((e): e is Record<string, Json> => !!e && typeof e === 'object' && !Array.isArray(e));
}

/**
 * Standard's root document has no layers of its own. Converting it silently produces an empty
 * style, so say what is missing and where the layers actually are.
 */
export function importOnly(style: MapboxStyle): string | null {
    const entries = imports(style);
    if (entries.length === 0 || (style.layers ?? []).length > 0) return null;
    const urls = entries.map((e) => e.url ?? e.id ?? '?').join(', ');
    return `the style has no layers of its own - it imports ${urls}. ` +
        'Fetch the imported fragment and convert THAT; a root document carries only the config.';
}

/**
 * What `["measure-light", "brightness"]` resolves to for a given config.
 *
 * Standard reads the scene's brightness 113 times, always as a ramp that switches a colour between
 * its lit and its unlit form (`[0.25, 0.3]` in 66 of them). It is how the style says "night"
 * without naming the preset. Our renderer has its own lighting and nothing measures it back into a
 * style value, so the number has to be resolved at conversion time.
 *
 * This is `Style.calculateLightsBrightness` (src/style/style.ts) ported, not approximated: the
 * DIRECTIONAL light counts too, weighted by how high it stands, and the luminance is the W3C
 * relative one - a 2.4 gamma with a linear toe - not the colour's HSL lightness. The ambient-only
 * proxy this replaces read Standard's presets as 0.80 / 0.70 / 0.23 / 0.06 where mapbox computes
 * 0.478 / 0.397 / 0.026 / 0.013, so every ramp whose stops are not the common `[0.25, 0.3]` pair
 * was sampled at the wrong place.
 */
export function sceneBrightness(lights: Json, fold: (node: Json) => Json): number | null {
    const scene = foldedLights(lights, fold);
    if (!scene) return null;
    const { ambient, directional } = scene;
    const ambientBrightness = relativeLuminance(ambient.colour) * ambient.intensity;
    // The polar angle is measured from straight up, so a sun at the zenith counts fully and one on
    // the horizon not at all.
    const polarIntensity = 1 - directional.polar / 90;
    const directionalBrightness = relativeLuminance(directional.colour) * directional.intensity * polarIntensity;
    return Number(((directionalBrightness + ambientBrightness) / 2).toFixed(6));
}

/** One light, resolved. */
interface FoldedLight {
    colour: [number, number, number];
    intensity: number;
    polar: number;
}

/**
 * The lights block folded for one config, as sRGB channels and plain numbers. An intensity may be
 * a zoom ramp; `pick` takes its last stop, the value every zoom a building is drawn at gets.
 */
function foldedLights(lights: Json, fold: (node: Json) => Json):
        { ambient: FoldedLight; directional: FoldedLight } | null {
    const folded = fold(lights);
    if (!Array.isArray(folded)) return null;
    const propertiesOf = (type: string): Record<string, Json> | null => {
        const light = folded.find((l) => !!l && typeof l === 'object'
            && (l as Record<string, Json>).type === type) as Record<string, Json> | undefined;
        const properties = light?.properties;
        return properties && typeof properties === 'object' && !Array.isArray(properties)
            ? properties as Record<string, Json> : null;
    };
    const ambient = propertiesOf('ambient');
    if (!ambient) return null;
    const directional = propertiesOf('directional') ?? {};
    const rgb = (colour: Json): [number, number, number] | null =>
        typeof colour === 'string' ? hslChannels(colour) : null;
    const ambientRgb = rgb(ambient.color);
    if (!ambientRgb) return null;
    const directionalRgb = rgb(directional.color);
    const direction = ((): number[] | null => {
        const value = directional.direction;
        const list = Array.isArray(value) && value[0] === 'literal' ? value[1] as Json : value as Json;
        return Array.isArray(list) && list.length === 2 && list.every((v) => typeof v === 'number')
            ? list as number[] : null;
    })();
    return {
        ambient: { colour: ambientRgb, intensity: lastStop(ambient.intensity, 1), polar: 0 },
        directional: directionalRgb && direction
            ? { colour: directionalRgb, intensity: lastStop(directional.intensity, 1), polar: direction[1] }
            : { colour: [0, 0, 0], intensity: 0, polar: 90 },
    };
}

/** The last stop of a ramp, or the value itself when it is already one. */
function lastStop(value: Json, fallback: number): number {
    if (typeof value === 'number') return value;
    if (!Array.isArray(value)) return fallback;
    const head = value[0];
    if (head === 'literal') return lastStop((value[1] ?? null) as Json, fallback);
    if ((head === 'step' || head === 'interpolate' || head === 'case' || head === 'match') && value.length >= 4) {
        return lastStop(value[value.length - 1] as Json, fallback);
    }
    return fallback;
}

/** W3C relative luminance of sRGB channels - mapbox's own, gamma 2.4 with a linear toe. */
function relativeLuminance([r, g, b]: [number, number, number]): number {
    const channel = (c: number) => (c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4));
    return 0.2126 * channel(r) + 0.7152 * channel(g) + 0.0722 * channel(b);
}

/** sRGB channels 0-1 of an `hsl()`/`hsla()` colour. Standard writes every light colour that way. */
function hslChannels(colour: string): [number, number, number] | null {
    const match = /^hsla?\(([^()]*)\)$/i.exec(colour.trim());
    if (!match) return null;
    const parts = match[1].split(/[,/\s]+/).filter(Boolean).map((p) => parseFloat(p));
    if (parts.length < 3 || parts.some((p) => !Number.isFinite(p))) return null;
    const [h, s, l] = [parts[0], parts[1] / 100, parts[2] / 100];
    const c = (1 - Math.abs(2 * l - 1)) * s;
    const hp = (((h % 360) + 360) % 360) / 60;
    const x = c * (1 - Math.abs((hp % 2) - 1));
    const [r, g, b] = hp < 1 ? [c, x, 0] : hp < 2 ? [x, c, 0] : hp < 3 ? [0, c, x]
        : hp < 4 ? [0, x, c] : hp < 5 ? [x, 0, c] : [c, 0, x];
    const m = l - c / 2;
    return [r + m, g + m, b + m];
}

/** The presets a converted Standard style can be built for, taken from what it actually declares. */
export function presetsOf(parameters: Map<string, StyleParameterSpec>): string[] {
    const spec = parameters.get(LIGHT_PRESET);
    if (!spec) return [];
    return spec.values ? Object.keys(spec.values) : [String(spec.default)];
}
