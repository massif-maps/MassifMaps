---
title: Style preview — two panes, two tilesets
description: "Comparing one style over OpenFreeMap and over a locally built tileset, and listing what the local one is missing"
sidebar_position: 5
---

# Style preview

`tools/style-preview` renders **one style twice, side by side** — left over
[OpenFreeMap](https://openfreemap.org), right over a local `.mbtiles`. The cameras are synced, so
the panes differ only in the tiles behind them, and every difference on screen is a difference in
the DATA.

That is what it is for. Massif's own style is authored against OpenMapTiles, and the tileset we
build with
[alpimaps_data_generator](https://github.com/Akylas/alpimaps_data_generator) is a planetiler fork of
that schema with its own additions and its own omissions. The preview is how we find out which.

```sh
python3 tools/style-preview/serve.py \
  --mbtiles rhone-alpes=/path/to/rhone-alpes.mbtiles
# http://127.0.0.1:8787
```

Python's standard library only — `serve.py` reads tiles straight out of the SQLite archive and
serves a TileJSON beside them, so there is no conversion step and no 250 MB copy. `--mbtiles`
repeats; the picker in the toolbar chooses which archive the right pane reads.

An archive may be several joined with `+` — `--mbtiles rhone-alpes=a.mbtiles+b.mbtiles` — and each
tile is then the two MVTs concatenated, which is a valid tile carrying both sets of layers: the
same merge `MergedMBVTTileDataSource` does on a device, so the Massif pane sees an optional archive
such as the bathymap. Past an archive's own `maxzoom` its last tile is cut into the one asked for
(`subtile`, the same cut as `MBVTSubtile.h`): the bathymap stops at z6 and still reaches z8. A style source whose URL is a bare file name (`"url": "bathymap.json"`) is
pointed at the registered archive of that name.

`--styles` mounts a folder of style projects at `/styles`, defaulting to the repo's own, so
`styles/massif` is served without being copied anywhere. `?style=<url>` opens straight on
one.

The `style` box takes any MapLibre style URL. The left pane loads it as written; the right pane gets
the same JSON with **every vector source repointed** at the local archive. Nothing else is touched,
so a style with a raster or DEM source keeps it on both sides.

## The Massif row

`massif row` adds a second row underneath: the same style again, this time as the **CartoCSS the
converter wrote from it**, rendered by the SDK's own web build. Four panes, two questions at once —
left against right is what the tiles carry, top against bottom is what survives the conversion.

```sh
massif-style mapbox2css style.json carto --fold-casings --tile-draw-size 512 --fonts fonts   # from the project's folder
gh run download <run-id> --repo massif-maps/MassifMaps    # the web-preview artefact
cp web-preview/massif-demo.* web/demo/
```

The sprite URL is resolved against the process's working directory, not the style file, so the
conversion has to run from the project's own folder. The output lands in `carto/`, which is
gitignored — it is generated, never edited.

`?blendspeed=0` turns the tile cross-fade off for the Massif panes, which is what maplibre does
with vector geometry - it fades rasters only. A cased road fades badly at the default 1: the fill
sits near the background colour, so for the length of the fade only the casing reads and the road
looks like an outline waiting to be filled. It is `VectorTileLayer::setLayerBlendingSpeed`.

`?labelperspective=0` holds a label at a constant on-screen size however far away it is, which is
what the SDK did before 6.1; the default `0.5` is maplibre's damping, so a distant label shrinks.
It is `VectorTileLayer::setLabelPerspectiveScaling`, and the A/B is only visible under TILT — at
tilt 0 the whole map is one depth and every setting draws the same picture.

`--tile-draw-size 512` because the panes run the SDK on maplibre's tile convention, where the zoom
number already is maplibre's. The SDK shifts the style's zoom to whatever TileDrawSize the app draws
at (the Map block's `tile-draw-size`, [style-tools](style-tools.md#a-zoom-stop-is-relative-to-a-tile-size)),
so the panes and an app on the default 256 draw the same map.

Three things about the web build shape this:

- **One map per document.** `WebMapView("#map")` is a hardcoded selector, so the two panes are two
  `<iframe>`s of `massif-pane.html` rather than two canvases.
- **The style comes out of the module's filesystem**, and `main()` runs as the module starts, so
  the converted project is fetched first and written in `preRun`.
- **`main()` reads the query string before `MASSIF_DEFAULTS`**, and `project` is one of the names it
  reads — hence `carto=` for the pane's own parameter. A URL there is otherwise taken for a folder
  name inside the module and throws.

`serve.py` sends COOP/COEP so the module gets SharedArrayBuffer. OpenFreeMap keeps working through
that: MapLibre fetches with CORS, which satisfies `require-corp`, so nothing has to be proxied.

## The reference pane

The `reference` picker swaps the local MapLibre pane for a style we are measuring ourselves against,
fed the same camera as the rest of the grid: `mapbox-standard` (drawn by mapbox-gl-js v3, its real
config — `?preset=night` sets `lightPreset`) or one of MapTiler's `streets-v4`, `outdoor-v4`,
`topo-v4`, `hybrid-v4` and `openstreetmap`. `?ref=mapbox-standard&massif=1` opens straight on the
comparison: ours over OpenFreeMap, the reference, and the SDK row under both.

The `hour` slider (`?hour=21.5`) sets the Massif panes' day-cycle light — the sun at that local solar
time on the equinox, at the camera — which is what lights a style's emissive layers, and moves the
Standard reference to the matching preset: night before 6 and from 20:30, dawn to 8, dusk from 18.
Beside that reference the Massif panes hold Standard's day sun (azimuth 180, altitude 70) from 8 to 18,
so shadows compare one for one; without it they follow the real sun at every hour.

For a Massif style the bar gets a **variant** picker: the page reloads on the sibling file
(`streets.json` → `eink.json`, …) with the camera, the hour, the reference and the Massif row kept;
`custom` and `osm` load streets with `?project=custom|osm`, the
[extension examples](../styles/massif-extending.md).

The Massif panes cast building shadows the way Standard does, at its day depth (`shadowStrength` 1),
when the **shadows** box is ticked (`?shadows=1`); off by default, since the shadow pass costs the page
most of its frame rate. Ours land on a terrain surface, so each pane then carries a flat one (the
Mapterhorn DEM at exaggeration 0, never auto-flattened).

The **terrain** box (`?terrain=1`) drapes every pane over real relief: the MapLibre panes over
Mapterhorn, the Mapbox reference over its own DEM, the Massif panes at exaggeration 1. The Massif
panes never auto-flatten either, so a top-down view keeps its shadows and ground AO. Every pane
tilts to pitch 85, Mapbox's limit, past MapLibre's default 60.

`?styleparams=poi_on_roof=1,label_occlusion=0` sets the style's own parameters on the Massif panes. The
**POIs on roofs** box (`?poiroof=1`) adds `poi_on_roof=1` to them.
The shadows, terrain and POIs-on-roofs boxes apply to the running Massif panes, without reloading
them: once made, the terrain stays and goes flat (exaggeration 0) when both shadows and terrain are off.

`?compare=<project folder>` puts **another CartoCSS project** in the left Massif pane, over the local
tiles, against the style's own on the right; `&compareVariant=<name>` picks its `<name>.json`. A
release flavour (`?compare=/styles/massif/dist/cartocss-iconfont`), an older conversion, or another
app's style mounted under `--styles` (Alpimaps' OSM style is how the OSM example was matched).

The tokens are read by `serve.py` from `~/.mapbox_token` and `~/.maptiler_token`
(`--mapbox-token`, `--maptiler-token` to point elsewhere) and served to the page at `/tokens.json`,
so nothing is committed. The pane is an iframe: mapbox-gl and maplibre in one document fight over
their globals, and its script is loaded in CORS mode, which the page's COEP requires.

A MapLibre error that a source-layer "does not exist on source" is not shown on the pane: a style
written for both tilesets reads layers only our fork carries, and the gaps panel lists those.

A family project (see `styles/massif/README.md`) is drawn as the variant the style names in
`metadata["massif:variant"]` - the pane loads `carto/<variant>.json`. A style with a hillshade layer
carrying `massif:sdk-layer` gets a `HillshadeRasterTileLayer` built from those settings above the
Massif pane's base layer, since the SDK draws relief as a layer of its own and not from CartoCSS.
Its DEM gets the style source's `encoding` as `dem_encoding`; the MapLibre panes are on 5.24, whose
hillshade is the one the SDK ports ([Matching MapLibre](../features/hillshade.md#matching-maplibre)).

`--remote NAME=URL` serves a hosted TileJSON at `/tiles/NAME.json`, fetched by the server so a key
never reaches the style: `--remote satellite=https://api.maptiler.com/tiles/satellite-v2/tiles.json?key={maptiler}`
is how the hybrid's optional `satellite` source is drawn, `{maptiler}` and `{mapbox}` taking the
tokens. A raster layer carrying `massif:sdk-layer` becomes a `RasterTileLayer` below the Massif
pane's base layer.

## The gaps panel

`gaps` answers the only two questions worth asking:

1. **What does the style read that the local tileset does not carry?** Source-layers first, then
   fields, per layer. This is the list that turns into planetiler work.
2. **How do the two schemas differ otherwise?** Layers and fields present in one `vector_layers` and
   not the other, in both directions — the local tileset's own additions show up here.

A `name:xx` or `name_xx` read counts as satisfied when the tileset carries `name` or `name_int`. Our
planetiler fork drops a translation that is identical to the default, so its absence from
`vector_layers` is not a missing language — the style falls back and draws the same text.

## What it found on the Rhône-Alpes build, 2026-09-08

Against OpenFreeMap's Liberty style, on a tileset built 2026-08-28 (planetiler `8f911c0c`, OMT
3.16.0):

| Missing | Consequence |
|---|---|
| `transportation_name.network` | **no road shields at all** — the left pane draws `A 480`, `N 481`, `D 1090`, the right draws none |
| `boundary.maritime`, `.class` | coastline boundaries cannot be dropped from the land ones |
| `transportation.network`, `.expressway`, `.horse`, `.mtb_scale` | commented out in the fork's `Transportation.java` |
| `place.iso_a2`, `building.colour`, `poi.agg_stop` | country-keyed shields, mapped building colour, station grouping |

Going the other way, the local tileset carries what upstream OpenMapTiles does not: `building_name`,
`landcover_name`, `landuse_name`, and on `transportation` the fields the outdoor variants need —
`tracktype`, `sac_scale`, `surface_detail`, `difficulty`, `maxspeed`, `official`.

## Limits

Screenshots of a WebGL canvas go stale: the buffer is only re-read after the map paints, so a
capture right after `reload` shows the previous frame or nothing at all. Pan by a few pixels, then
capture. Two captures in a row is not enough on its own.

The panes share a camera but not a frame budget, so the two are not a fair performance comparison;
use the demo app's bench for that.
