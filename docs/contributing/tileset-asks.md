---
title: What the style needs from the tileset
description: "Every field and layer the Massif styles want from the OpenMapTiles fork, and what each one buys"
sidebar_position: 6
---

# What the style needs from the tileset

Our basemap comes from a planetiler fork of the OpenMapTiles profile, built by
[alpimaps_data_generator](https://github.com/Akylas/alpimaps_data_generator). This page is the
running list of what the styles want out of it and what each item buys — the input to a schema
change, not a record of one.

Everything here was read off a real build with the [style preview](./style-preview.md) `gaps` panel:
Rhône-Alpes, 2026-08-28, planetiler `8f911c0c`, OMT 3.16.0, compared against OpenFreeMap.

Nothing on this list blocks a style from rendering. A style that reads a field the tiles do not
carry has to fall back on its own — see [the shield fallback](#the-rule-for-a-missing-field).

## Blocking a feature already written

| Field | Where | Buys |
|---|---|---|
| `network` | `transportation_name` | **road shields.** Commented out at `TransportationName.java:268`. Without it no shield can tell an interstate from a departmental road, and every ref falls to the neutral plate |
| `iso_a2` | `transportation_name` | **shield colours per country.** A French `N` is red, a Dutch `N` is yellow, a Belgian `N` is blue; the ref's letter alone cannot separate them. MapTiler's `road_label` carries it for exactly this reason |
| `maritime`, `class` | `boundary` | a coastline boundary cannot be told from a land one, so both draw the same |

`network` is worth more than the shields. Stock OMT collapses the OSM route network onto a
14-value enum — `us-interstate`, `ca-transcanada`, `gb-motorway`, `e-road`, … — which has no French,
German or Italian value at all. Emitting the **raw OSM `network`** (`FR:A-road`, `DE:BAB`) instead
lets the style read the route class directly rather than guessing it from the ref's first letter,
and it is how [OSM Americana](https://github.com/osm-americana/openstreetmap-americana) does
per-country shields.

## Wanted by the OpenStreetMap-oriented variants

| Layer or field | Buys |
|---|---|
| `tree` | canopy dots. Both MapTiler v4 and Mapbox Standard carry one |
| `street_furniture` | benches, crossings, traffic signals, drinking water, picnic tables |
| `motorway_junction` | numbered exits, with their own plate |
| `poi.network` | metro and RER roundels — the badge is per network, and `class`/`subclass` cannot name one |
| `place.iso_a2` | country-keyed place labels |
| `building.colour` | a mapped building colour instead of one flat fill |
| `poi.agg_stop` | grouping the platforms of one station into a single label |
| `expressway`, `horse`, `mtb_scale` | on `transportation`, all three commented out in the fork |

## Not wanted — do not add these

- **`ref_length`.** MapLibre has `length`, CartoCSS has `length()`
  ([CartoCSSMapnikTranslator.cpp:606](https://github.com/massif-maps/MassifMaps/blob/master/libs-massif/cartocss/src/cartocss/CartoCSSMapnikTranslator.cpp)),
  and one stretchable sprite covers every ref length, so there is no per-length artwork to pick.
- **`route_1_*` … `route_n_*`.** Hiking and cycling relations already live in their own archive.
  On `transportation_name` they are the single largest field group in stock OMT — 80-odd columns.
- **A localized name equal to the default.** The fork already drops those and should keep doing it;
  the style coalesces `name:xx` onto `name`.

## The rule for a missing field

A field this list asks for is **read with a fallback, never assumed**. `road-shield-plate` reads
`["coalesce", ["get", "iso_a2"], ""]`, so a tileset without it takes no country branch and colours
the plate by `class` instead — grey for a motorway, white for the rest. Adding `iso_a2` to the
tiles changes the picture without touching the style, and removing it again is not a crash.

The same applies the other way: the fork carries fields stock OpenMapTiles does not, and the styles
should use them where they are and cope where they are not.

## Already in the fork — do not re-add

`transportation` carries `tracktype`, `sac_scale`, `surface_detail`, `difficulty`, `maxspeed`,
`official`, `access`, `surface`, `oneway`, `toll`, `bicycle`, `foot`. There are `building_name`,
`landcover_name` and `landuse_name` layers, and a `Route.java` for route relations. Contours and
terrain-RGB are separate archives.
