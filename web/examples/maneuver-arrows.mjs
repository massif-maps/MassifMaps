/** Turn arrows cut from a route at each maneuver, the head drawn by the line style itself. */
import { massifStyle, vectorTiles } from './shared.mjs';

/** A drive round Annecy station, as OSRM routes it: roundabouts, a U-turn, both turns. */
const ROUTE = [
  [6.121812, 45.901688], [6.121752, 45.90166], [6.121601, 45.901624], [6.120724, 45.901472], [6.119987, 45.901336],
  [6.119685, 45.901347], [6.119667, 45.901362], [6.119623, 45.901381], [6.119589, 45.901386], [6.119537, 45.901382],
  [6.11948, 45.901356], [6.119457, 45.90133], [6.119447, 45.901301], [6.119213, 45.901192], [6.11827, 45.901021],
  [6.118021, 45.900993], [6.11827, 45.901021], [6.119213, 45.901192], [6.119505, 45.901219], [6.119555, 45.901205],
  [6.119609, 45.901206], [6.119664, 45.901077], [6.119682, 45.901049], [6.119713, 45.901022], [6.119974, 45.900895],
  [6.120225, 45.90075], [6.120287, 45.900686], [6.120363, 45.900643], [6.120416, 45.900599], [6.120501, 45.900591],
  [6.120565, 45.900594], [6.121097, 45.900714], [6.121143, 45.900719], [6.121209, 45.900716], [6.121222, 45.900697],
  [6.121259, 45.900678], [6.121321, 45.900679], [6.121345, 45.900689], [6.121366, 45.900709], [6.121374, 45.900734],
  [6.121366, 45.900758], [6.121409, 45.900806], [6.121458, 45.900838], [6.121733, 45.900903], [6.121889, 45.900912],
  [6.121956, 45.9009], [6.122027, 45.900879], [6.122141, 45.900825], [6.122171, 45.900807], [6.122211, 45.900767],
  [6.122356, 45.900473], [6.122629, 45.89988], [6.122331, 45.899798], [6.121724, 45.899628], [6.121764, 45.89952],
  [6.122008, 45.899], [6.12207, 45.89881], [6.121217, 45.898719],
];

/**
 * Route point index of each maneuver as a routing engine reports it, the metres of route kept
 * before and after it, and a sideways shift in metres for a lane change (0 = follow the route).
 */
const MANEUVERS = [
  [3, 30, 35, 3.5, "Move to the left lane on Rue de l'Industrie"],
  [5, 20, 45, 0, 'At the roundabout, take the exit onto Avenue de Chevêne'],
  [15, 30, 30, 0, 'Make a U-turn on Avenue de Chevêne'],
  [18, 30, 30, 0, 'At the small roundabout, keep right on Avenue de Chevêne'],
  [33, 25, 45, 0, 'At the roundabout, take the exit onto Rue Vaugelas'],
  [51, 30, 30, 0, 'Turn right onto Rue Royale'],
  [53, 30, 30, 0, 'Turn left onto Rue de la Gare'],
  [56, 30, 30, 0, 'Turn right: you have arrived'],
];

const METRES_PER_DEGREE = 111319.5;

const HEADS = ['classic', 'wide', 'long'];

const ROUTE_STYLE = [
  '#route::case { line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.5), (17, 11)); line-join: round; line-cap: round; }',
  '#route { line-color: #1A73E8; line-width: linear([view::zoom], (12, 2.4), (17, 7.5)); line-join: round; line-cap: round; }',
].join('\n');

// Casing first, head over its shaft; the casing's head numbers are smaller because they are read
// against its own wider line (docs/features/maneuver-arrows.md).
const ARROW_STYLE = [
  '#maneuver::case { line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.9), (17, 13)); line-join: round; line-cap: round; }',
  '#maneuver::fill { line-color: #FFFFFF; line-width: linear([view::zoom], (12, 2.4), (17, 8)); line-join: round; line-cap: round; }',
  '#maneuver::headcase {',
  '  line-color: #0D47A1; line-width: linear([view::zoom], (12, 3.9), (17, 13));',
  '  line-end-arrow: true; line-arrow-only: true; line-arrow-width: 2.18; line-arrow-length: 1.72;',
  "  [head='wide'] { line-arrow-width: 2.94; line-arrow-length: 1.38; }",
  "  [head='long'] { line-arrow-width: 1.71; line-arrow-length: 2.51; }",
  '}',
  '#maneuver::head {',
  '  line-color: #FFFFFF; line-width: linear([view::zoom], (12, 2.4), (17, 8));',
  '  line-end-arrow: true; line-arrow-only: true; line-arrow-width: 2.4; line-arrow-length: 1.9;',
  "  [head='wide'] { line-arrow-width: 3.2; line-arrow-length: 1.5; }",
  "  [head='long'] { line-arrow-width: 1.9; line-arrow-length: 2.8; }",
  '}',
].join('\n');

/** Ahead of point `index` along its segment, moving `shift` metres left over the first 40%. */
function laneChange(index, after, shift) {
  const [at, next] = [ROUTE[index], ROUTE[index + 1]];
  const k = Math.cos((at[1] * Math.PI) / 180);
  const [dx, dy] = [(next[0] - at[0]) * k, next[1] - at[1]];
  const d = Math.hypot(dx, dy);
  const left = shift / METRES_PER_DEGREE;
  return [0.4, 1].map((f) => {
    const along = (f * after) / METRES_PER_DEGREE;
    return [at[0] + (dx * along - dy * left) / d / k, at[1] + (dy * along + dx * left) / d];
  });
}

function arrows(builder, head) {
  return {
    type: 'FeatureCollection',
    features: MANEUVERS.flatMap(([index, before, after, shift]) => {
      // A lane change leaves the route: the builder walks the part behind, the shift is drawn ahead.
      builder.set('lengthBefore', before).set('lengthAfter', shift ? 0 : after);
      return builder.call('buildArrowAtIndex', ROUTE, index).features.map((arrow) => {
        let coordinates = arrow.geometry.coordinates;
        if (shift) {
          const [lng, lat] = ROUTE[index];
          coordinates = coordinates.filter(([x, y], i) => i < coordinates.length - 1 || Math.abs(x - lng) + Math.abs(y - lat) > 1e-9);
          coordinates = [...coordinates, ...laneChange(index, after, shift)];
        }
        return { ...arrow, geometry: { type: 'LineString', coordinates }, properties: { head } };
      });
    }),
  };
}

/** Compass bearing of the route as it arrives at point `index`, for a heading-up camera. */
function bearing(index) {
  const [a, b] = [ROUTE[index - 1], ROUTE[index]];
  return (Math.atan2((b[0] - a[0]) * Math.cos((b[1] * Math.PI) / 180), b[1] - a[1]) * 180) / Math.PI;
}

export default async function start(host) {
  const map = host.map;

  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: await massifStyle(map) });

  const route = map.source('route-data', { type: 'geojson', maxZoom: 18 });
  route.setGeoJSON(route.createLayer('route'), {
    type: 'FeatureCollection',
    features: [{ type: 'Feature', properties: {}, geometry: { type: 'LineString', coordinates: ROUTE } }],
  });
  map.addLayer('route', { type: 'vector', source: 'route-data', style: { type: 'mbvt', cartocss: { type: 'cartocss', css: ROUTE_STYLE } } });

  // A layer of its own, added last: it draws over the route and every layer before it.
  const maneuvers = map.source('maneuver-data', { type: 'geojson', maxZoom: 18 });
  const layer = maneuvers.createLayer('maneuver');
  const builder = map.object('geometry', 'maneuver-arrows', { type: 'maneuver-arrow' });
  let head = 0;
  maneuvers.setGeoJSON(layer, arrows(builder, HEADS[head]));
  map.addLayer('maneuver', { type: 'vector', source: 'maneuver-data', style: { type: 'mbvt', cartocss: { type: 'cartocss', css: ARROW_STYLE } } });

  const overview = (duration) => {
    map.camera().moveTo([6.1203, 45.9002], { zoom: 16.3, rotation: 0, tilt: 80, duration });
    host.caption('One arrow per maneuver, cut from the route either side of it.');
  };
  overview(0);

  let step = -1;
  host.button('Next maneuver', () => {
    step = (step + 1) % MANEUVERS.length;
    const [index, , , , text] = MANEUVERS[step];
    map.camera().moveTo(ROUTE[index], { zoom: 17, rotation: -bearing(index), tilt: 70, duration: 1500 });
    host.caption(`${step + 1}/${MANEUVERS.length}: ${text}.`);
  });
  host.button('Head shape', () => {
    head = (head + 1) % HEADS.length;
    maneuvers.setGeoJSON(layer, arrows(builder, HEADS[head]));
    host.caption(`${HEADS[head]} head: line-arrow-width and -length, no marker and no bitmap.`);
  });
  host.button('Overview', () => overview(1500));
}
