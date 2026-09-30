/** The Massif style family: five maps from one project, switched by a style parameter. */
import { demTiles, massifStyle, satelliteTiles, vectorTiles } from './shared.mjs';

const VARIANTS = [
  ['streets', 'Streets', 'the everyday map'],
  ['outdoor', 'Outdoor', 'trails by difficulty, peaks, huts, cliffs'],
  ['topo', 'Topo', 'outdoor on a cooler, map-like ground'],
  ['hybrid', 'Hybrid', 'roads and labels over imagery'],
  ['eink', 'E-ink', 'black on white, for e-paper; inverts at night'],
];
/** CompositeSourceType, as the facade takes it. */
const SOURCE_HILLSHADE = 1;
const SOURCE_VECTOR = 2;

export default async function start(host) {
  const map = host.map;

  // Hybrid draws over imagery the app supplies: a raster under the vector layer, shown for it alone.
  const imagery = map.addLayer('imagery', { type: 'raster', source: satelliteTiles(), visible: false });

  // ONE project for all five: the variant is a style parameter, so switching loads nothing.
  const style = map.style('massif', await massifStyle(map, 'streets'));
  // Every variant has a `hillshade` and a `contour` slot; a composite layer fills them with the app's
  // own DEM, here only for outdoor and topo.
  const base = map.addLayer('basemap', { type: 'composite-vector', source: vectorTiles(), style: 'massif' });
  const dem = map.source('dem', demTiles());
  const contours = map.source('contours', { type: 'contour', source: 'dem', baseInterval: 20 });
  const showRelief = (on) => {
    if (on) {
      base.call('addExternalDataSource', 'hillshade', dem.handle, SOURCE_HILLSHADE);
      base.call('addExternalDataSource', 'contour', contours.handle, SOURCE_VECTOR);
    } else {
      base.call('removeExternalDataSource', 'hillshade');
      base.call('removeExternalDataSource', 'contour');
    }
  };
  map.camera().moveTo([5.7262, 45.1968], { zoom: 14.5 });

  for (const [variant, label, what] of VARIANTS) {
    host.button(label, () => {
      style.set('params.variant', variant);
      imagery.set('visible', variant === 'hybrid');
      showRelief(variant === 'outdoor' || variant === 'topo');
      host.caption(`Massif ${label}: ${what}.`);
    });
  }
  host.caption('Massif Streets: the everyday map. Pick another variant.');
}
