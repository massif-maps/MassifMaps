/**
 * @title Peak finder
 * @section terrain
 * @order 90
 * A panorama drawn as peakfinder.com draws it: the terrain as ink on paper from a shader, every
 * summit named along the skyline, and the sun's path with its rise and set over the ridges. Drag to
 * look around, tap a name, then fly to it.
 */
import { find } from '@massif-maps/web';
import { RELIEF_DEPTH_OUTLINE_SHADER, RELIEF_SURFACE_SHADER } from './peak-finder/relief-shaders.js';
import { peaksStyle } from './peak-finder/peaks-style.js';
import { createChrome, toCompass } from './peak-finder/chrome.mjs';
import { createSky } from './peak-finder/sun.mjs';

const DEM = 'https://tiles.mapterhorn.com/{z}/{x}/{y}.webp';
const SUMMITS = 'https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf';

// The ink pass: silhouettes only (operator 2), a heavier skyline. Uniforms left out read zero.
const INK = {
  uOperator: 2, uOutlineGain: 12, uOutlinePower: 1, uOutlineFloor: 0.008, uOutlineCeiling: 1, uOutlineWidth: 1,
  uIntensity: 0.8, uHorizonBoost: 0.9, uHorizonWidth: 2.5, uDepthThreshold: 1, uCreaseThreshold: 0.12,
  uRidgeStrength: 2, uRidgeThreshold: 0.05, uRidgeGroundSpan: 90, uDepthTexelSize: 2, uGrazingFloor: 0.15,
  uInkDistance: 50000, uMetersPerUnit: 40075016.68558 / (1 << 20), uSilhouetteGate: 865, uHazeDistance: 60000,
  uDistortCenterX: 0.5, uDistortCenterY: 0.5, uDistortScreenTanX: 1, uDistortScreenTanY: 1, uDistortRenderTanX: 1, uDistortRenderTanY: 1,
};
// The surface: ridge ink capped by the light, and a touch of hillshade after the cap.
const SURFACE = { uAmbient: 0.06, uInkCap: 0.3, uRidgeInkStrength: 0.3, uHillshade: 0.15 };
// White paper, black ink: the palette is constants in the shaders, so it is written into the source.
const COLOURS = { uPaperColor: [1, 1, 1, 1], uInkColor: [0, 0, 0, 1], uShadeColor: [0, 0, 0, 1] };
const withColours = (shader) => Object.entries(COLOURS).reduce((glsl, [name, rgba]) =>
  glsl.replace(new RegExp(`const vec4 ${name} = vec4\\([^)]*\\);`), `const vec4 ${name} = vec4(${rgba.join(', ')});`), shader);

export default async function start(host) {
  const map = host.map;
  const query = new URLSearchParams(location.search);
  const num = (name, fallback) => (query.has(name) ? Number(query.get(name)) : fallback);
  // The viewpoint: Grenoble, 400 m up, looking east at Belledonne - rotation is minus the heading.
  const view = { lat: num('lat', 45.1885), lon: num('lon', 5.7245), eye: num('elevation', 400), rotation: num('rotation', -100), tilt: num('tilt', -4), fov: num('fov', 46) };
  const label = { layout: 'band', band: 0.12, angle: 45, size: 13, occlusion: 0.15 };

  // Tile caches in IndexedDB, as an app keeps them on disk: loaded before the sources open them.
  await mountCache(map.module, '/cache');

  // The terrain, and everything decided when its meshes are built: geo-three's cut (subdivide
  // distance 70, levels to 17) and mesh, no stitching, drawn 173 km out.
  const terrain = map.terrain({
    type: 'terrain',
    source: {
      type: 'persistent-cache', databasePath: '/cache/dem.db', metaData: { dem_encoding: 'terrarium' },
      source: { type: 'http', url: DEM, maxZoom: 16, metaData: { dem_encoding: 'terrarium' } },
    },
    autoFlattenTilt: 0, autoFlattenParallax: 0,
    meshResolution: 171, tileEdgeStitchingEnabled: false, subdivideDistance: 70, maxZoom: 17,
    viewDistance: 173000, meshCacheSize: 640, normalSampleDistance: 40, postProcessDownscale: 1,
    // The picture is the surface shader, so nothing is draped over it; the one layer is billboards.
    surfaceShaderSource: withColours(RELIEF_SURFACE_SHADER), backgroundColor: '#ffffff',
    sharedGroundEnabled: false, drapeFillsEnabled: false, drapeLinesEnabled: false,
    billboardOcclusionEnabled: true, billboardOcclusionTolerance: label.occlusion, maxTileZoomCoarsening: 4,
  });
  const terrainObject = map.child('terrainOptions');
  const setSurface = (name, value) => terrainObject.call('setSurfaceParameter', name, value);
  for (const [name, value] of Object.entries(SURFACE)) {
    setSurface(name, value);
  }

  // The ink is a post-process over the frame, reading the terrain's depth.
  map.camera();
  const mapView = find('view', `${map.id}:view`, 'massif::BaseMapView');
  const ink = map.object('effect', 'relief', {
    type: 'postprocess', name: 'relief', fragmentShader: withColours(RELIEF_DEPTH_OUTLINE_SHADER), terrainDepthRequired: true,
  });
  for (const [name, value] of Object.entries(INK)) {
    ink.call('setFloatParameter', name, value);
  }
  mapView.set('mapRenderer.postProcessEffect', ink.handle);

  // No sky, no background pattern: the panorama is read against the paper.
  map.child('skyOptions')?.set('enabled', false);
  map.apply({ skyColor: '#00000000', clearColor: '#ffffff', backgroundBitmap: 0, labelPadding: 200 });
  map.light({ type: 'light', sunAzimuth: 315, sunAltitude: 45 });

  // First person: the position IS the eye and a drag turns the view about it. A panorama looks at the
  // horizon, so the tilt range opens above it too (tilt 90 is straight down).
  map.apply({ freeRoamMode: 'FREE_ROAM_MODE_FIRST_PERSON', tiltRange: [-90, 90] });
  const setFov = (degrees) => {
    view.fov = degrees;
    map.set('fieldOfViewY', degrees);
    // The ridge term's tap spacing: radians per screen pixel.
    setSurface('uPixelAngle', (degrees * Math.PI / 180) / Math.max(host.root.clientHeight * devicePixelRatio, 1));
  };
  // The eye stands focusLift over the ground under it, which the renderer keeps every frame.
  const placeCamera = () => {
    mapView.call('moveCameraTo', [view.lon, view.lat, 0], 13, view.rotation, view.tilt);
    terrain.set('focusLift', view.eye);
  };
  setFov(view.fov);
  placeCamera();

  const chrome = createChrome(host.root, {
    north: () => turnTo(0),
    lookAt: () => selected && turnTo(-bearing(view, selected)),
    flyTo: () => selected && flyTo(selected),
    select: (peak) => select(peak),
  });
  const turnTo = (rotation) => {
    view.rotation = rotation;
    view.tilt = map.camera().tilt();
    placeCamera();
  };

  // THE SUMMIT NAMES, as the app draws them: OpenMapTiles' mountain_peak, and a style that puts every
  // name in one row above the skyline. The eye's altitude is baked into the style's rank, so a new
  // viewpoint is a new style - and the selected summit is a style parameter, set on the live one.
  const summits = map.source('peaks', {
    type: 'persistent-cache', databasePath: '/cache/peaks.db', source: { type: 'http', url: SUMMITS, maxZoom: 14 },
  });
  const skyBelow = map.add(map.buildLayer('sky', { type: 'celestial' }), 0);
  const skyAbove = map.addLayer('sky.top', { type: 'celestial' });
  let ground = await groundElevation(view);
  let generation = 0;
  let current = null;
  let selectedKey = '';
  const rebuildPeaks = () => {
    generation += 1;
    const css = peaksStyle({
      eyeElevation: ground + view.eye, textAngle: label.angle, textSize: label.size, band: label.band, topOffset: label.band,
      pinTop: label.layout === 'top', followSkyline: label.layout === 'skyline', minDistance: 1, persistPasses: 10, maxRows: 1,
    }).replace('  text-name: [name];', "  text-name: [name];\n  text-face-name: 'Roboto';");
    const style = map.style(`peaks.style.${generation}`, { type: 'mbvt', cartocss: { type: 'cartocss', css: withSelection(css) } });
    style.set('params.selected_peak', selectedKey);
    const layer = map.add(map.buildLayer(`peaks.layer.${generation}`, {
      type: 'vector', source: summits.handle, style: style.handle, preloading: true,
      labelRenderOrder: 'VECTOR_TILE_RENDER_ORDER_LAST', tileSubstitutionPolicy: 'TILE_SUBSTITUTION_POLICY_VISIBLE',
    }), map.layerCount() - 1);
    layer.onFeatureClick((e) => {
      const position = e.getPos('featurePos');
      const name = e.get('feature.properties.name');
      if (name && position) {
        select({ name, ele: e.get('feature.properties.ele'), lon: position[0], lat: position[1] });
      }
    });
    if (current) {
      map.removeLayer(current.layer);
      current.layer.destroy();
      current.style.destroy();
    }
    current = { layer, style };
  };
  rebuildPeaks();

  let selected = null;
  const select = (peak) => {
    selected = peak;
    selectedKey = peak ? `${peak.name}|${peak.ele ?? ''}` : '';
    current?.style.set('params.selected_peak', selectedKey);
    const km = peak ? distance(view, peak) / 1000 : 0;
    const heading = peak ? (bearing(view, peak) + 360) % 360 : 0;
    const height = peak?.ele ? `${Math.round(Number(peak.ele)).toLocaleString('en')} m  ·  ` : '';
    chrome.showPeak(peak, `${height}${km.toFixed(km < 10 ? 1 : 0)} km  ·  ${Math.round(heading)}° ${toCompass(heading)}`);
  };
  // Standing on the summit, facing the way it was seen from.
  const flyTo = async (peak) => {
    view.rotation = -bearing(view, peak);
    Object.assign(view, { lat: peak.lat, lon: peak.lon });
    select(null);
    ground = await groundElevation(view);
    placeCamera();
    rebuildPeaks();
  };

  const updateSky = createSky(map, terrainObject, skyBelow, skyAbove, (text) => chrome.showRiseSet(text));

  chrome.section('viewpoint');
  chrome.slider('eye height, m above the ground', view.eye, 0, 4000, 10, (metres) => {
    view.eye = metres;
    terrain.set('focusLift', metres);
  });
  chrome.slider('field of view', view.fov, 5, 120, 1, setFov);
  chrome.section('summit names');
  let rebuildTimer = 0;
  const relabel = (name) => (value) => {
    label[name] = value;
    clearTimeout(rebuildTimer);
    rebuildTimer = setTimeout(rebuildPeaks, 250);
  };
  chrome.choice('layout', label.layout, [['band', 'a row above the skyline'], ['top', 'a row at a fixed height'], ['skyline', 'over each summit']], relabel('layout'));
  chrome.slider('name angle', label.angle, 0, 90, 1, relabel('angle'));
  chrome.slider('text size', label.size, 8, 24, 0.5, relabel('size'));
  chrome.slider('how far behind a ridge a summit keeps its name', label.occlusion, 0, 1, 0.01, (value) => terrain.set('billboardOcclusionTolerance', value));
  chrome.finishPanel();

  const tick = () => {
    const camera = map.camera();
    view.rotation = camera.rotation();
    view.tilt = camera.tilt();
    chrome.showHeading(((-view.rotation % 360) + 360) % 360);
    updateSky({ ...view, ...chrome.sunState() });
    chrome.setPeakfinderLink(peakfinderUrl(view, ground, host.root));
    requestAnimationFrame(tick);
  };
  tick();
}

/** The selected summit's name bold and blue: a style parameter compared with each summit. */
const withSelection = (css) => "Map { param-selected_peak: ''; }\n@selected: [name] + '|' + [ele] = [param::selected_peak];\n" + css
  .replace("text-face-name: 'Roboto';", "text-face-name: @selected ? 'Roboto-Bold' : 'Roboto';")
  .replace(/\n {2}text-fill: ([^;]+);/, '\n  text-fill: @selected ? #2f4f9e : $1;')
  .replace(/text-placement-priority: ([^;]+);/, 'text-placement-priority: @selected ? 100000 : $1;');

async function mountCache(module, path) {
  module.FS.mkdir(path);
  module.FS.mount(module.FS.filesystems.IDBFS, {}, path);
  await new Promise((resolve) => module.FS.syncfs(true, resolve));
  let syncing = false;
  const persist = () => {
    if (!syncing) {
      syncing = true;
      module.FS.syncfs(false, () => (syncing = false));
    }
  };
  setInterval(persist, 10000);
  addEventListener('pagehide', persist);
}

/** The ground under a viewpoint, off the DEM tile itself: the summit names rank by the eye's altitude. */
async function groundElevation({ lat, lon }) {
  try {
    const tiles = 2 ** 12;
    const x = (lon + 180) / 360 * tiles;
    const y = (1 - Math.log(Math.tan(lat * Math.PI / 180) + 1 / Math.cos(lat * Math.PI / 180)) / Math.PI) / 2 * tiles;
    const url = DEM.replace('{z}', 12).replace('{x}', Math.floor(x)).replace('{y}', Math.floor(y));
    const bitmap = await createImageBitmap(await (await fetch(url)).blob());
    const context = new OffscreenCanvas(bitmap.width, bitmap.height).getContext('2d');
    context.drawImage(bitmap, 0, 0);
    const [r, g, b] = context.getImageData(Math.floor((x % 1) * bitmap.width), Math.floor((y % 1) * bitmap.height), 1, 1).data;
    return r * 256 + g + b / 256 - 32768;
  } catch (error) {
    return 0;
  }
}

const toRad = Math.PI / 180;
function bearing(from, to) {
  const y = Math.sin((to.lon - from.lon) * toRad) * Math.cos(to.lat * toRad);
  const x = Math.cos(from.lat * toRad) * Math.sin(to.lat * toRad) - Math.sin(from.lat * toRad) * Math.cos(to.lat * toRad) * Math.cos((to.lon - from.lon) * toRad);
  return Math.atan2(y, x) / toRad;
}
function distance(from, to) {
  const a = Math.sin((to.lat - from.lat) * toRad / 2) ** 2 + Math.cos(from.lat * toRad) * Math.cos(to.lat * toRad) * Math.sin((to.lon - from.lon) * toRad / 2) ** 2;
  return 2 * 6371008.8 * Math.asin(Math.sqrt(a));
}

/** The same view on peakfinder.com: its azimuth is a heading, its field of view horizontal. */
function peakfinderUrl(view, ground, root) {
  const fovX = 2 * Math.atan(Math.tan(view.fov * Math.PI / 360) * root.clientWidth / Math.max(root.clientHeight, 1)) / toRad;
  return 'https://www.peakfinder.com/?' + new URLSearchParams({
    lat: view.lat, lng: view.lon, ele: Math.round(ground), off: Math.round(view.eye), azi: -view.rotation, alt: view.tilt, fov: fovX.toFixed(2), cfg: 'es',
  }) + '&name';
}
