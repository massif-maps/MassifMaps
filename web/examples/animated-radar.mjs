/** A weather radar loop: one tile per time step, two steps mixed in a single draw. */
import { cached, demTiles, massifStyle, vectorTiles } from './shared.mjs';

// LibreWXR serves RainViewer's API: the steps of the loop, then a tile URL per step.
const STEPS = 'https://api.librewxr.net/public/weather-maps.json';
const TILES = 'https://api.librewxr.net/v2/radar/{time}/256/{z}/{x}/{y}/2/1_1.png';

// Where each state looks: all of Europe flat, the Alps from the north in 3D.
const EUROPE = [4.5, 47.0];
const ALPS = [7.2, 46.2];

export default async function start(host) {
  const map = host.map;

  // The ground, set up BEFORE any layer decodes and left flat: the 3D switch below is the `flattened`
  // flag, so a map that never goes 3D builds not one tile for it. Mapterhorn's open DEM.
  map.terrain({ type: 'terrain', source: demTiles() }).apply({
    flattened: true,
    exaggeration: 1.5,
    flattenMode: 'TERRAIN_FLATTEN_MODE_FULL',
    viewDistanceFactor: 1.6,
    cameraClearance: 40,
  });
  map.sky({ type: 'sky' });
  map.fog({ type: 'fog', rangeStart: 2.2, rangeEnd: 8 });
  map.light({ type: 'light', terrainLightingEnabled: false, sunAzimuth: 315, sunAltitude: 42, shadowStrength: 0 });

  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: await massifStyle(map) });
  map.camera().moveTo([4.5, 47.0], { zoom: 5.5 });
  host.caption('Loading the radar steps...');

  let times;
  try {
    const radar = (await (await fetch(STEPS)).json()).radar;
    times = [...radar.past, ...(radar.nowcast ?? [])].map((step) => step.time);
  } catch (e) {
    host.caption(`The radar is not reachable: ${e.message}`);
    return;
  }

  // The memory cache keeps a step fetched once: it is the second raster of one tile and the first
  // of the next, and its default 6 MB holds a third of a loop. prefetchFrames = every step, so
  // the loop does not stall on its first pass.
  map.addLayer('radar', {
    type: 'animated-raster',
    source: {
      type: 'memory-cache',
      capacity: 64 * 1024 * 1024,
      // The server renders a tile on demand and takes seconds over it, so a tile fetched once is kept - for
      // the 5 minutes its Cache-Control gives: a forecast step is revised, and must not be shown later as
      // an observation.
      source: cached('librewxr-radar', 100, { type: 'http', url: TILES, maxZoom: 12, maxAgeHeaderCheck: true }),
    },
    // Radar is coarse data: one level fewer is a quarter of the tiles to wait for.
    zoomLevelBias: -1,
    frameTimestamps: times.map(String),
    prefetchFrames: times.length,
    opacity: 0.85,
  });

  let playing = true;
  let stepsPerSecond = 2;
  let position = 0;

  const show = () => {
    map.layer('radar')?.set('animationPosition', position);
    const step = Math.min(times.length - 1, Math.floor(position));
    const forecast = times[step] * 1000 > Date.now();
    const clock = new Date(times[step] * 1000).toISOString().slice(11, 16);
    host.caption(`${clock} UTC${forecast ? '  forecast' : ''}`);
  };

  host.toggle('Play', true, (on) => (playing = on));
  host.toggle('3D', false, (on) => {
    map.terrain().set('flattened', !on);
    map.light().set('terrainLightingEnabled', on);
    // tilt 90 is straight down in this SDK, so the 3D view is a LOW tilt.
    map.camera().moveTo(on ? ALPS : EUROPE, { zoom: on ? 9 : 5.5, rotation: on ? 180 : 0, tilt: on ? 35 : 90, duration: 1500 });
  });
  host.slider('Time', 0, 1, 0, (value) => {
    position = value * (times.length - 1);
    show();
  });
  host.slider('Steps a second', 0.5, 6, stepsPerSecond, (value) => (stepsPerSecond = value));

  // The clock: advances the position by the time since the last frame, at the chosen rate.
  let last = performance.now();
  const tick = (now) => {
    const seconds = (now - last) / 1000;
    last = now;
    if (playing) {
      position = (position + seconds * stepsPerSecond) % times.length;
      host.setSlider('Time', times.length > 1 ? position / (times.length - 1) : 0);
      show();
    }
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}
