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
 * Standard reads the scene's brightness 113 times, always as a two-stop ramp that switches a colour
 * between its lit and its unlit form (`[0.25, 0.3]` in 66 of them). It is how the style says
 * "night" without naming the preset. Our renderer has its own lighting and nothing measures it back
 * into a style value, so the number has to be resolved at conversion time.
 *
 * It is taken from the style's OWN `lights` block, which is config-driven like everything else:
 * the ambient light's lightness times its intensity. Nothing here reproduces Mapbox's internal
 * formula - this is a proxy - but it is read from the style rather than invented, and it separates
 * the four presets the way they are written: dawn 0.70, day 0.80, dusk 0.23, night 0.06.
 */
export function sceneBrightness(lights: Json, fold: (node: Json) => Json): number | null {
    const folded = fold(lights);
    if (!Array.isArray(folded)) return null;
    const ambient = folded.find((light) => !!light && typeof light === 'object'
        && (light as Record<string, Json>).type === 'ambient') as Record<string, Json> | undefined;
    const properties = ambient?.properties;
    if (!properties || typeof properties !== 'object' || Array.isArray(properties)) return null;

    const lightness = hslLightness((properties as Record<string, Json>).color);
    const intensity = (properties as Record<string, Json>).intensity;
    if (lightness === null || typeof intensity !== 'number') return null;
    return lightness * intensity;
}

/** The L of an `hsl()`/`hsla()` colour, 0-1. Standard writes every light colour that way. */
function hslLightness(colour: Json): number | null {
    if (typeof colour !== 'string') return null;
    const match = /^hsla?\(([^()]*)\)$/i.exec(colour.trim());
    if (!match) return null;
    const parts = match[1].split(/[,/\s]+/).filter(Boolean).map((p) => parseFloat(p));
    return parts.length >= 3 && Number.isFinite(parts[2]) ? parts[2] / 100 : null;
}

/** The presets a converted Standard style can be built for, taken from what it actually declares. */
export function presetsOf(parameters: Map<string, StyleParameterSpec>): string[] {
    const spec = parameters.get(LIGHT_PRESET);
    if (!spec) return [];
    return spec.values ? Object.keys(spec.values) : [String(spec.default)];
}
