/*
 * What every ported example needs - the web twin of the NativeScript examples' shared.ts. Sources
 * carry no User-Agent (a page cannot set one) and no disk cache (the browser's HTTP cache is it).
 */

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

// The Massif CartoCSS project (docs/styles/massif-sdk.md): the published copy beside these examples on
// the site, which the release workflow keeps current; anywhere else, the site's.
const MASSIF_SITE = 'https://massif-maps.github.io/MassifMaps/styles/massif/carto/';
const MASSIF_VARIANTS = ['streets', 'outdoor', 'topo', 'hybrid', 'eink'];
const IMAGE = /[\w./-]+\.(?:png|jpg|svg)/g;

/**
 * Massif as an `mbvt` style spec for `variant`: the project is fetched into the module's filesystem
 * once, and every variant is a style parameter of it (`style.set('params.variant', 'eink')`).
 */
export async function massifStyle(map, variant = 'streets') {
  const folder = '/massif-style/massif';
  if (!map.module.FS.analyzePath(`${folder}/project.json`).exists) {
    let base = new URL('../../styles/massif/carto/', import.meta.url).href;
    let project = await fetch(`${base}project.json`).then((r) => (r.ok ? r.json() : null)).catch(() => null);
    if (!project) {
      base = MASSIF_SITE;
      project = await (await fetch(`${base}project.json`)).json();
    }
    const texts = await Promise.all(project.styles.map((name) => fetch(base + name).then((r) => r.text())));
    const images = new Set(Object.values(project.styleparameters ?? {}).filter((v) => typeof v === 'string' && /\.(?:png|jpg|svg)$/.test(v)));
    for (const text of texts) for (const match of text.matchAll(IMAGE)) images.add(match[0]);
    const names = ['project.json', ...MASSIF_VARIANTS.map((v) => `${v}.json`), ...project.styles, ...images,
                   ...(project.fonts ?? []).map((f) => `fonts/${f}`)];
    const files = await Promise.all(names.map(async (name) => [name, new Uint8Array(await (await fetch(base + name)).arrayBuffer())]));
    for (const [name, bytes] of files) {
      const path = `${folder}/${name}`;
      map.module.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
      map.module.FS.writeFile(path, bytes);
    }
  }
  return { type: 'mbvt', project: { type: 'project', assets: { type: 'dir', path: folder }, name: variant } };
}
