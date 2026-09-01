/**
 * Fold a casing layer into the fill layer it runs under, as one `line-border-*` rule.
 *
 * A MapBox style draws a road casing as its OWN layer, wider and earlier, and the SDK can now draw
 * that border from the line's own vertex buffer (vt, `line-border-width`). Folding the pair halves
 * the rules, the tesselation and the tile geometry.
 *
 * It is opt-in because it MOVES the casing in the draw order: a style writes every casing layer
 * before every fill layer, so a wide road's fill covers a narrow road's casing at a junction;
 * folded, each class carries its own casing and a later class's casing draws over an earlier
 * class's fill. That is what most styles look like anyway, but it is not what the source said.
 */

import type { Json, MapboxLayer } from './types.js';

/** Paint keys a foldable pair may carry. Anything else (dashes, patterns, gaps) is left alone. */
const FOLDABLE = new Set(['line-color', 'line-width', 'line-opacity', 'line-emissive-strength']);

export interface FoldedCasing {
    casing: string;
    fill: string;
}

function same(a: Json | undefined, b: Json | undefined): boolean {
    return JSON.stringify(a ?? null) === JSON.stringify(b ?? null);
}

/** Same features, same shape: only then do the two layers describe one road. */
function pairs(casing: MapboxLayer, fill: MapboxLayer): boolean {
    if (casing.type !== 'line' || fill.type !== 'line') return false;
    if (casing.source !== fill.source || casing['source-layer'] !== fill['source-layer']) return false;
    if (!same(casing.filter, fill.filter)) return false;
    if (casing.minzoom !== fill.minzoom || casing.maxzoom !== fill.maxzoom) return false;
    // Joins and caps come from ONE geometry once folded, so they have to already agree.
    const layout = (l: MapboxLayer) => {
        const { visibility, ...rest } = (l.layout ?? {}) as Record<string, Json>;
        return rest;
    };
    if (!same(layout(casing) as Json, layout(fill) as Json)) return false;
    const paint = (l: MapboxLayer) => Object.keys((l.paint ?? {}) as Record<string, Json>);
    if (![...paint(casing), ...paint(fill)].every((k) => FOLDABLE.has(k))) return false;
    // A casing is a WIDTH, stated on both. Two layers that merely draw the same features in the
    // same colour are not a pair, and the border would come out of a width neither of them has.
    const width = (l: MapboxLayer) => (l.paint as Record<string, Json>)?.['line-width'];
    const [wc, wf] = [width(casing), width(fill)];
    if (wc === undefined || wf === undefined) return false;
    if (typeof wc === 'number' && typeof wf === 'number' && wc <= wf) return false;
    // The border takes the line's own opacity - see LineSymbolizer. Unstated is 1, and a casing
    // that states it while its fill does not is the common way to write the same thing.
    const opacity = (l: MapboxLayer) => (l.paint as Record<string, Json>)?.['line-opacity'] ?? 1;
    return same(opacity(casing), opacity(fill));
}

/**
 * Rewrites the fill of every foldable pair to carry `line-border-*`, and hides the casing. The
 * array keeps its length and order: callers index the original `style.layers` by position.
 */
export function foldCasings(layers: MapboxLayer[]): { layers: MapboxLayer[]; folded: FoldedCasing[] } {
    const out = layers.slice();
    const folded: FoldedCasing[] = [];
    const taken = new Set<number>();
    for (let i = 0; i < out.length; i++) {
        if (taken.has(i)) continue;
        for (let j = i + 1; j < out.length; j++) {
            if (taken.has(j) || !pairs(out[i], out[j])) continue;
            const casing = (out[i].paint ?? {}) as Record<string, Json>;
            const fill = (out[j].paint ?? {}) as Record<string, Json>;
            out[j] = { ...out[j], paint: { ...fill,
                'line-border-color': casing['line-color'] ?? '#000000',
                // Half the difference: the casing's width is the WHOLE road, the border is what
                // sticks out on one side.
                'line-border-width': ['/', ['-', casing['line-width'] ?? 1, fill['line-width'] ?? 1], 2],
            } } as MapboxLayer;
            out[i] = { ...out[i], layout: { ...(out[i].layout ?? {}), visibility: 'none' } } as MapboxLayer;
            folded.push({ casing: out[i].id, fill: out[j].id });
            taken.add(i);
            taken.add(j);
            break;
        }
    }
    return { layers: out, folded };
}
