/** A GeoJSON document served AS vector tiles, through the ordinary style and renderer. */
import { osmRaster } from './shared.mjs';

/** A stretch of the Tour du Mont Blanc, plus the huts along it. */
const ROUTE = {
  type: 'FeatureCollection',
  features: [
    {
      type: 'Feature',
      properties: { kind: 'trail' },
      geometry: {
        type: 'LineString',
        coordinates: [
          [6.8694, 45.9237],
          [6.829, 45.9081],
          [6.8027, 45.8862],
          [6.7861, 45.8548],
          [6.8062, 45.8281],
          [6.8556, 45.809],
          [6.9016, 45.7992],
          [6.9584, 45.8138],
          [6.9821, 45.8452],
          [6.9612, 45.8813],
          [6.9163, 45.9096],
          [6.8694, 45.9237],
        ],
      },
    },
    { type: 'Feature', properties: { kind: 'hut', name: 'Lac Blanc' }, geometry: { type: 'Point', coordinates: [6.829, 45.9081] } },
    { type: 'Feature', properties: { kind: 'hut', name: 'Bonhomme' }, geometry: { type: 'Point', coordinates: [6.8062, 45.8281] } },
    { type: 'Feature', properties: { kind: 'hut', name: 'Elisabetta' }, geometry: { type: 'Point', coordinates: [6.9584, 45.8138] } },
  ],
};

const STYLE = [
  '#tour {',
  '  line-color: #E5484D;',
  '  line-width: linear([view::zoom], (8, 2), (14, 6));',
  '  line-join: round;',
  '  line-cap: round;',
  '}',
  "#tour['kind'='hut'] {",
  '  marker-fill: #FFFFFF;',
  '  marker-line-color: #E5484D;',
  '  marker-line-width: 2;',
  '  marker-width: 9;',
  '}',
].join('\n');

export default function start(host) {
  const map = host.map;

  map.addLayer('basemap', { type: 'raster', source: osmRaster() });

  // The source re-tiles whatever it is given, so replacing the document later is one call
  // rather than a layer rebuild.
  const tour = map.source('tour-data', { type: 'geojson', maxZoom: 14 });
  const layer = tour.createLayer('tour');
  tour.setGeoJSON(layer, ROUTE);

  map.addLayer('tour', {
    type: 'vector',
    source: 'tour-data',
    style: { type: 'mbvt', cartocss: { type: 'cartocss', css: STYLE } },
  });

  map.camera().moveTo([6.882, 45.866], { zoom: 10.4 });
  host.caption('One FeatureCollection, tiled on the fly and styled with CartoCSS.');
}
