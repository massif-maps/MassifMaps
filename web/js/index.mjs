/*
 * @massif-maps/web: the typed surface API (@massif-maps/api) on a canvas.
 *
 *   const map = await createMap(document.getElementById('map'));
 *   map.addLayer('osm', { type: 'raster', source: { type: 'http', url: '.../{z}/{x}/{y}.png' } });
 *   map.camera().moveTo([6.8652, 45.8326], { zoom: 11 });
 */
import { attach, setBridge } from '@massif-maps/api';
import { createBridge } from './bridge.mjs';
import { canvasSelector, loadModule } from './massif.mjs';

export { persistDirectory } from './massif.mjs';

export * from '@massif-maps/api';

let created = false;

/**
 * Loads the SDK module and starts a map on `canvas`. One per page: the API talks to one module.
 * `projection` is what positions read and write in; `moduleUrl` and the rest go to loadModule.
 */
export async function createMap(canvas, { id = 'map', projection = 'EPSG:4326', ...moduleOptions } = {}) {
  if (created) {
    throw new Error('createMap: this page already has a map');
  }
  created = true;
  const module = await loadModule(moduleOptions);
  const bridge = createBridge(module);
  setBridge(bridge);
  if (!module.ccall('massifCreateMap', 'number', ['string'], [canvasSelector(canvas, id)])) {
    throw new Error('createMap: this browser has no WebGL 2');
  }
  const view = {
    mapView: { getLayers: () => bridge.parts.layers, getBaseMapView: () => bridge.parts.view },
    getOptions: () => ({ getNative: () => bridge.parts.options }),
    getMeasuredWidth: () => canvas.width,
    getMeasuredHeight: () => canvas.height,
  };
  const map = attach(view, { id, projection });
  // The emscripten module, for what the facade does not cover (its FS, persistDirectory).
  map.module = module;
  return map;
}
