/** A CartoCSS style project, and the two kinds of runtime parameter it can declare. */
import { styleProject, vectorTiles } from './shared.mjs';

const WATER = ['#8fb8d8', '#2f6f4f', '#7f5af0'];

// `styles` lists .mss FILE NAMES, `layers` the tile layers to decode (last listed drawn first);
// runtime parameters go under `styleparameters` with a `default` - `parameters` are macro constants.
const PROJECT = JSON.stringify({
  styles: ['style.mss'],
  layers: ['building', 'transportation', 'landcover', 'water'],
  styleparameters: {
    water_color: { default: '#8fb8d8' },
    show_buildings: { default: true },
  },
});

const MSS = [
  'Map { background-color: #f4f1ec; }',
  // LIVE: the decoded tiles point at this value, so changing it swaps colour and redraws.
  '#water { polygon-fill: [param::water_color]; }',
  '#landcover { polygon-fill: #dbe8cc; polygon-opacity: 0.5; }',
  // In a FILTER: this decides what the tile CONTAINS, so changing it re-decodes every tile.
  // The name is QUOTED in a filter - unquoted it is a syntax error, not a missing parameter.
  "#building['param::show_buildings'=true] { polygon-fill: #d9d0c9; line-color: #c3b8ae; line-width: 0.6; }",
  '#transportation { line-color: #ffffff; line-width: linear([view::zoom], (10, 0.6), (16, 5)); line-join: round; line-cap: round; }',
].join('\n');

let water = 0;

export default function start(host) {
  const map = host.map;
  // A raw CartoCSS string cannot declare `param::` values, so the project is written into the
  // module's in-memory filesystem first; the SDK does not care where a project came from.
  const folder = styleProject(map, 'alpine', { 'alpine.json': PROJECT, 'style.mss': MSS });

  // Registered under its own id because the example talks to it afterwards - a layer's style
  // cannot be read back as a handle. The params are in the spec, so the first frame is right.
  const style = map.style('alpine', {
    type: 'mbvt',
    project: { type: 'project', assets: { type: 'dir', path: folder }, name: 'alpine' },
    params: { water_color: WATER[0], show_buildings: 'true' },
  });

  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: 'alpine' });
  map.camera().moveTo([5.7245, 45.1885], { zoom: 13.5 });

  host.button('Water colour', () => {
    water = (water + 1) % WATER.length;
    // A style parameter is a PROPERTY: the rest of the path is the parameter's name.
    style.set('params.water_color', WATER[water]);
  });
  host.toggle('Buildings', true, (on) => {
    // A STRING: a style parameter is std::string, converted against its DECLARED default -
    // 'true' becomes a bool because the project declares `show_buildings: { default: true }`.
    style.set('params.show_buildings', String(on));
  });
  host.button('Night', () => {
    // Several at once, in ONE crossing - which is what a theme swap is.
    style.apply({ params: { water_color: '#0b2b4a', show_buildings: 'false' } });
  });
  host.caption('Two parameters, two costs: a colour swaps live, a filter re-decodes.');
}
