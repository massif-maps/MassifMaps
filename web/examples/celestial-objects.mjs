/** Stars and a constellation over the Matterhorn at dusk, and a camera that can look up at them. */
import { demTiles, massifStyle, satelliteTiles, vectorTiles } from './shared.mjs';

const MATTERHORN = [7.6586, 45.9763];

// Right ascension in hours, declination in degrees (J2000), magnitude, colour.
const STARS = {
  Betelgeuse: [5.9195, 7.407, 0.5, '#ffc58f'],
  Rigel: [5.2423, -8.2016, 0.13, '#cad8ff'],
  Bellatrix: [5.4189, 6.3497, 1.64, '#d5e2ff'],
  Mintaka: [5.5334, -0.2991, 2.2, '#d5e2ff'],
  Alnilam: [5.6036, -1.2019, 1.69, '#d5e2ff'],
  Alnitak: [5.6793, -1.9426, 1.77, '#d5e2ff'],
  Saiph: [5.7959, -9.6696, 2.06, '#d5e2ff'],
  Meissa: [5.5856, 9.9342, 3.39, '#d5e2ff'],
  Sirius: [6.7525, -16.7161, -1.46, '#e6eeff'],
  Procyon: [7.655, 5.225, 0.34, '#fff4e8'],
  Aldebaran: [4.5987, 16.5093, 0.86, '#ffcf9e'],
  Capella: [5.2782, 45.998, 0.08, '#fff1c9'],
  Pollux: [7.7553, 28.026, 1.14, '#ffe3b8'],
  Castor: [7.5767, 31.888, 1.58, '#e6eeff'],
};
const NAMED = ['Betelgeuse', 'Rigel', 'Sirius', 'Procyon', 'Aldebaran', 'Capella', 'Pollux'];
const ORION = [
  'Betelgeuse', 'Bellatrix', 'Betelgeuse', 'Alnitak', 'Bellatrix', 'Mintaka', 'Mintaka', 'Alnilam',
  'Alnilam', 'Alnitak', 'Alnitak', 'Saiph', 'Mintaka', 'Rigel', 'Betelgeuse', 'Meissa', 'Meissa', 'Bellatrix',
];
// The sun of an April evening: it lights the sky from under the horizon.
const SUN = [1.5, 9.5];

const deg = Math.PI / 180;

/** Equatorial to horizontal: the SDK only knows directions, the astronomy is the app's. */
function direction([ra, dec], siderealTime) {
  const hourAngle = (siderealTime - ra * 15) * deg;
  const phi = MATTERHORN[1] * deg;
  const alt = Math.asin(Math.sin(phi) * Math.sin(dec * deg) + Math.cos(phi) * Math.cos(dec * deg) * Math.cos(hourAngle));
  const az = Math.atan2(Math.sin(hourAngle), Math.cos(hourAngle) * Math.sin(phi) - Math.tan(dec * deg) * Math.cos(phi));
  return [(az / deg + 540) % 360, alt / deg];
}

export default async function start(host) {
  const map = host.map;
  const ratio = window.devicePixelRatio || 1;
  let siderealTime = 132;
  let turning = false;

  map.addLayer('satellite', { type: 'raster', source: satelliteTiles() });
  map.addLayer('labels', { type: 'vector', source: vectorTiles(), style: await massifStyle(map, 'hybrid') });
  map.terrain({ type: 'terrain', source: demTiles() }).apply({ viewDistanceFactor: 3, cameraClearance: 40 });
  map.sky({ type: 'sky', atmosphereLuminance: 2.4 });
  map.fog({ type: 'fog', rangeStart: 2, rangeEnd: 10, color: 0xff2a3450, highColor: 0xff1c2a4a, spaceColor: 0xff070b18, starIntensity: 0.6 });
  map.light({ type: 'light', terrainLightingEnabled: true, sunIntensity: 0, ambientColor: 0xff7080b0, ambientIntensity: 0.28 });

  // First in the stack: the terrain then hides whatever sets behind a ridge.
  const sky = map.addLayer('sky', { type: 'celestial' }).moveTo(0);
  const add = (id, spec) => {
    const object = map.object('celestial', id, spec);
    sky.call('add', object.handle);
    return object;
  };
  const figure = add('orion', { type: 'arc', color: '#93c5fd80', width: 1.5 * ratio, belowHorizonVisible: true });
  const stars = Object.entries(STARS).map(([name, [, , magnitude, color]]) => [name, add(`star.${name}`, {
    type: 'sprite', screenSize: Math.max(3, 8 - 1.6 * magnitude) * ratio, color, softness: 0.5,
    clickRadius: 1.5, metaData: { name },
  })]);
  const label = (id, text, fontSize) => {
    const object = add(id, { type: 'label', text, fontSize, textColor: '#e2e8f0', haloColor: '#0f172acc', haloWidth: 3 });
    object.call('setOffset', 0, 8);
    return object;
  };
  const names = NAMED.map((name) => [name, label(`name.${name}`, name, 12)]);
  const title = label('name.Orion', 'ORION', 14);
  title.call('setAnchorPoint', -1, 0);
  title.call('setOffset', 10, 0);

  function place() {
    const at = (name) => direction(STARS[name], siderealTime);
    for (const [name, object] of [...stars, ...names]) {
      object.call('setDirection', ...at(name), 0);
    }
    title.call('setDirection', ...at('Bellatrix'), 0);
    figure.call('setSegments', ORION.flatMap(at));
    const [sunAzimuth, sunAltitude] = direction(SUN, siderealTime);
    map.light().apply({ sunAzimuth, sunAltitude });
  }

  function step() {
    if (!turning) {
      return;
    }
    siderealTime += 0.5;
    place();
    host.after(100, step);
  }

  place();
  // Tilt 90 is straight down; below 0 the camera keeps its place and only looks up.
  map.apply({ freeRoamMode: 'FREE_ROAM_MODE_LOOK', tiltRange: [-90, 90] });
  map.camera().moveTo(MATTERHORN, { zoom: 12.5, rotation: 110, tilt: -18 });

  sky.onCelestialClick((e) => {
    const name = e.get('celestialObject.metaData.name');
    if (name) {
      host.caption(name);
    }
  });

  host.button('Look up', () => {
    const up = map.camera().tilt() > -30;
    map.camera().animate(1200).tilt(up ? -55 : -18);
  });
  host.toggle('Figures', true, (on) => {
    for (const object of [figure, title, ...names.map(([, name]) => name)]) {
      object.set('visible', on);
    }
  });
  host.toggle('Turn the sky', false, (on) => {
    turning = on;
    step();
  });
  host.caption('Orion setting over the Matterhorn. Drag to look around, tap a star.');
}
