import type { Json, MapboxLayer } from './types.js';

/**
 * Retargeting a style at a tileset built to another schema. Two source vocabularies are known -
 * MapTiler's `planet_v4` and MapBox Streets v8 - and one target, OpenMapTiles.
 *
 * MapTiler splits by SOURCE LAYER what OpenMapTiles splits by a `class` field: a `forest` layer and
 * a `grass` layer where OpenMapTiles has one `landcover` with `class = 'wood' | 'grass'`. So that
 * half is a rename plus an extra filter clause, per layer.
 *
 * MapBox Streets is further away and needs three more moves: a field carries a different NAME
 * (`height` -> `render_height`), a field carries different VALUES (`class = 'street'` ->
 * `class = 'minor'`), and one source layer stands for several target ones (`road` is both
 * `transportation` and `transportation_name`, `landuse` is both `landuse` and `landcover`).
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
    /** Emit this mapping only for the layers it answers true for - `road` splits by layer type. */
    when?: (layer: MapboxLayer) => boolean;
    /** Source field -> target field, renamed everywhere in the layer. */
    fields?: Record<string, string>;
    /**
     * Field -> source value -> what the target calls it, applied in the filter AND in the paint,
     * because a style picks a road colour with a `match` on the same field it filters on. `null` is
     * "the target has no such feature": the test becomes false and, when that is the whole filter,
     * the layer is not emitted against this target at all. `'*'` answers for every value not
     * listed, which is what makes one MapBox layer sort itself between several target ones.
     */
    values?: Record<string, Record<string, Json>>;
    /**
     * Field -> source value -> a clause standing in for it, for a value the target expresses with a
     * second field instead (`class = 'motorway_link'` is `class = 'motorway'` AND `ramp`). Applied
     * wherever the value is TESTED, paint included - a road's width is a ternary on the same class
     * its layer filters on, and leaving it behind drew every ramp at nothing or at full width.
     */
    predicates?: Record<string, Record<string, Json>>;
    /**
     * A field the target does not carry but whose ABSENCE is not the answer we want: `["get", f]`
     * becomes this literal and `["has", f]` its truth. MapBox names a road shield's artwork after
     * `shield` and `reflen`, neither of which OpenMapTiles has, and dropping them leaves an
     * unspellable sprite name and no shield at all - pinning them to the default plate draws one.
     */
    constants?: Record<string, Json>;
}

function classIs(...values: string[]): Json {
    return values.length === 1
        ? ['==', ['get', 'class'], values[0]]
        : ['match', ['get', 'class'], values, true, false];
}

const MAPTILER_TO_OPENMAPTILES: Record<string, LayerMapping | LayerMapping[]> = {
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

/**
 * MapBox Streets v8 road classes on OpenMapTiles `transportation`. The `_link` classes have no
 * class of their own there - a ramp is the base class with `ramp = 1` - so they are predicates, and
 * the base classes then have to EXCLUDE ramps or every link is drawn twice, once at link width and
 * once at motorway width.
 */
const ROAD_CLASS: Record<string, Json> = {
    motorway: 'motorway', trunk: 'trunk', primary: 'primary', secondary: 'secondary',
    tertiary: 'tertiary', street: 'minor', street_limited: 'minor', service: 'service',
    track: 'track', path: 'path', pedestrian: 'path', ferry: 'ferry', aerialway: 'aerialway',
    major_rail: 'rail', minor_rail: 'rail', service_rail: 'rail',
    // Not carried by OpenMapTiles at all.
    construction: null, golf: null, turning_circle: null, turning_loop: null, intersection: null,
};

const notRamp = (klass: string): Json =>
    ['all', ['==', ['get', 'class'], klass], ['!=', ['get', 'ramp'], 1]];
const isRamp = (klass: string): Json =>
    ['all', ['==', ['get', 'class'], klass], ['==', ['get', 'ramp'], 1]];

const ROAD_PREDICATES: Record<string, Json> = {
    motorway: notRamp('motorway'), trunk: notRamp('trunk'), primary: notRamp('primary'),
    secondary: notRamp('secondary'), tertiary: notRamp('tertiary'),
    motorway_link: isRamp('motorway'), trunk_link: isRamp('trunk'), primary_link: isRamp('primary'),
    secondary_link: isRamp('secondary'), tertiary_link: isRamp('tertiary'),
};

/** `structure = 'none'` is the ABSENCE of brunnel in OpenMapTiles, not a value of it. */
const STRUCTURE_PREDICATES: Record<string, Json> = { none: ['!', ['has', 'brunnel']] };

/**
 * MapBox spells a oneway as the STRING "true"; OpenMapTiles as the number 1. Without this every
 * arrow layer filters on a value the tiles never hold and no oneway arrow is drawn at all.
 *
 * OpenMapTiles also has `-1`, a way digitised against its direction. MapBox has no counterpart, so
 * those get no arrow rather than one pointing the wrong way.
 */
const ONEWAY: Record<string, Json> = { true: 1, false: 0 };

/**
 * OpenMapTiles has no shield artwork name and no ref length, so MapBox's `shield-reflen` sprite
 * name is unspellable. Pinning both draws Standard's DEFAULT plate with the ref on it, which is a
 * shield; leaving them absent drew nothing. 4 is the middle of the `default-2` ... `default-6` set
 * the sprite carries, so a ref of any length gets a plate near its own width.
 */
const SHIELD_CONSTANTS: Record<string, Json> = {
    shield: 'default', reflen: 4,
    shield_beta: null, shield_text_color: null, shield_text_color_beta: null,
};

/**
 * A POI carries TWO things a MapBox style reads off different fields, and OpenMapTiles puts both on
 * `class`: the icon NAME (MapBox's `maki`, and OpenMapTiles' class values are already maki-ish) and
 * the coarse CATEGORY that picks the label colour (MapBox's own `class`). One field can serve both,
 * because a category test becomes a test for the OpenMapTiles classes that belong to it - which is
 * what gets a restaurant its orange back while its icon still resolves.
 *
 * Each class belongs to exactly ONE category: a `match` takes its first branch, so a class listed
 * twice would silently answer with whichever MapBox happened to write first.
 */
const POI_CATEGORY: Record<string, Json> = {
    food_and_drink: ['restaurant', 'fast_food', 'cafe', 'bar', 'beer', 'ice_cream'],
    food_and_drink_stores: ['grocery', 'bakery', 'butcher', 'alcohol_shop', 'greengrocer'],
    store_like: ['shop', 'clothing_store', 'furniture', 'hairdresser', 'laundry', 'florist',
                 'jewelry_store', 'optician', 'bicycle', 'bookshop', 'gift'],
    education: ['school', 'college', 'library', 'kindergarten'],
    medical: ['hospital', 'pharmacy', 'doctors', 'dentist', 'veterinary'],
    sport_and_leisure: ['stadium', 'pitch', 'swimming', 'golf', 'playground', 'fitness_centre'],
    arts_and_entertainment: ['art_gallery', 'music', 'cinema', 'theatre', 'nightclub', 'casino'],
    historic: ['castle', 'monument', 'memorial', 'ruins', 'archaeological_site'],
    landmark: ['attraction', 'place_of_worship'],
    lodging: ['lodging', 'campsite'],
    motorist: ['fuel', 'car', 'parking', 'charging_station'],
    commercial_services: ['post', 'atm', 'bank', 'town_hall', 'police', 'fire_station'],
    park_like: ['park', 'garden', 'cemetery', 'dog_park'],
    visitor_amenities: ['information', 'toilets', 'drinking_water', 'picnic_site', 'harbor'],
};

const POI: LayerMapping = {
    layer: 'poi',
    values: { class: POI_CATEGORY },
    // `maki` names the icon and `type` is the raw OSM value, which is what `subclass` holds.
    fields: { maki: 'class', maki_beta: 'class', type: 'subclass', sizerank: 'rank' },
};

/**
 * MapBox picks its transit stops out of the same POI layer with `mode` and `stop_type`;
 * OpenMapTiles has neither, so with those tests dropped the layer labelled EVERY poi in the transit
 * style - measured on device, every shop and school came out as a station and drew over the real
 * POI layer. The class test is what MapBox's `mode` was saying.
 */
const POI_TRANSIT: LayerMapping = {
    layer: 'poi',
    filter: classIs('bus', 'railway', 'aerialway', 'harbor', 'ferry_terminal'),
    fields: { maki: 'class', maki_beta: 'class', network: 'class', type: 'subclass' },
};

/**
 * OpenMapTiles spells a POI class with underscores where maki - and so every MapBox sprite - uses
 * hyphens, and renames a handful outright. The icon is looked up by the FIELD's value at draw time,
 * so it cannot be rewritten in the style; an extra entry in the sprite table under the name the
 * tiles actually carry is what makes it resolve.
 */
export const ICON_ALIASES: Record<SourceSchema, Record<string, string>> = {
    maptiler: {},
    mapbox: {
        town_hall: 'town-hall', fast_food: 'fast-food', ice_cream: 'ice-cream',
        alcohol_shop: 'alcohol-shop', art_gallery: 'art-gallery', clothing_store: 'clothing-store',
        place_of_worship: 'place-of-worship', picnic_site: 'picnic-site', dog_park: 'dog-park',
        drinking_water: 'drinking-water', toilets: 'toilet', railway: 'rail',
    },
};

const MAPBOX_TO_OPENMAPTILES: Record<string, LayerMapping | LayerMapping[]> = {
    water: { layer: 'water' },
    waterway: { layer: 'waterway', fields: { structure: 'brunnel', type: 'subclass' },
                predicates: { structure: STRUCTURE_PREDICATES } },
    housenum_label: { layer: 'housenumber', fields: { house_num: 'housenumber' } },

    // One source layer, two target ones: MapBox draws a road, its arrows and its name off the same
    // tile layer, OpenMapTiles puts only the NAMED ways in a second one. So the split is by whether
    // the layer writes text - a oneway arrow is a symbol layer too, and it needs the road geometry
    // and the `oneway` field, both of which live in `transportation`.
    road: [
        {
            layer: 'transportation',
            when: (layer) => layer.type !== 'symbol' || !layer.layout?.['text-field'],
            fields: { structure: 'brunnel', type: 'subclass' },
            values: { class: ROAD_CLASS, oneway: ONEWAY },
            predicates: { class: ROAD_PREDICATES, structure: STRUCTURE_PREDICATES },
        },
        {
            layer: 'transportation_name',
            when: (layer) => layer.type === 'symbol' && !!layer.layout?.['text-field'],
            fields: { structure: 'brunnel', type: 'subclass' },
            values: { class: ROAD_CLASS },
            predicates: { class: ROAD_PREDICATES, structure: STRUCTURE_PREDICATES },
            constants: SHIELD_CONSTANTS,
        },
    ],

    // MapBox splits a green area by what it IS; OpenMapTiles by whether it is ground cover or a
    // human use of the ground, so one MapBox layer feeds both. A mapping whose class test cannot
    // match its target drops out on its own.
    landuse: [
        {
            layer: 'landuse',
            values: {
                class: {
                    cemetery: 'cemetery', hospital: 'hospital', school: 'school',
                    residential: 'residential', industrial: 'industrial',
                    commercial_area: 'commercial', pitch: 'pitch', '*': null,
                },
            },
        },
        {
            layer: 'landcover',
            values: {
                class: {
                    wood: 'wood', scrub: 'grass', grass: 'grass', agriculture: 'farmland',
                    sand: 'sand', glacier: 'ice', rock: 'rock', '*': null,
                },
            },
            // OpenMapTiles keeps the raw OSM value under `subclass`, which is what MapBox's
            // `type` is - and it is how a playground or a pitch is told from plain grass.
            fields: { type: 'subclass' },
            // leisure=park and leisure=pitch are landcover 'grass' with their own subclass there.
            predicates: {
                class: {
                    park: ['==', ['get', 'subclass'], 'park'],
                    pitch: ['==', ['get', 'subclass'], 'pitch'],
                },
            },
        },
    ],
    landcover: {
        layer: 'landcover',
        values: { class: { wood: 'wood', scrub: 'grass', grass: 'grass', crop: 'farmland', snow: 'ice' } },
    },
    // OpenMapTiles `park` is protected areas only; a wetland has no equivalent there.
    landuse_overlay: {
        layer: 'park',
        values: { class: { national_park: 'national_park', '*': null } },
    },

    building: { layer: 'building', fields: { height: 'render_height', min_height: 'render_min_height' } },

    aeroway: { layer: 'aeroway', fields: { type: 'class' } },
    airport_label: {
        layer: 'aerodrome_label',
        values: {
            class: { civil: ['international', 'public', 'regional'], military: 'military', '*': null },
        },
    },

    admin: {
        layer: 'boundary',
        // MapBox counts levels from the country down (0/1/2); OpenMapTiles keeps OSM's own.
        values: {
            admin_level: { 0: 2, 1: 4, 2: 6 },
            disputed: { true: 1, false: 0 },
            maritime: { true: 1, false: 0 },
        },
    },

    place_label: {
        layer: 'place',
        // symbolrank and OpenMapTiles' rank are both "lower is more important" but not the same
        // scale; renaming keeps the major/minor split working, which dropping it would not.
        fields: { symbolrank: 'rank' },
        values: {
            class: {
                country: 'country', state: ['state', 'province'],
                settlement: ['city', 'town', 'village', 'hamlet', 'isolated_dwelling', 'locality'],
                settlement_subdivision: ['suburb', 'quarter', 'neighbourhood'],
                // Every `disputed_*` class: OpenMapTiles has no worldview, so there is no such
                // feature to draw and the layer that only asks for them drops out.
                '*': null,
            },
        },
    },

    // MapBox's one label layer is three OpenMapTiles ones, told apart by the class the style tests.
    natural_label: [
        {
            layer: 'water_name',
            values: {
                class: {
                    bay: 'lake', sea: 'ocean', ocean: 'ocean', water: 'lake', reservoir: 'lake',
                    dock: 'dock', river: 'river', canal: 'river', stream: 'river', '*': null,
                },
            },
        },
        {
            layer: 'mountain_peak',
            values: { class: { landform: ['peak', 'volcano'], '*': null } },
            fields: { elevation_m: 'ele' },
        },
        { layer: 'place', values: { class: { continent: 'continent', '*': null } } },
    ],

    poi_label: POI,
    transit_stop_label: POI_TRANSIT,
};

const TABLES: Record<SourceSchema, Record<string, LayerMapping | LayerMapping[]>> = {
    maptiler: MAPTILER_TO_OPENMAPTILES,
    mapbox: MAPBOX_TO_OPENMAPTILES,
};

/** Why a source layer with no entry has none, so the report says something useful. */
const NO_EQUIVALENT: Record<SourceSchema, Record<string, string>> = {
    maptiler: {
        archipelago_label: 'OpenMapTiles place has no archipelago class',
        country_disputed_label: 'OpenMapTiles place carries no worldview fields',
    },
    mapbox: {
        structure: 'OpenMapTiles carries no gates, fences, crosswalks or land structures',
        motorway_junction: 'OpenMapTiles has no junction layer',
        indoor_structure: 'OpenMapTiles carries no indoor mapping',
        wind_turbine: 'OpenMapTiles has no wind turbine layer',
        tree: 'OpenMapTiles carries no individual trees',
        hillshade: "MapBox's vector hillshade has no OpenMapTiles counterpart - use a DEM layer",
    },
};

/**
 * Fields the source schema has and the target does not, per TARGET layer. A style tests these all
 * the time - MapTiler gates every place label on `iso_a2`, MapBox Standard gates every POI on
 * `filterrank` - and against a tileset that never carries them the test can only fail, so the layer
 * draws nothing at all. That is strictly worse than not testing, so the test is dropped and
 * reported, and the layer draws too much rather than nothing.
 */
const MISSING_FIELDS: Record<SourceSchema, Record<string, string[]>> = {
    maptiler: {
        place: ['iso_a2', 'iso_3166_1', 'worldview:ch', 'capital'],
        boundary: ['worldview:ch'],
        park: ['iso_a2'],
        transportation_name: ['route_1', 'route_2', 'network'],
    },
    mapbox: {
        // Every layer: MapBox gates most of its labels on the map's worldview, which no
        // OpenMapTiles tileset carries at all.
        '*': ['worldview'],
        place: ['filterrank', 'text_anchor', 'type'],
        boundary: ['iso_3166_1'],
        poi: ['filterrank', 'mode', 'stop_type'],
        aerodrome_label: ['filterrank', 'sizerank'],
        water_name: ['filterrank', 'sizerank'],
        mountain_peak: ['filterrank', 'sizerank', 'elevation_ft'],
        transportation: ['iso_3166_1', 'shield', 'shield_beta', 'shield_text_color',
                         'shield_text_color_beta', 'reflen', 'bike_lane'],
        // shield / reflen are pinned to the default plate by SHIELD_CONSTANTS, not dropped.
        transportation_name: ['iso_3166_1', 'len'],
        building: ['underground', 'extrude', 'type'],
        landuse: ['sizerank', 'type'],
        landcover: ['sizerank'],
        park: ['sizerank', 'type', 'filterrank'],
        aeroway: ['ref'],
    },
};

/** Existence tests, which have an answer we can give; everything else is a comparison. */
function existenceTest(node: Json[], missing: string[]): { field: string; present: boolean } | null {
    const [head, ...args] = node;
    if ((head === 'has' || head === '!has') && typeof args[0] === 'string' && missing.includes(args[0])) {
        return { field: args[0], present: head === 'has' };
    }
    if ((head === '==' || head === '!=') && args.length === 2 && args[1] === null) {
        const field = fieldOf(args[0]);
        if (field && missing.includes(field)) return { field, present: head === '!=' };
    }
    return null;
}

/**
 * The field a test reads, in either the legacy or the expression spelling. Casts are transparent:
 * Standard writes `["<=", ["number", ["get", "filterrank"]], ...]`, and stopping at the `number`
 * left the whole test standing on a field the tiles do not carry.
 */
const CASTS = new Set(['number', 'to-number', 'string', 'to-string', 'boolean', 'to-boolean']);

function fieldOf(key: Json): string | null {
    if (typeof key === 'string') return key;
    if (!Array.isArray(key)) return null;
    if (key[0] === 'get' && typeof key[1] === 'string') return key[1];
    if (typeof key[0] === 'string' && CASTS.has(key[0]) && key.length === 2) return fieldOf(key[1] as Json);
    return null;
}

const COMPARISONS = new Set(['==', '!=', '<', '<=', '>', '>=', 'in', '!in', 'match']);

/** Whether a test's key is a plain field the target DOES carry. */
function presentField(key: Json, missing: string[]): boolean {
    const field = fieldOf(key);
    return field !== null && !missing.includes(field);
}

/** The first missing field a subtree reads, if any. Only `get`/`has`: a bare string may be a value. */
function missingFieldRead(node: Json, missing: string[]): string | null {
    if (!Array.isArray(node)) return null;
    if ((node[0] === 'get' || node[0] === 'has') && typeof node[1] === 'string'
            && missing.includes(node[1])) {
        return node[1];
    }
    for (const child of node) {
        const found = missingFieldRead(child as Json, missing);
        if (found) return found;
    }
    return null;
}

/**
 * Drop every test on a missing field and simplify the filter around it. Substituting a bare `true`
 * is not enough - `["all", true, ...]` is not a filter and the whole layer is then dropped as
 * malformed - so a constant is folded into its parent instead.
 *
 * Returns `false` when the whole filter collapses to it: the layer can then never match.
 */
export function dropMissingFieldTests(filter: Json, targetLayer: string, source: SourceSchema,
                                      onDrop: (field: string) => void): Json {
    const table = MISSING_FIELDS[source];
    const missing = [...(table['*'] ?? []), ...(table[targetLayer] ?? [])];
    if (missing.length === 0 || filter === null || filter === undefined) return filter;

    const walk = (node: Json): Json | boolean => {
        if (!Array.isArray(node)) return node;

        const existence = existenceTest(node, missing);
        if (existence) {
            onDrop(existence.field);
            return existence.present;
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

        // A comparison that READS a field the tiles do not carry can only ever fail. Answering it
        // TRUE is the lesser wrong: the layer then draws everything it would have filtered, which
        // is visible and reported, where answering false draws nothing and reads as a broken style.
        //
        // The whole subtree counts, not just the left operand: Standard gates its landuse polygons
        // on `(0 + sizerank) - <a zoom ramp> <= 14`, and stopping at the operand left that standing
        // on a field OpenMapTiles has none of.
        // A test whose OWN key is a field the tiles do have keeps standing, whatever its branches
        // read - `match class ... -> <a worldview test>` is a class test, and only the branch is
        // unanswerable. Recursion below takes care of that one.
        if (typeof node[0] === 'string' && COMPARISONS.has(node[0]) && !presentField(node[1] as Json, missing)) {
            const read = missingFieldRead(node, missing);
            if (read) {
                onDrop(read);
                return true;
            }
        }

        // MapBox buries the worldview test inside the OUTPUT of another `match`, so stopping at the
        // boolean combinators left it standing and every country label vanished. Recurse into
        // everything; a `true` substituted inside a `case` or a `match` output is still valid there.
        const walked = node.map((item) => walk(item as Json)) as unknown as Json[];
        return sameAnswer(walked) ?? (walked as unknown as Json);
    };

    const result = walk(filter);
    if (result === true) return null;          // nothing left to test
    return result as Json;                     // false reaches the caller as a filter that never matches
}

/**
 * A `case` or `match` all of whose branches now answer the same constant, which is what is left of a
 * worldview test once the field is gone. Folding it matters: a filter that comes out a plain
 * comparison becomes a bracketed CartoCSS predicate instead of a per-feature `when()`.
 */
function sameAnswer(node: Json[]): boolean | null {
    const head = node[0];
    const first = head === 'case' ? 2 : head === 'match' ? 3 : -1;
    if (first < 0 || node.length < first + 2) return null;
    const outputs: Json[] = [node[node.length - 1]];
    for (let i = first; i < node.length - 1; i += 2) outputs.push(node[i]);
    if (outputs.every((o) => o === true)) return true;
    if (outputs.every((o) => o === false)) return false;
    return null;
}

/** Rename a field everywhere in a value: filter tests, expressions and `{token}` text fields. */
export function renameField(value: Json, from: string, to: string): Json {
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

/** What one source value becomes: nothing, one target value, or several. */
function mapped(table: Record<string, Json>, value: Json): { hit: true; values: Json[] } | { hit: false } {
    if (value === null || typeof value === 'object') return { hit: false };
    const entry = table[String(value)] ?? (String(value) in table ? null : table['*']);
    if (entry === undefined) return { hit: false };
    if (entry === null) return { hit: true, values: [] };
    return { hit: true, values: Array.isArray(entry) ? (entry as Json[]) : [entry] };
}

/**
 * Pin a field the target does not carry to a fixed value: `["get", f]` becomes it, `["has", f]`
 * becomes whether it is one. Everywhere, because the name of a road shield's sprite is spelled in
 * the LAYOUT, not the filter.
 */
function applyConstants(node: Json, constants: Record<string, Json>): Json {
    if (!Array.isArray(node)) {
        if (node && typeof node === 'object') {
            return Object.fromEntries(
                Object.entries(node).map(([key, item]) => [key, applyConstants(item as Json, constants)]),
            ) as unknown as Json;
        }
        return node;
    }
    if ((node[0] === 'get' || node[0] === 'has') && typeof node[1] === 'string' && node[1] in constants) {
        const value = constants[node[1]];
        return node[0] === 'has' ? value !== null : value;
    }
    return node.map((item) => applyConstants(item as Json, constants)) as unknown as Json;
}

/**
 * Fold what a pinned field leaves behind. Substituting alone is not enough: MapBox spells its
 * shield name `coalesce(concat(shield_beta, "-", reflen), ...)` behind a `has shield_beta` gate, and
 * a literal `null` left inside that concat reached the SDK as `concat(null, '-')` - "Unsupported
 * binary operation", which fails the whole stylesheet, not just the layer.
 */
function foldConstant(node: Json): Json {
    if (!Array.isArray(node)) {
        if (node && typeof node === 'object') {
            return Object.fromEntries(
                Object.entries(node).map(([key, item]) => [key, foldConstant(item as Json)]),
            ) as unknown as Json;
        }
        return node;
    }

    const folded = node.map((item) => foldConstant(item as Json)) as Json[];
    const head = folded[0];
    const args = folded.slice(1);

    if (head === 'concat') return args.some((v) => v === null) ? null : (folded as unknown as Json);
    if (CASTS.has(String(head)) && folded.length === 2 && !Array.isArray(args[0])) {
        const value = args[0];
        if (value === null) return head === 'to-boolean' || head === 'boolean' ? false : null;
        if (head === 'to-string' || head === 'string') return String(value);
        if (head === 'to-boolean' || head === 'boolean') return Boolean(value);
        if (head === 'to-number' || head === 'number') return Number(value);
    }
    if (head === 'coalesce') {
        const kept = args.filter((v) => v !== null);
        if (kept.length === 0) return null;
        if (kept.length === 1) return kept[0];
        return ['coalesce', ...kept] as unknown as Json;
    }
    if (head === 'all' || head === 'any') {
        const all = head === 'all';
        const kept: Json[] = [];
        for (const arg of args) {
            if (arg === all) continue;              // true in an all, false in an any: free
            if (typeof arg === 'boolean') return arg; // the other one decides it outright
            kept.push(arg);
        }
        if (kept.length === 0) return all;
        if (kept.length === 1) return kept[0];
        return [head, ...kept] as unknown as Json;
    }
    if (head === '!' && folded.length === 2 && typeof args[0] === 'boolean') return !args[0];
    if (head === 'case' && folded.length >= 4) {
        const kept: Json[] = ['case'];
        for (let i = 1; i < folded.length - 1; i += 2) {
            if (folded[i] === false) continue;
            // A branch that always fires ENDS the chain - but only answers outright when nothing
            // undecided stands before it.
            if (folded[i] === true) {
                return kept.length === 1 ? folded[i + 1] : ([...kept, folded[i + 1]] as unknown as Json);
            }
            kept.push(folded[i], folded[i + 1]);
        }
        kept.push(folded[folded.length - 1]);
        return kept.length === 2 ? kept[1] : (kept as unknown as Json);
    }
    if (head === 'match' && folded.length >= 4 && args[0] === null) return folded[folded.length - 1];
    // `reflen <= 6` on a pinned reflen is a constant, and left standing it is a per-feature when().
    if (typeof head === 'string' && COMPARISONS.has(head) && folded.length === 3
            && !Array.isArray(args[0]) && !Array.isArray(args[1])
            && args[0] !== null && args[1] !== null && head !== 'match') {
        const [a, b] = args as [number | string | boolean, number | string | boolean];
        switch (head) {
        case '==': return a === b;
        case '!=': return a !== b;
        case '<': return a < b;
        case '<=': return a <= b;
        case '>': return a > b;
        case '>=': return a >= b;
        default: break;
        }
    }
    return folded as unknown as Json;
}

/** Several source values often share one target one - major_rail and minor_rail are both `rail`. */
function dedupe(values: Json[]): Json[] {
    const seen = new Set<string>();
    return values.filter((v) => {
        const key = JSON.stringify(v);
        if (seen.has(key)) return false;
        seen.add(key);
        return true;
    });
}

/**
 * Rewrite a field's VALUES to what the target schema calls them, wherever the field is read - the
 * filter and the paint alike, because a style picks a road's colour with a `match` on the class it
 * also filters on.
 */
export function remapValues(node: Json, values: Record<string, Record<string, Json>>): Json {
    if (!Array.isArray(node)) {
        if (node && typeof node === 'object') {
            return Object.fromEntries(
                Object.entries(node).map(([key, item]) => [key, remapValues(item as Json, values)]),
            ) as unknown as Json;
        }
        return node;
    }

    const head = node[0];
    const field = typeof head === 'string' ? fieldOf(node[1] as Json) : null;
    const table = field ? values[field] : undefined;

    if (table && (head === '==' || head === '!=')) {
        const result = mapped(table, node[2] as Json);
        if (result.hit) {
            const equal = head === '==';
            const unique = dedupe(result.values);
            if (unique.length === 0) return equal ? false : true;
            if (unique.length === 1) return [head, node[1], unique[0]] as unknown as Json;
            const each = unique.map((v) => [equal ? '==' : '!=', node[1], v] as unknown as Json);
            return [equal ? 'any' : 'all', ...each] as unknown as Json;
        }
    }

    if (table && (head === 'in' || head === '!in')) {
        const out: Json[] = [];
        for (const value of node.slice(2)) {
            const result = mapped(table, value as Json);
            if (!result.hit) out.push(value as Json);
            else out.push(...result.values);
        }
        const unique = dedupe(out);
        if (unique.length === 0) return head === 'in' ? false : true;
        return [head, node[1], ...unique] as unknown as Json;
    }

    // `match` is both a filter and a paint value; its labels are at the odd positions and the last
    // argument is the default. A branch whose every label drops takes its output with it.
    if (table && head === 'match' && node.length >= 4) {
        const out: Json[] = ['match', remapValues(node[1] as Json, values)];
        for (let i = 2; i < node.length - 1; i += 2) {
            const labels = Array.isArray(node[i]) ? (node[i] as Json[]) : [node[i] as Json];
            const kept: Json[] = [];
            for (const label of labels) {
                const result = mapped(table, label);
                if (!result.hit) kept.push(label);
                else kept.push(...result.values);
            }
            const unique = dedupe(kept);
            if (unique.length === 0) continue;
            out.push(unique.length === 1 && !Array.isArray(node[i]) ? unique[0] : (unique as unknown as Json));
            out.push(remapValues(node[i + 1] as Json, values));
        }
        out.push(remapValues(node[node.length - 1] as Json, values));
        // Nothing but the default left: a `match` needs at least one branch, so hand back the
        // default itself - which for a filter is the `false` that drops the layer.
        return (out.length === 3 ? out[2] : (out as unknown as Json));
    }

    return node.map((item) => remapValues(item as Json, values)) as unknown as Json;
}

/**
 * Replace `field = value` tests the target expresses with a SECOND field. Only the equality
 * spellings, wherever they appear: those are boolean contexts, and a `match` whose outputs are not
 * booleans is a value table, not a test, so it is left to the value map.
 */
function applyPredicates(node: Json, predicates: Record<string, Record<string, Json>>): Json {
    if (!Array.isArray(node)) {
        if (node && typeof node === 'object') {
            return Object.fromEntries(
                Object.entries(node).map(([key, item]) => [key, applyPredicates(item as Json, predicates)]),
            ) as unknown as Json;
        }
        return node;
    }

    const head = node[0];
    const field = typeof head === 'string' ? fieldOf(node[1] as Json) : null;
    const table = field ? predicates[field] : undefined;

    if (table && (head === '==' || head === '!=')) {
        const clause = table[String(node[2])];
        if (clause !== undefined) return head === '==' ? clause : (['!', clause] as unknown as Json);
    }
    if (table && (head === 'in' || head === '!in')) {
        const clauses = node.slice(2).map((v) => table[String(v)] ?? ['==', node[1], v] as unknown as Json);
        if (node.slice(2).some((v) => table[String(v)] !== undefined)) {
            const any = clauses.length === 1 ? clauses[0] : (['any', ...clauses] as unknown as Json);
            return head === 'in' ? any : (['!', any] as unknown as Json);
        }
    }
    if (table && head === 'match' && node.length >= 4 && node[node.length - 1] === false) {
        // The expression spelling of `in`: match these labels -> true, anything else -> false.
        const clauses: Json[] = [];
        let rewrote = false;
        for (let i = 2; i < node.length - 1; i += 2) {
            if (node[i + 1] !== true) return node.map((item) => applyPredicates(item as Json, predicates)) as unknown as Json;
            for (const label of Array.isArray(node[i]) ? (node[i] as Json[]) : [node[i] as Json]) {
                const clause = table[String(label)];
                if (clause !== undefined) { clauses.push(clause); rewrote = true; }
                else clauses.push(['==', node[1], label] as unknown as Json);
            }
        }
        if (rewrote) return clauses.length === 1 ? clauses[0] : (['any', ...clauses] as unknown as Json);
    }

    // A `match` that picks a VALUE cannot carry a predicate as a label, so it becomes a `case`.
    // This is what a road's width is: `match class [motorway] 3.2 ... [motorway_link] 0.8`, and
    // left alone the link branch is unreachable on a tileset where a ramp IS a motorway - every
    // ramp drew at full motorway width.
    if (table && head === 'match' && node.length >= 4) {
        const labelsOf = (i: number) => (Array.isArray(node[i]) ? (node[i] as Json[]) : [node[i] as Json]);
        let needed = false;
        for (let i = 2; i < node.length - 1; i += 2) {
            if (labelsOf(i).some((l) => table[String(l)] !== undefined)) needed = true;
        }
        if (needed) {
            const out: Json[] = ['case'];
            for (let i = 2; i < node.length - 1; i += 2) {
                const clauses = labelsOf(i).map((l) => table[String(l)] ?? (['==', node[1], l] as unknown as Json));
                out.push(clauses.length === 1 ? clauses[0] : (['any', ...clauses] as unknown as Json));
                out.push(applyPredicates(node[i + 1] as Json, predicates));
            }
            out.push(applyPredicates(node[node.length - 1] as Json, predicates));
            return out as unknown as Json;
        }
    }

    return node.map((item) => applyPredicates(item as Json, predicates)) as unknown as Json;
}

export type Schema = 'openmaptiles';
export type SourceSchema = 'maptiler' | 'mapbox';

/** Source layers only one of the two vocabularies has, which is how a style names itself. */
const SIGNATURE: Record<SourceSchema, string[]> = {
    mapbox: ['place_label', 'poi_label', 'natural_label', 'landuse_overlay', 'motorway_junction',
             'admin', 'structure', 'transit_stop_label', 'airport_label', 'housenum_label'],
    maptiler: ['forest', 'grass', 'scrub', 'vegetation', 'city_label', 'town_label',
               'country_border', 'sub_border', 'poi_station', 'housenumber', 'peak', 'volcano',
               'road_label', 'aviation', 'water_label'],
};

/**
 * Which vocabulary the style's source layers are written in. Only a CLEAR winner counts: a tie is
 * returned as null so the caller can ask, rather than retargeting half the style at the wrong
 * table and drawing the wrong features.
 */
export function detectSourceSchema(sourceLayers: Iterable<string>): SourceSchema | null {
    const names = new Set(sourceLayers);
    const score = (schema: SourceSchema) => SIGNATURE[schema].filter((n) => names.has(n)).length;
    const mapbox = score('mapbox');
    const maptiler = score('maptiler');
    if (mapbox >= 2 && mapbox > maptiler) return 'mapbox';
    if (maptiler >= 2 && maptiler > mapbox) return 'maptiler';
    return null;
}

export interface SchemaResult {
    /** Every target this source layer feeds, in the order they should be drawn. */
    mappings: LayerMapping[];
    /** Set when there is none: what to tell the user. */
    why?: string;
}

export function mapSourceLayer(sourceLayer: string, schema: Schema, source: SourceSchema): SchemaResult {
    if (schema !== 'openmaptiles') return { mappings: [{ layer: sourceLayer }] };
    const entry = TABLES[source][sourceLayer];
    if (entry) return { mappings: Array.isArray(entry) ? entry : [entry] };
    return { mappings: [], why: NO_EQUIVALENT[source][sourceLayer] ?? 'no OpenMapTiles equivalent' };
}

/** The layer's own filter with the mapping's extra clause ANDed in. */
export function withMappingFilter(layer: MapboxLayer, mapping: LayerMapping): Json {
    const own = layer.filter ?? null;
    if (!mapping.filter) return own;
    return own === null ? mapping.filter : ['all', mapping.filter, own];
}

export interface RetargetNotes {
    /** A field the target does not carry, whose test was dropped. */
    dropped: (field: string) => void;
    /** A rename or a value map that is close rather than exact. */
    approximate: (what: string) => void;
}

/**
 * The layer as the target schema would have written it, or null when its filter can never match
 * there - which is how one MapBox layer feeding several targets picks the ones it belongs to.
 *
 * Order matters: predicates and value maps are keyed by the SOURCE field names, so both run before
 * the rename, and dropping a test on a field the target lacks runs before the rename too.
 */
export function retargetLayer(layer: MapboxLayer, mapping: LayerMapping, source: SourceSchema,
                              notes: RetargetNotes): MapboxLayer | null {
    if (mapping.when && !mapping.when(layer)) return null;

    const filter = withMappingFilter(layer, mapping);
    let out: MapboxLayer = { ...layer, filter: (filter ?? undefined) as MapboxLayer['filter'] };
    if (mapping.constants) {
        out = foldConstant(applyConstants(out as unknown as Json, mapping.constants)) as unknown as MapboxLayer;
    }
    if (mapping.predicates) {
        out = applyPredicates(out as unknown as Json, mapping.predicates) as unknown as MapboxLayer;
    }
    if (mapping.values) {
        out = remapValues(out as unknown as Json, mapping.values) as unknown as MapboxLayer;
    }

    const merged = dropMissingFieldTests(out.filter ?? null, mapping.layer, source, notes.dropped);
    if (merged === false) return null;
    // A layer that had a filter and has none left now matches EVERY feature of its target. That is
    // how Standard's transit stops came to label every shop and school: their `mode`/`stop_type`
    // tests are all OpenMapTiles has no counterpart for, so nothing was left to select on.
    if (out.filter !== undefined && merged === null) {
        notes.approximate('every test in its filter was on a field the target does not carry, ' +
            'so it now draws the whole layer');
    }
    out = { ...out, filter: (merged ?? undefined) as MapboxLayer['filter'] };

    for (const [from, to] of Object.entries(mapping.fields ?? {})) {
        out = renameField(out as unknown as Json, from, to) as unknown as MapboxLayer;
        notes.approximate(`"${from}" read as the target's "${to}"`);
    }
    return out;
}
