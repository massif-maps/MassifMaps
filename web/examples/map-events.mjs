/** The camera event an app should hang its data refresh on, and how to tell whose move it was. */
import { alpineStyle, vectorTiles } from './shared.mjs';

export default function start(host) {
  const map = host.map;

  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: alpineStyle() });
  map.camera().moveTo([6.8652, 45.8326], { zoom: 11 });

  let moves = 0;
  let stables = 0;

  // Every camera change, 47 to 159 a second during a drag - the wrong place to refresh anything.
  // Throttled to 4 a second: events inside the window are DROPPED, not delivered late.
  map.subscribe(
    'map.moved',
    (e) => {
      moves++;
      host.caption(`moving (${e.reason}) - ${moves} frames delivered`);
    },
    { throttle: 250 }
  );

  // The end of a movement, once, with what caused it. A tap that did not move the camera does
  // not fire it, so there is no "did it actually move?" flag and no debounce to add.
  map.subscribe('map.stable', (e) => {
    stables++;
    const reason = e.reason;
    // A refresh should follow the USER, not the app's own camera calls - otherwise the
    // button below would trigger the very fetch it just made stale.
    const action = reason === 'MAP_MOVE_REASON_GESTURE' ? 'refreshing' : `ignored (${reason})`;
    host.caption(`${moves} moves -> ${stables} stable  ·  ${action}`);
  });

  host.button('Fly to Chamonix', () => {
    // Raises map.stable with reason "animation", not "gesture".
    map.camera().moveTo([6.8652, 45.9237], { zoom: 13, duration: 1500 });
  });

  host.caption('Drag the map, then press the button. Watch the reason change.');
}
