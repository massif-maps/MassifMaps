/*
 * Type tests for the generated massif.d.ts.
 *
 * This file is never run - `tsc --noEmit --strict` on it IS the test. Every `@ts-expect-error`
 * only compiles when the line under it really is an error, so the file fails both ways: if the
 * types stop catching a mistake, and if they start rejecting something legal.
 *
 *   node_modules/.bin/tsc --noEmit --strict bindings/typescript/massif.test.ts
 */

import { call, create, get, on, set, Handle, Position } from './massif';

// --- specs -------------------------------------------------------------------------------------

const source = create('source', 'osm', {
  type: 'http',
  url: 'https://tile.example.com/{z}/{x}/{y}.png',
  maxZoom: 19,
});

// A type the kind does not have.
// @ts-expect-error
create('source', 'bad', { type: 'nosuchsource' });

// A layer spec is not a source spec, even though both are objects.
// @ts-expect-error
create('layer', 'bad', { type: 'http', url: 'x' });

// A nested spec is checked too - this is the path that used to drop its keys silently.
create('layer', 'basemap', {
  type: 'raster',
  source: { type: 'http', url: 'https://tile.example.com/{z}/{x}/{y}.png', maxZoom: 19 },
});

// --- properties --------------------------------------------------------------------------------

const layer = create('layer', 'base', { type: 'raster' }) as Handle<'massif::RasterTileLayer'>;

set(layer, 'opacity', 0.5);
set(layer, 'visible', true);

// A misspelled path.
// @ts-expect-error
set(layer, 'opacty', 0.5);

// The right path, the wrong value type.
// @ts-expect-error
set(layer, 'visible', 0.5);

// A property of a different class.
// @ts-expect-error
set(layer, 'rangeStart', 2.5);

// A dotted path into an object property, which is the case the closure exists for.
const options = create('options', 'main', { type: 'fog' }) as Handle<'massif::Options'>;
set(options, 'fogOptions.rangeStart', 2.5);
set(options, 'fogOptions.enabled', true);

// @ts-expect-error
set(options, 'fogOptions.rangeStrat', 2.5);

// Reading gives the value's type back, not `any`.
const zoomRange: [number, number] = get(options, 'zoomRange');
const panning = get(options, 'panningMode');
const stillPanning: 'PANNING_MODE_FREE' | 'PANNING_MODE_STICKY' | 'PANNING_MODE_STICKY_FINAL' =
  panning;

// An enum takes its constant NAME, not an arbitrary string.
set(options, 'panningMode', 'PANNING_MODE_STICKY');
// @ts-expect-error
set(options, 'panningMode', 'PANNING_MODE_WRONG');

// A read-only property cannot be written.
const tileLayer = layer as unknown as Handle<'massif::TileLayer'>;
// @ts-expect-error
set(tileLayer, 'dataSource.minZoom', 3);

// --- methods -----------------------------------------------------------------------------------

const tile = call(source as unknown as Handle<'massif::TileDataSource'>, 'loadTile', [8467, 5852, 14]);
const tileData: Handle<'massif::TileData'> = tile;

// A method the class does not have.
// @ts-expect-error
call(source as unknown as Handle<'massif::TileDataSource'>, 'getElevation', [0, 0]);

// The right method, the wrong argument shape.
// @ts-expect-error
call(source as unknown as Handle<'massif::TileDataSource'>, 'loadTile', 'nope');

// A bulk result is an array of numbers, not a handle.
const hillshade = layer as unknown as Handle<'massif::HillshadeRasterTileLayer'>;
const metres: number[] = call(hillshade, 'getElevations', [[5.76, 45.24], [5.77, 45.25]]);

const positions: Position[] = [[5.76, 45.24], [5.77, 45.25, 1200]];
call(hillshade, 'getElevations', positions);

// --- events ------------------------------------------------------------------------------------

on(options, 'map.clicked', (payload) => {
  // The payload is the click info, so its own paths are typed.
  const where: Position = get(payload, 'clickPos');
  void where;
});

// map.idle carries nothing, and saying so is the point of the null.
on(options, 'map.idle', (payload) => {
  const nothing: null = payload;
  void nothing;
});

// An event that does not fire on this class.
// @ts-expect-error
on(options, 'vectortile.clicked', () => {});

export { tileData, zoomRange, stillPanning, metres };
