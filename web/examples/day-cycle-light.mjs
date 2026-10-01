/** The hour drives the whole palette, and the curve that decides how is the app's to replace. */
import { demTiles, massifStyle, vectorTiles } from './shared.mjs';

// Massif is written to be lit by the hour: every layer states Standard's emissive strength, so the
// ground darkens at night while labels and lit streets keep their colour.

// A curve is a list of lights anchored on SUN HEIGHTS, interpolated by the SDK. Nothing about this
// one is a special case: the 2D grade, 3D sun and ambient all derive from whatever it returns.
const PSYCHEDELIC = JSON.stringify([
  { sunAltitude: -15, ambientColor: '#2d0a4e', ambientIntensity: 0.7, sunColor: '#00e5ff', sunIntensity: 0.4 },
  { sunAltitude: 2, ambientColor: '#ff2d95', ambientIntensity: 0.85, sunColor: '#ff8a00', sunIntensity: 0.6 },
  { sunAltitude: 25, ambientColor: '#7cff4f', ambientIntensity: 0.9, sunColor: '#ff00d4', sunIntensity: 0.5 },
  { sunAltitude: 60, ambientColor: '#00fff0', ambientIntensity: 1.0, sunColor: '#fff700', sunIntensity: 0.45 },
]);

// The BUILT-IN curves written out (empty lists select exactly these). TWO, because Standard's dawn
// and dusk share a sun height: the SDK reads the setting one while the sun is west, rising east.
const MAPBOX_SETTING = JSON.stringify([
  { sunAltitude: -9, ambientColor: '#464d69', ambientIntensity: 0.5, sunColor: '#3f4455', sunIntensity: 0.5 },
  { sunAltitude: 3, ambientColor: '#363e5e', ambientIntensity: 0.8, sunColor: '#fec286', sunIntensity: 0.2 },
  { sunAltitude: 12, ambientColor: '#363e5e', ambientIntensity: 0.8, sunColor: '#fec286', sunIntensity: 0.2 },
  { sunAltitude: 38, ambientColor: '#ffffff', ambientIntensity: 0.8, sunColor: '#ffffff', sunIntensity: 0.2 },
]);

const MAPBOX_RISING = JSON.stringify([
  { sunAltitude: -9, ambientColor: '#464d69', ambientIntensity: 0.5, sunColor: '#3f4455', sunIntensity: 0.5 },
  { sunAltitude: 3, ambientColor: '#ffecdc', ambientIntensity: 0.75, sunColor: '#feca8b', sunIntensity: 0.5 },
  { sunAltitude: 12, ambientColor: '#ffecdc', ambientIntensity: 0.75, sunColor: '#feca8b', sunIntensity: 0.5 },
  { sunAltitude: 38, ambientColor: '#ffffff', ambientIntensity: 0.8, sunColor: '#ffffff', sunIntensity: 0.2 },
]);

/** [name, setting curve, rising curve] - one curve for both when a formula has no dawn. */
const FORMULAS = [
  ['Mapbox', MAPBOX_SETTING, MAPBOX_RISING],
  ['Psychedelic', PSYCHEDELIC, PSYCHEDELIC],
];

/** Paris, and the camera the example opens on. */
const LON = 2.3376;
const LAT = 48.86;

/** The EQUINOX (2026-03-20, Julian day at noon UTC): sunrise 6, sunset 18 at every latitude. */
const JULIAN_NOON = 2461120.0;

/** Local solar time: 12 is the sun at its highest, whatever the longitude. */
const START_HOUR = 17.4;
/** The hours that land on MapBox's four presets EXACTLY at this camera and date; others blend two. */
const PRESETS = [['dawn', 6.8], ['day', 12], ['dusk', 17.4], ['night', 22]];

/** The SDK's own thresholds, written out because the toggle switches between them and off. */
const AUTO_FLATTEN_TILT = 88;
const AUTO_FLATTEN_PARALLAX = 2;

let formula = 0;
let hour = START_HOUR;
let sunAltitude = 0;
let sunAzimuth = 0;
let preset = 2;

// Local solar time to a sun position - the NOAA low-accuracy form LightOptions.setSunPositionFromTime
// computes in C++; the facade cannot reach that method, so the example spells it out.
function sunPosition(local) {
  const rad = Math.PI / 180;
  const n = JULIAN_NOON + (local - LON / 15 - 12) / 24 - 2451545.0;
  const meanAnom = (357.528 + 0.9856003 * n) * rad;
  const eclipticLong = (280.46 + 0.9856474 * n + 1.915 * Math.sin(meanAnom) + 0.02 * Math.sin(2 * meanAnom)) * rad;
  const obliquity = (23.439 - 0.0000004 * n) * rad;
  const rightAsc = Math.atan2(Math.cos(obliquity) * Math.sin(eclipticLong), Math.cos(eclipticLong));
  const decl = Math.asin(Math.sin(obliquity) * Math.sin(eclipticLong));

  // Greenwich mean sidereal time, then the local hour angle.
  let gmst = (18.697374558 + 24.06570982441908 * n) % 24;
  if (gmst < 0) gmst += 24;
  const hourAngle = (gmst * 15 + LON) * rad - rightAsc;

  const lat = LAT * rad;
  const altitude = Math.asin(Math.sin(lat) * Math.sin(decl) + Math.cos(lat) * Math.cos(decl) * Math.cos(hourAngle)) / rad;
  // atan2 here is measured from south; the SDK wants clockwise from north.
  const azimuth = Math.atan2(Math.sin(hourAngle), Math.cos(hourAngle) * Math.sin(lat) - Math.tan(decl) * Math.cos(lat)) / rad + 180;
  return [altitude, azimuth];
}

function clock(hours) {
  const minutes = Math.floor(hours * 60);
  return `${Math.floor(minutes / 60) % 24}:${String(minutes % 60).padStart(2, '0')}`;
}

export default async function start(host) {
  const map = host.map;

  // Keep a TILTED far field uniform: a low levels-on-screen decays the grazing term more
  // slowly, so the horizon band stops jumping between levels as the camera turns.
  map.set('tileLODMaxZoomLevelsOnScreen', 6.0);

  map.addLayer('basemap', {
    type: 'vector',
    source: vectorTiles(),
    style: await massifStyle(map),
  });

  // A TERRAIN, for the shadows: they land on the terrain surface, and with none nothing casts.
  // The auto 2D/3D thresholds are the defaults, set out loud because the toggle turns them off.
  map.terrain({ type: 'terrain', source: demTiles() }).apply({
    exaggeration: 1,
    cameraClearance: 40,
    autoFlattenTilt: AUTO_FLATTEN_TILT,
    autoFlattenParallax: AUTO_FLATTEN_PARALLAX,
  });

  // The curve is only read while this is on; off, the style's and the app's own sun colours
  // stand, which is what every map did before the curve existed.
  map.light({
    type: 'light',
    dayCycleLightsEnabled: true,
    sunOverridingStyle: true,
    // Without this the ground is never lit, and the shadow multiply lives in the same
    // block - so the buildings cast nothing.
    terrainLightingEnabled: true,
    // Buildings cast, following the same sun the curve reads. Strength scales the sun's share of
    // the light, which is small in Standard's day: below about 2 a shadow is hard to see.
    shadowStrength: 3,
    shadowSoftness: 1.2,
  });

  // A sky integrated against the SAME sun, so it reddens and darkens with the slider. Options
  // starts with no SkyOptions, so nothing is drawn behind the map until this line.
  map.sky({ type: 'sky' });

  applyFormula();
  applyHour();
  map.camera().moveTo([LON, LAT], { zoom: 15.6, rotation: 20, tilt: 35 });

  function applyFormula() {
    // An empty list is the built-in curve; stops replace it, and only a redraw follows. Both are
    // written every time, or a formula without a dawn would keep the previous one's.
    map.light().apply({
      dayCycleLightStops: FORMULAS[formula][1],
      dayCycleRisingLightStops: FORMULAS[formula][2],
    });
  }

  function applyHour() {
    // The curve reads a sun POSITION, and the hour is where the sun actually is then.
    [sunAltitude, sunAzimuth] = sunPosition(hour);
    map.light().apply({ sunAzimuth, sunAltitude });
  }

  // Which MapBox preset this hour renders: exact only where the curve is flat (below -9, 3 to 12,
  // above 38) - elsewhere a blend, which is why an arbitrary hour never matches a screenshot.
  function light() {
    if (formula !== 0) return 'custom curve';
    const twilight = sunAzimuth <= 180 ? 'dawn' : 'dusk';
    if (sunAltitude <= -9) return 'night';
    if (sunAltitude >= 38) return 'day';
    if (sunAltitude >= 3 && sunAltitude <= 12) return twilight;
    return sunAltitude < 3 ? `night to ${twilight}` : `${twilight} to day`;
  }

  function caption() {
    host.caption(`${clock(hour)} - sun ${sunAltitude.toFixed(0)}° - ${light()} - ${FORMULAS[formula][0]}`);
  }

  host.button('Formula', () => {
    formula = (formula + 1) % FORMULAS.length;
    applyFormula();
    caption();
  });
  // The HOUR, because that is what a day is: the sun walks its real arc, so dawn and dusk
  // come with the azimuth swinging round rather than being picked by hand.
  host.slider('Hour', 0, 24, START_HOUR, (value) => {
    hour = value;
    applyHour();
    caption();
  }, clock);
  // Straight to MapBox's own four, so the render can be held against theirs.
  host.button('Preset', () => {
    preset = (preset + 1) % PRESETS.length;
    [, hour] = PRESETS[preset];
    applyHour();
    caption();
  });
  // Off holds the map in 3D at any tilt: the thresholds are a pair, and a 0 disables its own
  // half of the rule.
  host.toggle('Auto 2D/3D', true, (on) => {
    map.terrain().apply({
      autoFlattenTilt: on ? AUTO_FLATTEN_TILT : 0,
      autoFlattenParallax: on ? AUTO_FLATTEN_PARALLAX : 0,
    });
    host.caption(on ? 'Auto 2D/3D on: tilt past 88° and the map renders flat.' : 'Auto 2D/3D off: the map stays 3D all the way to 90°.');
  });
  host.caption('One palette, no night theme: the hour picks the light, the curve picks the look. ' + 'Zoom out, or tilt to 90, and the buildings lie down.');
}
