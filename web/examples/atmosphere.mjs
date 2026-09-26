/** Everything the sky and the fog can do, on one map: scattering, a day cycle, stars, and peaks clear of a valley haze. */
import { demTiles, overlayStyle, satelliteTiles, vectorTiles } from './shared.mjs';

/** Looking south at the Matterhorn from over Zermatt - see terrain-3d for the framing. */
const VIEW = [7.6586, 45.9763];

// The colours the atmosphere is not responsible for - the FOG's own tint and the sky exposure.
// Sun position, ground light and the colour the haze is lit to all follow from the hour.
const MOMENTS = [
  { label: 'Dawn', hour: 6.5, color: 0xffd8b48c, high: 0xffe08a5a, space: 0x66202a4a, stars: 0.25, exposure: 1.6 },
  { label: 'Noon', hour: 13.0, color: 0xffb8c6d8, high: 0x00000000, space: 0x00000000, stars: 0.0, exposure: 1.0 },
  { label: 'Dusk', hour: 19.5, color: 0xffc98a63, high: 0xffe06a3a, space: 0x88141c38, stars: 0.35, exposure: 1.8 },
  { label: 'Night', hour: 23.0, color: 0xff1a2338, high: 0xff101a34, space: 0xff05070f, stars: 0.9, exposure: 3.2 },
];

const CAPTIONS = [
  'the sun just up, the haze warm and low',
  'high sun, thin blue sky, almost no haze',
  'a low sun reddens the whole sky, not just the disc',
  'the sun gone, stars beyond the atmosphere',
];

// A custom sky: `vec4 skyColor(vec3 rayDir)`, NON-premultiplied, reading only what the wrapper
// declares. It must NOT fog itself - the SDK hazes whatever it returns, so the skyline still meets.
const CUSTOM_SKY = `
    float hash21(vec2 p) {
      return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
    }
    float valueNoise(vec2 p) {
      vec2 i = floor(p), f = fract(p);
      f = f * f * (3.0 - 2.0 * f);
      return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
                 mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x), f.y);
    }

    // A flat cloud deck the ray is projected onto: cheap, and the perspective is right.
    // The octaves are ROTATED against each other - stacking axis-aligned value noise leaves
    // the grid visible, which reads as soft squares once the sun lights them.
    float clouds(vec3 dir) {
      if (dir.z <= 0.02) return 0.0;
      vec2 p = dir.xy / dir.z * 1.7 + vec2(u_time * 0.004, 0.0);
      mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
      float f = 0.50 * valueNoise(p);
      p = rot * p * 2.1; f += 0.25 * valueNoise(p);
      p = rot * p * 2.3; f += 0.15 * valueNoise(p);
      p = rot * p * 2.7; f += 0.10 * valueNoise(p);
      // Thinned towards the horizon, where the deck would be edge-on and solid.
      return smoothstep(0.46, 0.74, f) * smoothstep(0.02, 0.25, dir.z);
    }

    // Three comets, laid out in (azimuth, elevation) so a streak keeps its width near the
    // horizon - a flat projection smears it into a band there. Each one crosses on its own
    // stagger, head first, with the tail BEHIND it.
    float comets(vec3 dir) {
      vec2 sky = vec2(atan(dir.y, dir.x), asin(clamp(dir.z, -1.0, 1.0)));
      float total = 0.0;
      for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float t = fract(u_time * 0.07 + fi * 0.37);
        vec2 from = vec2(-2.6 + fi * 0.7, 1.25);
        vec2 to = vec2(1.4 + fi * 0.5, 0.18);
        vec2 head = mix(from, to, t);
        vec2 axis = normalize(to - from);
        vec2 d = sky - head;
        float along = dot(d, axis);
        float across = length(d - axis * along);
        float tail = exp(-across * across / 0.00012)
                     * smoothstep(-0.5, 0.0, along) * step(along, 0.0);
        float glow = exp(-dot(d, d) / 0.00006);
        // Faded in and out over the pass, so nothing pops at the edge of the frame.
        total += (tail * 0.55 + glow) * smoothstep(0.0, 0.12, t) * smoothstep(1.0, 0.86, t);
      }
      return total;
    }

    vec4 skyColor(vec3 rayDir) {
      float elevation = asin(clamp(rayDir.z, -1.0, 1.0));
      // Below the horizon is the wedge between the last terrain tile and the mathematical
      // horizon. The wrapper has the right answer for it, coverage included.
      if (elevation < 0.0) return groundBelowHorizon(rayDir);

      // The SDK's own scattering, tonemapped the way the built-in sky tonemaps it.
      vec3 scattered = atmosphere(rayDir, u_sunDir) * (8.0 / u_atmosphere.y);
      vec3 col = tonemap(scattered) / tonemap(vec3(11.2));
      col = atmosphereTint(col, elevation);

      // How much of a day it is, from the sun's own altitude - no clock is passed in.
      float day = clamp(u_sunDir.z * 4.0 + 0.15, 0.0, 1.0);

      // Clouds take the sun's colour where it strikes them, grey where it does not.
      float cover = clouds(rayDir);
      vec3 lit = mix(vec3(0.55, 0.58, 0.66), u_sunColor.rgb * 1.05, day);
      col = mix(col, lit, cover * (0.30 + 0.55 * day));

      // Comets and stars only once the sun is down, and ADDED rather than mixed: they are
      // lights, not surfaces.
      col += vec3(0.80, 0.88, 1.0) * comets(rayDir) * (1.0 - day) * (1.0 - cover);
      col += vec3(starAmount(rayDir, elevation)) * (1.0 - cover);

      return sunDisc(vec4(col, 1.0), rayDir);
    }
`;

export default function start(host) {
  const map = host.map;

  let moment = 2; // start at dusk: it is what shows the scattering off best
  let hour = MOMENTS[moment].hour;
  let cycling = false;
  let atmosphere = true;
  let customSky = false;

  map.addLayer('satellite', { type: 'raster', source: satelliteTiles() });
  map.addLayer('labels', { type: 'vector', source: vectorTiles(), style: overlayStyle() });

  map.terrain({ type: 'terrain', source: demTiles() }).apply({
    exaggeration: 1.25,
    viewDistanceFactor: 1.6,
    cameraClearance: 40,
  });

  // ATMOSPHERE is the default sky type, so this only names what differs from it - the
  // exposure the moment wants is set below, with everything else the hour drives.
  map.sky({ type: 'sky', sunDiscEnabled: true });

  // The range is in multiples of the camera-to-focus distance, so one pair holds at every zoom.
  // horizonBlend carries the haze into the sky and scales the ground too, so the skyline has no seam.
  map.fog({
    type: 'fog',
    rangeStart: 1.4,
    rangeEnd: 7.0,
    horizonBlend: 0.22,
    // The summits stand clear of the haze filling the valley (mapbox vertical-range).
    verticalRangeStart: 1800,
    verticalRangeEnd: 3200,
  });

  map.light({ type: 'light', terrainLightingEnabled: true, shadowStrength: 0.35, shadowSoftness: 1.5 });

  // One owner for both sky switches: the custom shader calls `atmosphere()`, compiled only under
  // SKY_TYPE_ATMOSPHERE, and a missing function falls back to the built-in sky silently.
  function applySky() {
    if (customSky) {
      map.sky().apply({ type: 'SKY_TYPE_ATMOSPHERE', shaderSource: CUSTOM_SKY });
    } else {
      map.sky().apply({
        shaderSource: '',
        type: atmosphere ? 'SKY_TYPE_ATMOSPHERE' : 'SKY_TYPE_GRADIENT',
      });
    }
  }

  // A deliberately crude sun model: this example is about the sky, not ephemerides. The fog is
  // NOT tinted here - the SDK lights it with the same sun the ground gets.
  function applyHour() {
    const altitude = 62 * Math.sin((Math.PI * (hour - 6)) / 12);
    const azimuth = 90 + (hour - 6) * 15;
    // Below the horizon there is no sun to light anything with, and the ambient is what
    // keeps the map readable at all.
    const sunUp = Math.max(0, Math.min(1, altitude / 8));
    map.light().apply({
      sunAzimuth: azimuth,
      sunAltitude: altitude,
      sunIntensity: 0.15 + 0.85 * sunUp,
      ambientIntensity: 0.45 - 0.15 * sunUp,
      shadowStrength: 0.35 * sunUp,
    });
  }

  function applyMoment() {
    const m = MOMENTS[moment];
    map.fog().apply({ color: m.color, highColor: m.high, spaceColor: m.space, starIntensity: m.stars });
    map.sky().set('atmosphereLuminance', m.exposure);
    applyHour();
    host.caption(customSky ? 'custom sky: clouds by day, comets once the sun is down' : `${m.label} - ${CAPTIONS[moment]}`);
  }

  /** Advances the clock while the toggle is on. */
  function step() {
    if (!cycling) {
      return;
    }
    hour = (hour + 0.25) % 24;
    applyHour();
    host.after(100, step);
  }

  applyMoment();
  map.camera().moveTo(VIEW, { zoom: 11.5, rotation: 180, tilt: 33 });

  host.button('Time', () => {
    moment = (moment + 1) % MOMENTS.length;
    hour = MOMENTS[moment].hour;
    applyMoment();
  });
  host.toggle('Run the day', false, (on) => {
    cycling = on;
    if (on) {
      step();
    }
  });
  // The A/B for both the look and the cost: GRADIENT is the two-colour ramp the SDK drew
  // before the atmosphere, and it ignores every Atmosphere* property.
  host.toggle('Atmosphere', true, (on) => {
    atmosphere = on;
    applySky();
  });
  host.toggle('Comets & clouds', false, (on) => {
    customSky = on;
    applySky();
    applyMoment();
  });
  host.toggle('Peaks above the fog', true, (on) => {
    // Equal values disable the vertical fade, so the haze fills the whole view again.
    map.fog().apply({ verticalRangeStart: on ? 1800 : 0, verticalRangeEnd: on ? 3200 : 0 });
  });
  // Enabled is a real switch: nothing has to be driven through zero and back, and it stops
  // the HAZE only - the atmosphere colours and the stars stay.
  host.toggle('Fog', true, (on) => map.fog().set('enabled', on));
}
