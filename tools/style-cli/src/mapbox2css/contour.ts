import type { Json, MapboxLayer } from './types.js';

/** MapBox source-layers that carry contour lines. */
const CONTOUR_SOURCE_LAYERS = new Set(['contour', 'contours']);

/** nth_line values MapTiler uses to mark an index (major) contour. */
const INDEX_NTH_LINE = new Set([5, 10]);

export interface ContourOptions {
    /** Target attribute schema. 'div' rewrites nth_line tests onto an interval-in-metres field. */
    schema?: 'div';
    /** div value at or above which a contour counts as major. */
    majorDiv: number;
}

export function isContourLayer(layer: MapboxLayer): boolean {
    const sourceLayer = layer['source-layer'];
    return sourceLayer !== undefined && CONTOUR_SOURCE_LAYERS.has(sourceLayer);
}

/**
 * MapTiler says "every 5th/10th line" (`nth_line`); tiles built with the gdal ladder say "this line
 * is a multiple of N metres" (`div`). Neither states the other's unit - the base interval is not in
 * the style at all - so only the major/minor SPLIT survives the trip, and `nth_line in (5, 10)`
 * becomes `div >= majorDiv`.
 *
 * Everything else about the layer is left alone: colours, widths and zoom ranges stay whatever the
 * source style asked for.
 */
export function rewriteContourFilter(filter: Json, options: ContourOptions, onRewrite: () => void): Json {
    if (options.schema !== 'div' || !Array.isArray(filter) || filter.length === 0) return filter;

    const [head, ...args] = filter;
    if (head === 'all' || head === 'any' || head === '!') {
        return [head, ...args.map((a) => rewriteContourFilter(a as Json, options, onRewrite))];
    }
    if (typeof head !== 'string' || !namesNthLine(args[0] as Json)) return filter;

    const values = args.slice(1).filter((v): v is number => typeof v === 'number');
    if (values.length === 0 || !values.every((v) => INDEX_NTH_LINE.has(v))) return filter;

    if (head === 'in' || head === '==') {
        onRewrite();
        return ['>=', 'div', options.majorDiv];
    }
    if (head === '!in' || head === '!=') {
        onRewrite();
        return ['<', 'div', options.majorDiv];
    }
    return filter;
}

/** The key of a filter test, in either the legacy or the expression spelling. */
function namesNthLine(key: Json): boolean {
    if (key === 'nth_line') return true;
    return Array.isArray(key) && key[0] === 'get' && key[1] === 'nth_line';
}
