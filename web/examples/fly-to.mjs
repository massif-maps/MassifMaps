/** Flying between places, moving everything in one flight. */
import { osmRaster } from './shared.mjs';

const PLACES = [
  { name: 'Mont Blanc', at: [6.8652, 45.8326], zoom: 12.5, rotation: 0, tilt: 60 },
  { name: 'Grenoble', at: [5.7245, 45.1885], zoom: 13.5, rotation: 25, tilt: 45 },
  { name: 'Verdon', at: [6.332, 43.75], zoom: 13, rotation: -30, tilt: 70 },
];

export default function start(host) {
  const map = host.map;

  map.addLayer('basemap', { type: 'raster', source: osmRaster() });
  map.camera().moveTo([5.7245, 45.1885], { zoom: 6 });

  for (const place of PLACES) {
    host.button(place.name, () => {
      // One call moves position, zoom, rotation and tilt together. Four separate
      // setters animate independently and visibly fight each other.
      map.camera().moveTo(place.at, {
        zoom: place.zoom,
        rotation: place.rotation,
        tilt: place.tilt,
        duration: 3000,
      });
    });
  }
  host.button('Stop', () => map.camera().stop());
  host.caption('Tap a place. Each flight moves all four camera values at once.');
}
