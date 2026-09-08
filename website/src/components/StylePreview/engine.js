/*
 * The style preview's map, driven entirely through the facade's C ABI.
 *
 * There is no preview-specific C++: the page loads the same wasm module web/demo builds, then
 * clears its layers and puts up one of its own, built from a spec. Everything below is therefore
 * `create`, `set` and `call` on ids and handles - see docs/internals/api-facade.md - which is also
 * the point of the page: what it can do here, an app can do from Java, Objective-C or TypeScript.
 */

import { Massif, MassifCamera } from '../../../../web/js/massif.mjs';
import { VARIABLES_FILE, convert } from '../../../../tools/style-cli/dist/mapbox2css/index.js';
import { browserSpriteHost } from '../../../../tools/style-cli/dist/mapbox2css/browser-host.js';
import { loadSprites, setSpriteHost } from '../../../../tools/style-cli/dist/mapbox2css/sprite.js';
import propertyTable from '../../../../tools/style-cli/src/generated/properties.json';

/** Where a converted MapBox style is assembled inside the module's in-memory filesystem. */
const PROJECT_DIR = '/styles/preview';
/** Preloaded by the wasm build from web/demo/fonts - the fallback for a face the build has not got. */
const FALLBACK_FONT = 'file:///fonts/Roboto.ttf';

/**
 * OpenFreeMap's planet, which needs no key and allows any origin. Its tile URL carries the date of
 * the planet cut and rolls, so the TileJSON is read for the current one rather than a URL being
 * pinned here and going stale.
 */
export const DEFAULT_TILEJSON = 'https://tiles.openfreemap.org/planet';
export const DEFAULT_MAX_ZOOM = 14;

export async function resolveTileUrl(tilejson) {
  const response = await fetch(tilejson);
  if (!response.ok) throw new Error(`${response.status} ${response.statusText} for ${tilejson}`);
  const document = await response.json();
  const url = document?.tiles?.[0];
  if (!url) throw new Error(`No "tiles" in the TileJSON at ${tilejson}`);
  return { url, maxZoom: document.maxzoom ?? DEFAULT_MAX_ZOOM };
}

/**
 * Cross-origin isolation, which the module needs before it can start: the SDK's tile pools are
 * pthreads, so it wants SharedArrayBuffer, and a browser only hands that to an isolated page.
 *
 * The dev server sends COOP/COEP itself (website/plugins/style-preview). GitHub Pages sends no
 * custom headers at all, so there the service worker adds them and reloads once.
 */
const RELOADED = 'coi-reloaded';

export async function ensureIsolated(serviceWorkerUrl) {
  if (globalThis.crossOriginIsolated) {
    sessionStorage.removeItem(RELOADED);
    return { isolated: true };
  }
  if (!navigator.serviceWorker) {
    return {
      isolated: false,
      reason: 'This browser has no service workers, so the page cannot be made cross-origin '
        + 'isolated — and without that it has no SharedArrayBuffer to run the renderer in.',
    };
  }
  try {
    await navigator.serviceWorker.register(serviceWorkerUrl);
    // `ready`, not the register() result: right after registering the worker is still installing,
    // and only an ACTIVE one can serve the reload that makes the page isolated.
    await navigator.serviceWorker.ready;
  } catch (error) {
    return {
      isolated: false,
      reason: `This browser refused the service worker that supplies cross-origin isolation `
        + `(${error.message}). The preview needs either that or a host sending COOP/COEP.`,
    };
  }
  if (sessionStorage.getItem(RELOADED)) {
    return {
      isolated: false,
      reason: 'The service worker is running but the page is still not cross-origin isolated.',
    };
  }
  // The worker only controls a page it was already serving, so this is the reload that arms it.
  sessionStorage.setItem(RELOADED, '1');
  globalThis.location.reload();
  return { isolated: false, reason: 'Reloading to pick up cross-origin isolation…' };
}

/*
 * A real, unbundled dynamic import. The emscripten loader resolves its .wasm and its workers
 * relative to its own URL at runtime, so webpack must not touch it - and it is not there at build
 * time anyway. A `webpackIgnore` comment does not survive the server-side compile, which is what
 * makes this an indirection rather than a comment.
 */
const importAtRuntime = new Function('url', 'return import(url)');

/** Boots the module on a canvas and returns the handles the page drives. */
export async function startMap(canvas, moduleUrl) {
  const { default: factory } = await importAtRuntime(moduleUrl);
  // locateFile explicitly, rather than letting emscripten guess. Its fallback resolves the .wasm
  // and the preloaded .data against the DOCUMENT, and the page sits at a different depth in the two
  // places it runs: /preview/ on the dev server, /preview.html once built. The built one then
  // asked for /massif-demo.data, one directory too high - a 404 that only ever appeared in
  // production.
  const baseUrl = new URL(moduleUrl, globalThis.location.href);
  const directory = baseUrl.href.slice(0, baseUrl.href.lastIndexOf('/') + 1);
  const module = await factory({ canvas, locateFile: (path) => directory + path });
  const massif = new Massif(module);
  const camera = await MassifCamera.attach(massif);
  const layers = massif.find('layers', 'map');
  if (!layers) throw new Error('The module adopted no layer list');
  const map = { module, massif, camera, layers };
  // The whole facade from the console, which is how this page gets debugged:
  //   massif.call(camera.handle, 'flyTo', [[2.35, 48.86], 15, 0, 60, 0, 1.5])
  globalThis.massifPreview = map;
  return map;
}

/** Drops the object registered under `kind`/`id`, if there is one. Re-creating an id is an error. */
function forget(massif, kind, id) {
  if (massif.find(kind, id)) massif.destroy(kind, id);
}

function writeFile(module, path, contents) {
  const slash = path.lastIndexOf('/');
  module.FS.mkdirTree(path.slice(0, slash));
  module.FS.writeFile(path, contents);
}

/**
 * Replaces whatever the map is showing with one vector layer over `source`, styled by `style`.
 *
 * `style` is either `{css}` for inline CartoCSS or `{project}` for a directory already written into
 * the module's filesystem. Both go through the same `mbvt` decoder, which is what makes the two
 * halves of the page one code path.
 */
export function applyStyle({ module, massif, layers }, { sourceUrl, maxZoom, style }) {
  forget(massif, 'layer', 'preview');
  forget(massif, 'style', 'preview');
  forget(massif, 'styleset', 'preview');
  forget(massif, 'data', 'fallback-font');

  const styleSet = style.project
    ? { type: 'project', assets: { type: 'dir', path: `${PROJECT_DIR}/` }, name: style.project }
    : { type: 'cartocss', css: style.css };
  const decoder = massif.create('style', 'preview', {
    type: 'mbvt',
    [style.project ? 'project' : 'cartocss']: styleSet,
  });

  // A converted MapBox style names DIN Pro, which no build carries; without a fallback every label
  // it declares is dropped silently. The font is preloaded into the module's filesystem.
  try {
    const font = massif.create('data', 'fallback-font', { type: 'url', url: FALLBACK_FONT });
    massif.call(decoder, 'addFallbackFont', [font]);
  } catch (error) {
    // A build without the preloaded font still draws everything except those labels.
    console.warn('No fallback font:', error.message);
  }

  const layer = massif.create('layer', 'preview', {
    type: 'vector',
    source: { type: 'http', minZoom: 0, maxZoom, url: sourceUrl },
    style: 'preview',
  });
  massif.call(layers, 'clear', []);
  massif.call(layers, 'add', [layer]);
  return layer;
}

/*
 * 3D terrain, from Mapterhorn's global DEM - Terrarium-coded WebP, z1-16, CORS open.
 *
 * Not a nicety here: shadows are cast over the TERRAIN cover (MapRenderer::applyTerrainShadows), so
 * with no height field the map has no shadow pass at all and the hour only moves the shading.
 */
export const DEFAULT_DEM_URL = 'https://tiles.mapterhorn.com/{z}/{x}/{y}.webp';
const DEM_MAX_ZOOM = 16;
/** A desktop holds the height field up at any tilt and affords a finer mesh than a phone budget. */
const TERRAIN_MESH_RESOLUTION = 128;

export function applyTerrain({ massif }, enabled, { url = DEFAULT_DEM_URL } = {}) {
  const options = massif.find('options', 'map');
  let terrain = massif.getObject(options, 'terrainOptions');
  if (!terrain) {
    if (!enabled) return false;
    // The elevation decoder is not a spec argument: it is resolved from the source's own
    // "dem_encoding", which is why that meta data entry is set here rather than a decoder built.
    const built = massif.create('options', 'preview-terrain', {
      type: 'terrain',
      source: {
        type: 'http',
        url,
        minZoom: 1,
        maxZoom: DEM_MAX_ZOOM,
        'metaData.dem_encoding': 'terrarium',
      },
      meshResolution: TERRAIN_MESH_RESOLUTION,
      autoFlattenTilt: 0,
      autoFlattenParallax: 0,
    });
    massif.setObject(options, 'terrainOptions', built);
    terrain = massif.getObject(options, 'terrainOptions');
  }
  if (!terrain) return false;
  massif.set(terrain, 'enabled', enabled);
  return true;
}

/**
 * The atmosphere. Values are the demo's "haze" preset: daylight, thin, no stars.
 *
 * A FogOptions with the default transparent colour changes nothing, so the colour is the setting
 * that matters - and a colour property takes an ARGB NUMBER: a "#rrggbb" string reads as 0, which
 * is a transparent fog that draws nothing. Ranges are multiples of the camera-to-focus distance,
 * so one pair holds at every zoom.
 */
const FOG = {
  color: 0xffb8c6d8 | 0,
  highColor: 0xff8fb4dc | 0,
  spaceColor: 0xff5b86c4 | 0,
  horizonBlend: 0.2,
  starIntensity: 0,
};

/** MapBox's own documented range, and FogOptions' default. The demo's haze preset (0.4) is closer. */
export const FOG_RANGE = { start: 0.8, end: 8 };

export function applyFog({ massif }, enabled, { rangeStart, rangeEnd } = {}) {
  const options = massif.find('options', 'map');
  let fog = massif.getObject(options, 'fogOptions');
  if (!fog) {
    if (!enabled) return false;
    massif.setObject(options, 'fogOptions', massif.create('options', 'preview-fog', {
      type: 'fog',
      ...FOG,
      rangeStart: FOG_RANGE.start,
      rangeEnd: FOG_RANGE.end,
    }));
    fog = massif.getObject(options, 'fogOptions');
  }
  if (!fog) return false;
  if (rangeStart !== undefined) massif.set(fog, 'rangeStart', rangeStart);
  if (rangeEnd !== undefined) massif.set(fog, 'rangeEnd', rangeEnd);
  massif.set(fog, 'enabled', enabled);
  return true;
}

/*
 * The sun, and the light preset that goes with an hour.
 *
 * The hour drives the SDK's own solar model (LightOptions.setSunPositionFromTime), not a copy of it
 * in JavaScript: that is the model the shadows and the sky were tuned against. It needs the place
 * as well as the time, because the sun's path over a day depends on where you are standing - so it
 * is re-applied when the map is moved, not only when the slider is.
 */
const SUN_DATE = { year: 2026, month: 6, day: 21 };

/** Where each of a converted style's light presets takes over, in hours. */
const PRESET_HOURS = [
  { name: 'night', until: 5 },
  { name: 'dawn', until: 8 },
  { name: 'day', until: 17 },
  { name: 'dusk', until: 20 },
  { name: 'night', until: 24 },
];

/** The preset a converted style should be shown in at this hour, of the ones it actually has. */
export function presetForHour(hour, available) {
  if (!available || available.length === 0) return null;
  const wanted = PRESET_HOURS.find((band) => hour < band.until)?.name ?? 'day';
  return available.includes(wanted) ? wanted : null;
}

/** Puts the sun where it would be at `hour` over the map's centre, and turns shadows on. */
export function applyHour({ massif, camera }, hour, { shadows = true } = {}) {
  const options = massif.find('options', 'map');
  // A handle, not a value: the sun is set by calling a METHOD on the light options, and only a
  // handle can be called. mm_get_object reaches the existing sub-object rather than rebuilding it,
  // so whatever a style put there is kept.
  let light = massif.getObject(options, 'lightOptions');
  if (!light) {
    // A map with no light options yet, which is the default. An object property takes a HANDLE,
    // not a value and not a spec - mm_set_object, not mm_set_string.
    if (!massif.find('options', 'preview-light')) {
      massif.create('options', 'preview-light', { type: 'light' });
    }
    massif.setObject(options, 'lightOptions', massif.find('options', 'preview-light'));
    light = massif.getObject(options, 'lightOptions');
  }
  if (!light) return false;

  const [lon, lat] = camera.focusPos;
  const whole = Math.floor(hour);
  const minutes = Math.round((hour - whole) * 60);
  massif.call(light, 'setSunPositionFromTime',
    [SUN_DATE.year, SUN_DATE.month, SUN_DATE.day, whole, minutes, lat, lon]);
  // Otherwise a style that states its own sun wins and the slider appears to do nothing.
  massif.set(light, 'sunOverridingStyle', true);
  // Off by default, and the shadow pass is gated on it - without this the strength below is inert.
  massif.set(light, 'terrainLightingEnabled', true);
  massif.set(light, 'shadowStrength', shadows ? 1 : 0);
  return true;
}

/**
 * Place search, through Komoot's public Photon (photon.komoot.io) - OSM data, no key, CORS open.
 *
 * Only the typed query goes out, and only when the user asks for it. What comes back is DATA:
 * names are rendered as text and coordinates are used as coordinates, and nothing in a result is
 * ever treated as markup or as an instruction.
 */
const PHOTON_URL = 'https://photon.komoot.io/api';

export async function searchPlaces(query, { limit = 6, signal } = {}) {
  const trimmed = query.trim();
  if (!trimmed) return [];
  const url = `${PHOTON_URL}?q=${encodeURIComponent(trimmed)}&limit=${limit}`;
  const response = await fetch(url, { signal });
  if (!response.ok) throw new Error(`${response.status} ${response.statusText} from Photon`);
  const document = await response.json();
  return (document.features ?? []).flatMap((feature) => {
    const [lon, lat] = feature.geometry?.coordinates ?? [];
    if (!Number.isFinite(lon) || !Number.isFinite(lat)) return [];
    const p = feature.properties ?? {};
    // Photon has no single display string, so it is assembled from the parts that exist.
    const where = [p.city, p.state, p.country].filter(Boolean).join(', ');
    return [{
      name: String(p.name ?? p.street ?? `${lat.toFixed(4)}, ${lon.toFixed(4)}`),
      detail: [p.osm_value, where].filter(Boolean).join(' · '),
      lon,
      lat,
      // Photon returns an extent for anything with an area, which is a better landing than a point.
      extent: Array.isArray(p.extent) && p.extent.length === 4 ? p.extent : null,
    }];
  });
}

/**
 * The zoom at which `extent` just fills the canvas, measured from the map rather than derived from
 * a convention: read how much ground the screen covers NOW, and each halving of that span is one
 * zoom level. So it stays right whatever ZoomOffset and TileDrawSize are set to.
 */
function fitZoomFor({ massif, camera }, extent, canvas, padding) {
  const [minLon, maxLat, maxLon, minLat] = extent;
  const inset = Math.min(padding, Math.min(canvas.width, canvas.height) / 4);
  const left = massif.call(camera.handle, 'screenToMap', [inset, canvas.height / 2]);
  const right = massif.call(camera.handle, 'screenToMap', [canvas.width - inset, canvas.height / 2]);
  const top = massif.call(camera.handle, 'screenToMap', [canvas.width / 2, inset]);
  const bottom = massif.call(camera.handle, 'screenToMap', [canvas.width / 2, canvas.height - inset]);
  const lonNow = Math.abs(right[0] - left[0]);
  const latNow = Math.abs(top[1] - bottom[1]);
  const lonWanted = Math.abs(maxLon - minLon);
  const latWanted = Math.abs(maxLat - minLat);
  if (!(lonNow > 0) || !(latNow > 0) || !(lonWanted > 0) || !(latWanted > 0)) return null;
  // The tighter of the two axes wins, or the extent spills off the short side.
  const ratio = Math.min(lonNow / lonWanted, latNow / latWanted);
  return camera.zoom + Math.log2(ratio);
}

/**
 * Flies to a search result.
 *
 * Always flyTo, never fitBounds: fitBounds moves with the pan and zoom animations, whose duration
 * is taken literally, so a fixed one drifts across a country at the same rate it crosses a suburb.
 * flyTo follows van Wijk's arc and, given a duration of 0, picks S/1.4 seconds for it - which is
 * maplibre's rule and the reason a long flight feels like one. An extent only decides the zoom.
 */
export function flyToPlace({ massif, camera }, place, canvas,
                           { seconds = 0, pointZoom = 15, padding = 40, maxZoom = 17 } = {}) {
  let zoom = pointZoom;
  if (place.extent && canvas) {
    const fitted = fitZoomFor({ massif, camera }, place.extent, canvas, padding);
    if (fitted !== null) zoom = Math.min(fitted, maxZoom);
  }
  return massif.call(camera.handle, 'flyTo',
    [[place.lon, place.lat], zoom, camera.rotation, camera.tilt, 0, seconds]);
}

/** A style as JSON text, or the URL of one - a published style is a link far more often than a file. */
async function readStyle(text) {
  const trimmed = text.trim();
  if (!/^https?:\/\//.test(trimmed)) return JSON.parse(trimmed);
  const response = await fetch(trimmed);
  if (!response.ok) throw new Error(`${response.status} ${response.statusText} for ${trimmed}`);
  return response.json();
}

/**
 * Translates a MapBox/MapLibre style JSON to a CartoCSS project and writes it into the module's
 * filesystem, icons and all. Returns the style name to open it under, plus what was dropped.
 *
 * This is `massif-style mapbox2css` run in the page - the same converter, the same property table,
 * only its sprite host swapped for one over `fetch` and the codec in png.ts. Nothing is uploaded.
 */
export async function convertMapboxStyle(module, styleJson, { spriteKey = '' } = {}) {
  const style = typeof styleJson === 'string' ? await readStyle(styleJson) : styleJson;

  setSpriteHost(browserSpriteHost((outDir, file, bytes) => {
    writeFile(module, `${outDir}/${file}`, bytes);
  }));

  let sprites;
  const notes = [];
  try {
    const sheets = await loadSprites(style, spriteKey);
    if (sheets.size > 0) sprites = { sheets, outDir: PROJECT_DIR };
  } catch (error) {
    notes.push(`Sprite sheet not loaded (${error.message}); icons are dropped.`);
  }

  const { mss, project, coverage, variables, presets, defaultPreset, presetOverrides } =
    convert(style, propertyTable, { sprites, variables: true });

  // Exactly the layout `massif-style mapbox2css` writes on disk, because that is what
  // CompiledStyleSet opens: project.json names the .mss files it is made of, so writing fewer of
  // them than were produced leaves it pointing at a file that is not there.
  writeFile(module, `${PROJECT_DIR}/style.mss`, mss);
  writeFile(module, `${PROJECT_DIR}/project.json`, project);
  if (variables) writeFile(module, `${PROJECT_DIR}/${VARIABLES_FILE}`, variables);
  const overrides = presetOverrides ?? new Map();
  for (const [preset, palette] of presets ?? []) {
    writeFile(module, `${PROJECT_DIR}/${preset}.mss`, palette);
    const params = overrides.get(preset);
    writeFile(module, `${PROJECT_DIR}/${preset}.json`, `${JSON.stringify({
      extends: './project.json',
      styles: [`${preset}.mss`, 'style.mss'],
      ...(params ? { styleparameters: params } : {}),
    }, null, 2)}\n`);
  }
  if (defaultPreset) {
    writeFile(module, `${PROJECT_DIR}/${defaultPreset}.json`, `${JSON.stringify({
      extends: './project.json',
      styles: [VARIABLES_FILE, 'style.mss'],
    }, null, 2)}\n`);
  }

  if (coverage?.droppedCount) {
    notes.push(`${coverage.droppedCount} style properties have no CartoCSS equivalent.`);
  }
  // A style with light presets is opened under one of them; otherwise under project.json itself.
  const themes = [defaultPreset, ...(presets?.keys() ?? [])].filter(Boolean);
  return { themes: themes.length > 0 ? themes : ['project'], mss, notes };
}
