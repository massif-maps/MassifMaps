/** Searching the vector tiles the map is already showing, and pinning what comes back. */
import { massifStyle, vectorTiles } from './shared.mjs';

const CENTRE = [5.7245, 45.1885];
/** Degrees around the centre. A search with NO geometry scans the whole world at its zoom. */
const SPAN = 0.08;

function corner(dLon, dLat) {
  return [CENTRE[0] + dLon, CENTRE[1] + dLat];
}

export default async function start(host) {
  const map = host.map;

  map.addLayer('basemap', { type: 'vector', source: vectorTiles(), style: await massifStyle(map) });
  map.camera().moveTo(CENTRE, { zoom: 13.5 });

  // Built FROM THE LAYER, sharing its source and decoder. `{ type: 'vectortile', layer }` has a
  // HAND-WRITTEN factory the spec table does not describe, so naming the class gives it its API.
  const service = map.object('search', 'poi', { type: 'vectortile', layer: 'basemap' }, 'massif::VectorTileSearchService');
  service.apply({ minZoom: 14, maxZoom: 14, maxResults: 12 });
  // A struct property: a JSON array, not a call that had to be added for it.
  service.set('layers', ['place', 'poi', 'mountain_peak']);

  const hit = map.elements().style('hit', { type: 'marker', size: 20, color: 0xff3a6ea5 });

  async function search(regex) {
    map.elements().clear();
    host.caption('Searching…');

    const request = map.object('search', `request-${regex.length}`, { type: 'request' });
    request.set('regexFilter', regex);
    // Bound it, or the search walks every tile in the world at zoom 14.
    const area = map.object(
      'geometry',
      'area',
      {
        type: 'geojson',
        geojson: {
          type: 'Polygon',
          coordinates: [[corner(-SPAN, -SPAN), corner(SPAN, -SPAN), corner(SPAN, SPAN), corner(-SPAN, SPAN), corner(-SPAN, -SPAN)]],
        },
      },
      'massif::PolygonGeometry'
    );
    request.set('geometry', area.handle);
    request.set('projection', map.object('projection', 'wgs84', { type: 'EPSG:4326' }, 'massif::EPSG4326').handle);

    try {
      // ASYNC, because findFeatures fetches and decodes every tile in range. `extract` runs while
      // the result is ALIVE - the facade frees an async payload once the handlers have run.
      const found = await service.callAsync('findFeatures', [request.handle], (result) => result.collect((feature) => feature.getPos('geometry.centerPos', 'EPSG:4326')));
      for (const at of found) {
        if (at) {
          map.addMarker({ type: 'marker', position: at, style: hit.id });
        }
      }
      host.caption(`${found.length} result${found.length === 1 ? '' : 's'}.`);
    } catch (e) {
      // The REASON, not just "failed" - a bad regex and an unreachable tile server
      // read the same otherwise, and both happen.
      host.caption(`Search failed: ${e?.message ?? e}`);
      console.error(e);
    }
  }

  // The filter is an ECMAScript regex, which has no inline flags - "(?i)" is a parse error,
  // not a case-insensitive match, and findFeatures comes back as a failure.
  host.button('Search "gare"', () => search('.*[Gg]are.*'));
  host.button('Search "parc"', () => search('.*[Pp]arc.*'));
  host.caption('Grenoble. Tap a search - results are pinned as markers.');
}
