/** The site's front page map: Grenoble in 3D, the Bastille's fort and cable car above the Isère. */
import { demTiles, massifStyle, vectorTiles } from './shared.mjs';

const VIEW = [5.7262, 45.1955];
// Massif outdoor's own relief settings (its style.json, `massif:sdk-layer`), as the style preview draws it.
const RELIEF = { hillshadeMethod: 'STANDARD', contrast: 0.35, heightScale: 1, shadowColor: '#544c45', highlightColor: '#faf8f5', accentColor: '#847362', visibleZoomRange: [0, 16] };

export default async function start(host) {
  const map = host.map;
  // The style preview's tile LOD: finer tiles than the web default, so a tilted city stays sharp.
  map.set('tileLODFactor', 0.5);
  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: await massifStyle(map, 'outdoor'), labelPerspectiveScaling: 0.5 });
  // One DEM source behind the relief and the terrain: one cache file, fetched and decoded once.
  map.source('dem', demTiles());
  map.addLayer('relief', { type: 'hillshade', source: 'dem', ...RELIEF });
  map.terrain({ type: 'terrain', source: 'dem' }).apply({ meshResolution: 128, autoFlattenTilt: 0, autoFlattenParallax: 0 });
  map.sky({ type: 'sky', atmosphereLuminance: 0.4 });
  map.light({ type: 'light', dayCycleLightsEnabled: true, sunOverridingStyle: true, terrainLightingEnabled: true, shadowStrength: 1 });
  // Midsummer, half past one solar time: the sun 62 deg up in the south-west, the full day light
  // (the curve holds dusk tones below 38 deg), and the Bastille's south face lit toward the camera.
  const utc = 13.5 - VIEW[0] / 15;
  map.child('lightOptions').call('setSunPositionFromTime', 2026, 6, 21, Math.floor(utc), Math.round((utc % 1) * 60), VIEW[1], VIEW[0]);
  // The same framing in any frame: at a given zoom the eye's distance follows the canvas height.
  map.camera().moveTo(VIEW, { zoom: 16.1 + Math.log2(host.root.clientHeight / 600), rotation: -5, tilt: 30 });
}
