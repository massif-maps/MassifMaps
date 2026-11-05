/** A line coloured along its length: line metrics tag each tile piece with its part of the route, the style makes a ramp of it. */
import { massifStyle, vectorTiles } from './shared.mjs';

/** A stretch of the Tour du Mont Blanc, coloured smoothly, and a spur coloured in steps. */
const ROUTE = {
  type: 'FeatureCollection',
  features: [
    {
      type: 'Feature',
      properties: { gradient: '0:#2ecc71,0.5:#f1c40f,1:#e74c3c' },
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
    {
      type: 'Feature',
      properties: { gradient: 'step;0:#3b82f6,0.33:#8b5cf6,0.66:#ec4899' },
      geometry: {
        type: 'LineString',
        coordinates: [
          [6.74, 45.93],
          [6.78, 45.95],
          [6.83, 45.96],
          [6.88, 45.965],
        ],
      },
    },
  ],
};

const STYLE = [
  '#tour {',
  '  line-gradient: [gradient];',
  '  line-width: linear([view::zoom], (8, 3), (14, 8));',
  '  line-join: round;',
  '  line-cap: round;',
  '}',
].join('\n');

export default async function start(host) {
  const map = host.map;

  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: await massifStyle(map) });

  // lineMetrics is what lets the style know where along a route each tile piece sits.
  const tour = map.source('tour-data', { type: 'geojson', maxZoom: 14, lineMetrics: true });
  const layer = tour.createLayer('tour');
  tour.setGeoJSON(layer, ROUTE);

  map.addLayer('tour', {
    type: 'vector',
    source: 'tour-data',
    style: { type: 'mbvt', cartocss: { type: 'cartocss', css: STYLE } },
  });

  map.camera().moveTo([6.862, 45.886], { zoom: 10.2 });
  host.caption('A ramp along each route, smooth or in steps, from a string on the feature.');
}
