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

`--styles` mounts a folder of style projects at `/styles`, defaulting to the repo's own, so
`styles/massif-streets` is served without being copied anywhere. `?style=<url>` opens straight on
one.

The `style` box takes any MapLibre style URL. The left pane loads it as written; the right pane gets
the same JSON with **every vector source repointed** at the local archive. Nothing else is touched,
so a style with a raster or DEM source keeps it on both sides.

## The Massif row

`massif row` adds a second row underneath: the same style again, this time as the **CartoCSS the
converter wrote from it**, rendered by the SDK's own web build. Four panes, two questions at once —
left against right is what the tiles carry, top against bottom is what survives the conversion.

```sh
massif-style mapbox2css style.json carto --fold-casings --tile-draw-size 512   # from the project's folder
gh run download <run-id> --repo massif-maps/MassifMaps    # the web-preview artefact
cp web-preview/massif-demo.* web/demo/
```

The sprite URL is resolved against the process's working directory, not the style file, so the
conversion has to run from the project's own folder. The output lands in `carto/`, which is
gitignored — it is generated, never edited.

`--tile-draw-size 512` because the panes run the SDK on maplibre's tile convention, where the zoom
number already is maplibre's: converted at the default 256 every zoom stop fires a level late and
the roads come out visibly thin against the row above.

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
