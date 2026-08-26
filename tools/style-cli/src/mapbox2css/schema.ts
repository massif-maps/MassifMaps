import type { Json, MapboxLayer } from './types.js';

/**
 * MapTiler's `planet_v4` splits by SOURCE LAYER what OpenMapTiles splits by a `class` field: it has
 * a `forest` layer and a `grass` layer where OpenMapTiles has one `landcover` with
 * `class = 'wood' | 'grass'`. So retargeting a MapTiler style at an OpenMapTiles tileset is a
 * rename plus an extra filter clause, per layer.
 *
 * Only what is actually equivalent is listed. A source layer with no entry is DROPPED and named in
 * the coverage report rather than guessed at - a wrong guess draws the wrong features, which is
 * worse than drawing none and much harder to notice.
 */
export interface LayerMapping {
    /** The OpenMapTiles source layer. */
    layer: string;
    /** A MapBox filter ANDed into the layer's own, standing in for what the split used to say. */
    filter?: Json;
}

function classIs(...values: string[]): Json {
    return values.length === 1
        ? ['==', ['get', 'class'], values[0]]
        : ['match', ['get', 'class'], values, true, false];
}

const OPENMAPTILES: Record<string, LayerMapping> = {
    // Same name, same meaning.
    water: { layer: 'water' },
    waterway: { layer: 'waterway' },
    building: { layer: 'building' },
    housenumber: { layer: 'housenumber' },

    // landcover, split by class in OpenMapTiles.
    ice: { layer: 'landcover', filter: classIs('ice') },
    sand: { layer: 'landcover', filter: classIs('sand') },
    wood: { layer: 'landcover', filter: classIs('wood') },
    forest: { layer: 'landcover', filter: classIs('wood') },
    grass: { layer: 'landcover', filter: classIs('grass') },
    // OpenMapTiles has no scrub CLASS - natural=scrub lands in class 'grass' with subclass 'scrub'.
    scrub: { layer: 'landcover', filter: ['==', ['get', 'subclass'], 'scrub'] },
    // MapTiler's 'vegetation' layer carries the crops; OpenMapTiles calls that class 'farmland'.
    vegetation: { layer: 'landcover', filter: classIs('farmland') },

    // landuse, likewise.
    residential: { layer: 'landuse', filter: classIs('residential') },
    industrial: { layer: 'landuse', filter: classIs('industrial') },
    cemetery: { layer: 'landuse', filter: classIs('cemetery') },
    hospital: { layer: 'landuse', filter: classIs('hospital') },
    dam: { layer: 'landuse', filter: classIs('dam') },
    pedestrian: { layer: 'landuse', filter: classIs('pedestrian') },
    // The style's own class test (pitch/stadium) does the work, as it does for the road layers.
    leisure: { layer: 'landuse' },
    park: { layer: 'park' },
    protected_area_major_label: { layer: 'park' },
    protected_area_minor_label: { layer: 'park' },

    // transportation carries every way, and its class names are the ones the MapTiler style already
    // filters on, so the style's own class test keeps working after the rename.
    road: { layer: 'transportation' },
    pathway: { layer: 'transportation' },
    railway: { layer: 'transportation' },
    ferry: { layer: 'transportation', filter: classIs('ferry') },
    pier: { layer: 'transportation', filter: classIs('pier') },
    aerialway: { layer: 'transportation', filter: classIs('aerialway') },
    bridge: { layer: 'transportation', filter: ['==', ['get', 'brunnel'], 'bridge'] },
    road_label: { layer: 'transportation_name' },
    pathway_label: { layer: 'transportation_name' },
    aerialway_label: { layer: 'transportation_name', filter: classIs('aerialway') },

    // place, split by class.
    continent_label: { layer: 'place', filter: classIs('continent') },
    country_label: { layer: 'place', filter: classIs('country') },
    state_label: { layer: 'place', filter: classIs('state', 'province') },
    city_label: { layer: 'place', filter: classIs('city') },
    town_label: { layer: 'place', filter: classIs('town') },
    place_label: {
        layer: 'place',
        // 'locality' is the OpenMapTiles class for a named place with no settlement type, which is
        // most of a French mountain valley - leaving it out dropped every hamlet name on the map.
        filter: classIs('village', 'hamlet', 'suburb', 'neighbourhood', 'quarter',
                        'isolated_dwelling', 'locality'),
    },
    island_label: { layer: 'place', filter: classIs('island') },

    water_label: { layer: 'water_name' },
    water_centroid: { layer: 'water_name' },

    peak: { layer: 'mountain_peak', filter: classIs('peak') },
    volcano: { layer: 'mountain_peak', filter: classIs('volcano') },

    aviation: { layer: 'aeroway' },
    aviation_line: { layer: 'aeroway' },
    poi_station: { layer: 'poi' },

    country_border: { layer: 'boundary' },
    country_border_disputed: { layer: 'boundary', filter: ['==', ['get', 'disputed'], 1] },
    sub_border: { layer: 'boundary' },

    // Not an OpenMapTiles layer in either schema: contours come from their own source, which an app
    // merges in under this name (CompositeVectorTileLayer::addVectorDataSource). Renaming it would
    // break that, so it passes through.
    contour: { layer: 'contour' },
};

/** Why a source layer with no entry has none, so the report says something useful. */
const NO_EQUIVALENT: Record<string, string> = {
    archipelago_label: 'OpenMapTiles place has no archipelago class',
    country_disputed_label: 'OpenMapTiles place carries no worldview fields',
};

/**
 * Fields the source schema has and the target does not, per TARGET layer. A style tests these all
 * the time - MapTiler gates every place label on `iso_a2` - and against a tileset that never
 * carries them the test can only fail, so the layer draws nothing at all. That is strictly worse
 * than not testing, so an existence test on one is dropped and reported.
 */
const MISSING_FIELDS: Record<string, string[]> = {
    place: ['iso_a2', 'iso_3166_1', 'worldview:ch', 'capital'],
    boundary: ['worldview:ch'],
    park: ['iso_a2'],
    transportation_name: ['route_1', 'route_2', 'network'],
};

/** Existence tests only: anything else is a real comparison whose answer we cannot invent. */
function isExistenceTest(node: Json, missing: string[]): boolean {
    if (!Array.isArray(node)) return false;
    const [head, ...args] = node;
    if ((head === 'has' || head === '!has') && typeof args[0] === 'string') {
        return missing.includes(args[0]);
    }
    if ((head === '==' || head === '!=') && args.length === 2 && args[1] === null) {
        const key = args[0];
        return Array.isArray(key) && key[0] === 'get' && typeof key[1] === 'string' && missing.includes(key[1]);
    }
    return false;
}

/**
 * Drop every existence test on a missing field and simplify the filter around it. Substituting a
 * bare `true` is not enough - `["all", true, ...]` is not a filter and the whole layer is then
 * dropped as malformed - so a constant is folded into its parent instead.
 *
 * Returns `false` when the whole filter collapses to it: the layer can then never match.
 */
export function dropMissingFieldTests(filter: Json, targetLayer: string, onDrop: (field: string) => void): Json {
    const missing = MISSING_FIELDS[targetLayer];
    if (!missing || filter === null || filter === undefined) return filter;

    const walk = (node: Json): Json | boolean => {
        if (!Array.isArray(node)) return node;

        if (isExistenceTest(node, missing)) {
            const head = node[0];
            const field = head === 'has' || head === '!has'
                ? (node[1] as string)
                : ((node[1] as Json[])[1] as string);
            onDrop(field);
            // `has` / `!= null` means "present"; `!has` / `== null` means "absent".
            return head === 'has' || head === '!=';
        }

        if (node[0] === 'all' || node[0] === 'any') {
            const all = node[0] === 'all';
            const kept: Json[] = [];
            for (const child of node.slice(1)) {
                const value = walk(child as Json);
                if (value === true) {
                    if (!all) return true;     // one true satisfies an any
                    continue;                  // and is free inside an all
                }
                if (value === false) {
                    if (all) return false;     // one false kills an all
                    continue;                  // and is free inside an any
                }
                kept.push(value as Json);
            }
            if (kept.length === 0) return all; // an empty all is true, an empty any is false
            if (kept.length === 1) return kept[0];
            return [node[0], ...kept] as unknown as Json;
        }

        if (node[0] === '!' && node.length === 2) {
            const value = walk(node[1] as Json);
            if (typeof value === 'boolean') return !value;
            return ['!', value] as unknown as Json;
        }

        return node;
    };

    const result = walk(filter);
    if (result === true) return null;          // nothing left to test
    return result as Json;                     // false reaches the caller as a filter that never matches
}

export type Schema = 'openmaptiles';

export interface SchemaResult {
    mapping: LayerMapping | null;
    /** Set when there is no mapping: what to tell the user. */
    why?: string;
}

export function mapSourceLayer(sourceLayer: string, schema: Schema): SchemaResult {
    if (schema !== 'openmaptiles') return { mapping: { layer: sourceLayer } };
    const mapping = OPENMAPTILES[sourceLayer];
    if (mapping) return { mapping };
    return { mapping: null, why: NO_EQUIVALENT[sourceLayer] ?? 'no OpenMapTiles equivalent' };
}

/** The layer's own filter with the mapping's extra clause ANDed in. */
export function withMappingFilter(layer: MapboxLayer, mapping: LayerMapping): Json {
    const own = layer.filter ?? null;
    if (!mapping.filter) return own;
    return own === null ? mapping.filter : ['all', mapping.filter, own];
}
