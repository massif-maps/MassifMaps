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
  const module = await factory({ canvas });
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
