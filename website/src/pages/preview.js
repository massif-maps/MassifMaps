import {useCallback, useEffect, useRef, useState} from 'react';
import BrowserOnly from '@docusaurus/BrowserOnly';
import Layout from '@theme/Layout';
import useBaseUrl from '@docusaurus/useBaseUrl';

import styles from './preview.module.css';

/*
 * The style preview: the SDK itself, compiled to WebAssembly, rendering a style you can edit.
 *
 * It is the same renderer Android and iOS run and the same converter `massif-style mapbox2css`
 * runs, so what shows up here is what an app would draw - which is the whole reason for the page.
 * Everything is client-side: no style, no token and no sprite sheet is uploaded anywhere.
 *
 * The wasm is a build artefact, not tracked. Build it with
 * `python3 scripts/build-web.py --profile lite --build-demo --website`, or let the web-preview
 * workflow do it; without it the page says so rather than hanging.
 */

/** 13.25 -> "13:15". */
function formatHour(hour) {
  const whole = Math.floor(hour) % 24;
  const minutes = Math.round((hour - Math.floor(hour)) * 60);
  return `${String(whole).padStart(2, '0')}:${String(minutes).padStart(2, '0')}`;
}

const MODE_CARTOCSS = 'cartocss';
const MODE_MAPBOX = 'mapbox';

/**
 * Place search over the map. Photon (photon.komoot.io) is OSM data with no key and an open CORS
 * policy; only the typed query is sent, and only when the form is submitted.
 */
function SearchBox({state, actions}) {
  return (
    <div className={styles.search}>
      <form
        onSubmit={(event) => {
          event.preventDefault();
          // From the form, not from state: submitting in the same tick as the last keystroke runs
          // a handler that still closes over the previous render's query, and the search then
          // quietly looked for the wrong thing - or for nothing at all.
          actions.search(event.currentTarget.elements.q.value);
        }}>
        <input
          type="search"
          name="q"
          value={state.query}
          placeholder="Search a place…"
          aria-label="Search a place"
          onChange={(event) => actions.setQuery(event.target.value)}
          onKeyDown={(event) => {
            // Not only the form's submit: implicit submission is what a browser is SUPPOSED to do
            // with Enter in a single-field form, and not every one does.
            if (event.key === 'Enter') {
              event.preventDefault();
              actions.search(event.currentTarget.value);
            }
          }}
        />
        <button type="submit" disabled={state.searching}>
          {state.searching ? '…' : 'Go'}
        </button>
      </form>
      {state.searchError && <p className={styles.searchError}>{state.searchError}</p>}
      {state.results.length > 0 && (
        <ul className={styles.results}>
          {state.results.map((place, index) => (
            <li key={`${place.lon},${place.lat},${index}`}>
              <button type="button" onClick={() => actions.goTo(place)}>
                <strong>{place.name}</strong>
                {place.detail && <span>{place.detail}</span>}
              </button>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

function Panel({state, actions}) {
  const {
    mode, css, styleJson, spriteKey, tilejson, themes, theme, busy, notes, error,
  } = state;

  return (
    <div className={styles.panel}>
      <div className={styles.panelSection}>
        <h2>Style</h2>
        <div className={styles.tabs}>
          <button
            type="button"
            className={`${styles.tab} ${mode === MODE_CARTOCSS ? styles.tabActive : ''}`}
            onClick={() => actions.setMode(MODE_CARTOCSS)}>
            CartoCSS
          </button>
          <button
            type="button"
            className={`${styles.tab} ${mode === MODE_MAPBOX ? styles.tabActive : ''}`}
            onClick={() => actions.setMode(MODE_MAPBOX)}>
            MapBox style JSON
          </button>
        </div>

        {mode === MODE_CARTOCSS ? (
          <>
            <label className={styles.field}>
              Starter
              <select
                value={state.starter}
                onChange={(event) => actions.pickStarter(event.target.value)}>
                {actions.starters.map((starter) => (
                  <option key={starter.id} value={starter.id}>{starter.label}</option>
                ))}
              </select>
            </label>
            <textarea
              className={styles.editor}
              spellCheck={false}
              value={css}
              onChange={(event) => actions.setCss(event.target.value)}
            />
          </>
        ) : (
          <>
            <textarea
              className={styles.editor}
              spellCheck={false}
              placeholder={'Paste a MapBox or MapLibre style JSON here — or just the URL of one — '
                + 'then Apply. The sprite sheet is fetched from wherever the style names it, so '
                + 'that host has to allow this origin.'}
              value={styleJson}
              onChange={(event) => actions.setStyleJson(event.target.value)}
            />
            <label className={styles.field}>
              Sprite URL suffix (a key, when the provider needs one)
              <input
                type="text"
                value={spriteKey}
                placeholder="?key=…"
                onChange={(event) => actions.setSpriteKey(event.target.value)}
              />
            </label>
            {themes.length > 1 && (
              <label className={styles.field}>
                Theme
                <select value={theme} onChange={(event) => actions.pickTheme(event.target.value)}>
                  {themes.map((name) => <option key={name} value={name}>{name}</option>)}
                </select>
              </label>
            )}
          </>
        )}

        <div className={styles.actions}>
          <button
            type="button"
            className="button button--primary button--sm"
            disabled={busy}
            onClick={actions.apply}>
            {busy ? 'Applying…' : 'Apply'}
          </button>
          <button
            type="button"
            className="button button--secondary button--sm"
            onClick={actions.share}>
            Copy link
          </button>
        </div>
        {error && <p className={styles.error}>{error}</p>}
        {notes.map((note) => <p className={styles.note} key={note}>{note}</p>)}
      </div>

      <div className={styles.panelSection}>
        <h2>Light</h2>
        <label className={styles.field}>
          Hour (UTC): <strong>{formatHour(state.hour)}</strong>
          <input
            type="range"
            min="0"
            max="24"
            step="0.25"
            value={state.hour}
            onChange={(event) => actions.setHour(Number(event.target.value))}
          />
        </label>
        <label className={styles.checkbox}>
          <input
            type="checkbox"
            checked={state.autoPreset}
            disabled={state.themes.length < 2}
            onChange={(event) => actions.setAutoPreset(event.target.checked)}
          />
          Follow the style&apos;s light preset
        </label>
        <p className={styles.note}>
          The hour drives the SDK&apos;s own solar model at the map centre, so the sun moves the way
          it would there on 21 June — pan a long way and nudge the slider to re-place it. Cast
          shadows are the <strong>Shadows</strong> button over the map, and they need 3D terrain:
          the shadow pass runs over the terrain cover, so a flat map has none.
          {state.themes.length > 1
            ? ' A converted style carries presets, and the checkbox switches between them.'
            : ' A converted MapBox style adds dawn/day/dusk/night presets here.'}
        </p>
      </div>

      <div className={styles.panelSection}>
        <h2>Fog</h2>
        <label className={styles.field}>
          Starts at <strong>{state.fogStart.toFixed(1)}×</strong> the camera distance
          <input
            type="range"
            min="0.2"
            max="6"
            step="0.1"
            value={state.fogStart}
            onChange={(event) => actions.setFogRange({fogStart: Number(event.target.value)})}
          />
        </label>
        <label className={styles.field}>
          Saturates at <strong>{state.fogEnd.toFixed(0)}×</strong>
          <input
            type="range"
            min="1"
            max="24"
            step="0.5"
            value={state.fogEnd}
            onChange={(event) => actions.setFogRange({fogEnd: Number(event.target.value)})}
          />
        </label>
        <p className={styles.note}>
          Both are multiples of the camera-to-focus distance, not metres — that distance is a
          function of the zoom alone, so one pair holds at every zoom. 0.8 and 8 are MapBox&apos;s
          own numbers; push the start out to clear the near ground.
        </p>
      </div>

      <div className={styles.panelSection}>
        <h2>Tiles</h2>
        <label className={styles.field}>
          TileJSON or tile URL template
          <input
            type="text"
            value={tilejson}
            onChange={(event) => actions.setTilejson(event.target.value)}
          />
        </label>
        <p className={styles.note}>
          The default is OpenFreeMap&apos;s planet, which needs no key. A style written for a
          different schema — MapBox Streets names <code>#road</code> where OpenMapTiles names
          <code>#transportation</code> — draws nothing until the source matches it.
        </p>
      </div>

      <div className={styles.panelSection}>
        <h2>Controls</h2>
        <p className={styles.note}>
          Drag to pan, wheel to zoom, right-drag to rotate and tilt. Everything renders in your
          browser: no style, key or sprite sheet leaves this page.
        </p>
        <p className={styles.note}>
          <strong>Terrain</strong> is <a href="https://mapterhorn.com">Mapterhorn</a>&apos;s global
          DEM, streamed straight from their tiles. <strong>Fog</strong> is the SDK&apos;s
          MapBox-modelled atmosphere, which is what hides the horizon a tilted view ends at.
        </p>
      </div>
    </div>
  );
}

function StylePreview() {
  const moduleUrl = useBaseUrl('/preview/massif-demo.mjs');
  const serviceWorkerUrl = useBaseUrl('/coi-serviceworker.js');

  const canvasRef = useRef(null);
  const mapRef = useRef(null);
  // The startup effect runs before apply() is defined and must not re-run when it changes, so it
  // reaches the latest one through a ref rather than through the dependency list.
  const applyRef = useRef(null);
  const [status, setStatus] = useState('Starting the map…');
  const [readout, setReadout] = useState('');
  const [engine, setEngine] = useState(null);
  const [starters, setStarters] = useState([]);

  const [state, setState] = useState({
    mode: MODE_CARTOCSS,
    starter: '',
    css: '',
    styleJson: '',
    spriteKey: '',
    tilejson: '',
    themes: [],
    theme: '',
    busy: false,
    notes: [],
    error: '',
    hour: 12,
    terrain: true,
    fog: true,
    fogStart: 0.8,
    fogEnd: 8,
    shadows: true,
    autoPreset: true,
    query: '',
    results: [],
    searching: false,
    searchError: '',
  });
  const patch = useCallback((next) => setState((prev) => ({...prev, ...next})), []);

  // The engine reaches into the wasm module and the converter, neither of which can be imported
  // during a server-side render - so it is pulled in here, once the canvas exists.
  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        const [engineModule, startersModule] = await Promise.all([
          import('../components/StylePreview/engine.js'),
          import('../components/StylePreview/starters.js'),
        ]);
        if (cancelled) return;
        setEngine(engineModule);
        setStarters(startersModule.STARTERS);

        const params = new URLSearchParams(window.location.search);
        const starter = startersModule.STARTERS.find((s) => s.id === params.get('starter'))
          ?? startersModule.DEFAULT_STARTER;
        const mapboxStyle = params.get('style');
        patch({
          mode: mapboxStyle ? MODE_MAPBOX : MODE_CARTOCSS,
          starter: starter.id,
          css: params.get('css') ?? starter.css,
          styleJson: mapboxStyle ?? '',
          spriteKey: params.get('spriteKey') ?? '',
          tilejson: params.get('source') ?? engineModule.DEFAULT_TILEJSON,
        });

        const isolation = await engineModule.ensureIsolated(serviceWorkerUrl);
        if (!isolation.isolated) {
          setStatus(isolation.reason);
          return;
        }

        const map = await engineModule.startMap(canvasRef.current, moduleUrl);
        if (cancelled) return;
        mapRef.current = map;
        setStatus('');

        const camera = map.camera;
        const start = params.get('camera')?.split(',').map(Number);
        if (start?.length === 5 && start.every(Number.isFinite)) {
          map.massif.call(camera.handle, 'moveTo',
            [[start[0], start[1]], start[2], start[3], start[4]]);
        }
        // Straight into a real style. Leaving the module's own raster fallback up made the first
        // thing anyone saw a plain OSM basemap, which is the one thing this page is not for.
        await applyRef.current?.({
          mode: mapboxStyle ? MODE_MAPBOX : MODE_CARTOCSS,
          css: params.get('css') ?? starter.css,
          styleJson: mapboxStyle ?? '',
          tilejson: params.get('source') ?? engineModule.DEFAULT_TILEJSON,
        });
        if (cancelled) return;
        engineModule.applyTerrain(map, true);
        engineModule.applyFog(map, true);
        patch({fogStart: engineModule.FOG_RANGE.start, fogEnd: engineModule.FOG_RANGE.end});
        engineModule.applyHour(map, 12, {shadows: true});

        const tick = () => {
          if (cancelled) return;
          try {
            const [lon, lat] = camera.focusPos;
            setReadout(
              `zoom ${camera.zoom.toFixed(2)}  rotation ${camera.rotation.toFixed(0)}°  `
              + `tilt ${camera.tilt.toFixed(0)}°\n${lon.toFixed(5)}, ${lat.toFixed(5)}`);
          } catch {
            // The module is going away; stop reading it.
            return;
          }
          requestAnimationFrame(tick);
        };
        tick();
      } catch (error) {
        if (cancelled) return;
        setStatus(String(error?.message ?? error));
      }
    })();
    return () => { cancelled = true; };
  }, [moduleUrl, serviceWorkerUrl, patch]);

  /**
   * Puts the current style on the map.
   *
   * `override` exists for the very first call, which happens while the state that describes the
   * style is still on its way through React - passing the values beats hoping they landed.
   */
  const apply = useCallback(async (override = {}) => {
    const map = mapRef.current;
    if (!map || !engine) return;
    const mode = override.mode ?? state.mode;
    const css = override.css ?? state.css;
    const styleJson = override.styleJson ?? state.styleJson;
    const tilejson = override.tilejson ?? state.tilejson;
    patch({busy: true, error: '', notes: []});
    try {
      const source = tilejson.includes('{z}')
        ? {url: tilejson, maxZoom: engine.DEFAULT_MAX_ZOOM}
        : await engine.resolveTileUrl(tilejson);

      let style;
      let notes = [];
      let themes = state.themes;
      if (mode === MODE_MAPBOX) {
        const converted = await engine.convertMapboxStyle(map.module, styleJson,
          {spriteKey: state.spriteKey});
        notes = converted.notes;
        themes = converted.themes;
        style = {project: themes.includes(state.theme) ? state.theme : themes[0]};
      } else {
        style = {css};
      }
      engine.applyStyle(map, {sourceUrl: source.url, maxZoom: source.maxZoom, style});
      // Remembered so a light-preset switch can re-open the project over the same source without
      // resolving the TileJSON or converting the style again.
      map.sourceUrl = source.url;
      map.sourceMaxZoom = source.maxZoom;
      patch({busy: false, notes, themes, theme: style.project ?? '', error: ''});
    } catch (error) {
      patch({busy: false, error: String(error?.message ?? error)});
    }
  }, [engine, state, patch]);

  /**
   * Moves the sun, and with it the style's light preset when the style has any.
   *
   * Switching preset re-opens the SAME converted project under another of its .json entry points,
   * so it costs a re-decode and no conversion - the files are already in the module's filesystem.
   */
  const applyLight = useCallback((next) => {
    const map = mapRef.current;
    if (!map || !engine) return;
    const hour = next.hour ?? state.hour;
    const shadows = next.shadows ?? state.shadows;
    const autoPreset = next.autoPreset ?? state.autoPreset;
    engine.applyHour(map, hour, {shadows});

    const themes = state.themes;
    if (!autoPreset || themes.length < 2 || state.mode !== MODE_MAPBOX) return;
    const wanted = engine.presetForHour(hour, themes);
    if (wanted && wanted !== state.theme) {
      engine.applyStyle(map, {
        sourceUrl: map.sourceUrl,
        maxZoom: map.sourceMaxZoom,
        style: {project: wanted},
      });
      patch({theme: wanted});
    }
  }, [engine, state, patch]);

  const search = useCallback(async (query) => {
    const wanted = (query ?? state.query).trim();
    if (!engine || !wanted) return;
    patch({query: wanted, searching: true, searchError: '', results: []});
    try {
      const results = await engine.searchPlaces(wanted);
      patch({
        searching: false,
        results,
        searchError: results.length === 0 ? 'Nothing found.' : '',
      });
    } catch (error) {
      patch({searching: false, searchError: String(error?.message ?? error)});
    }
  }, [engine, state.query, patch]);

  const goTo = useCallback((place) => {
    const map = mapRef.current;
    if (!map || !engine) return;
    engine.flyToPlace(map, place, canvasRef.current);
    patch({results: [], query: place.name});
    // The sun depends on WHERE as much as on when, so it is re-placed once the flight has landed.
    setTimeout(() => engine.applyHour(map, state.hour, {shadows: state.shadows}), 1600);
  }, [engine, state.hour, state.shadows, patch]);

  useEffect(() => { applyRef.current = apply; }, [apply]);

  const share = useCallback(() => {
    const map = mapRef.current;
    const params = new URLSearchParams();
    params.set('source', state.tilejson);
    if (state.mode === MODE_CARTOCSS) {
      params.set('css', state.css);
    } else if (/^https?:\/\//.test(state.styleJson.trim())) {
      // A pasted style is far too big for a URL; a link to one is the shareable case.
      params.set('style', state.styleJson.trim());
      if (state.spriteKey) params.set('spriteKey', state.spriteKey);
    }
    if (map) {
      const {camera} = map;
      const [lon, lat] = camera.focusPos;
      params.set('camera', [lon.toFixed(5), lat.toFixed(5), camera.zoom.toFixed(2),
        camera.rotation.toFixed(1), camera.tilt.toFixed(1)].join(','));
    }
    const url = `${window.location.origin}${window.location.pathname}?${params}`;
    navigator.clipboard?.writeText(url);
    patch({notes: ['Link copied.']});
  }, [state, patch]);

  const flyTo = useCallback((tilt, rotation) => {
    const map = mapRef.current;
    if (!map) return;
    const {camera} = map;
    map.massif.call(camera.handle, 'flyTo', [camera.focusPos.slice(0, 2), camera.zoom,
      rotation ?? camera.rotation, tilt ?? camera.tilt, 0, 0.8]);
  }, []);

  const actions = {
    starters,
    setMode: (mode) => patch({mode}),
    setCss: (css) => patch({css}),
    setStyleJson: (styleJson) => patch({styleJson}),
    setSpriteKey: (spriteKey) => patch({spriteKey}),
    setTilejson: (tilejson) => patch({tilejson}),
    pickStarter: (id) => {
      const starter = starters.find((s) => s.id === id);
      if (starter) patch({starter: id, css: starter.css});
    },
    pickTheme: (theme) => patch({theme}),
    setHour: (hour) => { patch({hour}); applyLight({hour}); },
    setShadows: (shadows) => { patch({shadows}); applyLight({shadows}); },
    setTerrain: (terrain) => {
      const map = mapRef.current;
      if (!map || !engine) return;
      patch({terrain});
      engine.applyTerrain(map, terrain);
    },
    setFog: (fog) => {
      const map = mapRef.current;
      if (!map || !engine) return;
      patch({fog});
      engine.applyFog(map, fog);
    },
    setFogRange: (next) => {
      const map = mapRef.current;
      if (!map || !engine) return;
      // The end is never allowed under the start: the shader divides by the span, and a reversed
      // pair saturates the whole view in one step.
      const fogStart = next.fogStart ?? state.fogStart;
      const fogEnd = Math.max(next.fogEnd ?? state.fogEnd, fogStart + 0.5);
      patch({fogStart, fogEnd});
      engine.applyFog(map, state.fog, {rangeStart: fogStart, rangeEnd: fogEnd});
    },
    setAutoPreset: (autoPreset) => { patch({autoPreset}); applyLight({autoPreset}); },
    setQuery: (query) => patch({query}),
    search,
    goTo,
    apply,
    share,
  };

  return (
    <div className={styles.shell}>
      <div className={styles.stage}>
        <canvas id="map" ref={canvasRef} className={styles.canvas} />
        {status ? (
          <div className={styles.status}><p>{status}</p></div>
        ) : (
          <>
            <SearchBox state={state} actions={actions} />
            <div className={styles.viewButtons}>
              {/* Tilt is 90 looking straight down here, the opposite of MapBox's pitch. */}
              <button type="button" onClick={() => flyTo(45)}>3D</button>
              <button type="button" onClick={() => flyTo(90)}>2D</button>
              <button type="button" onClick={() => flyTo(undefined, 0)}>North</button>
              <button
                type="button"
                className={state.terrain ? styles.toggleOn : ''}
                onClick={() => actions.setTerrain(!state.terrain)}>
                Terrain
              </button>
              <button
                type="button"
                className={state.fog ? styles.toggleOn : ''}
                onClick={() => actions.setFog(!state.fog)}>
                Fog
              </button>
              <button
                type="button"
                className={state.shadows && state.terrain ? styles.toggleOn : ''}
                title={state.terrain ? 'Sun shadows cast by the ground and by the buildings'
                  : 'Turn 3D terrain on: the shadow pass runs over the terrain cover'}
                onClick={() => actions.setShadows(!state.shadows)}>
                Shadows
              </button>
            </div>
            <div className={styles.overlay}>{readout}</div>
          </>
        )}
      </div>
      <Panel state={state} actions={actions} />
    </div>
  );
}

export default function PreviewPage() {
  return (
    <Layout
      title="Style preview"
      description="Edit a CartoCSS or MapBox style and see it rendered by the Massif Maps SDK itself, compiled to WebAssembly.">
      <BrowserOnly fallback={<div className={styles.shell} />}>
        {() => <StylePreview />}
      </BrowserOnly>
    </Layout>
  );
}
