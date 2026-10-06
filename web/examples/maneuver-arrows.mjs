/** Turn arrows cut from a route at each maneuver, the head drawn by the line style itself. */
import { massifStyle, vectorTiles } from './shared.mjs';

/** A drive through the Eixample, Barcelona, along its one-way streets. */
const ROUTE = [
  [2.16274, 41.39225], [2.16321, 41.3926], [2.16365, 41.39294], [2.16422, 41.39249], [2.16433, 41.39244],
  [2.16476, 41.3921], [2.16493, 41.39196], [2.16522, 41.39168], [2.16583, 41.39119], [2.16597, 41.3913],
  [2.16606, 41.39136], [2.16613, 41.39141], [2.16615, 41.39143], [2.16657, 41.39174], [2.16697, 41.39204],
  [2.16729, 41.39229], [2.16755, 41.39229], [2.16763, 41.39233], [2.16813, 41.39195], [2.16861, 41.39158],
  [2.1694, 41.39098], [2.16971, 41.39075], [2.16987, 41.39063], [2.17061, 41.39117], [2.17077, 41.39135],
  [2.17097, 41.39153], [2.17086, 41.39162], [2.17016, 41.39215],
];

/** Route point index of each maneuver, as a routing engine reports it. */
const MANEUVERS = [
  [2, 'Turn right onto Passeig de Gràcia'],
  [8, 'Turn left onto Carrer del Consell de Cent'],
  [17, 'Turn right onto Carrer de Pau Claris'],
  [22, 'Turn left onto Gran Via de les Corts Catalanes'],
  [25, 'Turn left onto Carrer de Roger de Llúria'],
];

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

function arrows(builder, head) {
  return {
    type: 'FeatureCollection',
    features: MANEUVERS.flatMap(([index]) =>
      builder.call('buildArrowAtIndex', ROUTE, index).features.map((arrow) => ({ ...arrow, properties: { head } }))),
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
  const builder = map.object('geometry', 'maneuver-arrows', { type: 'maneuver-arrow', lengthBefore: 30, lengthAfter: 30 });
  let head = 0;
  maneuvers.setGeoJSON(layer, arrows(builder, HEADS[head]));
  map.addLayer('maneuver', { type: 'vector', source: 'maneuver-data', style: { type: 'mbvt', cartocss: { type: 'cartocss', css: ARROW_STYLE } } });

  const overview = (duration) => {
    map.camera().moveTo([2.1674, 41.3916], { zoom: 16.1, rotation: 0, tilt: 80, duration });
    host.caption('One arrow per maneuver, cut from the route 30 m either side of the turn.');
  };
  overview(0);

  let step = -1;
  host.button('Next maneuver', () => {
    step = (step + 1) % MANEUVERS.length;
    const [index, text] = MANEUVERS[step];
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
