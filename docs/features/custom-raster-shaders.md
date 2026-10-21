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
- read further sources drawn in the same pass with `sampleRaster(i)` ([below](#several-sources-in-one-draw)),
- read a number you set from the app, declared as `uniform float u_name;` ([below](#shader-parameters)),
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

`{"type": "custom-raster", "source": ...}` creates the layer, and `shaderSource` is an ordinary
property, so the shader can be swapped at runtime by one `set`:

```java
map.addLayer("tinted", Spec.of("custom-raster")
    .set("source", source)
    .set("shaderSource", grayscaleFilter));
```

A [`hillshade`](/docs/api/reference/layer) layer inherits `shaderSource` but does not use it: its
custom shader is `normalMapLightingShader`, a property of the `hillshade` spec.

| Member | Purpose |
|---|---|
| `setShaderSource(String)` | Install the GLSL filter shader; empty resets to the passthrough. |
| `getShaderSource()` | The shader you set. |
| `addDataSource(source)` | Draw another source with the first, up to 4 in total. |
| `getDataSourceCount()` | The number of sources, the layer's own included. |
| `setShaderParameter(name, value)` | Set the `uniform float` of that name. `getShaderParameter(name)` reads it. |
| `shaderParameters` *(surface API)* | All parameters as a name → number map; a write sets the ones it names. |
| `getRawColor()` *(in shader)* | The source pixel; `sampleRaster(0)`. |
| `sampleRaster(i)` *(in shader)* | Source `i`, a literal from 0 to 3. |
| `getMapZoom()` *(in shader)* | Current map zoom, for zoom-dependent effects. |

## Several sources in one draw

`addDataSource()` binds the tiles of every source to the same draw, so the shader mixes their
**values** rather than blending finished images, and the GPU pays one draw instead of one per
source. A satellite image under a cloud mask, or a colour ramp over a hillshade, in a single pass:

```java
layer.addDataSource(cloudMaskSource);
layer.setShaderSource("""
    vec4 applyLighting(lowp vec4 color, mediump vec3 normal, mediump vec3 surfaceNormal, mediump float intensity) {
        vec4 ground = sampleRaster(0);
        float cloud = sampleRaster(1).a;
        return vec4(mix(ground.rgb, vec3(1.0), cloud) * ground.a, ground.a);
    }
""");
```

- Up to **4** sources. The index must be a literal: GLES2 cannot index a sampler by a computed value.
- The sources must share a zoom range, and `sampleRasterAtPixel` reads in the first one's pixels; a source with no tile yet reads as source 0, so
  the shader sees the same picture twice instead of a hole, and the tile is fetched again after 1 s, 2 s, 4 s... up to a minute.
- `sampleRasterAtPixel(i, pixel)` reads source `i` at a pixel of the tile.
- Not supported on `HillshadeRasterTileLayer`.
- Known gap: every source tile must be the same tile of the pyramid. Per-source offsets, as tangram
  carries, are not ported.

## Shader parameters

`setShaderParameter("u_mix", 0.4f)` sets `uniform float u_mix;` in the shader, from any thread, with
no recompile. Draped on 3D terrain, a changed parameter bakes the drape stack again; a change reaches the
drape at most 10 times a second, so a parameter driven every frame costs at most that many bakes.

## Relationship to hillshade

`HillshadeRasterTileLayer` **is** a `CustomRasterTileLayer` whose shader is the DEM lighting shader.
Its virtual overrides shadow the base behavior, so hillshade works exactly as before — but you can
build your own DEM-driven effects (per-zoom contours, custom relief, slope shading) by combining a
custom shader with the elevation helpers. See
[Hillshade → Custom lighting shader](/docs/features/hillshade#custom-lighting-shader).
