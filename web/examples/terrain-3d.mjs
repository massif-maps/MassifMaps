/** The flagship: satellite imagery draped over 3D terrain, with roads and summits on top. */
import { demTiles, massifStyle, satelliteTiles, vectorTiles } from './shared.mjs';

// Looking south-west at the Matterhorn from over Zermatt, low enough a tilt for the pyramid to
// stand against the sky. Tilt 90 is straight down here, so a landscape is LOW.
const VIEW = [7.6586, 45.9763];

export default async function start(host) {
  const map = host.map;
  // Fetched before any layer exists: a layer added first draws the world view while this loads.
  const labelStyle = await massifStyle(map, 'hybrid');

  // Imagery underneath, one level finer: a draped tile is never finer than the camera's zoom, and
  // the ground in front of a tilted camera is far closer than its focus.
  map.addLayer('satellite', { type: 'raster', source: satelliteTiles(), zoomLevelBias: 1 });

  // Roads, place names and summits ON TOP: Massif's hybrid variant has no background of its own.
  map.addLayer('labels', { type: 'vector', source: vectorTiles(), style: labelStyle });

  // apply, not three sets: one crossing for the whole group. viewDistanceFactor is in multiples
  // of the camera-to-focus distance; cameraClearance is lowered from 200 m to sit among the peaks.
  map.terrain({ type: 'terrain', source: demTiles() }).apply({
    exaggeration: 1.25,
    viewDistanceFactor: 1.6,
    cameraClearance: 40,
  });

  // Options starts with these EMPTY, so they are BUILT here rather than written through.
  map.sky({ type: 'sky' });
  map.fog({ type: 'fog', rangeStart: 2.2, rangeEnd: 8 });
  // The sun comes from BEHIND the camera, or the face looked at is in shadow: this is the east
  // side, so a morning sun. Mid altitude: a low sun puts the whole massif in its own shadow.
  map.light({
    type: 'light',
    terrainLightingEnabled: true,
    sunAzimuth: 100,
    sunAltitude: 42,
    shadowStrength: 0.35,
    shadowSoftness: 1.5,
  });

  map.camera().moveTo(VIEW, { zoom: 11.6, rotation: 135, tilt: 14 });

  // 'terrain' is an alias for 'terrainOptions', so this is map.set('terrainOptions.enabled', on).
  host.toggle('Terrain', true, (on) => map.set('terrain.enabled', on));
  host.toggle('Labels', true, (on) => map.layer('labels')?.visible(on));
  // Right-drag (two-finger tilt on a phone) turns about the ground under the pointer, not the centre.
  host.toggle('Orbit pointer', false, (on) => map.set('orbitAroundPivot', on));
  host.button('Exaggerate', () => {
    const current = map.terrain().get('exaggeration');
    map.terrain().set('exaggeration', current >= 2 ? 1 : current + 0.35);
  });
  host.caption('The Matterhorn. Imagery on the mesh, roads and summits above it.');
}
