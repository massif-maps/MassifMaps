/** The smallest thing that is a map: one layer with the Massif style, and a camera. */
import { massifStyle, vectorTiles } from './shared.mjs';

export default async function start(host) {
  const map = host.map;

  // A spec describes the whole stack: the layer, and the source underneath it. Anything the
  // constructor does not take is applied as a property, so `opacity` needs no special case.
  map.addLayer('basemap', { type: 'vector', opacity: 1, source: vectorTiles(), style: await massifStyle(map) });

  // Positions are lon/lat: createMap uses EPSG:4326 unless told otherwise.
  map.camera().moveTo([6.8652, 45.8326], { zoom: 11 });

  host.caption('Mont Blanc, drawn by the Massif streets style over OpenFreeMap vector tiles.');
}
