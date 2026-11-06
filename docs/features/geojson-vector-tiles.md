---
title: GeoJSON Vector Tiling
sidebar_position: 12
mdx:
  format: mdx
---

import LiveExample from '@site/src/components/LiveExample';

# GeoJSON Vector Tiling

`GeoJSONVectorTileDataSource` turns app-owned GeoJSON into vector tiles a `VectorTileLayer` styles
with CartoCSS — so routes, tracks and result sets get the same renderer, styling and terrain draping
as the base map, instead of the `VectorLayer` element path.

:::info Fork feature
The source exists upstream; the fork rebuilt its tiler on **[geojson-vt](https://github.com/mapbox/geojson-vt)**
(PR [#54](https://github.com/massif-maps/MassifMaps/pull/54)) — a real tile pyramid instead of a
per-tile scan of every feature.
:::

<LiveExample id="geojson-line" />

## Usage

```kotlin
import com.massifmaps.datasources.GeoJSONVectorTileDataSource
import com.massifmaps.layers.VectorTileLayer

val source = GeoJSONVectorTileDataSource(0, 24).apply {
    simplifyTolerance = 1.0f      // tile pixels
    defaultLayerBuffer = 4f       // tile pixels, for layers created afterwards
}

val routes = source.createLayer("routes")          // returns the layer index
source.setLayerGeoJSONString(routes, geoJsonText)  // or setLayerGeoJSON(Variant) / setLayerFeatureCollection

mapView.layers.add(VectorTileLayer(source, MBVectorTileDecoder(styleSet)))
```

### With the surface API

```java
// The source re-tiles whatever it is given, so an update is one call rather than a rebuild.
MassifSource tour = map.source("tour-data", Spec.of("geojson").set("maxZoom", 14));
int layer = tour.createLayer("tour");
tour.setLayerGeoJSON(layer, routeGeoJson);

map.addLayer("tour", Spec.of("vector")
    .set("source", "tour-data")
    .set("style", "route-style"));
```

`addFeature`, `updateFeature` and `removeFeature` are reachable the same way, so incremental edits
need no object-API access either. Runnable, on four platforms (live on the web):
[the GeoJSON line example](/examples#geojson-line).

Style it by layer name, like any other vector source:

```css
#routes { line-color: #3388ff; line-width: 5; line-join: round; line-cap: round; }
```

| Method | Does |
|---|---|
| `createLayer(name)` / `deleteLayer(index)` | add / drop a named layer |
| `setLayerGeoJSON` / `setLayerGeoJSONString` | replace a layer's whole content, from a FeatureCollection or a single Feature |
| `setLayerFeatureCollection(index, projection, collection)` | replace it from SDK geometry |
| `addGeoJSONFeature` / `updateGeoJSONFeature` / `removeGeoJSONFeature` | incremental edits (also `…StringFeature`) |
| `SimplifyTolerance` | Douglas-Peucker tolerance, in **tile pixels** |
| `LineMetrics` | tag every tile piece of a line with the part of the original line it covers (default `false`, tiles unchanged); needed by a line gradient, see below |
| `DefaultLayerBuffer` | tile overflow, in **tile pixels** of a 256-px tile (default `4`); applies to layers created afterwards |

## Line metrics

A line is cut into one piece per tile it crosses, and a gradient along it needs to know where each
piece sits in the whole. With `LineMetrics = true` the builder writes that range, `0..1` of the
original line, on every piece as two tags, `mapbox_clip_start` and `mapbox_clip_end` (geojson-vt's
own names). A line that was not cut is the whole line, `0..1`.

The range is measured in Web Mercator units, not metres, as in Mapbox, so it drifts from geodesic
distance away from the equator. The tags are not feature fields: a style cannot read them, and
pieces of one line keep sharing their feature data. Tiles from any other source (OpenMapTiles,
PMTiles, MBTiles) carry none.

Turning it on re-tiles the layer, and costs one more feature per cut line piece and two small
values per piece in each tile.

### Colouring a line along its length

With line metrics on, a style can colour a line by how far along it is, `line-gradient`, as in
Mapbox's `line-gradient`. The ramp is a list of stops, position `0..1` and colour:

```css
/* the same ramp for every route */
#routes { line-width: 5; line-gradient: linear([mapnik::line_progress], (0, #2ecc71), (0.5, #f1c40f), (1, #e74c3c)); }

/* a ramp per route: a string the app puts on the feature */
#routes { line-width: 5; line-gradient: [gradient]; }
```

```json
{"type": "Feature", "properties": {"gradient": "0:#2ecc71,0.37:#f1c40f,1:#e74c3c"},
 "geometry": {"type": "LineString", "coordinates": [[6.1, 45.1], [6.2, 45.2]]}}
```

- **Smooth or hard.** `linear(...)` and the plain string blend between stops. `step(...)`, or a
  string that starts with `step;`, holds each colour until the next stop, which is how a route is
  coloured per segment. Two stops at the same position make a hard edge inside a smooth ramp.
- **The gradient is a string**, not an array: array properties are flattened when a tile is
  encoded. JSON stops (`[[0,"#fff"],[1,"#000"]]`) work too, and so does a table style parameter,
  `line-gradient: [param::route_gradient]`.
- **Limits.** A ramp is 2048 texels, so a route has at most 2048 distinct colour regions and a hard
  edge is placed to within route length / 4096. The string rides in every tile the route touches:
  keep it to a few hundred stops. Tiles from sources without line metrics draw the first stop.
- `line-color` is replaced, `line-opacity` still applies, and the casing of `line-border-width`
  keeps its own colour.

## Why it changed

The old tiler scanned every feature of a layer for **every** tile, and kept one re-simplified copy of
the dataset per zoom level. Long lines were the worst case: re-clipped from full resolution on every
tile they touched. The geojson-vt pyramid clips once per level from the parent.

Measured on a device (Adreno 610), 640 tiles z8–z17:

| Dataset | Before | After |
|---|---|---|
| 8 routes of 100–250 km (303k points) | 1709 ms | **513 ms** |
| 5000 short routes (165k points) | 1319 ms | **1176 ms** |

Known regression, documented rather than fixed: many *small* features at a single zoom are **1.6×
slower** (209 → 328 ms over 256 tiles at z14).

Binary cost: **+104 KiB** on `libmassif.so` (arm64-v8a, stripped) — header-only template
instantiation, nothing new linked.

## See also

- [Navigation Maneuver Arrows](/docs/features/maneuver-arrows) — built on this source.
- [3D Terrain](/docs/features/3d-terrain) — tiled GeoJSON drapes with the rest of the map.
