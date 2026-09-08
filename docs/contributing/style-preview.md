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

The `style` box takes any MapLibre style URL. The left pane loads it as written; the right pane gets
the same JSON with **every vector source repointed** at the local archive. Nothing else is touched,
so a style with a raster or DEM source keeps it on both sides.

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

Screenshots of a WebGL canvas are only valid once the tiles have drawn — take one, then take
another. A first capture right after `reload` is usually blank.

The panes share a camera but not a frame budget, so the two are not a fair performance comparison;
use the demo app's bench for that.
