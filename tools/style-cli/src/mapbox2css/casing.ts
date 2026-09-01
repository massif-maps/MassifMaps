/**
 * Fold a casing layer into the fill layer it runs under, as one `line-border-*` rule.
 *
 * A MapBox style draws a road casing as its OWN layer, and the SDK can now draw that border from
 * the line's own vertex buffer (vt, `line-border-width`). Folding the pair halves the rules, the
 * tesselation and the tile geometry. Two shapes are recognised:
 *
 *   PLAIN   a wider line under a narrower one. The border is half the width difference, and the
 *           two must describe the same features, so the filters have to match.
 *   GAPPED  `line-gap-width` equal to the fill's width: the casing already hugs the fill, so the
 *           border is the casing's own width, exactly. Mapbox Standard writes every road casing
 *           this way, and its filters are a subset of the fill's - so the border width is gated on
 *           the casing's filter rather than the pair being refused.
 *
 * It MOVES the casing in the draw order: a style writes every casing layer before every fill layer,
 * so a wide road's fill covers a narrow road's casing at a junction; folded, each class carries its
 * own casing. That is what most styles look like anyway, but it is not what the source said.
 */

import type { Json, MapboxLayer } from './types.js';

/** Paint keys a foldable pair may carry. Anything else (patterns, offsets) is left alone. */
const FOLDABLE = new Set([
    'line-color', 'line-width', 'line-gap-width', 'line-opacity', 'line-emissive-strength',
]);

export interface FoldedCasing {
    casing: string;
    fill: string;
    kind: 'plain' | 'gapped';
}

/**
 * `strict` additionally refuses a pair with anything still drawn BETWEEN the two layers. Folding
 * moves the casing down to the fill's position, so an interposed layer that painted over the
 * casing ends up under it - Mapbox Standard has seven of those between `roads-case` and `roads`
 * (turning features, road polygons, construction) and the map visibly changes.
 */
export type FoldMode = boolean | 'strict';

interface Border {
    color: Json;
    width: Json;
}

function same(a: Json | undefined, b: Json | undefined): boolean {
    return JSON.stringify(a ?? null) === JSON.stringify(b ?? null);
}

const paintOf = (l: MapboxLayer) => (l.paint ?? {}) as Record<string, Json>;

/**
 * Is this value 1 at every zoom from `from` up? The border takes the LINE's opacity, so a casing
 * stating none may only fold into a fill that is opaque wherever the casing draws. Standard's roads
 * ramp their opacity in over z3-3.5 and case from z15, which is exactly this question.
 */
function opaqueFrom(value: Json | undefined, from: number): boolean {
    if (value === undefined || value === 1) return true;
    if (!Array.isArray(value)) return false;
    const [op, ...rest] = value as Json[];
    if (op === 'interpolate' && JSON.stringify(rest[1]) === '["zoom"]') {
        const stops = rest.slice(2);
        let last: Json = 0;
        for (let i = 0; i < stops.length; i += 2) {
            const [key, val] = [stops[i], stops[i + 1]];
            if (typeof key !== 'number') return false;
            if (key <= from) last = val;
            else if (val !== 1) return false;
        }
        return last === 1;
    }
    if (op === 'step' && JSON.stringify(rest[0]) === '["zoom"]') {
        const stops = rest.slice(2);
        let last: Json = rest[1];
        for (let i = 0; i < stops.length; i += 2) {
            const [key, val] = [stops[i], stops[i + 1]];
            if (typeof key !== 'number') return false;
            if (key <= from) last = val;
            else if (val !== 1) return false;
        }
        return last === 1;
    }
    return false;
}

/** The part of the casing's zoom range the fill does not already impose; undefined when none. */
const layerMin = (c: MapboxLayer, f: MapboxLayer) => (c.minzoom !== undefined && c.minzoom !== f.minzoom ? c.minzoom : undefined);
const layerMax = (c: MapboxLayer, f: MapboxLayer) => (c.maxzoom !== undefined && c.maxzoom !== f.maxzoom ? c.maxzoom : undefined);

/** Gate a border width on the zoom range the casing layer had, now that the layer is gone. */
function gateByZoom(width: Json, minzoom?: number, maxzoom?: number): Json {
    let gated = width;
    if (maxzoom !== undefined) gated = ['step', ['zoom'], gated, maxzoom, 0];
    if (minzoom !== undefined) gated = ['step', ['zoom'], 0, minzoom, gated];
    return gated;
}

/** Same features, same shape. Only then can one geometry carry both. */
function pairable(casing: MapboxLayer, fill: MapboxLayer): boolean {
    if (casing.type !== 'line' || fill.type !== 'line') return false;
    if (casing.source !== fill.source || casing['source-layer'] !== fill['source-layer']) return false;
    // The casing's zoom range must sit INSIDE the fill's - what is left of it is carried by the
    // border width (gateByZoom), and a casing outliving its fill has nothing to be a border of.
    if ((casing.minzoom ?? -Infinity) < (fill.minzoom ?? -Infinity)) return false;
    if ((casing.maxzoom ?? Infinity) > (fill.maxzoom ?? Infinity)) return false;
    // Joins and caps come from ONE geometry once folded, so they have to already agree.
    // line-sort-key is ordering WITHIN a layer, not shape, so it is not part of the comparison.
    const layout = (l: MapboxLayer) => {
        const { visibility, 'line-sort-key': sortKey, ...rest } = (l.layout ?? {}) as Record<string, Json>;
        return rest as Json;
    };
    if (!same(layout(casing), layout(fill))) return false;
    // A dash or a pattern rides the whole quad, so it would cover the border too - there is no
    // "solid casing under a dashed fill" in one rule.
    if (['line-dasharray', 'line-pattern'].some((k) => k in paintOf(casing) || k in paintOf(fill))) return false;
    if (![...Object.keys(paintOf(casing)), ...Object.keys(paintOf(fill))].every((k) => FOLDABLE.has(k))) return false;
    // The border takes the line's own opacity - see LineSymbolizer. Unstated is 1, and a casing
    // that states it while its fill does not is the common way to write the same thing.
    const opacity = (l: MapboxLayer) => paintOf(l)['line-opacity'] ?? 1;
    if (same(opacity(casing), opacity(fill))) return true;
    // Or the fill is opaque wherever the casing draws, which is the same thing for the border.
    return opacity(casing) === 1 && opaqueFrom(opacity(fill), casing.minzoom ?? -Infinity);
}

/** `line-gap-width` equal to the fill's width: the border is the casing's own width, exactly. */
function gappedBorder(casing: MapboxLayer, fill: MapboxLayer): Border | null {
    const [c, f] = [paintOf(casing), paintOf(fill)];
    if (c['line-gap-width'] === undefined || !same(c['line-gap-width'], f['line-width'])) return null;
    if (f['line-gap-width'] !== undefined) return null; // a gapped fill has no inside to border
    if (c['line-width'] === undefined) return null;
    // The casing usually draws fewer classes than the fill. The border is gated on its filter
    // rather than the pair refused - a filter is already a boolean expression.
    const width = same(casing.filter, fill.filter)
        ? c['line-width']
        : ['case', casing.filter as Json, c['line-width'], 0];
    return { color: c['line-color'] ?? '#000000',
             width: gateByZoom(width, layerMin(casing, fill), layerMax(casing, fill)) };
}

/** A wider line under a narrower one: the border is half the width difference. */
function plainBorder(casing: MapboxLayer, fill: MapboxLayer): Border | null {
    const [c, f] = [paintOf(casing), paintOf(fill)];
    if (c['line-gap-width'] !== undefined || f['line-gap-width'] !== undefined) return null;
    // Nothing gates the border here, so the two must draw the same features.
    if (!same(casing.filter, fill.filter)) return null;
    // A casing is a WIDTH, stated on both. Two layers that merely draw the same features in the
    // same colour are not a pair, and the border would come out of a width neither of them has.
    const [wc, wf] = [c['line-width'], f['line-width']];
    if (wc === undefined || wf === undefined) return null;
    if (typeof wc === 'number' && typeof wf === 'number' && wc <= wf) return null;
    return { color: c['line-color'] ?? '#000000',
             width: gateByZoom(['/', ['-', wc, wf], 2], layerMin(casing, fill), layerMax(casing, fill)) };
}

/**
 * Rewrites the fill of every foldable pair to carry `line-border-*`, and hides the casing. The
 * array keeps its length and order: callers index the original `style.layers` by position.
 */
export function foldCasings(layers: MapboxLayer[], mode: FoldMode = true): { layers: MapboxLayer[]; folded: FoldedCasing[]; blocked: FoldedCasing[] } {
    // Which layers the permissive pass takes away, so `strict` can tell an interposed layer that
    // survives from one that is itself folded out of the way.
    const hidden = mode === 'strict'
        ? new Set(foldCasings(layers, true).folded.map((f) => layers.findIndex((l) => l.id === f.casing)))
        : new Set<number>();
    const out = layers.slice();
    const folded: FoldedCasing[] = [];
    const blocked: FoldedCasing[] = [];
    const taken = new Set<number>();
    for (let i = 0; i < out.length; i++) {
        if (taken.has(i)) continue;
        for (let j = i + 1; j < out.length; j++) {
            if (taken.has(j) || !pairable(out[i], out[j])) continue;
            // Gapped first: a gapped casing states a width too, so the plain rule would read it
            // as a wider line and halve the wrong difference.
            const gapped = gappedBorder(out[i], out[j]);
            const border = gapped ?? plainBorder(out[i], out[j]);
            if (!border) continue;
            if (mode === 'strict') {
                // A label draws in its own pass, so it is never between anything.
                const interposed = out.slice(i + 1, j)
                    .some((l, k) => l.type !== 'symbol' && !hidden.has(i + 1 + k));
                if (interposed) {
                    blocked.push({ casing: out[i].id, fill: out[j].id, kind: gapped ? 'gapped' : 'plain' });
                    break;
                }
            }
            out[j] = { ...out[j], paint: { ...paintOf(out[j]),
                'line-border-color': border.color,
                'line-border-width': border.width,
            } } as MapboxLayer;
            out[i] = { ...out[i], layout: { ...(out[i].layout ?? {}), visibility: 'none' } } as MapboxLayer;
            folded.push({ casing: out[i].id, fill: out[j].id, kind: gapped ? 'gapped' : 'plain' });
            // One border per line: a fill with a second casing over it (Standard's bridge
            // `-shadow` beside its `-case`) keeps that one as its own rule.
            taken.add(i);
            taken.add(j);
            break;
        }
    }
    return { layers: out, folded, blocked };
}
