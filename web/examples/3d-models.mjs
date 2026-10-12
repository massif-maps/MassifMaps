/**
 * glTF models placed at points by a CartoCSS `model` rule: Massif's own trees, and a wind turbine
 * whose rotor spins - the way an app adds models of its own.
 */
import { demTiles, massifStyle, vectorTiles } from './shared.mjs';

/** The models Massif ships (styles/massif/models), read straight from the repository. */
const MODELS = 'https://raw.githubusercontent.com/massif-maps/MassifMaps/master/styles/massif/models/';

/** The trees of Grenoble's Jardin de Ville, from OpenStreetMap (natural=tree). */
const TREES = {
  type: 'Feature',
  properties: {},
  geometry: {
    type: 'MultiPoint',
    coordinates: [
      [5.72924,45.19367],[5.72935,45.19370],[5.72937,45.19366],[5.72985,45.19367],[5.72698,45.19293],[5.72688,45.19293],
      [5.72797,45.19306],[5.72786,45.19304],[5.72771,45.19271],[5.72909,45.19364],[5.72911,45.19272],[5.72685,45.19239],
      [5.72692,45.19224],[5.72710,45.19240],[5.72709,45.19226],[5.72781,45.19245],[5.72788,45.19247],[5.72691,45.19200],
      [5.72714,45.19220],[5.72717,45.19214],[5.72710,45.19207],[5.72706,45.19200],[5.72717,45.19207],[5.72684,45.19175],
      [5.72694,45.19173],[5.72697,45.19195],[5.72699,45.19189],[5.72722,45.19189],[5.72705,45.19181],[5.72712,45.19175],
      [5.72717,45.19178],[5.72723,45.19182],[5.72725,45.19176],[5.72683,45.19165],[5.72681,45.19153],[5.72700,45.19169],
      [5.72718,45.19168],[5.72705,45.19155],[5.72753,45.19172],[5.72989,45.19364],[5.72992,45.19361],[5.72989,45.19258],
      [5.73045,45.19224],[5.72996,45.19179],[5.72997,45.19179],[5.72998,45.19179],[5.72993,45.19178],[5.72993,45.19176],
      [5.72993,45.19175],[5.72993,45.19174],[5.73048,45.19214],[5.73103,45.19252],[5.73100,45.19249],[5.73119,45.19265],
      [5.73115,45.19261],[5.73109,45.19257],[5.73103,45.19243],[5.73109,45.19240],[5.73111,45.19239],[5.73117,45.19236],
      [5.73110,45.19181],[5.72992,45.19170],[5.72992,45.19169],[5.72992,45.19167],[5.72992,45.19166],[5.72992,45.19164],
      [5.72991,45.19163],[5.72991,45.19161],[5.73000,45.19163],[5.72987,45.19149],[5.72986,45.19148],[5.73003,45.19153],
      [5.73001,45.19146],[5.73009,45.19154],[5.73018,45.19154],[5.73021,45.19152],[5.73068,45.19167],[5.73068,45.19160],
      [5.73065,45.19152],[5.73012,45.19146],[5.73006,45.19144],[5.73016,45.19144],[5.73020,45.19143],
    ],
  },
};

/** Three turbines along the Isere quay, where no real wind farm stands. */
const TURBINES = {
  type: 'Feature',
  properties: {},
  geometry: { type: 'MultiPoint', coordinates: [[5.7232, 45.1938], [5.7262, 45.1939], [5.7292, 45.1941]] },
};

// random() is seeded by each point's position: every tree keeps its own model, size, heading and
// tint through every zoom. A spinning part is a model of its own, its origin on the axis, lifted
// onto the tower.
const STYLE = [
  '#trees {',
  `  model-file: step(random(0, 3), (0, url('${MODELS}tree-spreading.glb')),`,
  `    (1, url('${MODELS}tree-layered.glb')), (2, url('${MODELS}tree-clumped.glb')));`,
  '  model-scale: random(0.8, 1.2);',
  '  model-rotation: random(0, 360);',
  '  model-color: #b4dcb4;',
  '  model-color-mix: random(0, 0.3);',
  '  model-max-distance: 1200;',
  '  model-shadow-max-distance: 500;',
  '}',
  '#turbines::tower {',
  `  model-file: url('${MODELS}wind-turbine-tower.glb');`,
  '  model-max-distance: 5000;',
  '  model-shadow-max-distance: 1500;',
  '  model-rotation: 270;',
  '}',
  '#turbines::rotor {',
  `  model-file: url('${MODELS}wind-turbine-rotor.glb');`,
  '  model-max-distance: 5000;',
  '  model-shadow-max-distance: 1500;',
  '  model-rotation: 270;',
  '  model-z-offset: 81.4;',
  '  model-spin: 14;',
  '  model-spin-axis: y;',
  '}',
].join('\n');

export default async function start(host) {
  const map = host.map;

  // Massif's own trees and turbines: models_3d draws them at the tiles' poi points of class
  // tree and wind_turbine. Their .glb files are NOT in the style package - download the five
  // from https://github.com/massif-maps/MassifMaps/tree/master/styles/massif/models
  // and put them in a models/ folder at the package root, beside project.json. OpenFreeMap's
  // tiles carry no such points anyway, so the models below are drawn on the app's own.
  map.style('massif', { ...(await massifStyle(map)), params: { models_3d: '1' } });
  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: 'massif' });

  const points = map.source('model-points', { type: 'geojson', maxZoom: 14 });
  points.setGeoJSON(points.createLayer('trees'), TREES);
  points.setGeoJSON(points.createLayer('turbines'), TURBINES);
  map.addLayer('models', {
    type: 'vector',
    source: 'model-points',
    style: { type: 'mbvt', cartocss: { type: 'cartocss', css: STYLE } },
  });

  // A terrain and a lit ground, for the shadows: cast shadows land on the terrain surface.
  map.terrain({ type: 'terrain', source: demTiles() });
  map.light({
    type: 'light',
    sunOverridingStyle: true,
    sunAzimuth: 220,
    sunAltitude: 40,
    terrainLightingEnabled: true,
    shadowStrength: 1.5,
  });

  map.camera().moveTo([5.7262, 45.1928], { zoom: 17.0, rotation: 0, tilt: 32 });

  // Off, the rotors stand still and the map stops redrawing every frame for them.
  host.toggle('Spin', true, (on) => map.set('modelAnimationsEnabled', on));
  // On a slow GPU the trees' shadows can cost more than the trees themselves.
  host.toggle('Model shadows', true, (on) => map.set('modelShadowsEnabled', on));
  host.caption('Every tree draws its own model, size and heading; the rotors spin.');
}
