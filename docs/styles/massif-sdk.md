---
title: Massif with the SDK
description: "Loading the Massif CartoCSS flavours in the Massif Maps SDK: the variant, style parameters, legends, relief and the optional archives."
sidebar_position: 3
---

# Massif with the SDK

The SDK takes Massif as a **CartoCSS project**: one set of rules for all five variants, the variant
being a style parameter like the others. Get a flavour from npm (`@massif-maps/styles`) or from a
[`massif-styles-v*` release](https://github.com/massif-maps/MassifMaps/releases); what the four
CartoCSS flavours are and which to pick: [Massif](massif.mdx#get-it).

## Load it

Ship the flavour's folder (or its zip) in the app's assets, and name the variant as the style:

```ts
map.addLayer('basemap', {
    type: 'vector',
    source: { type: 'http', url: 'https://tiles.openfreemap.org/planet/{z}/{x}/{y}.pbf', maxZoom: 14 },
    style: {
        type: 'mbvt',
        // cartocss/ or cartocss-iconfont/: <name>.json; the compiled flavours: <name>.xml
        project: { type: 'project', assets: { type: 'dir', path: 'styles/massif' }, name: 'outdoor' },
    },
});
```

The same spec from Kotlin, Swift and the web: [your first map](../getting-started/your-first-map.mdx).
A zip works as it is: `assets: { type: 'zip', data: … }` ([asset packages](../api/reference/assets.md)).
With the object API it is a `CompiledStyleSet(assetPackage, "outdoor")` handed to an
`MBVectorTileDecoder`.

`name` is `streets`, `outdoor`, `topo`, `hybrid` or `eink`, or `osm` / `custom` for the two
[extension examples](massif-extending.md). A variant is `project.json` plus that one parameter, so
switching variants live is a parameter change, not a reload:

```java
decoder.setStyleParameter("variant", "eink");   // object API: the MBVectorTileDecoder
style.set("params.variant", "eink");            // surface API: the layer's `style` object
```

## Style parameters

The full list, with defaults, is in the
[style's README](https://github.com/massif-maps/MassifMaps/blob/master/styles/massif/README.md#style-parameters-an-app-sets).
The ones an app usually sets:

| Parameter | Default | What |
|---|---|---|
| `variant` | `streets` | which of the five maps |
| `poiStyle` | `badge` | `plain`: POI glyphs without their disc |
| `poi-boost.<class>` | 0 | added to that POI class's placement priority: 100000 = one layer, negative demotes |
| `buildings` | 2 | 0 none, 1 flat footprints at every zoom, 2 footprints then 3D blocks from z15 |
| `building_opacity` | 1 | the 3D buildings' alpha looking straight down |
| `track_min_zoom`, `path_min_zoom` | 12 | where tracks, paths and trails appear |
| `highlight_drinking_water` | 0 | water points larger and placed first |
| `sac_scale_labels` | 0 (1 on e-ink) | the T1..T6 grade along each trail |
| `road_osm_low` | 0 | major roads drawn as OSM Carto draws them at low zoom |
| `lighting` | 1 (0 on e-ink) | 0 draws every colour flat, lit by no hour |

A colour-only change repaints without decoding a tile again:
[live style parameters](../features/style-parameters.md).

## Legend

Every CartoCSS flavour ships `legend.json`. `getLegend("")` resolves it against the live parameters,
so the swatches follow the variant: [legends](../features/legends.md).

## Relief, imagery and the optional archives

- **Relief and contours** (every variant): the project lists a `hillshade` slot under the contour
  lines and a `contour` layer at three depths (lines, index lines, labels among the names). A
  [composite layer](../features/composite-vector-tile-layer.md) fills them from the app's own DEM, so
  a variant draws relief only where the app attaches it - the examples do for outdoor and topo:

  ```ts
  const base = map.addLayer('basemap', { type: 'composite-vector', source: vectorTiles, style: 'massif' });
  const dem = map.source('dem', { type: 'http', url: 'https://tiles.mapterhorn.com/{z}/{x}/{y}.webp',
                                  maxZoom: 16, metaData: { dem_encoding: 'terrarium' } });
  const contours = map.source('contours', { type: 'contour', source: 'dem', baseInterval: 20 });
  base.call('addExternalDataSource', 'hillshade', dem.handle, 1); // HILLSHADE
  base.call('addExternalDataSource', 'contour', contours.handle, 2);           // VECTOR
  ```

  The `#hillshade` rule carries the values Massif was tuned with (exaggeration 0.35, 0.5 in topo,
  opacity 0.55, gone by z16).
- **Imagery** (hybrid): a raster layer under the vector layer; the style's background is transparent
  for that variant.
- **Bathymetry and low-zoom landcover** (`bathymap`, all variants): merge the archive into the main
  tiles with a [`"merged-mbvt"`](../api/reference/source.md#spec-merged-mbvt) source. Past its max
  zoom (z6) the archive is cut into the requested tile, so it reaches the z7.8-8 handover to
  OpenMapTiles' own landcover.

  ```ts
  source: { type: 'merged-mbvt', source: { type: 'mbtiles', path: 'region.mbtiles' },
            source2: { type: 'mbtiles', path: 'bathymap.mbtiles' } }
  ```
- **Routes** (outdoor, topo): their own datasource in the `route` slot, the same way. Contours may
  also come from a tileset rather than the device ([contours](../features/contours.md)). The tile
  schema each expects: [Massif with MapLibre](massif-maplibre.md#the-optional-sources).

## Fonts

The CartoCSS flavours name font roles (regular, medium, bold, italic) and ship no font: the SDK
draws them with the device's fonts. The icon-font flavours ship `fonts/MassifIcons.ttf`, found by the
decoder in the project's `fonts/` folder.

**Your own POI icons** in the icon-font flavour: `iconfont/` holds the font's source, an
[iconotype](https://github.com/iconotype/iconotype) project, and the script that writes one from a
folder of SVGs. A codepoint is derived from the icon's name, so the same name lands on the same
character as in Massif's font, and the style's `glyph` table still finds it:

```sh
cp iconfont/MassifIcons.iconotype.json mine.iconotype.json    # Massif's codes, kept as they are
node iconfont/iconotype-project.mjs my-svgs/ mine.iconotype.json --name MassifIcons
npx @iconotype/cli build --input mine.iconotype.json          # MassifIcons.ttf + MassifIcons.json
cp MassifIcons.ttf fonts/
```

Each SVG is one glyph, named after the POI `class` or `subclass` it draws. A name Massif has no icon
for needs its `glyph.<name>` entry (the character `MassifIcons.json` gives it) in a
[child project](massif-extending.md). A name your folder lacks draws no glyph.
