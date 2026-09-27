---
title: Custom Raster Shaders
sidebar_position: 5
---

# Custom Raster Shaders

`CustomRasterTileLayer` runs a **custom GLSL "filter" shader** over any raster tile source. It is
the general base class that [`HillshadeRasterTileLayer`](/docs/features/hillshade) specializes.

:::info Fork feature
Added in PR [#18](https://github.com/massif-maps/MassifMaps/pull/18).
:::

## What it does

The layer draws a raster source through a fragment shader you supply. The shader defines
`vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity)`
(the hillshade entry point, shared), and inside it you can:

- read the source pixel with `getRawColor()`,
- read the current zoom with `getMapZoom()`,
- return a **premultiplied** color.

This lets you recolor, threshold, blend or otherwise post-process any raster source on the GPU,
per frame, with no CPU re-decode.

## Usage

```kotlin
import com.massifmaps.layers.CustomRasterTileLayer
import com.massifmaps.datasources.HTTPTileDataSource

val source = HTTPTileDataSource(0, 19, "https://your.tiles/{z}/{x}/{y}.png")
val layer = CustomRasterTileLayer(source)

layer.setShaderSource("""
    // GLSL fragment "filter". Return a premultiplied vec4.
    vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {
        vec4 c = getRawColor();
        float g = dot(c.rgb, vec3(0.299, 0.587, 0.114)); // grayscale
        return vec4(vec3(g) * c.a, c.a);
    }
""".trimIndent())

mapView.layers.add(layer)
```

### With the surface API

There is no `{"type": "custom-raster"}` — the layer carries no `!spec` declaration. Construct it
and [adopt](/docs/api/#bringing-an-existing-app-across) it; `shaderSource` is an ordinary property
afterwards, so the shader can be swapped at runtime by one write:

```java
MassifLayer tinted = Massif.adopt("tinted", new CustomRasterTileLayer(source));
tinted.set("shaderSource", grayscaleFilter);
```

A [`hillshade`](/docs/api/reference/layer) layer inherits `shaderSource` but does not use it: its
custom shader is `normalMapLightingShader`, a property of the `hillshade` spec — so a shaded-relief
layer with a custom shader needs no adoption at all.

| Member | Purpose |
|---|---|
| `setShaderSource(String)` | Install the GLSL filter shader; empty resets to the passthrough. |
| `getShaderSource()` | The shader you set. |
| `getRawColor()` *(in shader)* | The source pixel. |
| `getMapZoom()` *(in shader)* | Current map zoom, for zoom-dependent effects. |

## Relationship to hillshade

`HillshadeRasterTileLayer` **is** a `CustomRasterTileLayer` whose shader is the DEM lighting shader.
Its virtual overrides shadow the base behavior, so hillshade works exactly as before — but you can
build your own DEM-driven effects (per-zoom contours, custom relief, slope shading) by combining a
custom shader with the elevation helpers. See
[Hillshade → Custom lighting shader](/docs/features/hillshade#custom-lighting-shader).
