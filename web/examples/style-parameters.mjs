/** A style's runtime parameters, and the two kinds a style can declare: Massif's own, changed live. */
import { massifStyle, vectorTiles } from './shared.mjs';

export default async function start(host) {
  const map = host.map;

  // Registered under its own id because the example talks to it afterwards - a layer's style
  // cannot be read back as a handle. The params are in the spec, so the first frame is right.
  const style = map.style('massif', { ...(await massifStyle(map)), params: { poiStyle: 'badge' } });

  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: 'massif' });
  map.camera().moveTo([5.7245, 45.1885], { zoom: 15.5 });

  host.toggle('POI discs', true, (on) => {
    // A style parameter is a PROPERTY: the rest of the path is the parameter's name. LIVE: the
    // decoded tiles point at this value, so the discs come and go with a redraw.
    style.set('params.poiStyle', on ? 'badge' : 'plain');
  });
  host.toggle('Boundaries', true, (on) => {
    // In a FILTER: this decides what the tile CONTAINS, so every tile decodes again. A string,
    // converted against the DECLARED default - 1 here, so '0' becomes the number 0.
    style.set('params.show_boundaries', on ? '1' : '0');
  });
  host.button('Walker', () => {
    // Several at once, in ONE crossing - which is what a theme swap is.
    style.apply({ params: { highlight_drinking_water: '1', path_min_zoom: '12', sac_scale_labels: '1' } });
  });
  host.caption('Two parameters, two costs: a value swaps live, a filter re-decodes.');
}
