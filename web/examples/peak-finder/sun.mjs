/*
 * The sun of the peak finder, as peakfinder.com draws it: the day's path, the sun at the chosen
 * minute, hour marks, and where it rises and sets over the TERRAIN in front of the viewpoint.
 * The SDK knows directions and the skyline (celestial layers, calculateHorizon); the astronomy is here.
 */

const deg = Math.PI / 180;

/** NOAA's low-accuracy solar position - the SDK's own (LightOptions::setSunPositionFromTime). */
export function sunAt(time, lat, lon) {
  const n = time / 86400000 + 2440587.5 - 2451545.0;
  const meanLong = (280.460 + 0.9856474 * n) % 360;
  const meanAnom = ((357.528 + 0.9856003 * n) % 360) * deg;
  const eclipticLong = (meanLong + 1.915 * Math.sin(meanAnom) + 0.020 * Math.sin(2 * meanAnom)) * deg;
  const obliquity = (23.439 - 0.0000004 * n) * deg;
  const rightAsc = Math.atan2(Math.cos(obliquity) * Math.sin(eclipticLong), Math.cos(eclipticLong));
  const decl = Math.asin(Math.sin(obliquity) * Math.sin(eclipticLong));
  const gmst = (((18.697374558 + 24.06570982441908 * n) % 24) + 24) % 24;
  const hourAngle = gmst * 15 * deg + lon * deg - rightAsc;
  const phi = lat * deg;
  const alt = Math.asin(Math.sin(phi) * Math.sin(decl) + Math.cos(phi) * Math.cos(decl) * Math.cos(hourAngle));
  const az = Math.atan2(Math.sin(hourAngle), Math.cos(hourAngle) * Math.sin(phi) - Math.tan(decl) * Math.cos(phi));
  return { az: (az / deg + 540) % 360, alt: alt / deg };
}

/** Where the air lifts the sun to (Saemundsson), so it is compared with an apparent skyline. */
const apparent = (alt) => alt + (alt > -2 ? 1.02 / Math.tan((alt + 10.3 / (alt + 5.11)) * deg) / 60 : 0);
const clock = (time) => new Date(time).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' });

// Under LOW the sun is below any skyline, over HIGH above it: only between is the terrain measured.
const LOW = -4;
const HIGH = 40;

/**
 * Builds the sky on `map` - the path under the summit names (`below`), the sun and the times over
 * them (`above`) - and returns `update({ lat, lon, eye, day, minute, hours })`, cheap to call often.
 */
export function createSky(map, terrain, below, above, report) {
  const ratio = window.devicePixelRatio || 1;
  const add = (layer, id, spec) => {
    const object = map.object('celestial', id, spec);
    layer.call('add', object.handle);
    return object;
  };
  // Widths are device pixels. Drawn under the horizon too: the terrain in front hides that part.
  const path = add(below, 'sky.path', { type: 'arc', color: '#f59e0bd0', width: 3 * ratio, belowHorizonVisible: true });
  const marks = add(below, 'sky.marks', { type: 'arc', color: '#b45309', width: 2 * ratio, belowHorizonVisible: true });
  const glow = add(above, 'sky.glow', { type: 'sprite', screenSize: 56 * ratio, color: '#fbbf2466', softness: 1 });
  const sun = add(above, 'sky.sun', { type: 'sprite', screenSize: 20 * ratio, color: '#f59e0b', softness: 0.15 });
  const PLATE = { fontName: 'sans-serif Bold', fontSize: 15, textColor: '#92400e', backgroundColor: '#ffffffe6', backgroundRadius: 7, paddingX: 7, paddingY: 3 };
  const HALO = { fontName: 'sans-serif Medium', fontSize: 13, textColor: '#b45309', haloColor: '#fffffff2', haloWidth: 4 };
  const label = (id, style, lift) => {
    const object = add(above, id, { type: 'label', visible: false, ...style });
    object.call('setOffset', 0, lift);
    return object;
  };
  const hourLabels = Array.from({ length: 24 }, (unused, hour) => label(`sky.hour.${hour}`, HALO, 4));
  const riseLabel = label('sky.rise', PLATE, 14);
  const setLabel = label('sky.set', PLATE, 14);
  // The rise and the set name the ridge they cross, so the ridge must not hide them.
  riseLabel.set('occludedByMap', false);
  setLabel.set('occludedByMap', false);

  const show = (object, text, az, alt) => {
    if (!text) {
      object.set('visible', false);
      return;
    }
    object.set('text', text);
    object.call('setDirection', az, alt, 0);
    object.set('visible', true);
  };

  let samples = [];
  let pathKey = '';
  let planKey = '';
  let plannedAt = 0;

  // The whole circle the sun runs through that day, every 2 minutes.
  const planPath = ({ lat, lon, day }) => {
    const key = `${day}|${lat.toFixed(3)}|${lon.toFixed(3)}`;
    if (key === pathKey) {
      return;
    }
    pathKey = key;
    samples = [];
    for (let minute = 0; minute <= 1440; minute += 2) {
      const time = day + minute * 60000;
      const { az, alt } = sunAt(time, lat, lon);
      samples.push({ time, az, alt: apparent(alt) });
    }
    path.call('setDirections', samples.flatMap((sample) => [sample.az, sample.alt]));
  };

  // Rise and set over the terrain: the skyline coarsely where the sun is low, finely where it crosses.
  const planCrossings = ({ lat, lon, eye, hours }) => {
    const skylineOf = (list) => (list.length
      ? Array.from(terrain.call('calculateHorizon', [lon, lat], eye, list.map((sample) => sample.az), 200000) ?? [])
      : []);
    const coarse = samples.filter((sample, index) => index % 4 === 0 && sample.alt > LOW && sample.alt < HIGH);
    const coarseSkyline = skylineOf(coarse);
    const known = coarse.map((sample, index) => [sample.time, coarseSkyline[index] > -90 ? coarseSkyline[index] : 0]);
    const skylineAt = (sample) => {
      let after = known.findIndex(([time]) => time >= sample.time);
      if (after < 0) {
        after = known.length - 1;
      }
      const before = Math.max(0, known[after]?.[0] === sample.time ? after : after - 1);
      const [t0, h0] = known[before] ?? [0, 0];
      const [t1, h1] = known[after] ?? [0, 0];
      return t1 === t0 ? h0 : h0 + (h1 - h0) * (sample.time - t0) / (t1 - t0);
    };
    const margin = (sample) => (sample.alt <= LOW ? -1 : sample.alt >= HIGH ? 1 : sample.alt - skylineAt(sample));
    let rise = null;
    let set = null;
    const step = 4;
    for (let index = step; index < samples.length; index += step) {
      const up = margin(samples[index - step]) >= 0;
      if (up === (margin(samples[index]) >= 0)) {
        continue;
      }
      const fine = samples.slice(index - step, index + 1);
      const fineSkyline = skylineOf(fine);
      const fineMargin = fine.map((sample, k) => sample.alt - (fineSkyline[k] > -90 ? fineSkyline[k] : 0));
      let k = 1;
      while (k < fine.length - 1 && (fineMargin[k] >= 0) === up) {
        k++;
      }
      const fraction = Math.max(0, Math.min(1, fineMargin[k - 1] / (fineMargin[k - 1] - fineMargin[k] || 1)));
      const between = (field) => fine[k - 1][field] + fraction * (fine[k][field] - fine[k - 1][field]);
      const crossing = { time: between('time'), az: between('az'), alt: between('alt') };
      if (!up) {
        rise = rise ?? crossing;
      } else {
        set = crossing;
      }
    }
    const riseText = rise ? `↑ ${clock(rise.time)}` : '';
    const setText = set ? `↓ ${clock(set.time)}` : '';
    report(rise || set ? [riseText, setText].filter(Boolean).join('  ·  ') : 'no sun over the terrain');
    show(riseLabel, riseText, rise?.az, rise?.alt);
    show(setLabel, setText, set?.az, set?.alt);

    // On the hour, a short stroke across the path and its time, clear of a rise or a set.
    const ticks = [];
    for (let hour = 0; hour < 24; hour++) {
      const sample = samples[hour * 30];
      const next = samples[hour * 30 + 1];
      const clear = [rise, set].every((crossing) => !crossing || Math.abs(crossing.time - sample.time) > 40 * 60000);
      if (!hours || !clear || margin(sample) < 0.5) {
        show(hourLabels[hour], '');
        continue;
      }
      show(hourLabels[hour], clock(sample.time), sample.az, sample.alt);
      const dAz = (next.az - sample.az) * Math.cos(sample.alt * deg);
      const dAlt = next.alt - sample.alt;
      const length = Math.hypot(dAz, dAlt) || 1;
      const nAz = -dAlt / length / Math.cos(sample.alt * deg) * 0.35;
      const nAlt = dAz / length * 0.35;
      ticks.push(sample.az - nAz, sample.alt - nAlt, sample.az + nAz, sample.alt + nAlt);
    }
    marks.call('setSegments', ticks);
  };

  const placeSun = ({ lat, lon, day, minute }) => {
    const { az, alt } = sunAt(day + minute * 60000, lat, lon);
    for (const object of [sun, glow]) {
      object.call('setDirection', az, apparent(alt), 0);
      object.set('visible', alt > -1.5);
    }
  };

  // Replanned when the view changes, and once a second besides: the terrain the skyline is measured
  // on keeps arriving after a move, and an early skyline is lower than the real one.
  return (view) => {
    placeSun(view);
    const key = `${view.day}|${view.lat.toFixed(5)}|${view.lon.toFixed(5)}|${view.eye.toFixed(1)}|${view.hours}`;
    const now = performance.now();
    if (key !== planKey || now - plannedAt > 1000) {
      planKey = key;
      plannedAt = now;
      planPath(view);
      planCrossings(view);
    }
  };
}

export { clock };
