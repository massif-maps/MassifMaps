/** The smallest thing that is a map: one raster layer and a camera. */
import { osmRaster } from './shared.mjs';

export default function start(host) {
  const map = host.map;

  // A spec describes the whole stack: the layer, and the source underneath it. Anything the
  // constructor does not take is applied as a property, so `opacity` needs no special case.
  map.addLayer('basemap', { type: 'raster', opacity: 1, source: osmRaster() });

  // Positions are lon/lat: createMap uses EPSG:4326 unless told otherwise.
  map.camera().moveTo([6.8652, 45.8326], { zoom: 11 });

  host.caption('Mont Blanc, from OpenStreetMap raster tiles.');
}
