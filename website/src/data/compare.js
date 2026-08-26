/**
 * How Massif Maps sits next to the other mobile map renderers.
 *
 * Rules for this file, because a comparison table ages badly and reads as marketing when it is
 * wrong:
 *   - only rows where the difference is real and checkable, never a row invented to win it;
 *   - `'partial'` and its note beat a `'yes'` that needs a paragraph of caveats;
 *   - `'unknown'` is an honest cell — leave it rather than guess;
 *   - `asOf` is the date these were last checked. Update it when you touch a cell.
 *
 * Corrections are welcome as a PR against this file.
 */

export const AsOf = 'August 2026';

export const Engines = [
  {
    id: 'massif',
    name: 'Massif Maps',
    tag: 'this SDK',
    self: true,
    href: 'https://github.com/massif-maps/MassifMaps',
  },
  {
    id: 'maplibre',
    name: 'MapLibre GL Native',
    tag: 'BSD-2',
    href: 'https://github.com/maplibre/maplibre-native',
  },
  {
    id: 'mapbox',
    name: 'Mapbox Maps SDK',
    tag: 'proprietary',
    href: 'https://docs.mapbox.com/',
  },
  {
    id: 'tangram',
    name: 'Tangram ES',
    tag: 'MIT',
    href: 'https://github.com/tangrams/tangram-es',
  },
];

export const Rows = [
  {
    group: 'The basics',
    items: [
      {
        label: 'Licence',
        massif: {v: 'yes', note: 'BSD 3-Clause'},
        maplibre: {v: 'yes', note: 'BSD 2-Clause'},
        mapbox: {v: 'no', note: 'Proprietary, account and token required'},
        tangram: {v: 'yes', note: 'MIT'},
      },
      {
        label: 'Access token / account needed',
        massif: {v: 'yes', note: 'None. Bring your own tiles'},
        maplibre: {v: 'yes', note: 'None'},
        mapbox: {v: 'no', note: 'Required, and billed per map load'},
        tangram: {v: 'yes', note: 'None'},
      },
      {
        label: 'Actively maintained',
        massif: {v: 'yes'},
        maplibre: {v: 'yes'},
        mapbox: {v: 'yes'},
        tangram: {v: 'no', note: 'Upstream archived; this project tracks a fork'},
      },
      {
        label: 'Platforms',
        massif: {v: 'partial', note: 'Android, iOS. Desktop and web next'},
        maplibre: {v: 'yes', note: 'Android, iOS, desktop, plus GL JS on the web'},
        mapbox: {v: 'yes', note: 'Android, iOS, web'},
        tangram: {v: 'yes', note: 'Android, iOS, Linux, macOS'},
      },
    ],
  },
  {
    group: 'Styling',
    items: [
      {
        label: 'Style language',
        massif: {v: 'yes', note: 'CartoCSS'},
        maplibre: {v: 'yes', note: 'MapLibre / Mapbox GL style JSON'},
        mapbox: {v: 'yes', note: 'Mapbox GL style JSON'},
        tangram: {v: 'yes', note: 'YAML scene files'},
      },
      {
        label: 'Convert a MapBox / MapLibre style',
        massif: {v: 'partial', note: 'massif-style mapbox2css, with a coverage report'},
        maplibre: {v: 'yes', note: 'Native format'},
        mapbox: {v: 'yes', note: 'Native format'},
        tangram: {v: 'no'},
      },
      {
        label: 'Change a style value without re-decoding tiles',
        massif: {v: 'yes', note: 'Live style parameters — repaint only'},
        maplibre: {v: 'partial', note: 'Runtime style API, per property'},
        mapbox: {v: 'partial', note: 'Runtime style API, per property'},
        tangram: {v: 'partial', note: 'Scene updates re-parse the scene'},
      },
      {
        label: 'Custom shaders in the style',
        massif: {v: 'yes', note: 'Raster shaders, sky shader, post-processing passes'},
        maplibre: {v: 'partial', note: 'Custom layers, written against the GL context'},
        mapbox: {v: 'partial', note: 'Custom layers'},
        tangram: {v: 'yes', note: 'Shader blocks in the scene file'},
      },
    ],
  },
  {
    group: 'Terrain and relief',
    items: [
      {
        label: '3D terrain',
        massif: {v: 'yes', note: 'RTT fill draping, depth-correct occlusion, auto-flatten'},
        maplibre: {v: 'yes'},
        mapbox: {v: 'yes'},
        tangram: {v: 'yes'},
      },
      {
        label: 'Hillshade',
        massif: {v: 'yes', note: 'GDAL, Igor and multidirectional, plus exaggeration'},
        maplibre: {v: 'yes'},
        mapbox: {v: 'yes'},
        tangram: {v: 'yes'},
      },
      {
        label: 'Contour lines generated on the fly',
        massif: {v: 'yes', note: 'From RGB elevation tiles — no pre-baked contour data'},
        maplibre: {v: 'no', note: 'Needs a contour tile source'},
        mapbox: {v: 'no', note: 'Needs a contour tile source'},
        tangram: {v: 'partial', note: 'Shader contours in the scene'},
      },
      {
        label: 'Sun shadows',
        massif: {v: 'partial', note: 'Cascaded shadow maps on 3D buildings; terrain cast shadows wired but off'},
        maplibre: {v: 'no'},
        mapbox: {v: 'partial', note: '3D lighting for models'},
        tangram: {v: 'no'},
      },
      {
        label: 'Elevation queries from the app',
        massif: {v: 'yes', note: 'getElevation on the hillshade layer'},
        maplibre: {v: 'yes', note: 'queryTerrainElevation'},
        mapbox: {v: 'yes'},
        tangram: {v: 'unknown'},
      },
    ],
  },
  {
    group: 'Data formats',
    items: [
      {
        label: 'Mapbox Vector Tiles',
        massif: {v: 'yes'},
        maplibre: {v: 'yes'},
        mapbox: {v: 'yes'},
        tangram: {v: 'yes'},
      },
      {
        label: 'PMTiles',
        massif: {v: 'yes', note: 'Built in, local or over HTTP range requests'},
        maplibre: {v: 'partial', note: 'Via a protocol handler / plugin'},
        mapbox: {v: 'no'},
        tangram: {v: 'no'},
      },
      {
        label: 'MBTiles',
        massif: {v: 'yes'},
        maplibre: {v: 'partial', note: 'Via a custom source'},
        mapbox: {v: 'no'},
        tangram: {v: 'yes'},
      },
      {
        label: 'MapLibre Tiles (MLT)',
        massif: {v: 'partial', note: 'Decoder only — reads MLT, does not write it'},
        maplibre: {v: 'partial', note: 'The reference implementation, in progress'},
        mapbox: {v: 'no'},
        tangram: {v: 'no'},
      },
      {
        label: 'App GeoJSON tiled and styled like a tile source',
        massif: {v: 'yes', note: 'geojson-vt pyramid, then CartoCSS'},
        maplibre: {v: 'yes', note: 'GeoJSON source'},
        mapbox: {v: 'yes'},
        tangram: {v: 'yes'},
      },
    ],
  },
  {
    group: 'Beyond drawing the map',
    items: [
      {
        label: 'Routing in the SDK',
        massif: {v: 'yes', note: 'Valhalla embedded, online and offline; SGRE indoor'},
        maplibre: {v: 'no'},
        mapbox: {v: 'partial', note: 'A separate Navigation SDK, billed separately'},
        tangram: {v: 'no'},
      },
      {
        label: 'Offline geocoding',
        massif: {v: 'yes', note: 'OSM packages on the device'},
        maplibre: {v: 'no'},
        mapbox: {v: 'no', note: 'Hosted service'},
        tangram: {v: 'no'},
      },
      {
        label: 'Offline maps',
        massif: {v: 'yes', note: 'MBTiles, PMTiles, persistent cache, bundled styles'},
        maplibre: {v: 'yes', note: 'Offline manager and ambient cache'},
        mapbox: {v: 'yes', note: 'Offline regions'},
        tangram: {v: 'partial', note: 'MBTiles'},
      },
      {
        label: 'JavaScript binding for a mobile framework',
        massif: {v: 'partial', note: 'NativeScript today; Flutter and React Native planned'},
        maplibre: {v: 'yes', note: 'React Native, Flutter, community-maintained'},
        mapbox: {v: 'yes', note: 'React Native, Flutter'},
        tangram: {v: 'no'},
      },
      {
        label: 'One id/JSON API across every language',
        massif: {v: 'yes', note: 'The surface API — same strings from Java, Swift, TS or C'},
        maplibre: {v: 'no', note: 'A per-platform binding'},
        mapbox: {v: 'no', note: 'A per-platform binding'},
        tangram: {v: 'no'},
      },
    ],
  },
];

export const Marks = {
  yes: {glyph: '●', className: 'compareYes', label: 'yes'},
  partial: {glyph: '◐', className: 'comparePartial', label: 'partial'},
  no: {glyph: '○', className: 'compareNo', label: 'no'},
  unknown: {glyph: '?', className: 'compareUnknown', label: 'not checked'},
};
