---
title: Massif with MapLibre
description: "Loading the Massif styles in MapLibre GL JS / Native or Mapbox GL, and adding the optional bathymetry, contour, route and imagery sources."
sidebar_position: 2
---

# Massif with MapLibre

Each variant is a standalone style JSON. Point the map at it:

```js
const map = new maplibregl.Map({
    container: 'map',
    style: 'https://massif-maps.github.io/MassifMaps/styles/massif/outdoor.json',
    center: [5.76, 45.31],
    zoom: 12,
});
```

| URL | What |
|---|---|
| `…/styles/massif/<variant>.json` | the latest release |
| `…/styles/massif/v<version>/<variant>.json` | one release, never changes — pin this in an app |
| `…/styles/massif/versions.json` | every published version |

`<variant>` is `streets`, `outdoor`, `topo`, `hybrid` or `eink`. The same files are in the npm
package (`@massif-maps/styles/maplibre/`) and in the `massif-maplibre-<version>.zip` release asset;
their sprite URL points at the website, so they work as they are or copied to your own host (rewrite
`sprite` to serve that too).

The styles read [OpenFreeMap](https://openfreemap.org/)'s OpenMapTiles planet and fonts, and
[Mapterhorn](https://mapterhorn.com/)'s DEM for the relief. They work in Mapbox GL JS too, which reads
the same style spec.

Every variant carries the relief (`hillshade`) and the contours (`contour`, `contour-index`,
`contour-label`); outdoor and topo show them, the others ship them with `visibility: none`:

```js
for (const id of ['hillshade', 'contour', 'contour-index', 'contour-label']) {
  map.setLayoutProperty(id, 'visibility', 'visible');
}
```

The SDK's style parameters (`poiStyle`, `track_min_zoom`, `building_opacity`...) are **baked** into
the MapLibre styles at each variant's default: MapLibre has no parameter mechanism. To change one,
edit the style JSON or [build your own](massif-extending.md#the-maplibre-side).

## The optional sources

Four sources have no public host. The published styles keep their layers, and the source itself is
an **empty placeholder of the same name** — GeoJSON with no features, or a transparent raster for
`satellite` — so the style validates and those layers draw nothing until you put the real source
back. `metadata["massif:placeholder-sources"]` in each style lists them.

| Source | Layers it feeds | Variants | Tiles it expects |
|---|---|---|---|
| `bathymap` | low-zoom landcover under z8, ocean depth | all | vector, z0-6: layer `global_landcover` (`class`: `built`, `glacier`, `swamp`, `marsh`, `tree`, `crop`, `scrub`, `heath`, `grass`, `sand`), layer `depth` (`min_depth` in metres, 0 = the whole ocean) |
| `contours` | contour lines and labels | all (shown in outdoor, topo) | vector, z11-14: layer `contour`, `ele` (m), `div` (100 = index line) |
| `routes` | waymarked route bands | outdoor, topo | vector: layer `route`, `class` `hiking` / `bicycle`, `network` 1-4 (1-2 = international / national) |
| `satellite` | the imagery under the roads | hybrid | raster XYZ |

Replace a placeholder before the map reads the style:

```js
const style = await (await fetch('https://massif-maps.github.io/MassifMaps/styles/massif/outdoor.json')).json();
style.sources.contours = { type: 'vector', url: 'https://tiles.example.com/contours.json' };
style.sources.bathymap = { type: 'vector', url: 'https://tiles.example.com/bathymap.json' };
const map = new maplibregl.Map({ container: 'map', style });
```

or keep loading it by URL and swap it in `transformStyle`:

```js
map.setStyle(url, {
    transformStyle: (previous, next) => ({
        ...next,
        sources: { ...next.sources, satellite: { type: 'raster', tiles: ['https://imagery.example.com/{z}/{x}/{y}.jpg'], tileSize: 256 } },
    }),
});
```

The bathymetry archive is [Alpimaps'](https://github.com/Akylas/alpimaps_data_generator)
(`scripts/make_bathymap.py`: ESA WorldCover classes and Natural Earth isobaths). A source whose
`maxzoom` is 6 is overzoomed to z8 by MapLibre, which is where the style hands over to OpenMapTiles'
own landcover.

## Night, lights and 3D

The styles carry the SDK's day-cycle colours in `metadata` only; MapLibre draws the day. 3D
buildings are `fill-extrusion` layers and draw in MapLibre as they are.
