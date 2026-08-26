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
    /** What the TARGET tiles call the elevation. MapTiler's own say `height`. */
    elevationField?: string;
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

/**
 * The elevation carries a different name in each schema: MapTiler's contour tiles call it `height`,
 * the gdal ladder and `ContourTileDataSource` call it `ele`. Unlike the nth_line/div pair this is
 * the SAME quantity in the same unit, so it is a plain rename - and without it a converted contour
 * label reads a field that is not there and draws nothing.
 */
const MAPBOX_ELEVATION_FIELD = 'height';
export const DEFAULT_ELEVATION_FIELD = 'ele';

export function rewriteContourFields(value: Json, options: ContourOptions): Json {
    const to = options.elevationField ?? DEFAULT_ELEVATION_FIELD;
    if (options.schema !== 'div' || to === MAPBOX_ELEVATION_FIELD) return value;
    return renameField(value, MAPBOX_ELEVATION_FIELD, to);
}

function renameField(value: Json, from: string, to: string): Json {

    if (Array.isArray(value)) {
        // The legacy spelling names the field as a bare string in argument 1.
        if (typeof value[0] === 'string' && value[1] === from && value[0] !== 'get') {
            return [value[0], to, ...value.slice(2)] as unknown as Json;
        }
        if (value[0] === 'get' && value[1] === from) {
            return ['get', to] as unknown as Json;
        }
        return value.map((item) => renameField(item as Json, from, to)) as unknown as Json;
    }
    // The token form a text-field uses: "{height}".
    if (typeof value === 'string' && value.includes(`{${from}}`)) {
        return value.split(`{${from}}`).join(`{${to}}`);
    }
    if (value && typeof value === 'object') {
        return Object.fromEntries(
            Object.entries(value).map(([key, item]) => [key, renameField(item as Json, from, to)]),
        ) as unknown as Json;
    }
    return value;
}
