---
title: GeoJSON Vector Tiling
sidebar_position: 12
---

# GeoJSON Vector Tiling

`GeoJSONVectorTileDataSource` turns app-owned GeoJSON into vector tiles a `VectorTileLayer` styles
with CartoCSS — so routes, tracks and result sets get the same renderer, styling and terrain draping
as the base map, instead of the `VectorLayer` element path.

:::info Fork feature
The source exists upstream; the fork rebuilt its tiler on **[geojson-vt](https://github.com/mapbox/geojson-vt)**
(PR [#54](https://github.com/massif-maps/MassifMaps/pull/54)) — a real tile pyramid instead of a
per-tile scan of every feature.
:::

<figure class="docs-figure">

![5000 GeoJSON routes tiled and styled over the base map](/img/features/geojson-tiling.jpg)

<figcaption>5000 short routes (165k points) served from one <code>GeoJSONVectorTileDataSource</code>, tiled at runtime and styled with CartoCSS over the base map and 3D terrain. Captured at z12.6, tilt 60.</figcaption>

</figure>

## Usage

```kotlin
import com.massifmaps.datasources.GeoJSONVectorTileDataSource
import com.massifmaps.layers.VectorTileLayer

val source = GeoJSONVectorTileDataSource(0, 24).apply {
    simplifyTolerance = 1.0f      // tile pixels
    defaultLayerBuffer = 4f       // FRACTION of a tile, not pixels (see below)
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
need no object-API access either. Runnable, on three platforms:
[the GeoJSON line example](/examples#geojson-line).

Style it by layer name, like any other vector source:

```css
#routes { line-color: #3388ff; line-width: 5; line-join: round; line-cap: round; }
```

| Method | Does |
|---|---|
| `createLayer(name)` / `deleteLayer(index)` | add / drop a named layer |
| `setLayerGeoJSON` / `setLayerGeoJSONString` | replace a layer's whole content |
| `setLayerFeatureCollection(index, projection, collection)` | replace it from SDK geometry |
| `addGeoJSONFeature` / `updateGeoJSONFeature` / `removeGeoJSONFeature` | incremental edits (also `…StringFeature`) |
| `SimplifyTolerance` | Douglas-Peucker tolerance, in **tile pixels** |
| `DefaultLayerBuffer` | tile overflow, as a fraction of a tile (default `4`) |

:::caution The layer buffer is a fraction of a tile
`DefaultLayerBuffer` is a **fraction of a tile**, not pixels as older doc comments said. `64` wraps
the internal `uint16` to zero.
:::

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
