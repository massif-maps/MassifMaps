import type { Json, MapboxLayer } from './types.js';

/**
 * MapBox spreads over three properties what CartoCSS says in one: `symbol-placement` decides
 * whether the label repeats along a line, and the two alignments decide whether its glyphs stay
 * upright. Both alignments default to `auto`, which the spec resolves as `map` for a line
 * placement and `viewport` otherwise - so a plain point label is a BILLBOARD, not CartoCSS's
 * `point` (flat in the ground plane, only swivelling). Leaving it unset gave every converted label
 * the flat look.
 */
export function resolvePlacement(layer: MapboxLayer, kind: 'text' | 'icon'): string {
    const layout = layer.layout ?? {};
    const placement = enumOf(layout['symbol-placement']);
    const alongLine = placement === 'line' || placement === 'line-center';
    const rotation = enumOf(layout[`${kind}-rotation-alignment`]) ?? (alongLine ? 'map' : 'viewport');
    const pitch = enumOf(layout[`${kind}-pitch-alignment`]) ?? rotation;
    const upright = pitch === 'viewport';

    if (!alongLine) return upright ? 'billboard' : 'point';
    if (rotation === 'viewport') return 'billboard-line-repeat'; // upright and not turning with the line
    return upright ? 'billboard-line' : 'line';
}

/** MapLibre lays a line-placed label out along the line and never wraps it. */
export function followsLine(layer: MapboxLayer): boolean {
    const placement = enumOf(layer.layout?.['symbol-placement']);
    return placement === 'line' || placement === 'line-center';
}

/** `line-center` is the one line placement that draws ONE label, at the middle, whatever the spacing. */
export function repeatsAlongLine(layer: MapboxLayer): boolean {
    return enumOf(layer.layout?.['symbol-placement']) === 'line';
}

/** The properties resolvePlacement/markerDeclarations consume, so the loop must not re-report them. */
export const HANDLED_ELSEWHERE = new Set([
    // foldLayer's synthetic keys, not MapBox properties - see RECOLOURABLE_ICON / ICON_PARAMS.
    'massif:recolourable-icon',
    'massif:icon-params',
    'massif:icon-param-scope',
    'symbol-placement',
    'text-rotation-alignment',
    'text-pitch-alignment',
    'icon-image',
    'icon-rotate',
    'icon-size',
    'icon-color',
    'icon-opacity',
    'icon-halo-color',
    'icon-halo-width',
    'icon-allow-overlap',
    'icon-overlap',
    'icon-rotation-alignment',
    'icon-pitch-alignment',
]);

function enumOf(value: Json | undefined): string | undefined {
    return typeof value === 'string' && value !== 'auto' ? value : undefined;
}
