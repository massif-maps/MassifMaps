/*
 * What every ported example needs - the web twin of the NativeScript examples' shared.ts. Sources
 * carry no User-Agent (a page cannot set one) and no disk cache (the browser's HTTP cache is it).
 */

/** OpenStreetMap's raster tiles - what most of the basics examples sit on. */
export function osmRaster() {
  return { type: 'http', url: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png', maxZoom: 19 };
}

/** OpenFreeMap's planet vector tiles, in the OpenMapTiles schema. */
export function vectorTiles() {
  return { type: 'http', url: 'https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf', maxZoom: 14 };
}

/** Esri's world imagery - the raster under the 3D terrain examples. */
export function satelliteTiles() {
  return { type: 'http', url: 'https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', maxZoom: 18 };
}

/** Mapterhorn's DEM, terrarium-encoded; `dem_encoding` is what picks the decoder. */
export function demTiles() {
  return { type: 'http', url: 'https://tiles.mapterhorn.com/{z}/{x}/{y}.webp', minZoom: 1, maxZoom: 16, metaData: { dem_encoding: 'terrarium' } };
}

/** A CartoCSS style for the OpenMapTiles schema, as an inline string. */
export function alpineStyle() {
  return {
    type: 'mbvt',
    cartocss: {
      type: 'cartocss',
      css: [
        'Map { background-color: #f4f1ec; }',
        '#water { polygon-fill: #9cc3e0; }',
        '#landcover { polygon-fill: #dbe8cc; polygon-opacity: 0.5; }',
        '#landuse { polygon-fill: #dddddd; polygon-opacity: 0.35; }',
        '#building { polygon-fill: #d9d0c9; line-color: #c3b8ae; line-width: 0.6; }',
        '#transportation { line-color: #ffffff; line-width: linear([view::zoom], (10, 0.6), (16, 5)); line-join: round; line-cap: round; }',
        "#transportation['class'='motorway'] { line-color: #f6c667; line-width: linear([view::zoom], (8, 1.2), (16, 8)); }",
        '#waterway { line-color: #9cc3e0; line-width: 1.2; }',
        '#place::labels { text-name: [name]; text-face-name: "sans-serif"; text-size: 12; text-fill: #33302c; text-halo-fill: #ffffffcc; text-halo-radius: 1.5; }',
        '#mountain_peak::labels { text-name: [name]; text-face-name: "sans-serif"; text-size: 11; text-fill: #6b4a2f; text-halo-fill: #ffffffcc; text-halo-radius: 1.5; }',
      ].join('\n'),
    },
  };
}

/** The same, with no background of its own, so it can be drawn over imagery or terrain. */
export function overlayStyle() {
  return {
    type: 'mbvt',
    cartocss: {
      type: 'cartocss',
      css: [
        '#transportation { line-color: #ffffffcc; line-width: linear([view::zoom], (10, 0.5), (16, 4)); line-join: round; line-cap: round; }',
        '#place::labels { text-name: [name]; text-face-name: "sans-serif"; text-size: 12; text-fill: #ffffff; text-halo-fill: #00000099; text-halo-radius: 2; }',
        '#mountain_peak::labels { text-name: [name]; text-face-name: "sans-serif"; text-size: 12; text-fill: #ffffff; text-halo-fill: #00000099; text-halo-radius: 2; }',
      ].join('\n'),
    },
  };
}

/** Writes a CartoCSS style project into the module's filesystem and returns its folder, for `{ type: 'dir' }`. */
export function styleProject(map, name, files) {
  const folder = `/massif-style/${name}`;
  map.module.FS.mkdirTree(folder);
  for (const [file, text] of Object.entries(files)) {
    map.module.FS.writeFile(`${folder}/${file}`, text);
  }
  return folder;
}
