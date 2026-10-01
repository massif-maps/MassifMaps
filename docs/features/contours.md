---
title: On-the-fly Contours
sidebar_position: 2
mdx:
  format: mdx
---

import LiveExample from '@site/src/components/LiveExample';

# On-the-fly Contour Lines

Generate contour lines **directly from RGB elevation tiles** at runtime — no pre-baked contour
mbtiles required — or draw them entirely in the fragment shader.

:::info Fork feature
Added in PR [#18](https://github.com/massif-maps/MassifMaps/pull/18). Two independent paths:
a vector `ContourTileDataSource` and shader contours on `HillshadeRasterTileLayer`.
:::

<LiveExample id="contour-lines" />

## 1. `ContourTileDataSource` — vector contours

`ContourTileDataSource` decorates any RGB-encoded elevation `TileDataSource` and produces
**vector contour tiles** via marching squares (stitched into polylines). It shares the
fetch/decode of the wrapped source, so there is **no second download**.

It emits a `contour` layer whose features carry:

- `ele` — elevation in metres.
- `div` — the largest "nice" divisor of the elevation (`1000 / 500 / 250 / 200 / 100 / 50 / 20 / 10`),
  matching the classic `gdal_contour` pipeline. Use it in CartoCSS to emphasise index contours.

Because it is a normal vector source, it drops straight into a `VectorTileLayer` + CartoCSS.

```kotlin
import com.massifmaps.datasources.ContourTileDataSource
import com.massifmaps.datasources.HTTPTileDataSource
import com.massifmaps.layers.VectorTileLayer
import com.massifmaps.vectortiles.MBVectorTileDecoder
import com.massifmaps.styles.CompiledStyleSet

// DEM source (MapBox encoding assumed if not set).
val dem = HTTPTileDataSource(0, 12, "https://your.tiles/dem/{z}/{x}/{y}.png").apply {
    setMetaDataElement("dem_encoding", Variant("terrarium"))
}

// Contour source. The decoder is resolved per tile from "dem_encoding".
val contourSource = ContourTileDataSource(dem).apply {
    baseInterval = 100          // metres: the finest interval, multiplied per zoom band
    resolution = 0              // tracing grid samples per side; 0 = the DEM's own (use 0 over 3D terrain)
    minVisibleZoom = 11
    simplifyTolerance = 1.0f
    setSeamlessEdgesEnabled(true) // fetch E/N/NE neighbours so lines meet across tiles
}

// Style the `contour` layer with your own CartoCSS.
val decoder = MBVectorTileDecoder(CompiledStyleSet(/* your .mbs/zip style */))
val contourLayer = VectorTileLayer(contourSource, decoder)
mapView.layers.add(contourLayer)
```

### With the surface API

The source is a `contour` spec wrapping the DEM (an id of kind `source`, or an inline spec); its
properties, including `baseInterval`, stay reachable through the id afterwards:

```java
map.source("contours-src", Spec.of("contour")
    .set("source", "dem")
    .set("baseInterval", 100));

map.addLayer("contours", Spec.of("vector")
    .set("source", "contours-src")
    .set("style", "contour-style"));

// Still a live property: change the spacing without rebuilding the layer.
Massif.source("contours-src").set("baseInterval", 50);
```

Example CartoCSS for the emitted `contour` layer (index vs intermediate lines via `div`):

```css
#contour {
  line-color: #a06a3a;
  line-width: 0.7;
  [div >= 100] { line-width: 1.4; }   /* index contours thicker */
  text-name: [ele];
  text-face-name: "Regular";
  text-size: 10;
  [div >= 100] { text-placement: line; }
}
```

### `ContourTileDataSource` options

| Property | Meaning |
|---|---|
| `BaseInterval` | Finest interval in metres (default `10`), multiplied per zoom band by `setIntervalMultiplier(maxZoom, m)`. |
| `Resolution` | Tracing grid, max samples per side (default `128`, min `8`); lower is coarser and cheaper, `0` = the DEM's own. Per zoom: `setResolutionForZoom(maxZoom, r)`. |
| `MinVisibleZoom` | Below this zoom, no contours are generated. |
| `SimplifyTolerance` | Douglas–Peucker tolerance for the output polylines, in tile pixels; `0` disables. |
| `SeamlessEdgesEnabled` | Fetch E/N/NE neighbours so lines join across tile boundaries. |
| `LayerName` | Name of the generated vector layer (default `contour`). |
| `LabelStubsEnabled` / `LabelInterval` | Emit only short label stubs (`stub` = 1) when the lines come from shader contours. |

It drapes correctly over [3D terrain](/docs/features/3d-terrain) — the geometry is displaced and
clamped to the terrain surface.

## 2. Shader contours (hillshade)

For contours that never re-tesselate with zoom, draw them **per fragment**. The
[`HillshadeRasterTileLayer`](/docs/features/hillshade) can render anti-aliased contour lines
straight from the elevation encoded into its normal map:

```kotlin
val hillshade = HillshadeRasterTileLayer(dem)
hillshade.setContourEnabled(true)
hillshade.setContourInterval(100f)          // metres
hillshade.setContourColor(Color(0xFF7A4B28.toInt()))
hillshade.setContourWidth(1.0f)
mapView.layers.add(hillshade)
```

- The elevation is opt-in encoded into the normal map (`normal.xy` in R,G; 16-bit elevation in
  B,A). With contours off the output is byte-identical to plain hillshade.
- Being per-fragment at a fixed metre interval, shader contours are **unaffected by tile zoom** —
  no LOD popping.

For a fully custom per-zoom or `div`-based contour shader, enable elevation encoding and supply
your own lighting shader; it can read `getElevation()` and `getMapZoom()`:

```kotlin
hillshade.setElevationEncodingEnabled(true)
hillshade.setNormalMapLightingShader(myGlslShaderSource)
```

See [Custom Raster Shaders](/docs/features/custom-raster-shaders) for the general shader
mechanism.

## Which one should I use?

| | `ContourTileDataSource` | Shader contours |
|---|---|---|
| Output | Real vector geometry | Fragment-shaded lines |
| Labels (`ele`) | ✅ via CartoCSS `text-name` | ❌ |
| Style flexibility | Full CartoCSS | Color / width / interval (or custom shader) |
| Zoom behavior | Regenerated per zoom band | Fixed metre interval, no popping |
| Cost | CPU marching squares | GPU only |
