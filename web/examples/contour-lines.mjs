/**
 * @title On-the-fly contour lines
 * @section terrain
 * @order 51
 * Contour lines traced from an elevation source at runtime, no contour tileset needed, and styled
 * with CartoCSS like any vector layer.
 */
import { demTiles, massifStyle, vectorTiles } from './shared.mjs';

// `div` is the largest round divisor of the elevation, so `div >= 100` picks the index lines.
const STYLE = `
#contour {
  line-color: #a0663a;
  line-opacity: 0.7;
  line-width: 0.6;
  [div >= 100] {
    line-width: 1.3;
    text-name: [ele] + ' m';
    text-face-name: 'Roboto';
    text-size: 10;
    text-fill: #7a4b28;
    text-halo-fill: #ffffffcc;
    text-halo-radius: 1.5;
    text-placement: line;
  }
}`;

export default async function start(host) {
  const map = host.map;
  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: await massifStyle(map) });
  // The contour source wraps the DEM: the tiles are fetched and decoded once, for both.
  const contours = map.source('contours', { type: 'contour', source: demTiles(), baseInterval: 20 });
  map.addLayer('contours', { type: 'vector', source: 'contours', style: { type: 'mbvt', cartocss: { type: 'cartocss', css: STYLE } } });
  map.camera().moveTo([5.73, 45.215], { zoom: 13.5 });

  for (const interval of [10, 20, 50]) {
    // A live property: the source traces again, the layer is not rebuilt.
    host.button(`${interval} m`, () => {
      contours.set('baseInterval', interval);
      host.caption(`Every ${interval} m, multiplied per zoom band.`);
    });
  }
  host.caption('Contours over the Bastille and the Chartreuse, traced from a terrarium DEM.');
}
