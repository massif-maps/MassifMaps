---
title: Hillshade
sidebar_position: 4
---

# Hillshade (Shaded Relief)

`HillshadeRasterTileLayer` renders shaded relief from an **RGB-encoded elevation** data source.
The fork extends it with multiple algorithms, smooth exaggeration, shader contours and a custom
shader base class.

<figure class="docs-figure">

![Hillshade relief](/img/features/hillshade.jpg)

<figcaption>Hillshade relief over a vector basemap, shaded from a terrarium DEM (demo capture).</figcaption>

</figure>

## Basic usage

```kotlin
import com.massifmaps.layers.HillshadeRasterTileLayer
import com.massifmaps.layers.HillshadeMethod
import com.massifmaps.datasources.HTTPTileDataSource

val dem = HTTPTileDataSource(0, 12, "https://your.tiles/dem/{z}/{x}/{y}.png").apply {
    setMetaDataElement("dem_encoding", Variant("terrarium"))   // or "mapbox"
}

val hillshade = HillshadeRasterTileLayer(dem).apply {
    setHillshadeMethod(HillshadeMethod.MULTIDIRECTIONAL)
    setExaggeration(1.3f)               // smooth per-frame relief factor (shader uniform)
    setIlluminationDirection(315f)      // light azimuth in degrees
}
mapView.layers.add(hillshade)
```

The illumination is also influenced by the main light source configured on `Options`.

### With the surface API

Every setter below is a property, so the whole layer is one spec:

```java
map.addLayer("hillshade", Spec.of("hillshade")
    .set("source", Spec.of("http")
        .set("url", "https://your.tiles/dem/{z}/{x}/{y}.png")
        .set("maxZoom", 12)
        // Attached to every tile the source loads; it is what picks the elevation decoder.
        .set("metaData", Spec.object().set("dem_encoding", "terrarium")))
    .set("hillshadeMethod", "HILLSHADE_METHOD_MULTIDIRECTIONAL")
    .set("exaggeration", 1.3)
    .set("contrast", 0.6));

// Animatable afterwards — a shader uniform, no re-decode.
map.layer("hillshade").set("exaggeration", 1.8);
```

Every property and its enum values: [`hillshade` in the layer reference](/docs/api/reference/layer).

## Hillshade methods

`setHillshadeMethod(...)` selects the algorithm:

| Method | Description |
|---|---|
| `STANDARD` | MapLibre's legacy hillshade algorithm. |
| `COMBINED` | Combined algorithm based on GDAL. |
| `IGOR` | Igor's soft hillshade (GDAL). |
| `MULTIDIRECTIONAL` | Multiple light sources for richer relief. |
| `BASIC` | Basic algorithm based on GDAL. |

## Appearance controls

| Setter | Purpose |
|---|---|
| `setExaggeration(float)` | Per-frame relief factor (no re-decode). Default `1.0`. |
| `setIlluminationDirection(float)` | Light azimuth (degrees). |
| `setHeightScale(float)` / `setExagerateHeightScaleEnabled(bool)` | Vertical scale of the elevation. |
| `setContrast(float)` | Shading contrast. |
| `setShadowColor` / `setAccentColor` / `setHighlightColor` | Tint the shadow / accent / highlight bands. |
| `setIlluminationMapRotationEnabled(bool)` | Rotate the light with the map. |

## Shader contours

The hillshade layer can also draw **contour lines in the fragment shader** from the same
elevation — see [On-the-fly Contours](/docs/features/contours#2-shader-contours-hillshade):

```kotlin
hillshade.setContourEnabled(true)
hillshade.setContourInterval(100f)
hillshade.setContourColor(Color(0xFF7A4B28.toInt()))
hillshade.setContourWidth(1.0f)
```

## Custom lighting shader

For full control, enable elevation encoding and provide your own GLSL lighting shader (it can read
`getElevation()` and `getMapZoom()`):

```kotlin
hillshade.setElevationEncodingEnabled(true)
hillshade.setNormalMapLightingShader(myGlslSource)
```

`HillshadeRasterTileLayer` is a specialization of
[`CustomRasterTileLayer`](/docs/features/custom-raster-shaders) — the DEM-specific case of the
general custom-shader raster layer.

## Sharing the DEM with 3D terrain

The same elevation source can drive both hillshade and [3D terrain](/docs/features/3d-terrain).
Wrap it in a `MemoryCacheTileDataSource` so both features share tiles:

```kotlin
val dem = MemoryCacheTileDataSource(httpDem)
mapView.layers.add(HillshadeRasterTileLayer(dem))
mapView.options.terrainOptions = TerrainOptions(dem).apply { isEnabled = true }
```

You can also fold hillshade into a single style with the
[Composite Vector Tile Layer](/docs/features/composite-vector-tile-layer).
