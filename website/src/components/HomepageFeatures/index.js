import clsx from 'clsx';
import Link from '@docusaurus/Link';

/*
 * The screenshots are the same files the feature pages use — one home, in the docs tree's
 * static assets. A feature with no capture yet falls back to a tinted band so every card in a
 * row starts its title at the same height.
 */
const shots = require.context('../../../static/img/features', false, /\.(jpg|png)$/);

function shotFor(name) {
  if (!name) {
    return null;
  }
  try {
    const asset = shots(`./${name}`);
    return asset.default ?? asset;
  } catch (e) {
    return null;
  }
}

const CoreFeatures = [
  {
    icon: '🗺️',
    title: 'High-performance vector tiles',
    body: 'A flexible OpenGL vector-tile renderer with CartoCSS styling, MBTiles, PMTiles, GeoJSON and Mapbox Vector Tiles support.',
  },
  {
    icon: '📱',
    title: 'Truly cross-platform',
    body: 'One C++ core, native bindings for Android (Java/Kotlin) and iOS (Objective-C/Swift), plus a NativeScript plugin.',
  },
  {
    icon: '🧭',
    title: 'Routing & geocoding',
    body: 'Embedded Valhalla street routing, SGRE indoor routing, and offline forward/reverse geocoding.',
  },
  {
    icon: '📦',
    title: 'Offline first',
    body: 'MBTiles and PMTiles files, a persistent tile cache, bundled styles and offline routing and geocoding packages — a full map with no connection.',
  },
];

const NewFeatures = [
  {
    icon: '⛰️',
    title: '3D Terrain',
    to: '/docs/features/3d-terrain',
    shot: 'terrain-hero.jpg',
    body: 'Real 3D elevation with render-to-texture fill draping, correct depth occlusion and fast-zoom performance.',
  },
  {
    icon: '〰️',
    title: 'On-the-fly Contours',
    to: '/docs/features/contours',
    shot: 'contours.jpg',
    body: 'Generate contour lines directly from RGB elevation tiles — no pre-baked mbtiles — plus GPU shader contours.',
  },
  {
    icon: '🎚️',
    title: 'Composite Vector Layers',
    to: '/docs/features/composite-vector-tile-layer',
    body: 'Weave external raster, hillshade and vector sources into a single CartoCSS style, ordered by layer name.',
  },
  {
    icon: '🌄',
    title: 'Advanced Hillshade',
    to: '/docs/features/hillshade',
    shot: 'hillshade.jpg',
    body: 'Multiple hillshade algorithms (GDAL, Igor, multidirectional), exaggeration and custom raster shaders.',
  },
  {
    icon: '🌅',
    title: 'Sky, Sun & Shadows',
    to: '/docs/features/sky-sun-shadows',
    shot: 'sun-lighting.jpg',
    body: 'One directional light and a shader sky shared by ground, terrain and fog — with a replaceable sky shader.',
  },
  {
    icon: '🌙',
    title: 'Objects in the Sky',
    to: '/docs/features/celestial-objects',
    shot: 'star-sky.jpg',
    body: 'Sun, moon, stars or an aircraft anchored by direction, plus a free-roam camera that can look above the horizon.',
  },
  {
    icon: '🖌️',
    title: 'Post-processing',
    to: '/docs/features/post-processing',
    shot: 'peakfinder.jpg',
    body: 'Full-screen fragment shaders with access to the terrain depth — the peak-finder relief look is one.',
  },
  {
    icon: '🏷️',
    title: 'Shields & Font Icons',
    to: '/docs/features/label-styling',
    shot: 'shield-labels.jpg',
    body: 'Names that take the free side of their icon, SDF icon glyphs, rounded plates and panorama callout labels.',
  },
  {
    icon: '🎨',
    title: 'Live Style Parameters',
    to: '/docs/features/style-parameters',
    shot: 'style-parameter-selection.jpg',
    body: 'Change a colour, a table or the selected feature and get a repaint instead of a re-decode of every tile.',
  },
  {
    icon: '📐',
    title: 'GeoJSON Vector Tiling',
    to: '/docs/features/geojson-vector-tiles',
    shot: 'geojson-tiling.jpg',
    body: 'App data tiled through a geojson-vt pyramid and styled with CartoCSS — 3.3× faster on long lines.',
  },
  {
    icon: '➡️',
    title: 'Maneuver Arrows',
    to: '/docs/features/maneuver-arrows',
    shot: 'maneuver-arrows.jpg',
    body: 'Turn arrows cut from the route and drawn as a line with an arrow head — no marker, no bitmap.',
  },
  {
    icon: '🧱',
    title: 'MapLibre Tiles',
    to: '/docs/features/maplibre-tiles',
    body: 'Read MLT as well as MVT — same decoder, same CartoCSS, format taken from the source or detected per tile.',
  },
  {
    icon: '🧩',
    title: 'PMTiles',
    to: '/docs/features/pmtiles',
    body: 'Single-file tile pyramids (PMTiles v3), local or over HTTP, anywhere a TileDataSource is expected.',
  },
];

function Card({icon, title, body, to, shot, isNew, withShot}) {
  const image = shotFor(shot);
  const inner = (
    <div className={clsx('featureCard', withShot && 'featureCard--shot')}>
      {withShot && (
        <div className={clsx('featureCardShot', !image && 'featureCardShot--empty')}>
          {image ? <img src={image} alt={title} loading="lazy" /> : <span>{icon}</span>}
        </div>
      )}
      <div className="featureCardText">
        {!withShot && <div className="featureCardIcon">{icon}</div>}
        <div className="featureCardTitle">
          {withShot && <span className="featureCardTitleIcon">{icon}</span>}
          {title}
          {isNew && <span className="badgeNew">New</span>}
        </div>
        <div className="featureCardBody">{body}</div>
      </div>
    </div>
  );
  return (
    <div className={clsx('col col--3')} style={{marginBottom: '1.5rem'}}>
      {to ? (
        <Link to={to} style={{textDecoration: 'none', color: 'inherit', display: 'block', height: '100%'}}>
          {inner}
        </Link>
      ) : (
        inner
      )}
    </div>
  );
}

export default function HomepageFeatures() {
  return (
    <>
      <section className="featureSection">
        <div className="container">
          <h2 style={{textAlign: 'center', marginBottom: '2rem'}}>Everything you need to build map apps</h2>
          <div className="row">
            {CoreFeatures.map((props, idx) => (
              <Card key={idx} {...props} />
            ))}
          </div>
        </div>
      </section>

      <section className="featureSection" style={{background: 'var(--ifm-color-emphasis-100)'}}>
        <div className="container">
          <h2 style={{textAlign: 'center', marginBottom: '0.4rem'}}>New in Massif Maps</h2>
          <p style={{textAlign: 'center', marginBottom: '2rem', color: 'var(--ifm-color-emphasis-700)'}}>
            Features shipping beyond the original CARTO SDK.
          </p>
          <div className="row">
            {NewFeatures.map((props, idx) => (
              <Card key={idx} {...props} isNew withShot />
            ))}
          </div>
        </div>
      </section>
    </>
  );
}
