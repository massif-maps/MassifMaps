/** The 2D/3D switch, and every way of driving it: the SDK's own animation, a tilt gesture, and the app's own clock. */
import { demTiles, osmRaster } from './shared.mjs';

/** What the 3D view looks AT. The viewpoint it is seen from is derived - see frameFlatStart. */
const SUMMIT = [7.6586, 45.9763];

const ZOOM = 12.5;
/** One rotation for both states: a switch that also spun the map made it impossible to tell where you came out. */
const ROTATION = 0;
/** tilt 90 is straight down in this SDK, so 2D is 90 and a landscape view is a LOW tilt. */
const TILT_2D = 90;
const TILT_3D = 20;
/** The tilt the auto rule switches at, and its default. */
const AUTO_TILT = 88;
/** How often the matched ramp samples the flight. */
const TICK_MS = 32;

export default function start(host) {
  const map = host.map;
  let in3D = false;
  let autoByTilt = false;
  let matchFlight = false;
  let seconds = 2.5;

  map.addLayer('basemap', { type: 'raster', source: osmRaster() });

  map.terrain({ type: 'terrain', source: demTiles() }).apply({
    // Configured and left on. The switch is `flattened`, and it opens flat - set BEFORE any
    // layer decodes, so not one tile is built for a 3D the map has not shown.
    enabled: true,
    flattened: true,
    // The whole way: a flat map decodes and culls as if no terrain were attached. RENDER
    // (the default) only stops the terrain passes and keeps 3D's triangles.
    flattenMode: 'TERRAIN_FLATTEN_MODE_FULL',
    // Off to start with, so the button below is the only thing switching.
    autoFlattenTilt: 0,
    autoFlattenParallax: 0,
    cameraClearance: 40,
  });
  map.sky({ type: 'sky' });
  map.fog({ type: 'fog', rangeStart: 2.2, rangeEnd: 8 });
  // The sun comes from BEHIND the camera or the face being looked at is the one in shadow.
  // This view is of the SOUTH side, so the light is south.
  map.light({
    type: 'light',
    terrainLightingEnabled: true,
    sunAzimuth: 170,
    sunAltitude: 42,
  });

  /** One number for both animations, which is what makes them the same length. */
  function applySeconds(value) {
    map.terrain().apply({
      autoFlattenDuration: value,
      // Timed apart from the sinking one: this direction waited for its tiles.
      autoFlattenRiseDuration: value,
    });
  }
  applySeconds(seconds);

  // Opens flat exactly where a round trip through 3D lands. That place depends on the viewport,
  // so put the camera where 3D would, ask where it stands, and drop to top-down there.
  function frameFlatStart() {
    map.camera().moveTo(SUMMIT, { zoom: ZOOM, rotation: ROTATION, tilt: TILT_3D });
    map.camera().moveTo(map.camera().eyePosition(), {
      zoom: ZOOM,
      rotation: ROTATION,
      tilt: TILT_2D,
    });
  }
  frameFlatStart();

  const flatCaption = () => (autoByTilt ? 'Flat. Tilt, or tap, to rise into the terrain.' : 'Flat, top-down. Tap to rise into the terrain.');
  const riseCaption = () => (autoByTilt ? '3D - the tilt asked for it, not the button.' : '3D. The SDK waited for its tiles before lifting the ground.');

  function fly() {
    // Where the camera IS, not what it is looking at: at tilt 20 the focus is kilometres
    // out in front, so re-centring on it would jump the map forward.
    const target = in3D ? SUMMIT : map.camera().eyePosition();
    // animate() takes milliseconds; the terrain durations above are seconds.
    map.camera()
      .animate(seconds * 1000)
      .moveTo(target, { zoom: ZOOM, rotation: ROTATION, tilt: in3D ? TILT_3D : TILT_2D });
  }

  /** Writing flattenRatio takes the ramp off the SDK's timer and puts it on the flight's. */
  function rampWithFlight() {
    host.after(TICK_MS, () => {
      if (map.camera().isMoving()) {
        const progress = map.camera().progress();
        map.terrain().set('flattenRatio', in3D ? 1 - progress : progress);
        rampWithFlight();
        return;
      }
      map.terrain().set('flattenRatio', in3D ? 0 : 1);
      // Hand the ratio back, or the switch stays MANUAL - which also keeps auto-flattening
      // suspended, and a tilt gesture would then do nothing.
      map.terrain().set('flattened', !in3D);
      host.caption(in3D ? riseCaption() : flatCaption());
    });
  }

  function waitForTiles() {
    host.after(TICK_MS, () => {
      if (map.terrain().get('switching')) {
        waitForTiles();
        return;
      }
      fly();
      rampWithFlight();
      host.caption("Rising on the flight's own clock.");
    });
  }

  // The app's own clock: feed the terrain the FLIGHT's progress, so the two cannot drift apart
  // even if the frame rate drops or the flight is interrupted.
  function matched() {
    if (!in3D) {
      fly(); // sinking has nothing to wait for
      rampWithFlight();
      host.caption("Sinking on the flight's own clock.");
      return;
    }
    // Rising does: ask for 3D so its tiles start loading, and fly only once the switch stops
    // holding the ground flat - a ratio driven up before then is held, and the start jumps.
    map.terrain().set('flattened', false);
    host.caption('Loading the tiles 3D needs before the flight starts.');
    waitForTiles();
  }

  // The SDK's own animation: ask for the state, and it ramps over autoFlattenDuration. Two
  // timers of the same length - which is close, and is all most apps need.
  function timed() {
    fly();
    // Written even with auto by tilt on: the rule fires on a THRESHOLD CROSSING, so it leaves an
    // explicit ask alone and the terrain moves with the flight instead of waiting for tilt 88.
    map.terrain().set('flattened', !in3D);
    host.caption(in3D ? riseCaption() : flatCaption());
  }

  host.button('2D / 3D', () => {
    if (map.camera().isMoving()) {
      host.caption('Still flying - let it land first.');
      return;
    }
    // Read the SDK's state rather than count presses: with auto by tilt on the RULE owns the
    // state, and a local flag drifts out of step until the button flies to the tilt it is at.
    in3D = map.terrain().get('flattened');
    if (matchFlight) {
      matched();
    } else {
      timed();
    }
  });
  host.slider('seconds', 0, 6, seconds, (value) => {
    seconds = value;
    applySeconds(value);
  });
  host.toggle('Match flight', false, (on) => {
    matchFlight = on;
    host.caption(on ? "Matched: the terrain reads the flight's own progress, so the two cannot drift." : 'Timed: two clocks of the same length. Close, but not the same clock.');
  });
  host.toggle('Full switch', true, (on) => {
    map.terrain().set('flattenMode', on ? 'TERRAIN_FLATTEN_MODE_FULL' : 'TERRAIN_FLATTEN_MODE_RENDER');
    host.caption(on ? 'FULL: flat costs nothing, each switch re-decodes the visible tiles.' : "RENDER: switching is free, but flat still carries 3D's triangles.");
  });
  host.toggle('Auto by tilt', false, (on) => {
    autoByTilt = on;
    map.terrain().set('autoFlattenTilt', on ? AUTO_TILT : 0);
    host.caption(on ? 'Auto on: tilt with two fingers and it switches itself. The button still leads - the rule only fires when the tilt CROSSES 88.' : 'Auto off: only the button switches.');
  });
  host.caption(flatCaption());
}
