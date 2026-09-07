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

const MODE_CARTOCSS = 'cartocss';
const MODE_MAPBOX = 'mapbox';

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
      </div>
    </div>
  );
}

function StylePreview() {
  const moduleUrl = useBaseUrl('/preview/massif-demo.mjs');
  const serviceWorkerUrl = useBaseUrl('/coi-serviceworker.js');

  const canvasRef = useRef(null);
  const mapRef = useRef(null);
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

  const apply = useCallback(async () => {
    const map = mapRef.current;
    if (!map || !engine) return;
    patch({busy: true, error: '', notes: []});
    try {
      const source = state.tilejson.includes('{z}')
        ? {url: state.tilejson, maxZoom: engine.DEFAULT_MAX_ZOOM}
        : await engine.resolveTileUrl(state.tilejson);

      let style;
      let notes = [];
      let themes = state.themes;
      if (state.mode === MODE_MAPBOX) {
        const converted = await engine.convertMapboxStyle(map.module, state.styleJson,
          {spriteKey: state.spriteKey});
        notes = converted.notes;
        themes = converted.themes;
        style = {project: themes.includes(state.theme) ? state.theme : themes[0]};
      } else {
        style = {css: state.css};
      }
      engine.applyStyle(map, {sourceUrl: source.url, maxZoom: source.maxZoom, style});
      patch({busy: false, notes, themes, theme: style.project ?? '', error: ''});
    } catch (error) {
      patch({busy: false, error: String(error?.message ?? error)});
    }
  }, [engine, state, patch]);

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
            <div className={styles.viewButtons}>
              {/* Tilt is 90 looking straight down here, the opposite of MapBox's pitch. */}
              <button type="button" onClick={() => flyTo(45)}>3D</button>
              <button type="button" onClick={() => flyTo(90)}>2D</button>
              <button type="button" onClick={() => flyTo(undefined, 0)}>North</button>
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
