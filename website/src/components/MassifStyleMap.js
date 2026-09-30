import React, {useEffect, useRef, useState} from 'react';
import useBaseUrl from '@docusaurus/useBaseUrl';

const VARIANTS = ['streets', 'outdoor', 'topo', 'hybrid', 'eink'];
const MAPLIBRE = 'https://unpkg.com/maplibre-gl@5/dist/maplibre-gl';
// hybrid ships a placeholder for its imagery; this page draws it over EOX's free Sentinel-2 mosaic (CC BY 4.0)
const SATELLITE = {
  type: 'raster', tileSize: 256, maxzoom: 15,
  tiles: ['https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless_3857/default/g/{z}/{y}/{x}.jpg'],
  attribution: '<a href="https://s2maps.eu">Sentinel-2 cloudless</a> by EOX IT Services GmbH (contains modified Copernicus Sentinel data 2016 & 2017)',
};

// MapLibre from the CDN at runtime: the site does not bundle a map library for one page.
function loadMapLibre() {
  if (window.maplibregl) return Promise.resolve(window.maplibregl);
  return new Promise((resolve, reject) => {
    const css = document.createElement('link');
    css.rel = 'stylesheet';
    css.href = `${MAPLIBRE}.css`;
    document.head.appendChild(css);
    const script = document.createElement('script');
    script.src = `${MAPLIBRE}.js`;
    script.onload = () => resolve(window.maplibregl);
    script.onerror = reject;
    document.head.appendChild(script);
  });
}

/** The published MapLibre styles, live, with a variant switcher. */
export default function MassifStyleMap({center = [5.7245, 45.1885], zoom = 13, height = 480}) {
  const base = useBaseUrl('/styles/massif/');
  const container = useRef(null);
  const map = useRef(null);
  const [variant, setVariant] = useState('streets');
  const [error, setError] = useState(null);

  // The sprite from this site's own copy: the published URL names the release host, which a preview
  // or a fork does not serve, and MapLibre draws nothing at all when the sprite fails.
  const styleOf = async (name) => {
    const style = await (await fetch(`${base}${name}.json`)).json();
    style.sprite = new URL(`${base}sprite/sprite`, window.location.href).href;
    if (style.sources.satellite) style.sources.satellite = SATELLITE;
    return style;
  };

  useEffect(() => {
    let cancelled = false;
    Promise.all([loadMapLibre(), styleOf('streets')]).then(([maplibregl, style]) => {
      if (cancelled || !container.current) return;
      map.current = new maplibregl.Map({container: container.current, style, center, zoom});
      map.current.addControl(new maplibregl.NavigationControl());
      // a source-layer only our tiles carry is not an error on OpenFreeMap's
      map.current.on('error', (e) => {
        const message = e.error?.message ?? String(e.error);
        if (!/does not exist on source/.test(message)) setError(message);
      });
    }).catch(() => setError('The map could not be loaded.'));
    return () => {
      cancelled = true;
      map.current?.remove();
    };
  }, []);

  const pick = (name) => {
    setVariant(name);
    setError(null);
    styleOf(name).then((style) => map.current?.setStyle(style)).catch((e) => setError(String(e)));
  };

  return (
    <div>
      <div style={{display: 'flex', gap: 8, marginBottom: 8, flexWrap: 'wrap'}}>
        {VARIANTS.map((name) => (
          <button key={name} className={`button button--sm ${name === variant ? 'button--primary' : 'button--secondary'}`}
                  onClick={() => pick(name)}>{name}</button>
        ))}
        <a className="button button--sm button--link" href={`${base}${variant}.json`}>{variant}.json</a>
      </div>
      <div ref={container} style={{height, borderRadius: 8, overflow: 'hidden'}} />
      {variant === 'hybrid' && <p><small>Imagery here: EOX's Sentinel-2 cloudless. The published hybrid style carries a placeholder for yours: <a href="massif-maplibre#the-optional-sources">adding it</a>.</small></p>}
      {error && <p><small>{error}</small></p>}
    </div>
  );
}

/** The release's screenshots, one per variant; nothing until a release has published them. */
export function MassifScreenshots() {
  const base = useBaseUrl('/styles/massif/screenshots/');
  const [loaded, setLoaded] = useState([]);
  // probed after hydration: an <img> that failed during the static render never reports it
  useEffect(() => {
    for (const name of ORDER) {
      const probe = new Image();
      probe.onload = () => setLoaded((names) => [...names, name].sort((a, b) => ORDER.indexOf(a) - ORDER.indexOf(b)));
      probe.src = `${base}${name}.jpg`;
    }
  }, [base]);
  if (loaded.length === 0) return null;
  return (
    <div style={{display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: 12}}>
      {loaded.map((name) => (
        <figure key={name} style={{margin: 0}}>
          <img src={`${base}${name}.jpg`} alt={`Massif ${name}`} style={{borderRadius: 8}} />
          <figcaption style={{textAlign: 'center'}}><small>{name}</small></figcaption>
        </figure>
      ))}
    </div>
  );
}

const ORDER = ['streets', 'outdoor', 'topo', 'hybrid', 'eink'];
