/**
 * What the generated typings are supposed to do, as code the compiler checks.
 *
 * Every `@ts-expect-error` is a NEGATIVE test: if the line ever stops being an error, tsc fails
 * the build. That is what keeps "a wrong path is a compile error" from quietly becoming false
 * when the schema or the generator changes.
 *
 * Run with `npm run typings.api.check`.
 */
import { MapCamera, MassifLayer, MassifMap, MassifObject, attach, create, createLayer, createSource, find } from '../../../../src/index.js';

declare const view: any;

// --- create -----------------------------------------------------------------------------------

const osm = createSource('osm', { type: 'http', minZoom: 0, maxZoom: 14, url: 'https://tiles/{z}/{x}/{y}.pbf' });

// @ts-expect-error `url` is required: no HTTPTileDataSource constructor overload does without it.
createSource('bad', { type: 'http', maxZoom: 14 });

// @ts-expect-error a key the SDK would drop with a warning is a compile error instead
createSource('typo', { type: 'http', url: 'x', maxZom: 14 });

// @ts-expect-error `local` needs a projection - both of its overloads take one
createSource('nolocal', { type: 'local' });

// A nested child is either an inline spec of the kind that builds it, or a registry id.
createSource('cached', { type: 'memory-cache', capacity: 33554432, source: { type: 'http', url: 'https://tiles/{z}/{x}/{y}.png' } });
createSource('cached2', { type: 'memory-cache', source: 'osm' });

// @ts-expect-error a style spec is not a source spec
createSource('wrongchild', { type: 'memory-cache', source: { type: 'mbvt' } });

const base = createLayer('base', {
    type: 'vector',
    opacity: 0.8,
    source: 'osm',
    style: { type: 'mbvt', cartocss: { type: 'cartocss', css: '#water{polygon-fill:#0000ff;}' } }
});

// --- the created handle carries its concrete class --------------------------------------------

// VectorTileLayer's own property, reached because the spec type named the class.
base.set('labelBlendingSpeed', 1.5);
// Layer's, reached through the base chain.
base.set('opacity', 0.5);
// TileDataSource's, on the source.
osm.set('maxOverzoomLevel', 2);

// @ts-expect-error a raster layer's property is not a vector layer's
base.set('rasterFilterMode', 'RASTER_TILE_FILTER_MODE_BILINEAR');

// @ts-expect-error a read-only property cannot be written
osm.set('minZoom', 3);

// --- a bag: the last segment is a KEY the app chose --------------------------------------------

const style = create('style', 'osm-style', { type: 'mbvt', cartocss: { type: 'cartocss', css: '#water{polygon-fill:#0af;}' } });

// One style parameter, as a property rather than call('setStyleParameter', ...).
style.set('params.water_color', '#0af');
// And every one at once, which is a single crossing.
style.set('params', { water_color: '#0af', land_color: '#eee' });

// @ts-expect-error a style parameter is text - the SDK parses it against what the style declares
style.set('params.water_color', 3);

// --- aliases are the same property under a readable name --------------------------------------

const aliased = attach(view);
aliased.set('fog.rangeStart', 2.5);
aliased.set('fogOptions.rangeStart', 2.5);

// @ts-expect-error an alias resolves one segment, it does not invent the rest
aliased.set('fog.nope', 1);

// --- enums are constant names, not numbers ----------------------------------------------------

base.set('labelRenderOrder', 'VECTOR_TILE_RENDER_ORDER_LAST');

// A number is accepted too: the value IS an int, and a name resolves to one.
base.set('labelRenderOrder', 1);

// @ts-expect-error a constant from another enum is not one of them either
base.set('labelRenderOrder', 'CLICK_TYPE_SINGLE');

const order: 'VECTOR_TILE_RENDER_ORDER_HIDDEN' | 'VECTOR_TILE_RENDER_ORDER_LAYER' | 'VECTOR_TILE_RENDER_ORDER_LAST' | number = base.get('labelRenderOrder');
void order;

// --- values come back as the JavaScript type they should be -----------------------------------

const speed: number = base.get('labelBlendingSpeed');
const filter: string = base.get('rendererLayerFilter');
const extent: [[number, number] | [number, number, number], [number, number] | [number, number, number]] = osm.get('dataExtent');
void speed;
void filter;
void extent;

// @ts-expect-error a float property is not a string
const wrong: string = base.get('opacity');
void wrong;

// --- dotted paths -----------------------------------------------------------------------------

const map: MassifMap = attach(view);
map.set('fogOptions.rangeStart', 2.5);
map.fog().set('rangeEnd', 4);
map.terrain().set('exaggeration', 1.4);

// @ts-expect-error a scalar is not traversable
map.set('fieldOfViewY.x', 1);

// @ts-expect-error an object property is not readable as a value - group() is how you reach it
map.get('fogOptions');

// group() carries the class through, so the nested property completes.
const range: number = map.group('fogOptions').get('rangeStart');
void range;

// @ts-expect-error not a property of FogOptions
map.group('fogOptions').set('exaggeration', 1);

// --- methods ----------------------------------------------------------------------------------

const tile = osm.call('loadTile', [8467, 5852, 14]);
const bytes: ArrayBuffer | null = tile.getData('data');
tile.destroy();
void bytes;

// @ts-expect-error a tile is [x, y, zoom], not a pair
osm.call('loadTile', [8467, 5852]);

// @ts-expect-error loadTile is on a source, not on a layer
base.call('loadTile', [8467, 5852, 14]);

base.call('clearTileCaches', true);
base.call('refresh');

const hillshade = createLayer('hs', { type: 'hillshade', source: 'dem' });
const metres: number[] = hillshade.call('getElevations', [
    [5.76, 45.24],
    [5.77, 45.25]
]);
void metres;

// --- events -----------------------------------------------------------------------------------

map.on('map.clicked', (e) => {
    const at = e.getPos('clickPos', 'EPSG:4326');
    void at;
});

// The Observable form narrows the same way.
base.on('vectortile.clicked', (e) => {
    const name: string = e.get('featureLayerName');
    void name;
});

// @ts-expect-error not an event this class declares
map.on('map.wiggled', () => {});

base.subscribe('vectortile.clicked', (e) => {
    // The payload's class comes from the event table, so its own properties complete.
    const layerName: string = e.get('featureLayerName');
    const id: number = e.get('featureId');
    void layerName;
    void id;
});

// --- consuming ---------------------------------------------------------------------------------

base.on('vectortile.clicked', (e) => {
    if (e.get('featureLayerName') === 'poi') {
        e.consumed = true; // nothing behind this handler sees the click
    }
    const claimable: boolean = e.consumable;
    void claimable;
});

map.on('map.clicked', (e) => {
    // Settable on every event, but `consumable` is false here - MapEventListener::onMapClicked
    // returns void, so there is nothing to tell.
    e.consumed = true;
});

base.on('vectortile.clicked', (e) => {
    // @ts-expect-error `consumable` is what the event reports, not something a handler sets
    e.consumable = true;
});

// @ts-expect-error a source has no click event
osm.subscribe('vectortile.clicked', () => {});

// @ts-expect-error not an event this class declares
map.subscribe('map.wiggled', () => {});

// --- find -------------------------------------------------------------------------------------

const found: MassifObject<'massif::VectorTileLayer'> | null = find('layer', 'base', 'massif::VectorTileLayer');
found?.set('opacity', 1);

// @ts-expect-error not a class the tables know
find('layer', 'base', 'massif::NotAClass');

// --- the fluent surface --------------------------------------------------------------------------

base.visible(true).opacity(0.5).zoomRange([4, 18]).refresh();
const isVisible: boolean = base.visible();
const currentOpacity: number = base.opacity();
void isVisible;
void currentOpacity;

// @ts-expect-error the setter takes a boolean
base.visible(1);

const camera: MapCamera = map.camera();
camera.animate(400).zoom(14).tilt(45).rotation(0);
camera.moveTo([5.7606, 45.2442], { zoom: 13.6, tilt: 25, duration: 800 });
camera.moveTo({ lat: 45.2442, lon: 5.7606 }, { zoom: 13.6 });
camera.fitBounds([
    [5.7, 45.1],
    [5.8, 45.3]
]);

const here: [number, number] | [number, number, number] = camera.position();
const z: number = camera.zoom();
void here;
void z;

map.eventProjection('EPSG:4326').onClick((e) => {
    const at = e.getPos('clickPos');
    void at;
});
map.onIdle(() => {});

const dem = createSource('dem', { type: 'http', url: 'https://dem/{z}/{x}/{y}.png' });
const bytes2: ArrayBuffer | null = dem.loadTile([8467, 5852, 14]);
void bytes2;

// @ts-expect-error a tile is [x, y, zoom]
dem.loadTile([8467, 5852]);

const profile: number[] = hillshade.elevations([[5.76, 45.24], { lat: 45.25, lon: 5.77 }]);
void profile;

// --- threads and throttling ----------------------------------------------------------------------

// Every handler is main-thread; there is no delivery knob to get wrong.
map.on('map.moved', () => {});
map.subscribe('map.moved', () => {}, { throttle: 16 });
map.subscribe('map.clicked', () => {}, { projection: 'EPSG:3857' });

// @ts-expect-error delivery is not a thing - see the note on DELIVERY_ORIGIN
map.subscribe('map.moved', () => {}, { delivery: 'ui' });

// @ts-expect-error nor is the facade's coalescing, which does nothing on this delivery
map.subscribe('map.moved', () => {}, { coalesce: true });

// --- routing needs no plugin code: it is create + callAsync --------------------------------------

const router = map.object('routing', 'valhalla', {
    type: 'valhalla-online',
    profile: 'bicycle',
    customServiceURL: 'https://valhalla1.openstreetmap.de/{service}'
});

// @ts-expect-error `profile` is a real property; this is not
map.object('routing', 'bad', { type: 'valhalla-online', proffile: 'bicycle' });

// The request is one of the hand-written factories, so it names its class to get its methods.
const route = map.object(
    'routing',
    'route-request',
    {
        type: 'request',
        projection: 'EPSG:4326',
        points: [
            [5.72, 45.18],
            [5.74, 45.24]
        ]
    },
    'massif::RoutingRequest'
);
route.call('setCustomParameter', 'language', 'fr-FR');

async function computeRoute() {
    const summary = await router.callAsync('calculateRoute', [route.handle], (result) => ({
        // Properties of RoutingResult, straight off the generated table.
        metres: result.get('totalDistance'),
        seconds: result.get('totalTime'),
        // The path is the FLAT channel - x0,y0,x1,y1,… - because a 9 km route is 562 positions.
        path: result.call('getPoints'),
        // A collection is read one element per crossing; the counts are properties.
        steps: result.collect((step) => step.get('streetName'), { countPath: 'instructionCount', method: 'getInstruction' })
    }));
    void summary.metres;
    void summary.path;
    void summary.steps;
}
void computeRoute;

// --- a nested object key takes a handle, an id, or a spec ------------------------------------
//
// The three branches of `childOf`. A handle is how an app shares an object it already holds -
// an overlay drawing the base map's tiles - and used to be a compile error on any key whose
// class had no writable property of the same name.

createSource('shared', { type: 'memory-cache', source: osm.handle });
createLayer('overlay', { type: 'raster', source: osm.handle });

// The same for a key that is a writable PROPERTY rather than a constructor argument:
// applySpecProperties sends those through childOf too, so an inline spec is read there as well.
create('elementstyle', 'border', {
    type: 'polygon',
    color: 0xff0e7afe,
    lineStyle: { type: 'line', color: 0xff0e7afe, width: 1 }
});
create('elementstyle', 'border2', { type: 'polygon', lineStyle: 'someRegisteredLineStyle' });

// @ts-expect-error a source spec is not an elementstyle spec
create('elementstyle', 'wrongchild', { type: 'polygon', lineStyle: { type: 'http', url: 'x' } });

// --- the hand-written factories ---------------------------------------------------------------
//
// Shapes SpecFactories.cpp accepts that no constructor signature describes, so the schema alone
// cannot produce them - see scripts/api-typings/factories.mjs.

// buildSearch: the layer already on the map, instead of the source/decoder pair.
map.object('search', 'from-layer', { type: 'vectortile', layer: base.handle, minZoom: 14, maxZoom: 14, preventDuplicates: true });
map.object('search', 'from-layer-id', { type: 'vectortile', layer: 'base' });

// @ts-expect-error the constructor form still needs both of its arguments
map.object('search', 'half', { type: 'vectortile', source: 'osm' });

// buildGeometry: a document, where every other geometry type takes the shape it names.
create('search', 'proximity', {
    type: 'request',
    filterExpression: "name='x'",
    geometry: { type: 'geojson', geojson: { type: 'Point', coordinates: [5.76, 45.24] } }
});
create('geometry', 'from-text', { type: 'geojson', geojson: '{"type":"Point","coordinates":[5.76,45.24]}' });

// @ts-expect-error a geojson geometry needs its document
create('geometry', 'empty', { type: 'geojson' });

// --- an object whose class is not narrowed ------------------------------------------------------
//
// What `layers().get(i)`, `source()` and `adoptLayer` hand back. Nothing is known about the class,
// the C++ resolves the path against the runtime one, and the typings must not pretend otherwise -
// `Path` and `WritablePath` already said `string` here while `ValuePath` and `MethodArgs` collapsed.

declare const bare: MassifLayer;
const anyValue: number = bare.get('maxZoom');
bare.set('opacity', 0.5);
bare.call('clearTileCaches', true);
void anyValue;

// A NARROWED class still checks, which is the whole point of the guard being conditional.
// @ts-expect-error PersistentCacheTileDataSource has no readable `databasePath` - it is a constructor argument
find('source', 'cached', 'massif::PersistentCacheTileDataSource')?.get('databasePath');

// --- getPos says which of the two coordinate shapes it is --------------------------------------

map.on('map.clicked', (e) => {
    const at: [number, number] | [number, number, number] | null = e.getPos('clickPos');
    void at;
});
const extentBounds: [[number, number] | [number, number, number], [number, number] | [number, number, number]] | null = osm.getPos('dataExtent');
void extentBounds;

// @ts-expect-error a MapPos path does not read back as a MapBounds
const notBounds: [[number, number], [number, number]] | null = map.getPos('focusPos');
void notBounds;

// --- the named click helpers carry their own payload -------------------------------------------
//
// They subscribe to ONE event each, so the payload is that event's - not the layer's `C`, which
// is `any` on everything the registry hands back and left the callback with no fields at all.

bare.onFeatureClick((e) => {
    const layerName: string = e.get('featureLayerName');
    const clickedAt = e.getPos('clickPos');
    e.consumed = true;
    void layerName;
    void clickedAt;
});

bare.onElementClick((e) => {
    const meta = e.get('vectorElement.metaData');
    void meta;
});
